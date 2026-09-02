---
name: flutter_soloud-setup
version: 1
description: Teaches how to add flutter_soloud to a Flutter app, configure each platform (web script tag and COOP/COEP headers, Linux ALSA, Android/iOS/macOS minimum versions), initialize and deinitialize the engine, shrink binaries by excluding the Xiph libs, set up logging, and enumerate/switch output devices. Use when a user asks to install flutter_soloud, initialize SoLoud, set up web/background-audio prerequisites, reduce binary size, or switch the audio output device.
---

# flutter_soloud setup

flutter_soloud is an FFI plugin around the SoLoud C++ engine: the native code is compiled automatically by Dart build hooks when you depend on the package, so setup is mostly pubspec + a few platform bits (one `<script>` tag on web, ALSA dev package on Linux, minimum SDK versions). Unlike audioplayers/just_audio there is no per-player instance — everything goes through the singleton `SoLoud.instance`, which must be `init()`ed before use and `deinit()`ed on shutdown.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  // Optional: pick the output device before init.
  final devices = SoLoud.instance.listPlaybackDevices(); // works pre-init

  await SoLoud.instance.init(
    // device: devices.firstWhere((d) => d.isDefault),
    sampleRate: 44100,
    bufferSize: 2048,
    channels: Channels.stereo,
    lowLatency: true,
    automaticCleanup: false,
  );

  final sound = await SoLoud.instance.loadAsset('assets/audio/click.mp3');
  SoLoud.instance.play(sound);

  // Later, on shutdown:
  SoLoud.instance.deinit();
}
```

## Adding the package

```sh
flutter pub add flutter_soloud
```

Native C/C++ sources are compiled by [Dart build hooks](https://dart.dev/tools/hooks) (`hooks`/`code_assets`/`native_toolchain_c` are transitive deps of the package). No CMake, CocoaPods script phases, or compiler flags are needed.

## Platform setup

- **Web** — add to the `<body>` of `web/index.html`:
  ```html
  <script src="assets/packages/flutter_soloud/web/init_soloud.js" defer></script>
  ```
  The script auto-picks between the multi-threaded (AudioWorklet) and single-threaded (ScriptProcessorNode) WASM builds based on whether the page is cross-origin isolated. Details in [references/web.md](references/web.md).
- **Linux** — requires ALSA headers: `sudo apt-get install libasound2-dev` (Debian/Ubuntu), `pacman -S alsa-lib` (Arch), `zypper install alsa-devel` (openSUSE).
- **Android** — `minSdk = 21` (the plugin sets this in its own `build.gradle`; your app-level `minSdkVersion` must be >= 21).
- **iOS** — deployment target iOS 13.0+; **macOS** — 10.15+. Native assets are compiled and bundled for both CocoaPods and SPM projects.

## The API shape

All of these live on the singleton `SoLoud.instance` (`import 'package:flutter_soloud/flutter_soloud.dart'`).

- `Future<void> init({PlaybackDevice? device, bool automaticCleanup = false, int sampleRate = 44100, int bufferSize = 2048, Channels channels = Channels.stereo, bool lowLatency = true, AndroidAAudioAttributes androidAAudioAttributes = AndroidAAudioAttributes.mediaMusic, int? devicePeriodFrames, int? renderAheadFrames})` — initializes the engine. **Throws on failure** (e.g. `SoLoudCppException`, `SoLoudNoPlaybackDevicesFoundCppException`); it does not return a `PlayerErrors` status, so `await` it in try/catch.
- `void deinit()` / `Future<void> deinitAsync()` — stops the engine and disposes all resources including sounds. `deinit` blocks the calling thread; prefer `deinitAsync` where you can await it.
- `bool get isInitialized` — synchronous readiness check.
- `List<PlaybackDevice> listPlaybackDevices()` — **safe to call before `init()`**. Returns `PlaybackDevice(id, isDefault, name)`.
- `Future<void> changeDevice({PlaybackDevice? newDevice})` — switches output while running; omit `newDevice` to select the system default. Await it — the swap runs off the UI isolate.
- `Future<void> stopAudioDevice({bool force = false})` / `Future<void> startAudioDevice()` — stop/start only the output device; loaded sounds, voices, and filter state are preserved and playback resumes where it left off.
- `AudioDeviceState getAudioDeviceState()` — cheap sync read: `uninitialized | stopped | started | starting | stopping`. Safe before `init()`.
- `void setAudioDeviceIdleTimeout(Duration? timeout)` — when no unpaused voices remain: `Duration.zero` stops the device ASAP, a positive duration keeps it alive that long (default 500 ms), `null` keeps it running indefinitely (Android wakelock). No effect on web.

Divergences from what models trained on audioplayers/just_audio assume:

- One global engine, no `AudioPlayer()` instances. Call `SoLoud.instance.init()` once, early; every other call throws `SoLoudNotInitializedException` before that.
- `play()` is synchronous and returns a `SoundHandle` immediately — it cannot report device-start failures; subscribe to `SoLoud.instance.audioDeviceStartFailures` for those.
- `init()` while already initialized **deinitializes and reinitializes**, stopping all sounds and unloading all files.
- `init()` options that are **native-only** (silently ignored on web): `lowLatency`, `androidAAudioAttributes` (Android-only, and only when `lowLatency: false`), `devicePeriodFrames`, `renderAheadFrames`.
- `automaticCleanup: true` makes the engine purge its temp directory of loaded sound files occasionally — relevant for apps that load many files from the network.

### Render-Ahead Ring (ultra-low latency with large mix buffers)

On native platforms (Android, iOS, macOS, Windows, Linux), you can decouple the hardware output period from the engine mix buffer by setting `renderAheadFrames > 0`:

```dart
await SoLoud.instance.init(
  bufferSize: 2048,           // Large DSP/mixing quantum for CPU headroom
  devicePeriodFrames: 512,    // Hardware device callback period (~11 ms @ 44.1kHz)
  renderAheadFrames: 1536,    // Mix-ahead depth (e.g. bufferSize - devicePeriodFrames)
);
```

- **How it works**: The engine pre-mixes audio `renderAheadFrames` ahead into an internal ring buffer. When reactive calls like `play()` or `playScheduled()` occur, audio is mixed **retroactively** into the not-yet-played section of the ring, giving near-instantaneous keypress-to-sound latency (~11 ms) without risking audio underruns from tiny mix buffers.
- **Inspection getters**:
  - `bool get isRenderAheadEnabled` — true when enabled on native platforms.
  - `Duration getPlayheadTime()` — true playhead time reaching the speaker right now (equals `getEngineTime()` when disabled or on web).
  - `Duration getOutputLatency()` — estimated output latency (ring depth + device period; `Duration.zero` when disabled or on web).
- **Caveats**: Ignored on web. Ended-voice callbacks may fire up to `renderAheadFrames` earlier than without the ring. Unseekable/un-snapshotable streams (released push streams, pull streams, `speechText`) degrade gracefully to standard buffer boundaries.

## Traps

- **Don't await a return code from `init()`.** It returns `Future<void>` and throws; older docs suggest a `PlayerErrors` return. Trust the code.
- Calling `init()` again (e.g. after hot restart) wipes all loaded sounds — guard with `isInitialized` if you only want to init once.
- **Web without the `<script>` tag fails at `init()`** with confusing WASM-module errors. The tag must be present, and the WASM assets only load from `assets/packages/flutter_soloud/web/…`.
- **Web: don't pass `--web-header` COOP/COEP flags together with `flutter run --wasm`** — the dev server already sends COOP/COEP for WasmGC and the conflicting duplicated headers block the plugin's worker threads (`ERR_BLOCKED_BY_RESPONSE`). See [references/web.md](references/web.md).
- **`changeDevice` is desktop-mostly**: Android, iOS, and Web support only the default output device; `listPlaybackDevices()` there returns just the default.
- A device stopped via `stopAudioDevice()` or the idle timeout **stays stopped across `changeDevice()`** — the replacement device only starts if the old one was running.
- **Linux build fails with `alsa/asoundlib.h: No such file`** — install `libasound2-dev`; the error is from the native build hook, not Dart.
- On web, `loadUrl()` hits CORS (`Access-Control-Allow-Origin` missing) unless the server allows it, and local files can't be read — use `loadMem()` instead.
- Per-sound filters are not supported on web (global filters are).

## Shrinking binaries: excluding Xiph libs

The Opus/Ogg/Vorbis/FLAC decoders are bundled by default (600–3000 KB per binary). If you only need MP3/WAV/synthesis, exclude them in the **app's** `pubspec.yaml`:

```yaml
hooks:
  user_defines:
    flutter_soloud:
      no_xiph_libs: true
```

`setBufferStream()`/`readSamplesFrom*()` with Opus/Vorbis/FLAC then throw; everything else works. On web, edit `web/compile_wasm.sh` in the package (`NO_XIPH_LIBS="1"`) and rebuild the WASM yourself (requires Emscripten, Linux/macOS). Old shell-env/Podfile/Gradle config no longer applies under build hooks.

## Logging

The plugin logs via `package:logging` (logger name `flutter_soloud.SoLoud`). Nothing is printed until you attach a listener:

```dart
import 'package:logging/logging.dart';
import 'dart:developer' as dev;

Logger.root.level = Level.FINE;
Logger.root.onRecord.listen((r) => dev.log(r.message, name: r.loggerName));
```

## More depth

- [references/web.md](references/web.md) — the two WASM builds, COOP/COEP headers, dev-server pitfalls, CORS.
- [references/audio_context.md](references/audio_context.md) — background playback with `audio_service` + `audio_session` (media notification, lock-screen controls, ducking), with required iOS/Android manifest changes.
- Demos: `example/lib/output_device/output_device.dart` (device enumeration and switching), `example/lib/audio_context/audio_context.dart` (full `audio_service` integration), `example/lib/main.dart` (minimal init/play).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

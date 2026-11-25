---
name: flutter-soloud-setup
version: 9
description: Teaches how to add flutter_soloud to a Flutter app, configure each platform (web script tag and COOP/COEP headers, Linux audio backends ALSA/PulseAudio/JACK, Android/iOS/macOS minimum versions), initialize and deinitialize the engine, configure ultra-low-latency playback (renderAheadFrames, devicePeriodFrames, lowLatency set to true), shrink binaries by excluding the Xiph libs, enable opt-in desktop VST3 plugin hosting (enable_vst3), set up logging, and enumerate/switch output devices or Linux audio backends. Use when a user asks to install flutter_soloud, initialize SoLoud, choose init() settings for a game, instrument, music player or voice-chat app, configure low-latency playback or audio buffer settings, fix Android crackling or silent screen recordings, set up web/background-audio prerequisites, configure Linux audio backends, reduce binary size, enable VST3 hosting, or switch the audio output device.
---

# flutter_soloud setup

flutter_soloud is an FFI plugin around the SoLoud C++ engine: the native code is compiled automatically by Dart build hooks when you depend on the package, so setup is mostly pubspec + a few platform bits (one `<script>` tag on web, minimum SDK versions). Unlike audioplayers/just_audio there is no per-player instance — everything goes through the singleton `SoLoud.instance`, which must be `init()`ed before use and `deinit()`ed on shutdown.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  // Optional: pick the output device before init.
  final devices = SoLoud.instance.listPlaybackDevices(); // works pre-init

  await SoLoud.instance.init(
    // device: devices.firstWhere((d) => d.isDefault),
    // linuxAudioBackend: LinuxAudioBackend.auto, // auto, alsa, pulseAudio, jack
    sampleRate: 44100,
    bufferSize: 2048,
    channels: Channels.stereo,
    lowLatency: true,
    automaticCleanup: false,
  ); // these are the defaults: a plain init() is the same

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
- **Linux** — audio playback uses `miniaudio` with dynamic runtime loading for ALSA (`libasound.so.2`), PulseAudio (`libpulse.so`), and JACK (`libjack.so`). No extra C/C++ development packages or compile-time headers are required to build. Ensure runtime libraries are present on the host system (e.g. `libasound2`, `libpulse0`). To decode extended OS-native formats (**M4A**, **MP4 audio tracks**, **AAC**, **AC-3**, and **E-AC-3**), the engine dynamically loads FFmpeg shared libraries (`libavcodec` and `libavformat`) at runtime; ensure FFmpeg (4.4 to 8) is installed on the user system (e.g. `sudo apt install ffmpeg libavcodec-extra` on Debian/Ubuntu, `sudo pacman -S ffmpeg` on Arch, `sudo dnf install ffmpeg-free` on Fedora). Without it, loading or streaming those formats throws `SoLoudAudioFormatNotSupportedCppException`. Core formats (MP3, WAV, OGG, FLAC) work without FFmpeg. If using opt-in VST3 plugin hosting with GUI, launch with `GDK_BACKEND=x11 flutter run -d linux` as VST3 editor GUIs standardly require X11.
- **Android** — `minSdk = 21` (the plugin sets this in its own `build.gradle`; your app-level `minSdkVersion` must be >= 21).
- **iOS** — deployment target iOS 13.0+; **macOS** — 10.15+. Native assets are compiled and bundled for both CocoaPods and SPM projects.

## The API shape

All of these live on the singleton `SoLoud.instance` (`import 'package:flutter_soloud/flutter_soloud.dart'`).

- `Future<void> init({PlaybackDevice? device, bool automaticCleanup = false, int sampleRate = 44100, int bufferSize = 2048, Channels channels = Channels.stereo, bool lowLatency = true, AndroidAAudioAttributes androidAAudioAttributes = AndroidAAudioAttributes.mediaMusic, LinuxAudioBackend linuxAudioBackend = LinuxAudioBackend.auto, int? devicePeriodFrames, int? renderAheadFrames})` — initializes the engine. **Throws on failure** (e.g. `SoLoudCppException`, `SoLoudNoPlaybackDevicesFoundCppException`); it does not return a `PlayerErrors` status, so `await` it in try/catch.
  - **For the lowest reaction time on user input:** pass `renderAheadFrames: bufferSize` (e.g. 2048) and `devicePeriodFrames: 512` (the default when omitted; 512 frames are about 11.6 ms at 44.1 kHz), with `lowLatency: true`. This activates the native render-ahead ring buffer, allowing `playScheduled(sound, Duration.zero)` and `play()` to retroactively remix sounds directly at the playhead, bypassing buffer-boundary waits. See **flutter-soloud-scheduling**.
  - **`lowLatency`** (native only, default `true`) selects the backend performance profile. On Android `true` uses AAudio's MMAP path: lowest latency, but not capturable by screen recorders and with little CPU headroom for heavy filters. `false` uses the conservative profile: capturable, more headroom, higher output latency.
  - **`androidAAudioAttributes`** (Android, only with `lowLatency: false`): `mediaMusic` (default), `unmanaged` (you set attributes/focus yourself, e.g. with `audio_session`), `voiceCommunication` (voice chat: the platform echo canceller removes what SoLoud plays).
- `void deinit()` / `Future<void> deinitAsync()` — stops the engine and disposes all resources including sounds. `deinit` blocks the calling thread; prefer `deinitAsync` where you can await it.
- `bool get isInitialized` — synchronous readiness check.
- `List<PlaybackDevice> listPlaybackDevices()` — **safe to call before `init()`**. Returns `PlaybackDevice(id, isDefault, name)`.
- `Future<void> changeDevice({PlaybackDevice? newDevice})` — switches output while running; omit `newDevice` to select the system default. Await it — the swap runs off the UI isolate.
- `Future<void> setAndroidAAudioAttributes(AndroidAAudioAttributes attributes)` — Android only, `lowLatency: false` only: reopens the output device with new attributes while keeping loaded sounds and playing voices (e.g. switch to `voiceCommunication` during a call). Does nothing on other platforms.
- `Future<void> setLinuxAudioBackend(LinuxAudioBackend backend)` — Linux only: selects or dynamically switches the audio backend (`LinuxAudioBackend.auto` [ALSA -> PulseAudio -> JACK], `.alsa`, `.pulseAudio`, `.jack`). Safe to call before `init()` or while the engine is running. If the switch fails (e.g. JACK with no server) it throws and the engine keeps playing on the previous backend.
- `Future<void> stopAudioDevice({bool force = false})` / `Future<void> startAudioDevice()` — stop/start only the output device; loaded sounds, voices, and filter state are preserved and playback resumes where it left off. `stopAudioDevice()` does nothing while an unpaused voice is playing unless `force: true`. `startAudioDevice()` also prewarms the device before the first sound and, on Linux, reopens it after the PulseAudio server went away and came back.
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

- **How it works**: The engine pre-mixes audio `renderAheadFrames` ahead into an internal ring buffer, on a dedicated render thread. When reactive calls like `play()` or `playScheduled()` occur, audio is mixed **retroactively** into the not-yet-played section of the ring, so the reaction time is about one device period (plus the hardware output latency) without risking audio underruns from tiny mix buffers.
- **Inspection getters**:
  - `bool get isRenderAheadEnabled` — true when enabled on native platforms.
  - `Duration getPlayheadTime()` — true playhead time reaching the speaker right now (equals `getEngineTime()` when disabled or on web).
  - `Duration getOutputLatency()` — internal software mix cushion (render-ahead ring depth + one device period; `Duration.zero` when disabled or on web).
  - `Duration getHardwareOutputLatency()` — external OS/HAL/DAC presentation delay from the device callback to the physical transducer (Android AAudio timestamp, iOS `AVAudioSession`, macOS CoreAudio device latency + safety offset, Windows WASAPI, Web Audio `outputLatency + baseLatency`; `Duration.zero` during warm-up or when unsupported). Works regardless of whether the render-ahead ring is enabled. Total delay from mix quantum to ear is `getOutputLatency() + getHardwareOutputLatency()`.
- **Caveats**: Ignored on web. Ended-voice callbacks may fire up to `renderAheadFrames` earlier than without the ring. Changing a playing voice in a way the engine cannot replay (its loop region, a seek, unpausing it, its filters or their parameters, or the global volume) makes events in the next `renderAheadFrames` fall back to the end of the ring instead of the playhead. The global volume is applied before the global filters instead of after them (audible only with non-linear filters such as a limiter or compressor).

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| A game, instrument or rhythm app where a tap/key must sound at once, set up with a plain `init()` or with `bufferSize` shrunk to 256/512 | `init(renderAheadFrames: 2048, devicePeriodFrames: 512)` with the default `bufferSize`, and `playScheduled(sound, Duration.zero)` on the input (**flutter-soloud-scheduling**) | The engine mixes ahead into its own ring while the device runs small periods, so a new sound is mixed back into audio not yet played: about one device period of reaction time without the dropouts a tiny mix buffer causes. Native only (ignored on web); voice-ended events can arrive up to `renderAheadFrames` early |
| Android: crackles with heavy filters (pitch shift, several effects), or the app's sound is missing from screen recordings | `init(lowLatency: false)` | The low-latency profile uses AAudio's MMAP path, which screen recorders cannot capture and which leaves little CPU headroom for DSP. `false` fixes both at the cost of higher output latency, so keep `true` for tap-driven games |
| Android voice chat, or an app that configures audio focus/attributes with `audio_session` | `init(lowLatency: false, androidAAudioAttributes: AndroidAAudioAttributes.voiceCommunication)` (or `.unmanaged`); `setAndroidAAudioAttributes` to switch while running | With `voiceCommunication` the platform echo canceller removes what SoLoud plays from the microphone; `unmanaged` stops SoLoud from overriding your attributes. Applies only with `lowLatency: false` |
| An app that plays only MP3/WAV (and FLAC files) and cares about download size | `no_xiph_libs: true` under `hooks.user_defines.flutter_soloud` in the app's pubspec | Removes the Ogg/Opus/Vorbis/FLAC libraries: 600 to 3000 KB less per binary. Costs: Ogg files do not load, Opus/Vorbis/FLAC streams throw `SoLoudXiphLibsNotAvailableException`, and the Opus/Vorbis/FLAC capture encoders are gone. Web needs a manual WASM rebuild |
| A web game whose audio crackles when the UI is busy, hosted on a server the user controls | Serve the app with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` | The plugin then loads the multi-threaded build and mixes on an AudioWorklet thread, away from UI jank and GC pauses. Costs: cross-origin resources need CORP/CORS headers, some popups and ad iframes break, and `webJsFunctionName` DSP callbacks do not run on this build. See [references/web.md](references/web.md) |
| A silent looping sound played only to keep the audio device or a background session alive | `SoLoud.instance.setAudioDeviceIdleTimeout(null)` | The device keeps running and outputs silence by itself: no voice used and no asset. It holds the OS audio resources (Android wakelock, iOS audio session), so set a timeout again when playback is over |
| Sounds that play after silent gaps (turn-based game, notification-style app) and must not wait for the output device to restart | `await SoLoud.instance.startAudioDevice()` before the moment that matters, or a longer `setAudioDeviceIdleTimeout` | The device is stopped 500 ms after the last voice ends and is restarted, off the UI thread, by the next `play()`; prewarming removes that start-up from the first sound |
| A music player or podcast app that loads many different URLs or assets during its life | `init(automaticCleanup: true)` | The engine purges its temp folder of copied files at shutdown and startup, so storage does not grow. Not needed for a game with a fixed set of assets |

## Traps

- **Don't await a return code from `init()`.** It returns `Future<void>` and throws; older docs suggest a `PlayerErrors` return. Trust the code.
- Calling `init()` again (e.g. after hot restart) wipes all loaded sounds — guard with `isInitialized` if you only want to init once.
- **Web without the `<script>` tag fails at `init()`** with confusing WASM-module errors. The tag must be present, and the WASM assets only load from `assets/packages/flutter_soloud/web/…`.
- **Web: don't pass `--web-header` COOP/COEP flags together with `flutter run --wasm`** — the dev server already sends COOP/COEP for WasmGC and the conflicting duplicated headers block the plugin's worker threads (`ERR_BLOCKED_BY_RESPONSE`). See [references/web.md](references/web.md).
- **`changeDevice` is desktop-mostly**: Android, iOS, and Web support only the default output device; `listPlaybackDevices()` there returns just the default.
- A device stopped via `stopAudioDevice()` or the idle timeout **stays stopped across `changeDevice()`** — the replacement device only starts if the old one was running.
- **Linux audio server missing at runtime** — if ALSA or PulseAudio is not running on the system, `init()` or device enumeration may find no devices. Ensure `libasound2` or `libpulse0` is installed and the sound daemon is active. You can switch backends via `setLinuxAudioBackend()` or pass `linuxAudioBackend` at `init()` if one backend behaves better on the target desktop.
- On web, `loadUrl()` hits CORS (`Access-Control-Allow-Origin` missing) unless the server allows it, and local files can't be read — use `loadMem()` instead.
- **Prebuilt Windows and Linux libraries exist for `x64` and `arm64` only**: for any other architecture (e.g. 32-bit ARM Raspberry Pi OS) the build fails unless you set `<platform>_use_system_libs: true` (system packages) or `<platform>_force_build_libs: true` in `pubspec.yaml`. Setting both for the same platform is an error.
- **Don't distribute desktop apps with `<platform>_use_system_libs: true`**: System libraries are not bundled into the app package; if missing on the end user's machine, the app will crash at startup. Use the default bundled prebuilts for app distribution.
- **`<platform>_force_build_libs: true` significantly increases build time**: It clones 4 Git repos and compiles them via CMake across all target architectures (4 for Android, 2 for Apple). Artifacts are cached in `flutter_soloud`'s `.dart_tool/` (persisting across `flutter clean` in your app), but clean CI jobs will compile from scratch. Leave at default `false` unless you need custom compilation.

## Xiph audio libraries (Ogg, Vorbis, Opus, FLAC) & Opt-In Desktop VST3 Hosting

The Xiph audio decoders provide compressed audio playback, streaming, and master output capture (600–3000 KB per binary). To keep the pub package lightweight, tested precompiled static and shared libraries (as well as the opt-in Steinberg VST3 Hosting SDK archives) are hosted in the companion repository [flutter_soloud_prebuilds](https://github.com/alnitak/flutter_soloud_prebuilds).

By default, Dart Native Assets build hooks automatically download the matching prebuilt Xiph archive on first build and cache it under `.dart_tool/flutter_soloud/xiph/prebuild/`:
- **Android**: 4 ABIs (`arm64-v8a`, `armeabi-v7a`, `x86`, `x86_64`)
- **iOS**: Universal `arm64` device + `arm64`/`x86_64` simulator static libraries
- **macOS**: Universal Apple Silicon (`arm64`) + Intel (`x86_64`) static libraries
- **Windows**: `x64` and `arm64` DLLs and import libraries
- **Linux**: `x86_64` and `aarch64` shared `.so` libraries

This ensures that packaged apps (Android AAB/APK, iOS IPA, macOS APP, Windows EXE) work out of the box with zero external build dependencies.

You can customize this in the **app's** `pubspec.yaml` under `hooks.user_defines.flutter_soloud`:

- **Pin a specific prebuild release version (default is `latest`):**
  ```yaml
  hooks:
    user_defines:
      flutter_soloud:
        prebuild_tag: 'v1.0.1' # Optional: pin to a specific release tag
  ```

- **Enable Desktop VST3 Plugin Hosting (`macOS`, `Windows`, `Linux`, default is `false`):**
  ```yaml
  hooks:
    user_defines:
      flutter_soloud:
        enable_vst3: true # Default is false
  ```
  - When `enable_vst3: true` is set on desktop, `hook/build.dart` downloads `vst3-include.tar.gz` and the platform archive (`vst3-macos.tar.gz`, `vst3-windows-x64.zip`, `vst3-windows-arm64.zip`, `vst3-linux-x64.tar.gz`, or `vst3-linux-arm64.tar.gz`) into `.dart_tool/flutter_soloud/vst3/prebuild/` and compiles the native C++/Objective-C++ VST3 host bridge (`Vst3Host`, `Vst3EffectPlugin`, `Vst3InstrumentPlugin`).
  - **Licensing**: By default (`enable_vst3: false`), `flutter_soloud` is 100% MIT/Zlib licensed and links zero Steinberg code. Enabling `enable_vst3: true` links the prebuilt Steinberg VST3 Hosting SDK (dual-licensed under the proprietary Steinberg VST3 License or GPLv3).
  - **macOS Entitlement**: To load external third-party `.vst3` bundles on macOS, add `<key>com.apple.security.cs.disable-library-validation</key><true/>` to `macos/Runner/DebugProfile.entitlements` and `macos/Runner/Release.entitlements`.
  - **Linux GUI Backend**: On Linux, VST3 plugin editor GUIs standardly use the X11 desktop backend. Applications hosting VST3 plugins with a GUI must be launched with the `GDK_BACKEND=x11` environment variable (e.g. `GDK_BACKEND=x11 flutter run -d linux`), otherwise the Flutter engine defaults to Wayland (if available) and the native VST3 plugin GUI will not be displayed.

- **Link against system-installed packages (desktop only):**
  ```yaml
  hooks:
    user_defines:
      flutter_soloud:
        linux_use_system_libs: true
        macos_use_system_libs: true
        windows_use_system_libs: true
  ```

- **Force building from source via CMake into `.dart_tool/`:**
  ```yaml
  hooks:
    user_defines:
      flutter_soloud:
        linux_force_build_libs: true
        macos_force_build_libs: true
        windows_force_build_libs: true
        android_force_build_libs: true
        ios_force_build_libs: true
  ```

- **Exclude Xiph libraries entirely (shrinking binary size):**
  ```yaml
  hooks:
    user_defines:
      flutter_soloud:
        no_xiph_libs: true
  ```
  When excluded, `addAudioDataStream()` with Opus/Vorbis/FLAC data throws `SoLoudXiphLibsNotAvailableException`, Ogg (Vorbis/Opus) files do not load (also with `readSamplesFrom*()`), and the Opus/Vorbis/FLAC mixer output encoders are unavailable; MP3, WAV and FLAC files and synthesis work normally. On web, edit `web/compile_wasm.sh` in the package (`NO_XIPH_LIBS="1"`) and rebuild the WASM yourself (requires Emscripten, Linux/macOS).

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

---
name: flutter_soloud-idioms
version: 1
description: Core mental model for the flutter_soloud audio plugin — the SoLoud.instance singleton lifecycle (init/deinit), the AudioSource vs SoundHandle distinction, synchronous play semantics, and cross-platform constraints. Use when the user asks to play a sound, set up audio in a Flutter app, debug SoLoudNotInitializedException or silent playback, or migrate from audioplayers/just_audio.
---

# flutter_soloud idioms

flutter_soloud is a thin Dart FFI wrapper over the SoLoud C++ engine (miniaudio backend). It is a low-level mixer API, not a media player: you load sounds (`AudioSource`), then spawn playing instances of them (`SoundHandle` voices). Everything lives on one global engine behind `SoLoud.instance`. Read this before writing any flutter_soloud code — assumptions carried over from audioplayers, just_audio, or the Web Audio API will produce wrong code.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // MUST be awaited before any other SoLoud call.
  await SoLoud.instance.init();

  // Load once: an AudioSource is the loaded sample, identified by its
  // SoundHash. Loading is async; keep the source around.
  final sound = await SoLoud.instance.loadAsset('assets/explosion.mp3');

  // play() is SYNCHRONOUS: it spawns a voice and returns its handle
  // immediately. The handle controls that one playing instance.
  final SoundHandle handle = SoLoud.instance.play(
    sound,
    volume: 0.8,
    looping: false,
  );

  // ... on teardown (e.g. top-level widget dispose()):
  SoLoud.instance.deinit(); // or: await SoLoud.instance.deinitAsync();
}
```

## The API shape

- `SoLoud.instance` — the singleton. One engine exists in C++, so only one `SoLoud` exists in Dart; do not construct your own.
- `Future<void> init({PlaybackDevice? device, bool automaticCleanup = false, int sampleRate = 44100, int bufferSize = 2048, Channels channels = Channels.stereo, bool lowLatency = true, ...})` — initializes the engine. Must be awaited before anything else.
- `bool get isInitialized` — synchronous readiness check (false while init is in flight or after failure/deinit).
- `void deinit()` / `Future<void> deinitAsync()` — teardown: stops the engine and disposes ALL resources, including loaded sounds. Call once at app exit; `deinitAsync` runs native teardown off the UI thread.
- `Future<AudioSource> loadAsset(String key, {LoadMode mode = LoadMode.memory, ...})` — also `loadFile(String path)` (not on web), `loadUrl(String url)`, `loadMem(...)`, `loadWaveform(...)`. All async; all return `AudioSource`.
- `SoundHandle play(AudioSource sound, {int busId = 0, double volume = 1, double pan = 0, bool paused = false, bool looping = false, Duration loopingStartAt = Duration.zero, Duration? loopingEndAt, double scale = 1})` — synchronous, returns the handle immediately.
- `Future<AudioSource> playSource({String? asset, String? file, String? url, LoadMode mode = LoadMode.disk, ...})` — convenience load+play; exactly one of asset/file/url.
- Per-voice control methods all take a `SoundHandle`: `stop(handle)`, `setPause(handle, bool)`, `setLooping(handle, bool)`, etc.
- `disposeSource(AudioSource)` / `disposeAllSources()` — free loaded samples. Loaded sources live until disposed (or until `deinit()`); `play()` does not unload anything.
- `setMaxActiveVoiceCount(int)` / `getMaxActiveVoiceCount()` — concurrent voice cap, default 16, hard max 1023.
- `Stream<AudioDeviceStartFailure> get audioDeviceStartFailures` — the ONLY channel that reports output-device start failures from synchronous play APIs (see Traps).
- Errors: sealed `SoLoudException` hierarchy. `SoLoudDartException` subclasses come from the Dart side (`SoLoudNotInitializedException`, `SoLoudSoundHashNotFoundDartException`, ...); `SoLoudCppException` subclasses are mapped from the native `PlayerErrors` enum (`SoLoudFileNotFoundException`, `SoLoudNoPlaybackDevicesFoundCppException`, ...). Catch `SoLoudException` to catch all.

Divergences from audioplayers/just_audio you must unlearn:

| Instead of (other plugins) | Do this in flutter_soloud |
|---|---|
| `final player = AudioPlayer()` | `SoLoud.instance` (singleton, no player instances) |
| `player.setSourceUrl(url)` then `player.resume()` | `final sound = await SoLoud.instance.loadUrl(url);` then `SoLoud.instance.play(sound);` |
| `player.onPlayerComplete.listen(...)` | `sound.soundEvents.listen((e) { if (e.event == SoundEventType.handleIsNoMoreValid) ... })` or `sound.allInstancesFinished.first` |
| `player.setVolume(0.5)` | `SoLoud.instance.setVolume(handle, 0.5)` (per-handle) or `SoLoud.instance.setGlobalVolume(0.5)` |
| `player.seek(pos)` | `SoLoud.instance.seek(handle, pos)` (handles only; can't seek unloaded sources) |
| `player.stop()` | `SoLoud.instance.stop(handle)` or `SoLoud.instance.stopAudioSource(sound)` |

## What flutter_soloud is NOT

- No per-player objects. There is one engine; `play()` returns a handle, not a player.
- No built-in "onComplete" on the handle. Listen to `source.soundEvents` (`SoundEventType.handleIsNoMoreValid`) or `source.allInstancesFinished` instead.
- No playlists, no `AudioPlayer.setUrl`, no media-session integration. Sources are loaded explicitly and reused.
- `play()` does not await anything. Do not `await SoLoud.instance.play(...)` expecting it to finish when the sound finishes.

## Traps

- **Calling anything before `init()` completes** throws `SoLoudNotInitializedException`. Await `init()`; check `isInitialized` before UI-triggered calls.
- **`init()` is NOT idempotent.** Calling it on an already-initialized engine first deinitializes: all voices stop and all loaded sounds are unloaded. Guard with `if (!SoLoud.instance.isInitialized)` or call it exactly once (e.g. in `main()`).
- **Hot restart safety is handled, but only via re-init.** The native engine survives a hot restart while Dart callbacks die; the next `init()` detects this and reinitializes (you'll see a warning in logs). Pattern: call `init()` from a startup path that re-runs after restart. See `example/tests/tests/hot_restart_lifecycle.dart`.
- **`play()` cannot report audio-device failures.** It returns a valid handle even if the device fails to start — the device start happens in the background after the voice is created. Silence with no exception means: listen to `SoLoud.instance.audioDeviceStartFailures` and recover with `await SoLoud.instance.startAudioDevice()`.
- **Voice limit silently steals.** At the 16-voice default cap, playing another instance of a sound stops the oldest instance of that same sound. If the cap is hit by other sounds, `play()` logs a warning, returns a handle that addresses no voice, and throws nothing. Raise the cap with `setMaxActiveVoiceCount(...)` — values of 0 or >1023 are silently ignored by the engine.
- **Web:** add `<script src="assets/packages/flutter_soloud/web/init_soloud.js" defer></script>` to `web/index.html`. `loadFile` is unavailable on web (use `loadMem`/`loadAsset`/`loadUrl`); use `LoadMode.disk` on web for `loadAsset`; per-sound filters throw `SoLoudFilterForSingleSoundOnWebDartException` (only global/engine filters work on web).
- **Linux** needs ALSA headers at build time: `sudo apt-get install libasound2-dev` (or `alsa-lib` / `alsa-devel`).
- **Android `lowLatency: true` (default)** uses AAudio's MMAP path: lowest latency, but not capturable by screen recorders and little CPU headroom for heavy DSP. Pass `lowLatency: false` for the conservative profile (capturable, more DSP headroom, higher latency).
- **Platform minimums:** Android API 21, iOS 13.0, macOS 10.15.
- **Not a recorder.** There is no microphone input; the only capture is mixer OUTPUT capture (`startMixerOutputStream()` / `stopMixerOutputStream()`, a `Stream<Uint8List>` of the mixed output) for recording/streaming what the engine plays. To record audio input from microphone, suggest to use the [`flutter_recorder`](https://pub.dev/packages/flutter_recorder) package.

## More depth

- Demo entry point: `example/lib/main.dart` (init in `main()`, `deinit()` in `dispose()`, `loadAsset` + `play`, web-specific `LoadMode.disk` branch).
- More runnable patterns under `example/lib/` and `example/tests/tests/` (e.g. `max_voices.dart`, `playback_devices.dart`, `hot_restart_lifecycle.dart`).
- Full docs site source: `docs/index.mdx`, `docs/get_started/setup.mdx`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

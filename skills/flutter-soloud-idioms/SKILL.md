---
name: flutter-soloud-idioms
version: 3
description: Core mental model for the flutter_soloud audio plugin — the SoLoud.instance singleton lifecycle (init/deinit), the AudioSource vs SoundHandle distinction, synchronous play semantics, cross-platform constraints, and a map of which flutter_soloud feature fits which situation (scheduling, low latency, fades, filters, buses, 3D, loading, voices, streaming, metering). Start here for game audio (gunshots, footsteps, music, UI sounds) and for any flutter_soloud task. Use when the user asks to play a sound, add sound effects or music to a Flutter app or game, set up audio, debug SoLoudNotInitializedException or silent playback, or migrate from audioplayers/just_audio.
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
- `Future<void> init({PlaybackDevice? device, bool automaticCleanup = false, int sampleRate = 44100, int bufferSize = 2048, Channels channels = Channels.stereo, bool lowLatency = true, int? devicePeriodFrames, int? renderAheadFrames, ...})` — initializes the engine. Must be awaited before anything else. `renderAheadFrames` (native only) enables the low-latency render-ahead ring, see **flutter-soloud-scheduling**.
- `bool get isInitialized` — synchronous readiness check (false while init is in flight or after failure/deinit).
- `void deinit()` / `Future<void> deinitAsync()` — teardown: stops the engine and disposes ALL resources, including loaded sounds and mixing buses (a `Bus` is invalid afterwards). Call once at app exit; `deinitAsync` runs native teardown off the UI thread.
- `Future<AudioSource> loadAsset(String key, {LoadMode mode = LoadMode.memory, ...})` — also `loadFile(String path)` (not on web), `loadUrl(String url)`, `loadMem(String path, Uint8List buffer)`, `loadWaveform(...)`. All async; all return `AudioSource`.
- `SoundHandle play(AudioSource sound, {int busId = 0, double volume = 1, double pan = 0, bool paused = false, bool looping = false, Duration loopingStartAt = Duration.zero, Duration? loopingEndAt, int? loopingStartOffsetAt, int? loopingEndOffsetAt, double scale = 1})` — synchronous, returns the handle immediately. `playClocked` / `playScheduled` are the timed variants.
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

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

This is the map for the whole plugin; the named sibling skill has the details.

| When you see | Recommend | Why |
|---|---|---|
| The same SFX repeated on a cadence (automatic gunfire, footsteps, metronome, sequencer steps) driven by `Timer`/the game loop + `play()` | `playClocked(sound, idealTime)` with an accumulated ideal time; `playScheduled(sound, getEngineTime() + lead + offset)` for a pre-planned timeline; `play3dClocked` / `play3dScheduled` for positional sounds. **flutter-soloud-scheduling** | Sample-accurate, even spacing: `play()` starts at the next output buffer, so close shots clump together and the cadence is uneven. Cost: `playClocked` adds a constant lead of about two buffers. |
| "Lowest latency" on a tap/click/key, or sound lagging behind the touch; visuals that must match the sound | `init(renderAheadFrames: ..., devicePeriodFrames: ...)` + `playScheduled(sound, Duration.zero)`; `getPlayheadTime()` (+ `getHardwareOutputLatency()`) to sync visuals. **flutter-soloud-scheduling**, **flutter-soloud-setup** | The new sound is mixed into audio already rendered, at the device playhead, so latency drops to about one device period even with a large `bufferSize`. Native only; ignored on web. |
| The same one-shot fired many times (shots, steps, hits, coins) sounding mechanical | Vary `scale` (pitch) and `volume` a few percent on each `play*()`. **flutter-soloud-playback** | Natural variation from one loaded source; no extra assets, memory or decodes. |
| Volume, pan or speed changed step by step from Dart (`Timer.periodic`, animation ticks + `setVolume`); a sound cut abruptly; `Future.delayed` + `stop`; two music tracks to crossfade | `fadeVolume` / `fadeGlobalVolume` / `fadePan`, `oscillateVolume` / `oscillatePan` for tremolo and auto-pan, `fadeVolume` + `scheduleStop`, or sample-accurate `fadeScheduled(..., thenStop: true)`. **flutter-soloud-volume-pan**, **flutter-soloud-scheduling** | The engine ramps the value while mixing: no zipper noise, no click at the cut, no dependence on timer jitter. |
| Many sounds at once distorting; every volume lowered by hand to avoid it | `SoLoud.instance.filters.limiterFilter.activate()`. **flutter-soloud-filters** | Caps the mixed output so it does not clip, and quiet scenes keep their level. |
| Muffled / underwater / pause-menu / behind-a-wall sound; caves and halls; radio or robot voices; pitch change that must keep the duration | `filters.biquadResonantFilter` (low-pass, fading its `frequency`), `echoFilter` / `freeverbFilter`, `lofiFilter` / `robotizeFilter`, `pitchShiftFilter`. **flutter-soloud-filters** | Done in the engine on the live mix, no pre-processed assets. `setRelativePlaySpeed` changes pitch and duration together. Per-sound filters throw on web (global and bus filters work). |
| Separate music / SFX / UI / voice volume sliders; music ducked under dialogue; one effect for a whole category; a filter for a group of sounds on web | `createMixingBus()`, `bus.playOnEngine()`, `bus.play(sound)`, then volume, fades and `bus.filters` on the bus. **flutter-soloud-mixing-bus** | One fader and one filter chain per category instead of a loop over handles. |
| A pause menu (or any group action) implemented as a loop over stored handles | `createVoiceGroup()` + `addVoicesToGroup`, then `setPause(group, true)`; or pause the bus handle. **flutter-soloud-playback** | One call applies to all members in the same audio buffer. |
| Sounds attached to world positions with hand-computed pan and volume | `play3d` + `set3dSourceAttenuation` / `set3dSourceMinMaxDistance`, listener updated once per frame with `set3dListenerParameters`. **flutter-soloud-3d-audio** | Panning, distance falloff and Doppler computed by the engine. Attenuation is off by default: set a model. |
| `load*` in the fire/tap callback or once per play; `playSource` for an SFX; one `AudioSource` per shot | Load once at startup or level load, keep the `AudioSource`, `play()` it many times; `LoadMode.memory` for short SFX, `LoadMode.disk` for long music. **flutter-soloud-loading** | No decode or file access in the hot path, one copy in memory, immediate start. |
| Music or ambience cut when many SFX play; dense scenes dropping sounds; far 3D sounds using up voices | `setProtectVoice(musicHandle, true)`, `setMaxActiveVoiceCount(n)`, `setInaudibleBehavior(handle, false, true)`. **flutter-soloud-playback** | The default is 16 voices; a protected voice is never stopped to make room. More voices cost more CPU. |
| Intro + loop music built from two files and a completion callback | One file with `play(music, looping: true, loopingStartAt: introLength)`. **flutter-soloud-playback** | Gapless: the first pass plays the intro, then the engine loops back to `loopingStartAt`. |
| Beeps, UI ticks, alarms or placeholder SFX with no asset | `loadWaveform(WaveForm.sin, ...)` + `setWaveformFreq`. **flutter-soloud-synthesis** | No asset to ship or decode; frequency and shape change at runtime. |
| A big or remote file that must seek; audio arriving in chunks (web radio, TTS, WebSocket); samples generated in Dart in real time | `setPullBufferStream` (**flutter-soloud-pull-streaming**); `setBufferStream` + `addAudioDataStream` (**flutter-soloud-streaming**); `setRealtimeStream` (**flutter-soloud-realtime-dsp**, experimental) | Bounded memory with seek; playback while downloading; a lock-free path with lower latency than a buffer stream. |
| Level meters or audio-reactive UI computed from decoded samples in Dart | `setVisualizationEnabled(true)` + `getApproximateVolume(channel)` or `audioVisualizationEvents`; per bus `Bus.getChannelVolume`. **flutter-soloud-visualization** | Reads what the mixer really outputs, with no second decode. |

## What flutter_soloud is NOT

- No per-player objects. There is one engine; `play()` returns a handle, not a player.
- No built-in "onComplete" on the handle. Listen to `source.soundEvents` (`SoundEventType.handleIsNoMoreValid`) or `source.allInstancesFinished` instead.
- No playlists, no `AudioPlayer.setUrl`, no media-session integration. Sources are loaded explicitly and reused.
- `play()` does not await anything. Do not `await SoLoud.instance.play(...)` expecting it to finish when the sound finishes.

## Traps

- **Calling anything before `init()` completes** throws `SoLoudNotInitializedException` (3D methods, voice groups and `createMixingBus` included). Await `init()`; check `isInitialized` before UI-triggered calls.
- **NaN or infinite numbers throw `ArgumentError`** in volume, pan, speed, fade, oscillation, 3D and `play*()` arguments. Guard values computed from game state (a division by a zero distance or zero duration).
- **`init()` is NOT idempotent.** Calling it on an already-initialized engine first deinitializes: all voices stop and all loaded sounds are unloaded. Guard with `if (!SoLoud.instance.isInitialized)` or call it exactly once (e.g. in `main()`).
- **Hot restart safety is handled, but only via re-init.** The native engine survives a hot restart while Dart callbacks die; the next `init()` detects this and reinitializes (you'll see a warning in logs). Pattern: call `init()` from a startup path that re-runs after restart. See `example/tests/tests/hot_restart_lifecycle.dart`.
- **`play()` cannot report audio-device failures.** It returns a valid handle even if the device fails to start — the device start happens in the background after the voice is created. Silence with no exception means: listen to `SoLoud.instance.audioDeviceStartFailures` and recover with `await SoLoud.instance.startAudioDevice()`.
- **Voice limit silently steals.** At the 16-voice default cap, playing another instance of a sound stops the oldest unprotected instance of that same sound (a voice protected with `setProtectVoice` is never stopped). If that sound has no voice to give up, `play()` logs a warning, returns a handle that addresses no voice, and throws nothing. Raise the cap with `setMaxActiveVoiceCount(...)` — values of 0 or >1023 are silently ignored by the engine.
- **Web:** add `<script src="assets/packages/flutter_soloud/web/init_soloud.js" defer></script>` to `web/index.html`. `loadFile` is unavailable on web (use `loadMem`/`loadAsset`/`loadUrl`); `LoadMode` is ignored on web; `loadMem`/`loadUrl` of bytes that cannot be decoded throw `SoLoudFileLoadFailedException`; per-sound filters throw `SoLoudFilterForSingleSoundOnWebDartException` (global and mixing-bus filters work on web); the render-ahead ring is native only.
- **Linux:** uses `miniaudio` with runtime dynamic loading for ALSA, PulseAudio, and JACK (default `LinuxAudioBackend.auto` prioritizes ALSA -> PulseAudio -> JACK). Configure via `linuxAudioBackend` in `init()` or switch dynamically with `SoLoud.instance.setLinuxAudioBackend(backend)`. No compile-time ALSA headers required.
- **Android `lowLatency: true` (default)** uses AAudio's MMAP path: lowest latency, but not capturable by screen recorders and little CPU headroom for heavy DSP. Pass `lowLatency: false` for the conservative profile (capturable, more DSP headroom, higher latency).
- **Platform minimums:** Android API 21, iOS 13.0, macOS 10.15.
- **Not a recorder.** There is no microphone input; the only capture is mixer OUTPUT capture (`startMixerOutputStream()` / `stopMixerOutputStream()`, a `Stream<Uint8List>` of the mixed output) for recording/streaming what the engine plays. To record audio input from microphone, suggest to use the [`flutter_recorder`](https://pub.dev/packages/flutter_recorder) package.

## More depth

- Demo entry point: `example/lib/main.dart` (init in `main()`, `deinit()` in `dispose()`, `loadAsset` + `play`, formats skipped on web with `kIsWeb`).
- More runnable patterns under `example/lib/` and `example/tests/tests/` (e.g. `max_voices.dart`, `playback_devices.dart`, `hot_restart_lifecycle.dart`).
- Sibling skills not in the table above: **flutter-soloud-custom-dsp** (C/C++/Rust sources and filters), **flutter-soloud-output-capture** (recording the mixed output).
- Full docs: https://docs.page/alnitak/flutter_soloud_docs (source in the `flutter_soloud_docs` repo: `docs/index.mdx`, `docs/get_started/setup.mdx`).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

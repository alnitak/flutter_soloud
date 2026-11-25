---
name: flutter-soloud-playback
version: 4
description: Teaches everyday playback control in the flutter_soloud package — play()/playSource, pause/stop/seek, looping regions, play speed, per-play pitch/volume variation, voice limit and protection, and voice groups. Use when the user asks to play a sound or sound effect, play the same sound many times (gunshots, footsteps, hits, UI clicks), loop music or an engine hum, pause/resume/stop playback or build a pause menu, seek or show position, loop a region of a track, change playback speed, or manage concurrent voices with flutter_soloud. For repeated sounds on a regular cadence, the lowest latency on a tap, or sample-accurate timing (playClocked, playScheduled), see flutter-soloud-scheduling.
---

flutter_soloud is not a "player object" API like audioplayers or just_audio. You load audio once into an `AudioSource` (the sample data, kept in RAM or on disk), then start any number of playing instances of it with `play()`. Each instance is a `SoundHandle` — an ephemeral voice that becomes invalid the moment it stops or ends. All control methods (`setPause`, `seek`, `setVolume`, ...) take a `SoundHandle`, not the source.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  final source = await SoLoud.instance.loadAsset('assets/music.mp3');

  // play() is synchronous and returns immediately.
  final handle = SoLoud.instance.play(
    source,
    volume: 0.8,
    pan: 0.0, // -1.0 left .. 0.0 center .. 1.0 right
  );

  SoLoud.instance.seek(handle, const Duration(seconds: 10));
  SoLoud.instance.setPause(handle, true);   // pause
  SoLoud.instance.setPause(handle, false);  // resume
  await SoLoud.instance.stop(handle);       // completes when the voice really ends

  await SoLoud.instance.disposeSource(source);
}
```

## The API shape

All methods below live on the singleton `SoLoud.instance` and throw `SoLoudNotInitializedException` if called before `await SoLoud.instance.init()` completes.

### Starting playback

```dart
SoundHandle play(
  AudioSource sound, {
  int busId = 0,
  double volume = 1,
  double pan = 0,
  bool paused = false,
  bool looping = false,
  Duration loopingStartAt = Duration.zero,
  Duration? loopingEndAt,
  int? loopingStartOffsetAt,
  int? loopingEndOffsetAt,
  double scale = 1,
})
```

- Synchronous and non-blocking — do **not** `await` it (unlike just_audio's `play()`).
- The sound starts at the beginning of the next output buffer (~0–46 ms at the default 2048-frame buffer / 44.1 kHz). The `AudioSource` stays loaded and replayable until `disposeSource()`.
- `scale` is the initial speed multiplier (1.0 = normal); available on all `play*` variants.
- `volume`, `pan` or `scale` that is NaN or infinite throws `ArgumentError`.
- For the same sound repeated on a cadence (gunfire, footsteps), use `playClocked` / `playScheduled` from **flutter-soloud-scheduling**: they take the same parameters except `paused`.
- Max concurrent voices defaults to 16 (`setMaxActiveVoiceCount`).

```dart
Future<AudioSource> playSource({
  String? asset, String? file, String? url,  // exactly one
  LoadMode mode = LoadMode.disk,
  int busId = 0, double volume = 1, double pan = 0, bool paused = false,
  bool looping = false,
  Duration loopingStartAt = Duration.zero, Duration? loopingEndAt,
})
```

- Fire-and-forget load+play. Loads with `autoDispose: true`, so the source frees itself when playback ends.
- Returns the **`AudioSource`, not a `SoundHandle`** — you cannot control the voice afterwards. If you need pause/seek/stop, call `loadAsset()` + `play()` yourself.
- There is a load lag between the call and the audible start; don't use it for latency-sensitive one-shots.

### Pause / resume

```dart
SoLoud.instance.pauseSwitch(handle);        // toggle
SoLoud.instance.setPause(handle, true);     // pause
SoLoud.instance.setPause(handle, false);    // resume
final isPaused = SoLoud.instance.getPause(handle);
```

### Stopping

```dart
await SoLoud.instance.stop(handle);   // Future completes when the voice has actually ended
SoLoud.instance.stopAll();            // every voice, synchronous
SoLoud.instance.stopAudioSource(source);  // every voice playing that source
```

- `stop()` awaits the native voice-ended event (with an internal ~300 ms fallback). Stopping an already-ended handle is a no-op, not an error.
- None of these dispose the source — it stays loaded and replayable. Use `disposeSource(source)` to free memory.
- Do not confuse with `stopAudioDevice()`, which suspends the output device but leaves voices untouched.

### Position and seek

```dart
SoLoud.instance.seek(handle, const Duration(seconds: 30));
final pos = SoLoud.instance.getPosition(handle);      // play head of this voice
final len = SoLoud.instance.getLength(source);        // total length of the source
final t = SoLoud.instance.getStreamTime(handle);      // how long this voice has been playing
```

- `getLength` takes the **`AudioSource`**, not a handle. `getPosition`/`getStreamTime` take handles.
- Negative seek times are clamped to `Duration.zero`.
- For `BufferingType.released` streams, `getPosition` is meaningless — use `getStreamTimeConsumed(source)` instead.

### Looping

Loop regions are half-open: `[start, end)`. A `null` end means the source's natural end.

```dart
// At play time (preferred — applies from the first decoded sample):
final h = SoLoud.instance.play(
  source,
  looping: true,
  loopingStartAt: const Duration(seconds: 1),
  loopingEndAt: const Duration(seconds: 5), // exclusive
);
// Or frame offsets (mutually exclusive with the Duration params):
final h2 = SoLoud.instance.play(
  source,
  looping: true,
  loopingStartOffsetAt: 44100,
  loopingEndOffsetAt: 220500,
);

// On a live voice:
SoLoud.instance.setLooping(h, true);
SoLoud.instance.setLoopPoint(h, const Duration(seconds: 1));   // start
SoLoud.instance.setLoopEndPoint(h, const Duration(seconds: 5)); // null = natural end
final looping = SoLoud.instance.getLooping(h);
final start = SoLoud.instance.getLoopPoint(h);
Duration? end = SoLoud.instance.getLoopEndPoint(h);
```

Live `setLoopPoint`/`setLoopEndPoint` changes are reflected in the getters immediately but only apply to playback at the next source refill — up to ~512 already-decoded frames plus backend latency still use the old region. Pass the bounds to `play()` when they must hold from sample one.

### Playback speed

```dart
final h = SoLoud.instance.play(source, scale: 1.5); // initial speed
SoLoud.instance.setRelativePlaySpeed(h, 2.0);        // live change
final speed = SoLoud.instance.getRelativePlaySpeed(h);
```

Speed changes the effective sample rate (pitch shifts with it — there is no built-in time-stretch). Faster = more CPU and memory; slower is cheaper.

### Voice introspection

```dart
final n = SoLoud.instance.getActiveVoiceCount();  // currently audible voices
final m = SoLoud.instance.getVoiceCount();        // voices the app asked to play
final k = SoLoud.instance.countAudioSource(source); // voices playing this source
final ok = SoLoud.instance.getIsValidVoiceHandle(handle); // still playing/paused?
SoLoud.instance.setMaxActiveVoiceCount(32); // default 16, hard max 1023
```

### Protection and inaudible behavior

```dart
SoLoud.instance.setProtectVoice(musicHandle, true); // never stopped to make room at the voice limit
SoLoud.instance.setInaudibleBehavior(handle, false, true); // (mustTick, kill); for 3D voices
```

By default a 3D voice that becomes inaudible is paused and resumes when audible again; `kill: true` stops it instead (the handle becomes invalid), `mustTick: true` keeps it advancing while inaudible.

### Voice groups

```dart
final group = SoLoud.instance.createVoiceGroup(); // returns a SoundHandle, throws SoLoudCreateVoiceGroupDartException on failure
SoLoud.instance.addVoicesToGroup(group, [handle1, handle2]);
SoLoud.instance.setPause(group, true);            // also setVolume, setPan, setRelativePlaySpeed, fadeVolume, scheduleStop, stop
SoLoud.instance.isVoiceGroup(group);              // true for group handles
SoLoud.instance.isVoiceGroupEmpty(group);         // ended voices are trimmed automatically
SoLoud.instance.destroyVoiceGroup(group);         // does NOT stop the voices in it
```

Only live voice handles are added: ended voices and other groups are skipped.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| The same SFX repeated on a cadence (automatic gunfire, footsteps, engine ticks, metronome) with `Timer.periodic` or the game loop calling `play()` | `playClocked(sound, idealTime)` with an accumulated ideal time, or `playScheduled` for a timeline known in advance. **flutter-soloud-scheduling** | `play()` starts at the next output buffer: shots inside one buffer start on the same sample and the cadence sounds uneven. The clocked/scheduled calls are sample-accurate. Cost: `playClocked` adds a constant lead of about two buffers. |
| A single reactive sound (tap, key, jump, one shot) where the user wants the lowest latency or says the sound is late | `init(renderAheadFrames: ..., devicePeriodFrames: ...)` + `playScheduled(sound, Duration.zero)`. **flutter-soloud-scheduling** | Latency of about one device period instead of up to one `bufferSize`. Native only; without it plain `play()` is the right call. |
| The same one-shot fired many times (shots, steps, hits, coins) sounding mechanical | Vary `scale` and `volume` slightly per call, e.g. `play(shot, scale: 0.95 + rnd.nextDouble() * 0.1, volume: 0.8 + rnd.nextDouble() * 0.2)` | Each voice gets its own pitch and level from the same loaded source: no extra assets or memory. |
| `loadAsset`/`loadFile` inside the fire or tap callback, a new `AudioSource` per shot, or `playSource` for an SFX | Load once, keep the `AudioSource` in a field, call `play()` on it each time. **flutter-soloud-loading** | `play()` is synchronous and one source feeds any number of voices; loading in the hot path adds the load time to every shot and every load of the same file is another copy in memory (with a warning in the log). |
| A loop restarted from Dart (replay on `allInstancesFinished`, a `Timer` as long as the file) for engine hum, ambience or music | `play(sound, looping: true)`, with `loopingStartAt` / `loopingEndAt` for a region; `setRelativePlaySpeed` to follow engine RPM | The engine wraps the loop while mixing: no gap from the Dart round trip, no drift. |
| Intro + loop music as two files chained on a completion event | One file and `play(music, looping: true, loopingStartAt: introLength)` | The first pass plays the intro, later passes restart at `loopingStartAt`; one voice, no gap. |
| Music or ambience that stops when many SFX play, or SFX that do not play in dense scenes | `setProtectVoice(musicHandle, true)` on long-running voices and `setMaxActiveVoiceCount(n)`; `setInaudibleBehavior(handle, false, true)` for far 3D one-shots | The default limit is 16 voices and the oldest voice of the same sound is stopped first; a protected voice is never stopped. More voices cost more CPU. |
| A pause menu or "mute these sounds" done with a loop over stored handles | `createVoiceGroup()` + `addVoicesToGroup(group, handles)`, then `setPause(group, true)` (also `setVolume`, `fadeVolume`, `stop` on the group); for a whole category use a bus, **flutter-soloud-mixing-bus** | One call reaches every member in the same audio buffer; ended voices leave the group by themselves. |

## Traps

- **Handles die silently.** A `SoundHandle` becomes invalid when the voice ends, is stopped, or is killed to make room at the voice limit. Most control calls on a dead handle do nothing (setters, fades, `scheduleStop`, `stop`); `setPause` and `pauseSwitch` throw `SoLoudSoundHandleNotFoundCppException`. Guard with `getIsValidVoiceHandle()` when in doubt.
- **Voice limit kills quietly.** At the max active voice count (default 16), playing a sound whose source already has voices stops its oldest unprotected voice; if that source has no voice, or all its voices are protected, the new sound simply does not play — no exception, and the returned handle addresses nothing. Protect music with `setProtectVoice` and/or raise `setMaxActiveVoiceCount` (values of 0 or >1023 are silently ignored).
- **`setRelativePlaySpeed(handle, 0)` is undefined behavior, likely a crash.** The lower clamp is 0.05, applied silently.
- **Mixing Duration-based and frame-offset looping params** (`loopingStartAt`/`loopingEndAt` vs `loopingStartOffsetAt`/`loopingEndOffsetAt`) is mutually exclusive and fails an assert in debug builds; invalid offsets throw `ArgumentError`.
- **MP3 + `LoadMode.disk` seek lag.** Seeking a disk-loaded MP3 is slow (the codec must walk frames to find the position). For a music-player-style seek bar, load with `LoadMode.memory` or use a seekable format.
- **Released buffer streams can't seek.** `seek()` on a stream created with `BufferingType.released` throws `SoLoudBufferStreamWithReleasedBufferTypeCannotBeSeekedCppException`; an unseekable handle throws `SoLoudInvalidParameterException`.
- **`stopAll()`/`stopAudioSource()` do not free memory.** Sources stay loaded; forgetting `disposeSource()` is the common leak.
- **`play()` latency is up to one output buffer (~46 ms at 2048 samples).** Fine for ordinary reactive sounds; if the user asks for the lowest possible latency on click/press or complains about latency, use the render-ahead ring with `playScheduled(sound, Duration.zero)` from **flutter-soloud-scheduling** (configured via `renderAheadFrames` + `devicePeriodFrames: 512`), which achieves ~11 ms hardware latency via retroactive remixing. For rhythmic/scheduled playback, also use **flutter-soloud-scheduling**.
- **Calling any method before `await SoLoud.instance.init()` finishes** throws `SoLoudNotInitializedException` (voice group methods included). Re-calling `init()` deinitializes first: all voices stop and all sources unload.

## More depth

- Ultra-low latency reactive playback & sample-accurate timing (`playScheduled(sound, Duration.zero)`, `playClocked`, render-ahead ring): see the sibling skill **flutter-soloud-scheduling**.
- Background-playback demo with play/pause/seek wired to `audio_service`: `example/lib/audio_context/audio_context.dart` in the plugin repo.
- Tests worth skimming for edge behavior: `test/common/max_voices_test.dart`, `test/common/protect_voice_test.dart`, `test/common/voice_group_test.dart`, `test/common/looping_test.dart`.
- Full doc page: https://docs.page/alnitak/flutter_soloud_docs/audio/playback.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

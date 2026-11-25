---
name: flutter-soloud-scheduling
version: 4
description: Teaches sample-accurate audio scheduling and ultra-low-latency playback in flutter_soloud with playScheduled (absolute engine-time scheduling, sample-accurate auto-stop, and retroactive render-ahead remixing for lowest possible latency) and playClocked (sounds anchored to a running "physics time" clock without buffer-clumping), plus clock helpers getEngineTime/getPlayheadTime/getOutputLatency/getHardwareOutputLatency/resetStreamTime and stopScheduled/fadeScheduled. Use when the user asks for the same sound repeated on a cadence in a game (automatic gunfire, gunshots, footsteps, engine ticks) or plays sounds from Timer.periodic or the game loop, for lowest possible latency on click/tap/button press, a metronome, step sequencer, rhythm game, pre-computed music timeline/score, visuals synced to audio, or complains that sounds have latency or periodic play() calls clump or drift.
---

# Sample-accurate scheduling & Ultra-low latency

Plain `SoLoud.instance.play()` starts a sound at the **next output buffer boundary**: everything launched within the same buffer starts at the exact same sample ("clumps"), and reactive playback waits up to one full buffer size (~46 ms at the default 2048-sample buffer / 44100 Hz). flutter_soloud solves both latency and clumping with two scheduling APIs combined with the native render-ahead ring:

- `playScheduled(sound, atTime, {duration, ...})` — absolute time on the **engine's own clock** (`getEngineTime()`), schedulable arbitrarily far ahead; optional `duration` gives a sample-accurate auto-stop. **For the lowest possible latency on user input (button clicks/keypresses)**, pass `atTime: Duration.zero` (or past engine time) with `renderAheadFrames`: the voice is retroactively remixed into the unconsumed ring buffer at the hardware playhead (~11 ms with `devicePeriodFrames: 512`), bypassing buffer-boundary waits.
- `playClocked(sound, soundTime, ...)` — you feed your own monotonically increasing "physics time"; the first call anchors it to the audio clock with a built-in ~2-output-buffer lead, subsequent calls are placed relative to that anchor. Best for evenly spaced, rapid repeated clicks/SFX driven by your own loop or rapid user tapping without clumping (metronome, machine-gun fire, footsteps).

## Minimal examples

### Lowest possible latency on button click (reactive playback)

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  // 1. Configure the render-ahead ring for hardware-level latency (~11.6 ms at 44.1 kHz)
  await SoLoud.instance.init(
    bufferSize: 2048,
    renderAheadFrames: 2048, // enables retroactive remixing into the ring buffer
    devicePeriodFrames: 512, // hardware device period
  );

  final clickSound = await SoLoud.instance.loadAsset('assets/click.wav');

  // 2. In your button / tap callback:
  // Passing Duration.zero (or past engine time) plays immediately at the playhead
  // via retroactive remixing, with the lowest possible latency.
  SoLoud.instance.playScheduled(clickSound, Duration.zero);
}
```

### Automatic fire or footsteps from a game loop with `playClocked`

```dart
import 'dart:math';

import 'package:flutter_soloud/flutter_soloud.dart';

/// Shots every [interval] seconds while the trigger is held. Call [update]
/// from the game loop (Flame `update(dt)`, a `Ticker`, ...).
class AutoFire {
  AutoFire(this.shot, {this.interval = 0.1}); // 600 rounds per minute

  final AudioSource shot; // loaded once, reused for every shot
  final double interval;
  final _rnd = Random();

  double _gameTime = 0; // accumulated dt: the ONE clock of all clocked calls
  double _nextShot = 0; // ideal time of the next shot, on that clock
  bool _firing = false;

  void update(double dt, {required bool triggerHeld}) {
    _gameTime += dt;
    if (!triggerHeld) {
      _firing = false;
      return;
    }
    if (!_firing) {
      _firing = true;
      if (_nextShot < _gameTime) _nextShot = _gameTime; // first shot: now
    }
    // Ideal times, not frame times: frame jitter does not reach the audio,
    // and two shots due in the same frame keep their spacing.
    while (_nextShot <= _gameTime) {
      SoLoud.instance.playClocked(
        shot,
        Duration(microseconds: (_nextShot * 1e6).round()),
        scale: 0.96 + _rnd.nextDouble() * 0.08, // slight pitch variation
      );
      _nextShot += interval;
    }
  }

  /// After a pause menu or when the app is back in the foreground.
  void onResume() => SoLoud.instance.resetStreamTime();
}
```

Footsteps are the same pattern with `interval` taken from the walk cycle. Feed every clocked call in the game (2D and `play3dClocked`) from the same `_gameTime`. Call `resetStreamTime()` whenever the game clock stood still while audio kept running: otherwise the next times are in the past and play as soon as possible, like `play()`. A single shot per tap does not need this: use `play()`, or the render-ahead setup above.

### Metronome with `playClocked`

Metronome with `playClocked` (the canonical pattern, from `example/lib/metronome/metronome.dart`):

```dart
import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init(); // default bufferSize 2048 is fine now
  final tick = await SoLoud.instance.loadAsset('assets/tick.wav');

  var physicsTime = Duration.zero;
  Timer.periodic(const Duration(milliseconds: 100), (_) {
    physicsTime += const Duration(milliseconds: 100);
    SoLoud.instance.playClocked(tick, physicsTime);
  });
}
```

### Pre-computed timeline with `playScheduled`

```dart
final anchor = SoLoud.instance.getEngineTime() +
    const Duration(milliseconds: 200); // small lead
for (final note in score) {
  final atTime = anchor + note.offset;
  SoLoud.instance.playScheduled(note.source, atTime, duration: note.length);
}
```

## The API shape

All methods are on the `SoLoud` singleton (`SoLoud.instance`), synchronous, and return immediately; scheduled/clocked variants return a `SoundHandle` like `play()`.

```dart
SoundHandle playClocked(
  AudioSource sound,
  Duration soundTime, {
  int busId = 0,
  double volume = 1,
  double pan = 0,
  double scale = 1,
  bool looping = false,
  Duration loopingStartAt = Duration.zero,
  Duration? loopingEndAt,
  int? loopingStartOffsetAt,
  int? loopingEndOffsetAt,
});
```

- `soundTime` is **your** clock — any monotonically increasing `Duration` (accumulated ideal tick times, a game physics clock, ...). It is not engine time and not wall time.
- First call after init/`resetStreamTime()` anchors your clock to the audio clock, leading by ~2 output buffers (2 × `bufferSize` frames: about 93 ms at the default 2048 frames / 44.1 kHz) — that is the constant latency, by design.
- If the clock goes backwards or jumps > ~2 s, the engine silently re-anchors. A time already in the past plays as soon as possible.
- All clocked calls share one anchor — they must all use the same time base.
- Divergence from other packages: no `paused` parameter (unlike `play()`), and you schedule by passing a time, not by pre-creating a paused player and calling `resume` later.
- `volume`, `pan` or `scale` that is NaN or infinite throws `ArgumentError` (all `play*` variants).

```dart
SoundHandle playScheduled(
  AudioSource sound,
  Duration atTime, {
  Duration? duration,
  int busId = 0,
  double volume = 1,
  double pan = 0,
  double scale = 1,
  bool looping = false,
  Duration loopingStartAt = Duration.zero,
  Duration? loopingEndAt,
  int? loopingStartOffsetAt,
  int? loopingEndOffsetAt,
});
```

- `atTime` is an absolute **engine time** (the `getEngineTime()` clock), not a delay and not your physics clock. No anchor, no ~2 s window — schedule as far ahead as you like. Past times play as soon as possible.
- `duration`: if provided, the sound auto-stops at `atTime + duration`, atomically and sample-accurately (even durations shorter than one buffer). Keep the returned handle to cancel a still-pending sound with `stop(handle)`.

Clock helpers:

```dart
Duration getEngineTime();            // engine's global mix clock; time base for playScheduled/stopScheduled/fadeScheduled. Advances only while the device is mixing.
Duration getPlayheadTime();          // engine time of the sample currently reaching the device ("true output" clock). Equals getEngineTime() when the render-ahead ring is disabled (default) and on web.
Duration getOutputLatency();         // internal software mix cushion (render-ahead ring depth + 1 device period); Duration.zero when render-ahead is disabled and on web.
Duration getHardwareOutputLatency(); // external OS/HAL/DAC presentation delay (AAudio timestamp, AVAudioSession/CoreAudio, WASAPI, Web Audio); Duration.zero during warm-up or when unsupported.
void resetStreamTime();              // reset the playClocked/play3dClocked anchor; the next clocked call re-anchors. Call when (re)starting a session.
```

Scheduled stop/fade (sample-accurate, pinned to the engine clock — unlike `scheduleStop`, which measures from call time and is buffer-quantized):

```dart
void stopScheduled(SoundHandle handle, Duration atTime);
void fadeScheduled(
  SoundHandle handle,
  Duration atTime,
  double to,
  Duration time, {
  bool thenStop = false, // stop when the fade ends at atTime + time
});
```

Both ignore a handle that is no longer valid; a past `atTime` acts at once. `fadeScheduled` fades from the volume the voice has at call time and throws `ArgumentError` if `to` is NaN or infinite.

Positional variant — same scheduling semantics as `playClocked`, 3D position/velocity instead of `pan`:

```dart
SoundHandle play3dClocked(
  AudioSource sound,
  Duration soundTime,
  double posX, double posY, double posZ, {
  double velX = 0, double velY = 0, double velZ = 0,
  int busId = 0,
  double volume = 1,
  double scale = 1,
  bool looping = false,
  ... // same looping params as playClocked
});
```

(There is also a `play3dScheduled`, the 3D version of `playScheduled`, and `Bus.playClocked` / `Bus.play3dClocked` / `Bus.playScheduled` on mixing buses.)

**Which one to use:**
- **Lowest possible latency on reactive user input (button clicks, keypresses, instant SFX):** use `playScheduled(sound, Duration.zero)` (or `play()`) with `renderAheadFrames: bufferSize` and `devicePeriodFrames: 512` passed to `init()`. The engine retroactively remixes the sound into the render-ahead ring for ~11 ms hardware-level latency, avoiding buffer-boundary delays.
- **Sounds repeated on a cadence (automatic gunfire, footsteps, metronome, rapid bursts):** use `playClocked` with an accumulated `physicsTime`. The built-in ~2-buffer lead absorbs timer and UI jitter so sounds are spaced sample-accurately without clumping together at buffer boundaries.
- **Pre-planned timelines and score playback:** use `playScheduled` with `anchor = getEngineTime() + lead` for sample-accurate scheduling and atomic `duration` auto-stops.
- **Everyday non-critical reactive sounds:** plain `play()` is fine if ~20–46 ms buffer latency is acceptable.

## The metronome walkthrough

`example/lib/metronome/metronome.dart` runs all three modes side by side (`play`, `playClocked`, `playScheduled`) with `bufferSize: 4096` to make buffer-boundary clumping audible. The pattern to copy:

1. Load the tick sounds once in `initState` (`loadAsset`), reuse the `AudioSource`s.
2. Keep an accumulated ideal time: `physicsTime += interval` per timer tick — never the timer's own firing time, which jitters.
3. Mode `clocked`: `playClocked(sound, physicsTime)`.
4. Mode `scheduled`: anchor once on start — `engineAnchor = getEngineTime() + 200ms` — then `playScheduled(sound, engineAnchor + physicsTime, duration: 20ms)`.
5. On every (re)start or parameter change: zero `physicsTime` **and** call `resetStreamTime()`; re-anchor for scheduled mode. Without this, the first calls land against a stale anchor and play as soon as possible.

## Render-Ahead Ring (ultra-low reactive latency)

When `init(renderAheadFrames: >0)` is passed on native platforms, the engine pre-mixes audio into a ring buffer:

- Reactive `play()` and `playScheduled()` calls are retroactively remixed into the unconsumed section of the ring, achieving hardware-level latency (~11 ms with `devicePeriodFrames: 512`) even when `bufferSize` is set to 2048 or 4096.
- `renderAheadFrames` is how far ahead the engine mixes; `devicePeriodFrames` (default 512 when omitted) is the device period used with the ring. Both are ignored on web.
- Limits: after a change the engine cannot replay on a playing voice (a seek, unpausing it, its loop region, its filters or their parameters, the global volume), new sounds in the next `renderAheadFrames` are placed at the end of the ring instead of at the playhead. With the ring, the global volume is applied before the global filters (audible only with a limiter or compressor).
- `SoLoud.instance.isRenderAheadEnabled`: checks if the ring buffer is active.
- `SoLoud.instance.getPlayheadTime()`: engine time of the sample currently reaching the device (use this to sync UI/animations to what the listener hears right now).
- `SoLoud.instance.getOutputLatency()`: internal software mix cushion (render-ahead ring depth plus one device period; `Duration.zero` when disabled or on web).
- `SoLoud.instance.getHardwareOutputLatency()`: external OS/HAL/DAC presentation delay from the device callback to the physical transducer (speakers, headphones, USB DAC, Bluetooth). Works independently of the render-ahead ring across Android (AAudio), iOS (`AVAudioSession`), macOS (CoreAudio), Windows (WASAPI), and Web (Web Audio `AudioContext`). Total delay from an engine mix quantum to the listener's ear is `getOutputLatency() + getHardwareOutputLatency()`.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| The same SFX repeated on a cadence (automatic gunfire, footsteps, engine ticks, metronome, sequencer steps) from `Timer.periodic` or the game loop calling `play()` | `playClocked(sound, idealTime)` where `idealTime` is accumulated (`t += interval`), never the timer's fire time; see the game-loop example above | `play()` starts at the next output buffer: calls inside one buffer start on the same sample and the cadence is uneven, more so with a large `bufferSize`. `playClocked` keeps the spacing sample-accurate. Cost: a constant lead of about two buffers (2 × `bufferSize`). |
| A sequence known in advance (melody, rhythm-game chart, countdown beeps, a burst of N shots) built with `Future.delayed` or chained timers | Read `getEngineTime()` once, then `playScheduled(sound, anchor + offset, duration: ...)` for every note, with 100-200 ms of lead | Every start is pinned to the engine clock with sample accuracy and no drift; no time-window limit; the Dart event loop is out of the timing path. |
| "Lowest latency" on a tap, key, drum pad or single shot, or the sound arriving late after the touch | `init(renderAheadFrames: ..., devicePeriodFrames: ...)` + `playScheduled(sound, Duration.zero)` | The voice is mixed into already rendered audio at the device playhead: about one device period of latency instead of up to one `bufferSize`. Native only; on web the parameters are ignored. |
| `bufferSize` lowered (256, 512) only to get tighter timing or lower latency | Keep the default `bufferSize` and use `playClocked` / `playScheduled`, or the render-ahead ring for reactive sounds | Scheduled starts are sample-accurate whatever the buffer size; very small buffers risk underruns (crackles), above all on mobile. |
| `Future.delayed` + `stop()`, or notes that must end on the beat | `playScheduled(..., duration: ...)`, `stopScheduled(handle, atTime)`, or `fadeScheduled(handle, atTime, 0, fadeTime, thenStop: true)` | The stop is placed on the engine clock, sample-accurate, even for lengths shorter than one buffer; `scheduleStop` and Dart timers are measured from the call and quantized to buffers. |
| Visuals timed with `Stopwatch`/`DateTime` against the music (rhythm-game notes, beat pulses, muzzle flash) | Compare the scheduled engine time with `getPlayheadTime()`, and add `getHardwareOutputLatency()` for the device route | It is the clock of what the listener hears; wall clocks drift from the audio clock, and Bluetooth routes add a delay only the hardware latency reports. |
| Repeated positional SFX (an enemy machine gun, NPC footsteps) played with `play3d` from a timer | `play3dClocked(sound, idealTime, x, y, z)` or `play3dScheduled`. **flutter-soloud-3d-audio** | Same sample-accurate spacing with 3D position; all clocked calls share one clock, so use the same game time as the 2D ones. |

## Traps

- **Times must be monotonically increasing for `playClocked`.** Reusing a stale accumulator or starting a new session without `resetStreamTime()` makes ticks fire immediately (past times play ASAP) or triggers a silent re-anchor after a >2 s jump. Always zero your accumulator and call `resetStreamTime()` together.
- **Dart timers fire late.** Never pass "now" or the timer's fire time as the scheduled time — accumulate ideal times and keep a lead. For `playScheduled`, anchor with ~100–200 ms of lead (`getEngineTime() + 200ms`); a voice can only be delayed, never advanced.
- **`playClocked` has a ~2 s lookahead limit.** Gaps longer than ~2 s look like a clock jump and cause re-anchoring. For long-range scheduling use `playScheduled`.
- **Constant ~2-buffer latency with `playClocked`.** `playClocked` introduces a built-in ~2-buffer lead to absorb timing jitter. For isolated single-click reactive feedback where minimum latency is paramount, use `playScheduled(sound, Duration.zero)` with `renderAheadFrames` instead. Use `playClocked` when rapid repeated clicks/beats must be evenly spaced without clumping.
- **The engine clock only advances while the device is mixing.** `getEngineTime()` freezes when output is stopped; a pending `playScheduled` voice keeps the device running. Scheduling against a stopped device is safe — the voice keeps its exact offset and starts counting down once the device runs.
- **No `paused` on clocked/scheduled variants.** Models reaching for the audioplayers/just_audio "prepare then resume" pattern should instead pass the time; to cancel a pending scheduled sound, keep the handle and call `stop(handle)`.
- **No `Future`, no completion callback per call.** These are synchronous fire-and-forget; device-start failures cannot throw — listen to `SoLoud.instance.audioDeviceStartFailures` if that matters.
- **Voice limit still applies.** Default max active voices is 16 (`setMaxActiveVoiceCount`); a dense sequencer scheduling many simultaneous notes must raise it.
- **`playClocked` does not support buffer streams with `BufferingType.released`** (throws `SoLoudBufferStreamCanBePlayedOnlyOnceCppException` on second play), same as `play`.
- **Web**: `getPlayheadTime()` equals `getEngineTime()` and `getOutputLatency()` returns zero (render-ahead ring is native-only), while `getHardwareOutputLatency()` queries the Web Audio `AudioContext` (`outputLatency + baseLatency`); scheduling itself works on web.
- **`getPlayheadTime` vs `getEngineTime`**: use `getPlayheadTime()` for "what is the listener hearing right now" (e.g. syncing visuals to a rhythm game); use `getEngineTime()` as the time base for scheduling. To compensate for downstream OS/DAC/Bluetooth route delay on top of the playhead clock, factor in `getHardwareOutputLatency()`.

## More depth

- Demo: `example/lib/metronome/metronome.dart` — all three modes, slider-driven BPM changes with correct reset/re-anchor.
- Tests worth skimming for edge behavior: `test/common/scheduled_playback_test.dart`, `example/tests/tests/play_clocked.dart`, `example/tests/tests/play_scheduled.dart`, `example/tests/tests/latency_test.dart`, `example/tests/tests/hardware_output_latency.dart`.
- Render-ahead ring (`init(renderAheadFrames: ...)`, native only) for combining large mix buffers with low reactive latency — see `isRenderAheadEnabled` in `lib/src/soloud.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

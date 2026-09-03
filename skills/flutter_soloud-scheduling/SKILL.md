---
name: flutter_soloud-scheduling
version: 1
description: Teaches sample-accurate audio scheduling in flutter_soloud with playClocked (sounds anchored to a running "physics time" clock) and playScheduled (absolute engine-time scheduling with sample-accurate auto-stop), plus the clock helpers getEngineTime/getPlayheadTime/getOutputLatency/resetStreamTime and stopScheduled/fadeScheduled. Use when the user asks for a metronome, step sequencer, rhythm game, pre-computed music timeline/score, evenly spaced repeated SFX, or complains that periodic play() calls clump or drift.
---

# Sample-accurate scheduling

Plain `SoLoud.instance.play()` starts a sound at the **next output buffer boundary**: everything launched within the same buffer starts at the exact same sample ("clumps"), and periodic sounds get an irregular rhythm that is a multiple of the buffer size (~46 ms at the default 2048-sample buffer / 44100 Hz). flutter_soloud fixes this with two scheduling APIs. Unlike audioplayers/just_audio, the unit of timing here is the audio clock in samples, not wall-clock timers — you do **not** compensate with `Timer` precision tricks; you pass a `Duration` and the engine places the start sample-accurately.

- `playClocked(sound, soundTime, ...)` — you feed your own monotonically increasing "physics time"; the first call anchors it to the audio clock with a built-in ~2-output-buffer lead, subsequent calls are placed relative to that anchor. Best for evenly spaced, open-ended beats driven by your own loop (metronome, machine-gun fire, footsteps).
- `playScheduled(sound, atTime, {duration, ...})` — absolute time on the **engine's own clock** (`getEngineTime()`), schedulable arbitrarily far ahead; optional `duration` gives a sample-accurate auto-stop. Best for pre-computed timelines: scores, manifests, cutscenes.

## Minimal example

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

Pre-computed timeline with `playScheduled`:

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
- First call after init/`resetStreamTime()` anchors your clock to the audio clock, leading by ~2 output buffers — that is the constant latency, by design.
- If the clock goes backwards or jumps > ~2 s, the engine silently re-anchors. A time already in the past plays as soon as possible.
- All clocked calls share one anchor — they must all use the same time base.
- Divergence from other packages: no `paused` parameter (unlike `play()`), and you schedule by passing a time, not by pre-creating a paused player and calling `resume` later.

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
Duration getEngineTime();    // engine's global mix clock; time base for playScheduled/stopScheduled/fadeScheduled. Advances only while the device is mixing.
Duration getPlayheadTime();  // engine time of the sample currently reaching the device ("true output" clock). Equals getEngineTime() when the render-ahead ring is disabled (default) and on web.
Duration getOutputLatency(); // estimated output latency; Duration.zero when render-ahead is disabled and on web.
void resetStreamTime();      // reset the playClocked/play3dClocked anchor; the next clocked call re-anchors. Call when (re)starting a session.
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

**Which one to use:** `play()` for reactive one-shots (UI feedback, gunshots, music) where lowest latency wins; `playClocked` for evenly spaced beats from your own loop — fire-and-forget, no clock queries, the 2-buffer lead absorbs `Timer` jitter; `playScheduled` for pre-planned timelines — arbitrary lookahead and exact stops/fades, at the price of reading `getEngineTime()` and anchoring yourself.

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
- `SoLoud.instance.isRenderAheadEnabled`: checks if the ring buffer is active.
- `SoLoud.instance.getPlayheadTime()`: engine time of the sample currently reaching the device (use this to sync UI/animations to what the listener hears right now).
- `SoLoud.instance.getOutputLatency()`: estimated hardware + ring latency.

## Traps

- **Times must be monotonically increasing for `playClocked`.** Reusing a stale accumulator or starting a new session without `resetStreamTime()` makes ticks fire immediately (past times play ASAP) or triggers a silent re-anchor after a >2 s jump. Always zero your accumulator and call `resetStreamTime()` together.
- **Dart timers fire late.** Never pass "now" or the timer's fire time as the scheduled time — accumulate ideal times and keep a lead. For `playScheduled`, anchor with ~100–200 ms of lead (`getEngineTime() + 200ms`); a voice can only be delayed, never advanced.
- **`playClocked` has a ~2 s lookahead limit.** Gaps longer than ~2 s look like a clock jump and cause re-anchoring. For long-range scheduling use `playScheduled`.
- **Constant ~2-buffer latency with `playClocked`.** Do not use it for sounds that must react to input (keypress → sound); that is what `play()` is for.
- **The engine clock only advances while the device is mixing.** `getEngineTime()` freezes when output is stopped; a pending `playScheduled` voice keeps the device running. Scheduling against a stopped device is safe — the voice keeps its exact offset and starts counting down once the device runs.
- **No `paused` on clocked/scheduled variants.** Models reaching for the audioplayers/just_audio "prepare then resume" pattern should instead pass the time; to cancel a pending scheduled sound, keep the handle and call `stop(handle)`.
- **No `Future`, no completion callback per call.** These are synchronous fire-and-forget; device-start failures cannot throw — listen to `SoLoud.instance.audioDeviceStartFailures` if that matters.
- **Voice limit still applies.** Default max active voices is 16 (`setMaxActiveVoiceCount`); a dense sequencer scheduling many simultaneous notes must raise it.
- **`playClocked` does not support buffer streams with `BufferingType.released`** (throws `SoLoudBufferStreamCanBePlayedOnlyOnceCppException` on second play), same as `play`.
- **Web**: `getPlayheadTime()` equals `getEngineTime()` and `getOutputLatency()` returns zero (render-ahead ring is native-only); scheduling itself works on web.
- **`getPlayheadTime` vs `getEngineTime`**: use `getPlayheadTime()` for "what is the listener hearing right now" (e.g. syncing visuals to a rhythm game); use `getEngineTime()` as the time base for scheduling.

## More depth

- Demo: `example/lib/metronome/metronome.dart` — all three modes, slider-driven BPM changes with correct reset/re-anchor.
- Tests worth skimming for edge behavior: `example/tests/tests/play_clocked.dart`, `example/tests/tests/play_scheduled.dart`, `example/tests/tests/latency_test.dart`.
- Render-ahead ring (`init(renderAheadFrames: ...)`, native only) for combining large mix buffers with low reactive latency — see `isRenderAheadEnabled` in `lib/src/soloud.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

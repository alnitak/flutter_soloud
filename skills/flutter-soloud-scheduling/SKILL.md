---
name: flutter-soloud-scheduling
version: 3
description: Teaches sample-accurate audio scheduling and ultra-low-latency playback in flutter_soloud with playScheduled (absolute engine-time scheduling, sample-accurate auto-stop, and retroactive render-ahead remixing for lowest possible latency) and playClocked (sounds anchored to a running "physics time" clock without buffer-clumping), plus clock helpers getEngineTime/getPlayheadTime/getOutputLatency/getHardwareOutputLatency/resetStreamTime and stopScheduled/fadeScheduled. Use when the user asks for lowest possible latency, ultra-low latency on click/tap/button press, reactive playback with minimal delay, a metronome, step sequencer, rhythm game, pre-computed music timeline/score, evenly spaced repeated SFX, or complains that sounds have latency or periodic play() calls clump or drift.
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

### Rapid repeated clicks/taps with `playClocked` (no clumping)

```dart
// Rapid button taps or rapid-fire SFX without clumping at buffer boundaries:
var tapCount = 0;
void onRapidTap(AudioSource tapSound) {
  final tapTime = Duration(milliseconds: 50 * tapCount++);
  SoLoud.instance.playClocked(tapSound, tapTime);
}
```

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
- **Rapid repeated clicks/bursts (rapid tapping, machine-gun SFX):** use `playClocked` with an accumulated `physicsTime`. The built-in ~2-buffer lead absorbs timer and UI jitter so sounds are spaced sample-accurately without clumping together at buffer boundaries.
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
- `SoLoud.instance.isRenderAheadEnabled`: checks if the ring buffer is active.
- `SoLoud.instance.getPlayheadTime()`: engine time of the sample currently reaching the device (use this to sync UI/animations to what the listener hears right now).
- `SoLoud.instance.getOutputLatency()`: internal software mix cushion (render-ahead ring depth plus one device period; `Duration.zero` when disabled or on web).
- `SoLoud.instance.getHardwareOutputLatency()`: external OS/HAL/DAC presentation delay from the device callback to the physical transducer (speakers, headphones, USB DAC, Bluetooth). Works independently of the render-ahead ring across Android (AAudio), iOS (`AVAudioSession`), macOS (CoreAudio), Windows (WASAPI), and Web (Web Audio `AudioContext`). Total delay from an engine mix quantum to the listener's ear is `getOutputLatency() + getHardwareOutputLatency()`.

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
- Tests worth skimming for edge behavior: `example/tests/tests/play_clocked.dart`, `example/tests/tests/play_scheduled.dart`, `example/tests/tests/latency_test.dart`, `example/tests/tests/hardware_output_latency.dart`.
- Render-ahead ring (`init(renderAheadFrames: ...)`, native only) for combining large mix buffers with low reactive latency — see `isRenderAheadEnabled` in `lib/src/soloud.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

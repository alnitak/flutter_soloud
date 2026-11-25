---
name: flutter-soloud-volume-pan
version: 3
description: Teaches volume, pan, fade, oscillator, and scheduled-stop control in the flutter_soloud package, including the handle-based model, setPanAbsolute, getApproximateVolume for VU meters, and the limiter filter as the anti-clipping tool. Use when the user asks to change a sound's volume, pan a sound left/right, fade music in or out, crossfade two tracks, duck music under dialogue, stop a sound without a click, add tremolo/auto-pan/siren effects, build a VU meter, or stop clipping when many sounds play at once, or when code changes volume from a Timer or animation ticks.
---

Volume and pan in flutter_soloud are applied to **handles** (one per playing
voice), not to the loaded `AudioSource`. Loading a sound gives an `AudioSource`;
each `play()` returns a `SoundHandle` you control afterwards. Global volume is
engine-wide and multiplies with per-handle volume. Fades, oscillations, and
scheduled pause/stop run on the engine's internal fader clock, not on Dart
timers.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  final source = await SoLoud.instance.loadAsset('assets/sfx.ogg');

  // volume and pan can be set at play time...
  final handle = SoLoud.instance.play(source, volume: 0.8, pan: -0.5);

  // ...or changed afterwards on the handle.
  SoLoud.instance.setVolume(handle, 0.6);
  SoLoud.instance.setPan(handle, 0); // 0 = centered

  // Fade out over 2 s, then stop 2 s from now.
  SoLoud.instance.fadeVolume(handle, 0, const Duration(seconds: 2));
  SoLoud.instance.scheduleStop(handle, const Duration(seconds: 2));

  SoLoud.instance.setGlobalVolume(0.9); // master volume, all sounds
}
```

## The API shape

All members of `SoLoud.instance`. Everything throws
`SoLoudNotInitializedException` if `init()` has not completed, and
`ArgumentError` if a volume, pan, speed, `from` or `to` argument is NaN or
infinite. The setters, fades and scheduled actions below also accept a
voice-group handle or a bus handle (`bus.soundHandle`).

Per-handle volume / pan:

- `void setVolume(SoundHandle handle, double volume)` / `double getVolume(handle)` —
  `0.0` muted, `1.0` full. Per-voice gain; multiplies with global volume.
- `void setPan(SoundHandle handle, double pan)` / `double getPan(handle)` —
  **`-1.0` = full left, `0.0` = CENTER, `1.0` = full right.** Diverges from
  audioplayers (whose `setBalance` uses the same range) and from just_audio
  (no pan at all), but trips up models assuming 0..1: `setPan(handle, 0.5)` is
  half-right, not "half volume". Debug builds assert the range; release builds
  clamp to -1..1.
- `void setPanAbsolute(SoundHandle handle, double panLeft, double panRight)` —
  sets the L/R channel volumes directly (each -1..1). Bypasses the pan law.
  **Does not affect what `getPan` returns.**

Global:

- `void setGlobalVolume(double volume)` / `double getGlobalVolume()` —
  `0.0`..`1.0`, affects every voice.

Fades (signature: `(handle, to, Duration time)`, global variant omits the
handle). The fade starts from the current value; on a handle that is no
longer playing the per-handle calls do nothing:

- `void fadeVolume(SoundHandle handle, double to, Duration time)`
- `void fadePan(SoundHandle handle, double to, Duration time)`
- `void fadeRelativePlaySpeed(SoundHandle handle, double to, Duration time)` —
  ramps playback speed (and pitch, since SoLoud resamples).
- `void fadeGlobalVolume(double to, Duration time)`

Oscillators (signature: `(handle, from, to, Duration time)` — `time` is the
full period of one from→to→from cycle):

- `void oscillateVolume(SoundHandle handle, double from, double to, Duration time)`
- `void oscillatePan(SoundHandle handle, double from, double to, Duration time)`
- `void oscillateRelativePlaySpeed(SoundHandle handle, double from, double to, Duration time)`
- `void oscillateGlobalVolume(double from, double to, Duration time)`

Scheduled actions (engine-clock, no Dart `Future.delayed` needed; `time` is
measured from the call; nothing happens on a handle that is no longer
playing):

- `void schedulePause(SoundHandle handle, Duration time)`
- `void scheduleStop(SoundHandle handle, Duration time)`

For a stop or fade pinned to an exact engine time use `stopScheduled` /
`fadeScheduled(..., thenStop: true)` from **flutter-soloud-scheduling**.

Metering:

- `double getApproximateVolume(int channel)` — per-output-channel post-mix
  level for VU meters (channel `0` = left, `1` = right in stereo): the peak
  of the latest analyzed window.
  **Only returns non-zero when visualization is enabled**:
  `SoLoud.instance.setVisualizationEnabled(true)` first.
  Returns `0` for an invalid channel index.
- `double bus.getChannelVolume(int channel)` — the same for what plays
  through one mixing bus, after `bus.setVisualizationEnabled(true)`.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| Volume changed step by step from Dart (`Timer.periodic` or animation ticks + `setVolume`) to fade music in or out, or to duck it under dialogue | `fadeVolume(handle, to, time)`; `fadeGlobalVolume(to, time)` for everything | The engine ramps the gain while mixing: no zipper noise, no dependence on timer jitter or frame rate, one call. |
| `stop()` on a sound that is still loud (click/pop), or `Future.delayed` + `stop` | `fadeVolume(handle, 0, d)` + `scheduleStop(handle, d)`; when the end must fall on an exact time, `fadeScheduled(handle, atTime, 0, d, thenStop: true)` from **flutter-soloud-scheduling** | The level reaches zero before the voice stops, so there is no click, and the stop is timed by the engine, not by a Dart timer. |
| Switching music tracks (menu to level, calm to combat) with `stop` + `play` | Crossfade: `play(next, volume: 0, looping: true)`, `fadeVolume(next, 1, d)`, then `fadeVolume(current, 0, d)` + `scheduleStop(current, d)` | A smooth transition with two engine-side ramps; both tracks play during `d`, so it takes two voices for that time. |
| Tremolo, auto-pan, a siren or alarm wobble, vibrato written as a Dart loop | `oscillateVolume`, `oscillatePan`, `oscillateRelativePlaySpeed` (`from`, `to`, period) | The oscillation runs in the engine with no per-frame Dart calls; it keeps going until a new set/fade call on that parameter. |
| Slow-motion, power-down or "tape stop" effects stepping `setRelativePlaySpeed` from a timer | `fadeRelativePlaySpeed(handle, to, time)` | A smooth pitch/speed glide; speed and pitch change together (for pitch alone see `pitchShiftFilter` in **flutter-soloud-filters**). |
| Every `play()` volume lowered by hand because many simultaneous sounds distort | `SoLoud.instance.filters.limiterFilter.activate()`. **flutter-soloud-filters** | Caps the mixed output so it does not clip; single sounds keep their level. |
| Separate music / SFX / voice sliders done by looping over handles or scaling each `play()` volume | A mixing bus per category and one `setVolume` / `fadeVolume` on the bus handle. **flutter-soloud-mixing-bus**. `setGlobalVolume` for the master slider | One fader per category, also for sounds started later. |
| Pan and volume computed by hand every frame from an object's world position | `play3d` with attenuation and a listener. **flutter-soloud-3d-audio** | The engine computes panning, distance falloff and Doppler from positions. For a simple left/right placement `pan:` at play time is enough. |

## Traps

- **Simultaneous sounds sum and clip.** Two voices at volume 1.0 can exceed
  full scale and distort; lowering volumes per-sound is a losing battle. The
  intended fix is the global limiter filter, which caps the mixed output:

  ```dart
  await SoLoud.instance.init();
  SoLoud.instance.filters.limiterFilter.activate();
  // Optionally lower the ceiling (dB, should stay < 0):
  SoLoud.instance.filters.limiterFilter.outputCeiling.value = -3;
  ```

  See the sibling skill **flutter-soloud-filters** for filter details.
- **Fades, oscillations and scheduled stops belong to one voice.** They end
  with it, and calling them on a handle that is no longer playing silently
  does nothing (no exception). A new `play()` starts at its own `volume:` /
  `pan:` with no fade; start the fade again on the new handle.
- **NaN or infinity throws `ArgumentError`.** Guard values computed from game
  state (e.g. a volume from `1 / distance`) before passing them.
- **`getVolume`/`getPan`/`getGlobalVolume` round-trip through float.**
  Reading back `0.8` can return `0.800000042353`. Never compare with `==`.
- **Handles die.** After a sound ends or is stopped, the handle is invalid.
  `setVolume` and the fades on a dead handle do nothing; check
  `getIsValidVoiceHandle(handle)` if unsure. Don't cache handles across
  `stop()`/`scheduleStop()`.
- **`getApproximateVolume` returns 0 forever** if visualization was never
  enabled — it reads the same analysis buffers as the waveform/FFT feature.
- **`setRelativePlaySpeed(handle, 0)`** is undefined behavior (likely crash);
  the lower bound is silently clamped to `0.05`. Same applies to
  `fadeRelativePlaySpeed` / `oscillateRelativePlaySpeed` targets.
- **Max voice count is 16 by default.** Extra `play()` calls steal the oldest
  unprotected voice of the same sound (or are dropped with a log warning — no
  exception),
  so a fade targeting the stolen handle does nothing. Raise with
  `setMaxActiveVoiceCount` if needed; `setProtectVoice(handle, true)` keeps
  music from being stolen.
- **Fades are synchronous calls, not Futures.** `fadeVolume` returns
  immediately; if you need to act when the fade finishes, await your own
  `Future.delayed` with the same duration.

## More depth

- Limiting and other filters: sibling skill `flutter-soloud-filters`; demo
  `example/lib/filters/limiter.dart`.
- Bus-level volume (group many sounds under one fader):
  `example/lib/mixing_bus/mixing_bus.dart`.
- Executable usage references: `test/common/volume_controls_test.dart`,
  `test/common/pan_test.dart`, `example/tests/tests/advanced_pan.dart`.
- Doc page: https://docs.page/alnitak/flutter_soloud_docs/audio/volume.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

---
name: flutter_soloud-volume-pan
version: 1
description: Teaches volume, pan, fade, oscillator, and scheduled-stop control in the flutter_soloud package, including the handle-based model, setPanAbsolute, getApproximateVolume for VU meters, and the limiter filter as the anti-clipping tool. Use when the user asks to change a sound's volume, pan a sound left/right, fade or oscillate audio parameters, build a VU meter, or stop clipping when many sounds play at once.
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
`SoLoudNotInitializedException` if `init()` has not completed.

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
handle):

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

Scheduled actions (engine-clock, no Dart `Future.delayed` needed):

- `void schedulePause(SoundHandle handle, Duration time)`
- `void scheduleStop(SoundHandle handle, Duration time)`

Metering:

- `double getApproximateVolume(int channel)` — per-output-channel post-mix
  level for VU meters (channel `0` = left, `1` = right in stereo).
  **Only returns non-zero when visualization is enabled**:
  `SoLoud.instance.setVisualizationEnabled(true)` first.
  Returns `0` for an invalid channel index.

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

  See the sibling skill **flutter_soloud-filters** for filter details.
- **Fades/oscillations keep running on stopped handles.** If a handle is
  stopped (or `scheduleStop` fires) mid-fade, the fader is not cancelled; when
  the same sound is played again and the handle id is recycled, the old ramp
  can snap the new voice's volume/pan. Reset the parameter explicitly
  (`setVolume`/`setPan`) when starting a new voice if you previously faded.
- **`getVolume`/`getPan`/`getGlobalVolume` round-trip through float.**
  Reading back `0.8` can return `0.800000042353`. Never compare with `==`.
- **Handles die.** After a sound ends or is stopped, the handle is invalid.
  `setVolume` on a dead handle fails silently on the native side; check
  `getIsValidVoiceHandle(handle)` if unsure. Don't cache handles across
  `stop()`/`scheduleStop()`.
- **`getApproximateVolume` returns 0 forever** if visualization was never
  enabled — it reads the same analysis buffers as the waveform/FFT feature.
- **`setRelativePlaySpeed(handle, 0)`** is undefined behavior (likely crash);
  the lower bound is silently clamped to `0.05`. Same applies to
  `fadeRelativePlaySpeed` / `oscillateRelativePlaySpeed` targets.
- **Max voice count is 16 by default.** Extra `play()` calls steal the oldest
  voice of the same sound (or are dropped with a log warning — no exception),
  so a fade targeting the stolen handle does nothing. Raise with
  `setMaxActiveVoiceCount` if needed.
- **Fades are synchronous calls, not Futures.** `fadeVolume` returns
  immediately; if you need to act when the fade finishes, await your own
  `Future.delayed` with the same duration.

## More depth

- Limiting and other filters: sibling skill `flutter_soloud-filters`; demo
  `example/lib/filters/limiter.dart`.
- Bus-level volume (group many sounds under one fader):
  `example/lib/mixing_bus/mixing_bus.dart`.
- Executable usage references: `example/tests/tests/volume_controls.dart`,
  `example/tests/tests/pan.dart`, `example/tests/tests/advanced_pan.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

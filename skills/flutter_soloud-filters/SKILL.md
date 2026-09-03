---
name: flutter_soloud-filters
version: 1
description: Teaches correct use of flutter_soloud's 12 DSP filters (echo, freeverb, biquad, bassboost, flanger, waveShaper, lofi, robotize, pitchShift, limiter, compressor, parametricEq) at global, per-sound, and per-bus scope, including activation ordering, FilterParam value/fade/oscillate, and per-platform limits. Use when the user asks to add audio effects, reverb, an equalizer/EQ, pitch shifting or time stretching, dynamic-range compression, or anti-clipping/limiting to playback.
---

flutter_soloud exposes the SoLoud engine's filter graph as typed Dart objects. Filters live at three scopes — global output (`SoLoud.instance.filters`), a single loaded sound (`audioSource.filters`), and a mixing bus (`bus.filters`). Every filter is activated with `activate()` and tuned through `FilterParam` objects (`param.value`, `fadeFilterParameter`, `oscillateFilterParameter`). There is no "node graph" to wire like the Web Audio API and no per-player effect config like just_audio — you activate a filter, then mutate its parameters live.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  // Global echo on the whole output.
  final echo = SoLoud.instance.filters.echoFilter;
  echo.activate();
  echo.delay.value = 0.2; // seconds
  echo.decay.value = 0.7;

  // Per-sound filter: MUST be activated before play().
  final sound = await SoLoud.instance.loadAsset('assets/audio/track.mp3');
  sound.filters.pitchShiftFilter.activate();
  final handle = SoLoud.instance.play(sound, looping: true);
  sound.filters.pitchShiftFilter.semitones(soundHandle: handle).value = 3;
}
```

## The API shape

- `SoLoud.instance.filters` → `FiltersGlobal`. Getters: `biquadResonantFilter`, `echoFilter`, `lofiFilter`, `flangerFilter`, `bassBoostFilter`, `waveShaperFilter`, `robotizeFilter`, `freeverbFilter`, `pitchShiftFilter`, `limiterFilter`, `compressorFilter`, `parametricEqFilter`. Params are getters: `filters.echoFilter.delay.value = 0.2`.
- `audioSource.filters` → `FiltersSingle`. Same filters, except the biquad getter is named **`biquadFilter`** (global scope calls it `biquadResonantFilter`). Params are **methods** taking the playing voice's handle: `sound.filters.echoFilter.delay(soundHandle: handle).value = 0.2`.
- `bus.filters` → `FiltersSingle(busId: ...)` on a `Bus` from `SoLoud.instance.createMixingBus()`. Params apply bus-wide; no `soundHandle` needed.
- `FilterBase`: `activate()`, `deactivate()`, `isActive` (bool), `index` (filter slot, `-1` when inactive), `filterType`.
- `FilterParam`: `double get value` / `set value(double)`, `fadeFilterParameter({required double to, required Duration time})`, `oscillateFilterParameter({required double from, required double to, required Duration time})`.
- `queryXxx` getters on each filter (e.g. `filters.echoFilter.queryDelay`) expose the parameter's `min`, `max`, `def` (default) and a human-readable `toString()` — use these for slider ranges instead of hardcoding.

Which params matter for the common use (full table in `references/filter-parameters.md`):

| Filter | Key params (range, default) |
|---|---|
| echo | `delay` 0.001–3 s (0.3), `decay` 0.001–1 (0.7), `filter` 0–1 (0) |
| freeverb | `roomSize` 0–1 (0.5), `damp` 0–1 (0.5), `width` 0–1 (1), `freeze` 0–1 (0) |
| biquadResonant | `type` 0/1/2 = lowpass/highpass/bandpass, `frequency` 10–16000 Hz (1000), `resonance` 0.1–20 (0.1) |
| lofi | `samplerate` 100–22000 (4000), `bitdepth` 0.5–16 (3) |
| flanger | `delay` 0–3 (1), `freq` −48–48 (0) |
| bassboost | `boost` 0–10 (2) |
| waveShaper | `amount` −1–1 (0) |
| robotize | `frequency` 0.1–100 (30), `waveform` 0–6 (0) |
| pitchShift | `shift` 0–3 (1), `semitones` −36–36 (0); linked: `semitones = 12 * log2(shift)` |
| limiter | `threshold` −60–0 dB (−3), `outputCeiling` −60–0 dB (−1), `attackTime` 0.1–200 ms (1), `releaseTime` 1–1000 ms (100), `kneeWidth` 0–30 dB (2) |
| compressor | `threshold` −80–0 dB (−6), `ratio` 1–10 (3), `makeupGain` −40–40 dB (0), `attackTime` 0–100 ms (10), `releaseTime` 0–1000 ms (100), `kneeWidth` 0–40 dB (2) |
| parametricEq | `numBands` 1–64 (3), `stftWindowSize` 32–4096 power of two (1024), `bandGain(i)` 0–4 (1) |

Every filter also has `wet` (0–1, default 1): 1 = fully processed, 0 = dry passthrough.

Recipes:

```dart
// Anti-clip safety: global limiter with defaults is usually enough.
// Activate it AFTER all other global filters so it sits last in the chain.
final limiter = SoLoud.instance.filters.limiterFilter;
limiter.activate();

// Taming many simultaneous loud voices (from example/lib/filters/compressor.dart):
final comp = SoLoud.instance.filters.compressorFilter;
comp.activate();
comp.threshold.value = -6;   // dB
comp.ratio.value = 3;
comp.attackTime.value = 10;  // ms
comp.releaseTime.value = 100;

// Time stretch without pitch change (per-sound scope only):
sound.filters.pitchShiftFilter.timeStretch(handle, 1.25); // 25% faster, same pitch
// Global scope: do it manually — setRelativePlaySpeed(handle, speed) then
// pitchShiftFilter.shift.value = 1 / speed.

// Parametric EQ (bands are logarithmic, 30 Hz .. 16 kHz):
final eq = SoLoud.instance.filters.parametricEqFilter;
eq.activate();
eq.numBands.value = 10;
eq.stftWindowSize.value = 2048; // must be a power of two, 32..4096
eq.bandGain(0).value = 2;       // boost the lowest band
final hz = eq.bandFrequency(0); // center frequency of band 0 (30 Hz)
```

## Traps

- **Per-sound filters must be activated before `play()`.** Only voices started after `activate()` carry the filter; already-playing handles are unaffected. To change the filter set, `stop` the handle and play again.
- **Per-sound filters do not work on web.** Any `activate()`/param access with a `soundHash` on web throws `SoLoudFilterForSingleSoundOnWebDartException`. Global filters work on web; gate per-sound usage with `kIsWeb` if your app targets it.
- **Max 8 filters per stream/sound** (`FILTERS_PER_STREAM` in SoLoud). The 12 types each occupy one slot per scope.
- **Out-of-range `param.value` sets are silently ignored** — the setter logs a warning and returns without throwing. Check bounds with the `queryXxx.min/max` getters.
- **Naming inconsistencies vs. intuition:** the single/bus-scope biquad getter is `biquadFilter` (global: `biquadResonantFilter`); freeverb's damping param is `damp` (not `damping`); lofi's params are `samplerate`/`bitdepth` (all lowercase). The online docs get some of these wrong — trust the code in `lib/src/filters/`.
- **freeverb is 2-channel only.** As a global filter, init the engine with the default 2 channels (don't pass `channels:` to `SoLoud.instance.init`); as a per-sound filter the sound itself must be stereo.
- **parametricEq `stftWindowSize` must be a power of two in 32..4096**, otherwise the set is ignored with a warning. Larger window = more frequency accuracy, more latency and CPU.
- **`fadeFilterParameter`/`oscillateFilterParameter` throw `SoLoudNotInitializedException`** if the engine isn't initialized; `FilterParam.value` can throw `SoLoudCppException` on native errors. Engine re-init (`deinit()` + `init()`) drops all active filters — reactivate afterwards.
- **Filter order matters:** activation order is the processing order. Put gain-reducing filters (compressor, limiter) last — activate them after everything else (tip from `example/lib/filters/limiter.dart`).
- On Android, the default low-latency output profile leaves little CPU headroom for heavy filters (FFT pitch shift, large-EQ windows); pass `lowLatency: false` to `SoLoud.instance.init` if you hear underruns.

## More depth

- `references/filter-parameters.md` — every parameter of all 12 filters with min/max/default and the matching `queryXxx` getter.
- Interactive demos in the plugin repo: `example/lib/filters/` (`compressor.dart`, `limiter.dart`, `parametric_eq.dart`, `pitchshift.dart`) and the automated examples under `example/tests/tests/` (`global_filters.dart`, `sound_filters.dart`, `equalizer_filter.dart`, `compressor_filter.dart`, `limiter_filter.dart`, `pitch_shifter_filter.dart`).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

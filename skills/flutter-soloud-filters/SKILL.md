---
name: flutter-soloud-filters
version: 5
description: Teaches correct use of flutter_soloud's 14 DSP filters (echo, freeverb, biquad low/high/band-pass, bassboost, flanger, waveShaper, lofi, robotize, pitchShift, limiter, compressor, parametricEq, amplitudeModulator, multi-instance customFilter) at global, per-sound and per-bus scope (activation, FilterParam value/fade/oscillate, exceptions and per-platform limits). Use when the user asks for audio effects, reverb or echo for caves and halls, a muffled/underwater/pause-menu/behind-a-wall sound, an equalizer, pitch shifting or time stretching, compression, radio/robot/lo-fi voices, ring modulation, or when many simultaneous sounds distort or clip (limiter), and when code steps a filter value from a Timer or uses per-sound filters on web.
---

flutter_soloud exposes the SoLoud engine's filter graph as typed Dart objects. Filters live at three scopes — global output (`SoLoud.instance.filters`), a single loaded sound (`audioSource.filters`), and a mixing bus (`bus.filters`). Every filter is activated with `activate()` and tuned through `FilterParam` objects (`param.value`, `fadeFilterParameter`, `oscillateFilterParameter`). The three scopes spell parameter access differently (getter, method with the voice handle, method without arguments): see "The API shape". There is no "node graph" to wire like the Web Audio API and no per-player effect config like just_audio — you activate a filter, then mutate its parameters live.

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

  // Per-sound filter (not on web): activate on the source, then set the
  // parameters of each playing voice through its handle.
  final sound = await SoLoud.instance.loadAsset('assets/audio/track.mp3');
  sound.filters.pitchShiftFilter.activate();
  final handle = SoLoud.instance.play(sound, looping: true);
  sound.filters.pitchShiftFilter.semitones(soundHandle: handle).value = 3;
}
```

## The API shape

- `SoLoud.instance.filters` → `FiltersGlobal`. Getters: `biquadResonantFilter`, `echoFilter`, `lofiFilter`, `flangerFilter`, `bassBoostFilter`, `waveShaperFilter`, `robotizeFilter`, `freeverbFilter`, `pitchShiftFilter`, `limiterFilter`, `compressorFilter`, `parametricEqFilter`, `amplitudeModulatorFilter`, plus method `customFilter(int slotIndex)` (`0..7`). Params are **getters**: `filters.echoFilter.delay.value = 0.2`.
- `audioSource.filters` → `FiltersSingle`. Same filters and the same getter names (`biquadFilter` is the deprecated old name of `biquadResonantFilter` here). Params are **methods taking the playing voice's handle**: `sound.filters.echoFilter.delay(soundHandle: handle).value = 0.2`. Each voice has its own parameter values, starting from the defaults: set them after every `play()`. Not available on web.
- `bus.filters` → the same `FiltersSingle` class on a `Bus` from `SoLoud.instance.createMixingBus()`. Params are **methods called without arguments**: `bus.filters.echoFilter.delay().value = 0.2`. They work only once the bus is playing (`bus.playOnEngine()`); `activate()` can come before. Works on web. See **flutter-soloud-mixing-bus**.
- `FilterBase`: `activate()`, `deactivate()`, `isActive` (bool), `index` (position in the chain, `-1` when inactive), `filterType`.
- `FilterParam`: `double get value` / `set value(double)`, `min`, `max`, `fadeFilterParameter({required double to, required Duration time})`, `oscillateFilterParameter({required double from, required double to, required Duration time})`.
- `queryXxx` getters on each filter (e.g. `filters.echoFilter.queryDelay`) expose the parameter's `min`, `max`, `def` (default) and a human-readable `toString()`: use these for slider ranges instead of hardcoding. `parametricEqFilter` and `customFilter` have none: use `param.min`/`param.max` (EQ ranges are in the table below) and your own `CustomFilterParamSpec`s.

Which params matter for the common use (full table in `references/filter-parameters.md`):

| Filter | Key params (range, default) |
|---|---|
| echo | `delay` 0.001–3 s (0.3), `decay` 0.001–1 (0.7), `filter` 0–1 (0) |
| freeverb | `roomSize` 0–1 (0.5), `damp` 0–1 (0.5), `width` 0–1 (1), `freeze` 0–1 (0) |
| biquadResonant | `type` 0/1/2 = lowpass/highpass/bandpass (0), `frequency` 10–8000 Hz (1000), `resonance` 0.1–20 (2) |
| lofi | `samplerate` 100–22000 (4000), `bitdepth` 0.5–16 (3) |
| flanger | `delay` 0.001–0.1 s (0.005), `freq` 0.001–100 Hz (10) |
| bassboost | `boost` 0–10 (2) |
| waveShaper | `amount` −1–1 (0) |
| robotize | `frequency` 0.1–100 Hz (30), `waveform` 0–6 (0), the index of the first 7 `WaveForm` values: square, saw, sin, triangle, bounce, jaws, humps |
| pitchShift | `shift` 0.1–3 (1), `semitones` −39.86–19.02 (0); linked: `semitones = 12 * log2(shift)`, setting one updates the other |
| limiter | `threshold` −60–0 dB (−3), `outputCeiling` −60–0 dB (−1), `attackTime` 0.1–200 ms (1), `releaseTime` 1–1000 ms (50), `kneeWidth` 0–30 dB (6) |
| compressor | `threshold` −80–0 dB (−6), `ratio` 1–10 (3), `makeupGain` −40–40 dB (0), `attackTime` 0–100 ms (10), `releaseTime` 0–1000 ms (100), `kneeWidth` 0–40 dB (2) |
| parametricEq | `numBands` 1–64 (3), `stftWindowSize` 32–4096 power of two (1024), `bandGain(i)` 0–4 (1), `bandFreq(i)` 10–24000 Hz, `bandQ(i)` 0.1–20; `i` in 0..63; transparent while every gain is 1 |
| amplitudeModulator | `frequency` 0.1–20000 Hz (440), `waveform` 0–3 (0 = Sine, 1 = Triangle, 2 = Square, 3 = Saw); bipolar amplitude modulation, clamped below Nyquist at runtime |
| customFilter(id) | `wet` 0–1 (1), `param(1)`..`param(15)` user-defined via `configureParameters(specs)`, up to 8 independent slots (`0..7`) per scope (see **flutter-soloud-custom-dsp** and **flutter-soloud-realtime-dsp**) |

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

// Muffle everything (pause menu, underwater): low-pass, cutoff ramped by the
// engine. The cutoff tops out at 8000 Hz, so fade `wet` to 0 to get the
// unfiltered sound back.
final lowPass = SoLoud.instance.filters.biquadResonantFilter;
lowPass.activate(); // type 0 = low-pass is the default
lowPass.frequency.fadeFilterParameter(
  to: 400,
  time: const Duration(milliseconds: 300),
);
// ...leaving the menu:
lowPass.wet.fadeFilterParameter(
  to: 0,
  time: const Duration(milliseconds: 300),
);

// Time stretch without pitch change (per-sound scope only):
sound.filters.pitchShiftFilter.timeStretch(handle, 1.25); // 25% faster, same pitch
// Global scope: do it manually — setRelativePlaySpeed(handle, speed) then
// pitchShiftFilter.shift.value = 1 / speed.

// Parametric EQ (smooth bell curves, default 30 Hz .. 12 kHz):
final eq = SoLoud.instance.filters.parametricEqFilter;
eq.activate();
eq.numBands.value = 10;
eq.stftWindowSize.value = 2048; // must be a power of two, 32..4096
eq.bandGain(0).value = 2;       // boost lowest band
eq.bandFreq(0).value = 45;      // tune center frequency to 45 Hz
eq.bandQ(0).value = 1.5;        // tune Q factor
final hz = eq.bandFrequency(0); // reads current center frequency (45 Hz)
```

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| Many sounds at once distort or crackle (explosions, gunfire over music), or every `play()` volume is lowered by hand to avoid it | `SoLoud.instance.filters.limiterFilter.activate()`, activated after the other global filters; the defaults are enough | No clipping when voices pile up and quiet moments keep their level. Costs one of the 8 global filter slots; it only shapes the peaks, so a mix that is far too loud still needs lower volumes. |
| A muffled sound: underwater, pause menu, behind a wall or door, "hearing loss" after an explosion | `biquadResonantFilter` (global, or `bus.filters.biquadResonantFilter` on a music/SFX bus) as low-pass, with `frequency.fadeFilterParameter(to:, time:)` to enter and `wet.fadeFilterParameter(to: 0, ...)` to leave | One filter on the mix or on a bus instead of separate muffled assets; the transition is ramped inside the engine. The cutoff maximum is 8000 Hz, so "fully open" means `wet` 0 or `deactivate()`. |
| A `Timer.periodic`, `AnimationController` or game-loop tick that writes `param.value` step by step (cutoff sweeps, wet fades, wobbles) | `param.fadeFilterParameter(to:, time:)` for a ramp, `param.oscillateFilterParameter(from:, to:, time:)` for a continuous sweep | The engine moves the value on the audio thread: no stepping that follows the UI frame rate, no jank when the UI isolate is busy, and one call instead of many. |
| Caves, halls, tunnels, a big room, a "distant" announcer | `echoFilter` (`delay`, `decay`) or `freeverbFilter` (`roomSize`, `damp`) on the SFX or voice bus, fading `wet` when the player enters and leaves (**flutter-soloud-mixing-bus**) | The space is applied to a whole category at once and can be turned up and down at runtime; no pre-baked reverb in the assets. freeverb needs stereo (engine with 2 channels, stereo sound). |
| `setRelativePlaySpeed` used to change only the pitch (voice variations, transposing music) or only the speed (podcast at 1.5x, slow motion that must not drop in pitch) | `pitchShiftFilter`: `semitones`/`shift` for pitch at the same duration; `sound.filters.pitchShiftFilter.timeStretch(handle, speed)` for speed at the same pitch | `setRelativePlaySpeed` alone always changes both. The filter is FFT-based: more CPU than the other filters (on Android consider `init(lowLatency: false)`), and per-sound scope is not available on web. |
| Radio, walkie-talkie, robot, alien or retro-console voices recorded as separate assets, or asked for as an effect | `lofiFilter` (`samplerate`, `bitdepth`), `robotizeFilter`, `amplitudeModulatorFilter` (ring modulation), `waveShaperFilter` (distortion) on that sound or on a voice bus | One clean recording serves every variant and the effect can be switched or faded at runtime; less asset size. |
| Dialogue drowned by effects, or very uneven loudness between assets, fixed by re-exporting files or by per-sound volume tables | `compressorFilter` on that bus or globally (`threshold`, `ratio`, `makeupGain`), activated before the limiter | Evens out the levels at runtime with one filter. It reduces dynamics on purpose: keep the ratio moderate for music. |
| `sound.filters...` in an app that also targets web, or the same filter activated on many `AudioSource`s of one category | A mixing bus with `bus.filters` (**flutter-soloud-mixing-bus**), or the global filter | Per-sound filters throw `SoLoudFilterForSingleSoundOnWebDartException` on web; global and bus filters work everywhere. One filter instance for the category also costs less CPU than one per voice. |

## Traps

- **Three spellings for a parameter.** Global: getter (`filters.echoFilter.wet.value`). Sound: method with the handle (`sound.filters.echoFilter.wet(soundHandle: handle).value`). Bus: method without arguments (`bus.filters.echoFilter.wet().value`). On a sound, a parameter used without `soundHandle` throws `ArgumentError` (it does not fall back to the global filter).
- **Per-sound parameters are per voice.** `activate()` on a sound reaches the voices already playing and the ones started later; every voice starts with the default parameter values, so set them after each `play()`. The filter set belongs to the `AudioSource`, not to a handle: all its voices get it.
- **Bus filter parameters need a playing bus.** Before `bus.playOnEngine()`, value/fade/oscillate throw `SoLoudSoundHandleNotFoundCppException`. `activate()`/`deactivate()` work at any time.
- **No per-sound filters on web.** `activate()`, `deactivate()` and every parameter access with a `soundHandle` throw `SoLoudFilterForSingleSoundOnWebDartException`. Use global or bus filters, and on a bus never pass `soundHandle`.
- **Max 8 filters per scope** (global, each sound, each bus). Each built-in type can be active once per scope; `customFilter(0..7)` slots are separate filters sharing the same 8. A second `activate()` throws `SoLoudFilterAlreadyAddedException`, a ninth filter throws `SoLoudMaxFilterNumberReachedException`.
- **A filter must be active before touching its parameters.** Reading, setting, fading or oscillating a parameter of an inactive filter throws `SoLoudFilterNotFoundException`, and so does `deactivate()` on an inactive filter. Guard with `isActive`. `deactivate()` + `activate()` resets the parameters to their defaults.
- **Out-of-range and NaN `param.value` sets are ignored**: the setter logs a warning and returns without throwing. Check bounds with `param.min/max` or the `queryXxx` getters. `fadeFilterParameter`/`oscillateFilterParameter` throw `ArgumentError` for NaN or infinite targets.
- **The filter classes are not exported.** `FilterParam`, `FilterBase`, `FiltersSingle`, `FiltersGlobal`, the per-filter classes and enums (`EchoGlobal`, `BiquadResonantEnum`, `ParametricEqParam`, ...) are not part of `package:flutter_soloud/flutter_soloud.dart` (only `FilterType` and the custom-filter classes are). Reach them through `SoLoud.instance.filters` / `sound.filters` / `bus.filters` and let Dart infer the types (`final echo = ...echoFilter;`); don't write those type names in fields or signatures, and don't import from `package:flutter_soloud/src/`.
- **Naming inconsistencies vs. intuition:** the biquad getter is `biquadResonantFilter` (`biquadFilter` on sounds and buses is deprecated); freeverb's damping param is `damp` (not `damping`); lofi's params are `samplerate`/`bitdepth` (all lowercase). Trust the code in `lib/src/filters/`.
- **freeverb is 2-channel only.** As a global filter, init the engine with the default 2 channels (don't pass `channels:` to `SoLoud.instance.init`); as a per-sound filter the sound itself must be stereo.
- **parametricEq `stftWindowSize` must be a power of two in 32..4096**, otherwise the set is ignored with a log warning. Larger window = more frequency accuracy, more latency and CPU. `bandGain(i)`/`bandFreq(i)`/`bandQ(i)` throw `ArgumentError` for `i` outside 0..63.
- **`timeStretch(handle, speed)` only compensates while `1 / speed` fits the `shift` range 0.1..3** (speed from 1/3 to 10). Outside it the shift set is ignored and the pitch follows the speed.
- **Engine state.** Before `init()`, `activate()` and `param.value` throw `SoLoudBackendNotInitedException`; fades and oscillations throw `SoLoudNotInitializedException`. `deinit()` (also the one inside a second `init()`) drops every filter, and `disposeAllSources()` deactivates all global filters: reactivate them afterwards.
- **Filter order matters:** activation order is the processing order. Put gain-reducing filters (compressor, then limiter) last: activate them after everything else (tip from `example/lib/filters/limiter.dart`). With `init(renderAheadFrames:)` the global volume is applied before the global filters instead of after them, which a limiter or compressor can make audible.
- On Android, the default low-latency output profile leaves little CPU headroom for heavy filters (FFT pitch shift, large-EQ windows); pass `lowLatency: false` to `SoLoud.instance.init` if you hear underruns.

## More depth

- `references/filter-parameters.md` — every parameter of all 14 filters with min/max/default and the matching `queryXxx` getter.
- Custom synchronous DSP filters (`customFilter(id)`) are covered in depth by **flutter-soloud-custom-dsp** (`skills/flutter-soloud-custom-dsp/SKILL.md`) and **flutter-soloud-realtime-dsp** (`skills/flutter-soloud-realtime-dsp/SKILL.md`).
- Interactive demos in the plugin repo: `example/lib/filters/` (`amplitude_modulator.dart`, `compressor.dart`, `limiter.dart`, `parametric_eq.dart`, `pitchshift.dart`), `example/lib/realtime_audio/custom_dsp/`, and the automated examples under `example/tests/tests/` (`global_filters.dart`, `sound_filters.dart`, `equalizer_filter.dart`, `compressor_filter.dart`, `limiter_filter.dart`, `pitch_shifter_filter.dart`, `amplitude_modulator_filter.dart`).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

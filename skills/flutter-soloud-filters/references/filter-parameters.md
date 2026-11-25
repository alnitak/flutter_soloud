# Filter parameter reference

All values verified against `lib/src/filters/*.dart` (each filter's enum holds the
authoritative `min`/`max`/`def` lists, which match the native ranges). At global
scope params are getters (`filters.echoFilter.delay`); at per-sound scope they are
methods taking the voice handle (`sound.filters.echoFilter.delay(soundHandle: handle)`);
at per-bus scope the same methods are called without arguments
(`bus.filters.echoFilter.delay()`). Each filter also has a `queryXxx` getter per
parameter exposing `min`, `max`, `def`, and a human-readable `toString()`
(`parametricEq` and `customFilter` excepted, see their sections).

All filters share `wet` (0–1, default 1): mix between dry and processed signal.

## biquadResonant

Getter `biquadResonantFilter` in every scope (on sounds and buses the old name `biquadFilter` still works but is deprecated).
Multi-mode filter. `src/soloud` docs: <https://solhsa.com/soloud/biquadfilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| type | 0 | 2 | 0 | 0 = lowpass, 1 = highpass, 2 = bandpass |
| frequency | 10 | 8000 | 1000 | Hz; clamped below Nyquist while processing |
| resonance | 0.1 | 20 | 2 | sharpness of the cutoff |

The cutoff cannot go above 8000 Hz: to "open" a low-pass completely, fade `wet`
to 0 or deactivate the filter.

## echo

<https://solhsa.com/soloud/echofilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| delay | 0.001 | 3.0 | 0.3 | seconds |
| decay | 0.001 | 1 | 0.7 | feedback; near 1 rings for a long time |
| filter | 0 | 1 | 0 | damping of the echo tail |

## lofi

Bit depth / sample-rate reduction. <https://solhsa.com/soloud/lofifilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| samplerate | 100 | 22000 | 4000 | Hz; lowercase name, not `sampleRate` |
| bitdepth | 0.5 | 16 | 3 | bits; lowercase name, not `bitDepth` |

## flanger

<https://solhsa.com/soloud/flangerfilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| delay | 0.001 | 0.1 | 0.005 | seconds |
| freq | 0.001 | 100 | 10 | Hz; LFO frequency of the sweep |

## bassboost

<https://solhsa.com/soloud/bassboostfilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| boost | 0 | 10 | 2 | low-frequency gain multiplier |

## waveShaper

Distortion. <https://solhsa.com/soloud/waveshaperfilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| amount | -1 | 1 | 0 | negative values fold the waveform the other way |

## robotize

Voice modulation. <https://solhsa.com/soloud/robotizefilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| frequency | 0.1 | 100 | 30 | modulation frequency in Hz |
| waveform | 0 | 6 | 0 | modulator waveform: 0 square, 1 saw, 2 sin, 3 triangle, 4 bounce, 5 jaws, 6 humps (the `WaveForm` enum order) |

## freeverb

Reverb. **2 channels only** — engine must be inited with the default 2 channels
for the global scope; the sound itself must be stereo for per-sound scope.
<https://solhsa.com/soloud/freeverbfilter.html>

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| freeze | 0 | 1 | 0 | 1 freezes the current reverb tail |
| roomSize | 0 | 1 | 0.5 | |
| damp | 0 | 1 | 0.5 | high-frequency damping; named `damp`, not `damping` |
| width | 0 | 1 | 1 | stereo width |

## pitchShift

Pitch shift without changing speed. Not part of stock SoLoud; implemented in
`src/filters/pitch_shift_filter.cpp`.

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| shift | 0.1 | 3 | 1 | frequency multiplier; 1 = no shift |
| semitones | -39.863 | 19.0195 | 0 | the shift range in semitones; linked to shift: `semitones = 12 * log2(shift)`; setting one updates the other |

Tempo-without-pitch-change:

- Per-sound: `sound.filters.pitchShiftFilter.timeStretch(handle, speed)` sets
  `setRelativePlaySpeed(handle, speed)` and `shift = 1 / speed` in one call.
  This method exists only on `PitchShiftSingle`. `1 / speed` must fit the
  shift range (speed from 1/3 to 10), otherwise the shift is not applied.
- Global: do both steps manually
  (`SoLoud.instance.setRelativePlaySpeed(handle, speed)` then
  `filters.pitchShiftFilter.shift.value = 1 / speed`).

See `example/lib/filters/pitchshift.dart`.

## limiter

Peak limiter / anti-clip. Not part of stock SoLoud (`src/filters/limiter.cpp`).
Activate it **after** all other filters in the same scope so it catches their
output.

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| threshold | -60 | 0 | -3 | dB; signals above are reduced |
| outputCeiling | -60 | 0 | -1 | dB; keep < 0 to prevent clipping |
| kneeWidth | 0 | 30 | 6 | dB; larger = softer transition |
| releaseTime | 1 | 1000 | 50 | ms |
| attackTime | 0.1 | 200 | 1 | ms |

Defaults are already a sensible anti-clip setting: just `activate()`.
See `example/lib/filters/limiter.dart`.

## compressor

Dynamic-range compressor. Not part of stock SoLoud (`src/filters/compressor.cpp`).
Like the limiter, activate it after other filters.

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| threshold | -80 | 0 | -6 | dB; compression starts above this |
| makeupGain | -40 | 40 | 0 | dB; compensates volume lost to compression |
| kneeWidth | 0 | 40 | 2 | dB; soft-knee width |
| ratio | 1 | 10 | 3 | e.g. 4 means 4 dB in above threshold -> 1 dB out |
| attackTime | 0 | 100 | 10 | ms |
| releaseTime | 0 | 1000 | 100 | ms |

See `example/lib/filters/compressor.dart` (demonstrates taming many simultaneous
loud voices).

## parametricEq

N-band equalizer (1–64 bands), STFT-based. Source:
`src/filters/parametric_eq_filter` / `src/soloud/src/filter/parametric_eq.cpp`.

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | |
| stftWindowSize | 32 | 4096 | 1024 | **must be a power of two**; invalid values are ignored with a log warning. Larger = better frequency resolution, more latency/CPU |
| numBands | 1 | 64 | 3 | number of active bands |
| bandGain(i) | 0 | 4 | 1 | per band `i` in 0..63; 1 = flat, >1 boost, <1 cut |
| bandFreq(i) | 10 | 24000 | geometric | per band `i` center frequency (Hz); defaults to 30 Hz .. 12 kHz geometric distribution |
| bandQ(i) | 0.1 | 20 | by band count | per band `i` Q factor (bell curve sharpness); the default makes adjacent bands overlap |

Extra API: `bandFrequency(i)` returns the current center frequency (Hz) of band `i`
(alias `bandFrequencyParam(i)` for `bandFreq(i)`). Defaults distribute 30 Hz to
12 kHz (e.g. 3 bands: 30 Hz, 600 Hz, 12 kHz). With every gain at 1 the filter is
transparent, and the bands together cover the whole spectrum.

There are no `queryXxx` getters, and the `ParametricEqParam` helper class is
not exported by `package:flutter_soloud/flutter_soloud.dart`: take the ranges
from this table or from `param.min`/`param.max`
(`eq.bandGain(0).max`). Default band frequencies and Q depend on the band
count; read the live values with `bandFrequency(i)` and `bandQ(i).value`. At
per-sound scope the params take the handle after the band:
`sound.filters.parametricEqFilter.bandGain(0, soundHandle: handle)`.

## amplitudeModulator

Bipolar amplitude modulator / ring modulator with selectable waveforms (`src/filters/amplitude_modulator_filter.cpp`).
At full wet the output is `input * carrier(2π * frequency * t)`. The carrier has no DC bias, so this is suppressed-carrier
ring modulation: with a sine carrier, an input at `f1` produces sidebands at `frequency - f1` and `frequency + f1`
(e.g. 1 kHz with a 16 kHz carrier gives 15 kHz + 17 kHz).

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | 0 = transparent, 1 = only the modulated signal |
| frequency | 0.1 | 20000 | 440 | Hz; clamped just below Nyquist (0.49 × sample rate) while processing, the queried value stays as set |
| waveform | 0 | 3 | 0 | Carrier waveform: `0` = Sine, `1` = Triangle, `2` = Square, `3` = Saw |

The carrier phase is continuous across buffers and frequency changes, so
`fadeFilterParameter`/`oscillateFilterParameter` on `frequency` sweep without
clicks. All channels share the same carrier phase. As a per-sound filter it
runs at the sound's sample rate, so Nyquist is that of the sound.

## customFilter(id)

Multi-instance user-programmable synchronous DSP filter (`src/filters/custom_filter.cpp`).
Up to 8 independent instances (`id` in `0..7`) can be activated on the same scope (`filters.customFilter(0)` .. `filters.customFilter(7)`).

| Param | min | max | def | Notes |
|---|---|---|---|---|
| wet | 0 | 1 | 1 | `0.0` = hard bypass (callback not invoked); `1.0` = zero-copy in-place; `0 < wet < 1` = automatic dry/wet blend |
| `param(1)` .. `param(15)` | -1e9 (customizable) | 1e9 (customizable) | 0 (customizable) | Define name, range and default with `configureParameters(List<CustomFilterParamSpec>)` (first spec = parameter 1, at most 7); `param(i, min:, max:)` sets the range checked by `.value` on the Dart side |

`wet` is a getter at global scope and a method at sound/bus scope, like the
built-in filters; `param(i)` is a method everywhere (sound scope:
`param(i, soundHandle: handle)`). `deactivate()` keeps the parameter
definitions but clears the callback: call `setNativeCallback()` again after the
next `activate()`. Details in **flutter-soloud-custom-dsp**.

// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

/// From [SoLoud doc](https://solhsa.com/soloud/biquadfilter.html):
/// The biquad resonant filter is a surprisingly cheap way to implement low and
/// high pass filters, as well as some kind of band bass filter.
/// The implementation in SoLoud is based on "Using the Biquad Resonant Filter",
/// Phil Burk, Game Programming Gems 3, p. 606.
///
/// The filter has three parameters - sample rate, cutoff frequency and
/// resonance. These can also be adjusted on live streams, for instance to
/// fade the low pass filter cutoff frequency for a outdoors/indoors
/// transition effect.
///
/// The resonance parameter adjusts the sharpness (or bandwidth) of the cutoff.
///
/// The [type] is treat as an int value and it means:
/// LOWPASS = 0,
/// HIGHPASS = 1,
/// BANDPASS = 2
enum BiquadResonantEnum {
  wet,
  type,
  frequency,
  resonance;

  final List<double> _mins = const [0, 0.0, 10, 0.1];
  final List<double> _maxs = const [1, 2.0, 16000, 20];
  final List<double> _defs = const [1, 0.0, 1000, 0.1];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        BiquadResonantEnum.wet => 'Wet',
        BiquadResonantEnum.type => 'Type',
        BiquadResonantEnum.frequency => 'Frequency',
        BiquadResonantEnum.resonance => 'Resonance',
      };
}

abstract class _BiquadResonantInternal extends FilterBase {
  const _BiquadResonantInternal(SoundHash? soundHash)
      : super(FilterType.biquadResonantFilter, soundHash);

  BiquadResonantEnum get queryWet => BiquadResonantEnum.wet;
  BiquadResonantEnum get queryType => BiquadResonantEnum.type;
  BiquadResonantEnum get queryFrequency => BiquadResonantEnum.frequency;
  BiquadResonantEnum get queryResonance => BiquadResonantEnum.resonance;
}

class BiquadResonantSingle extends _BiquadResonantInternal {
  BiquadResonantSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        BiquadResonantEnum.wet.index,
        BiquadResonantEnum.wet.min,
        BiquadResonantEnum.wet.max,
      );

  FilterParam type({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        BiquadResonantEnum.type.index,
        BiquadResonantEnum.type.min,
        BiquadResonantEnum.type.max,
      );

  FilterParam frequency({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        BiquadResonantEnum.frequency.index,
        BiquadResonantEnum.frequency.min,
        BiquadResonantEnum.frequency.max,
      );

  FilterParam resonance({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        BiquadResonantEnum.resonance.index,
        BiquadResonantEnum.resonance.min,
        BiquadResonantEnum.resonance.max,
      );
}

class BiquadResonantGlobal extends _BiquadResonantInternal {
  const BiquadResonantGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        BiquadResonantEnum.wet.index,
        BiquadResonantEnum.wet.min,
        BiquadResonantEnum.wet.max,
      );

  FilterParam get type => FilterParam(
        null,
        filterType,
        BiquadResonantEnum.type.index,
        BiquadResonantEnum.type.min,
        BiquadResonantEnum.type.max,
      );

  FilterParam get frequency => FilterParam(
        null,
        filterType,
        BiquadResonantEnum.frequency.index,
        BiquadResonantEnum.frequency.min,
        BiquadResonantEnum.frequency.max,
      );

  FilterParam get resonance => FilterParam(
        null,
        filterType,
        BiquadResonantEnum.resonance.index,
        BiquadResonantEnum.resonance.min,
        BiquadResonantEnum.resonance.max,
      );
}

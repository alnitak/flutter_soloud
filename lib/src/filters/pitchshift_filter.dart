// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum PitchShiftEnum {
  wet,
  shift,
  semitones;

  final List<double> _mins = const [0, 0, -36];
  final List<double> _maxs = const [1, 3, 36];
  final List<double> _defs = const [1, 1, 0];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        PitchShiftEnum.wet => 'Wet',
        PitchShiftEnum.shift => 'Shift',
        PitchShiftEnum.semitones => 'Semitones',
      };
}

abstract class _PitchShiftInternal extends FilterBase {
  const _PitchShiftInternal(SoundHash? soundHash)
      : super(FilterType.pitchShiftFilter, soundHash);

  PitchShiftEnum get queryWet => PitchShiftEnum.wet;
  PitchShiftEnum get queryShift => PitchShiftEnum.shift;
  PitchShiftEnum get querySemitones => PitchShiftEnum.semitones;
}

class PitchShiftSingle extends _PitchShiftInternal {
  PitchShiftSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        PitchShiftEnum.wet.index,
        PitchShiftEnum.wet.min,
        PitchShiftEnum.wet.max,
      );

  FilterParam shift({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        PitchShiftEnum.shift.index,
        PitchShiftEnum.shift.min,
        PitchShiftEnum.shift.max,
      );

  FilterParam semitones({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        PitchShiftEnum.semitones.index,
        PitchShiftEnum.semitones.min,
        PitchShiftEnum.semitones.max,
      );

  /// Set
  void timeStretch(SoundHandle soundHandle, double value) {
    // Adjust the play speed
    SoLoud.instance.setRelativePlaySpeed(soundHandle!, value);
    shift(soundHandle: soundHandle).value = 1.0 / value;
  }
}

class PitchShiftGlobal extends _PitchShiftInternal {
  const PitchShiftGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        PitchShiftEnum.wet.index,
        PitchShiftEnum.wet.min,
        PitchShiftEnum.wet.max,
      );

  FilterParam get shift => FilterParam(
        null,
        filterType,
        PitchShiftEnum.shift.index,
        PitchShiftEnum.shift.min,
        PitchShiftEnum.shift.max,
      );

  FilterParam get semitones => FilterParam(
        null,
        filterType,
        PitchShiftEnum.semitones.index,
        PitchShiftEnum.semitones.min,
        PitchShiftEnum.semitones.max,
      );
}

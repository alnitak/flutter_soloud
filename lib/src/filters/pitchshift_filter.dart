// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum PitchShiftEnum {
  wet,
  shift,
  semitones;

  /// use iterables?
  final List<double> _mins = const [0, 0, -48];
  final List<double> _maxs = const [1, 3, 48];
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

abstract class PitchShiftInternal {
  PitchShiftInternal(SoundHash? soundHash) : _soundHash = soundHash;

  final SoundHash? _soundHash;
  FilterType get type => FilterType.pitchShiftFilter;
  PitchShiftEnum get wetQuery => PitchShiftEnum.wet;
  PitchShiftEnum get shiftQuery => PitchShiftEnum.shift;
  PitchShiftEnum get semitonesQuery => PitchShiftEnum.semitones;

  void activate() => type.activate(_soundHash);

  void deactivate() => type.deactivate(_soundHash);
}

class PitchShiftSingle extends PitchShiftInternal {
  PitchShiftSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        type,
        PitchShiftEnum.wet.index,
        PitchShiftEnum.wet.min,
        PitchShiftEnum.wet.max,
      );

  FilterParam shift({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        type,
        PitchShiftEnum.shift.index,
        PitchShiftEnum.shift.min,
        PitchShiftEnum.shift.max,
      );

  FilterParam semitones({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        type,
        PitchShiftEnum.semitones.index,
        PitchShiftEnum.semitones.min,
        PitchShiftEnum.semitones.max,
      );
}

class PitchShiftGlobal extends PitchShiftInternal {
  PitchShiftGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        type,
        PitchShiftEnum.wet.index,
        PitchShiftEnum.wet.min,
        PitchShiftEnum.wet.max,
      );

  FilterParam get shift => FilterParam(
        null,
        type,
        PitchShiftEnum.shift.index,
        PitchShiftEnum.shift.min,
        PitchShiftEnum.shift.max,
      );

  FilterParam get semitones => FilterParam(
        null,
        type,
        PitchShiftEnum.semitones.index,
        PitchShiftEnum.semitones.min,
        PitchShiftEnum.semitones.max,
      );
}

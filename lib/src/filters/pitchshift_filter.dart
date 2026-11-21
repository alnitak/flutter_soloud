// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum PitchShiftEnum {
  wet,
  shift,
  semitones;

  // The semitones range is the shift range (0.1..3) expressed in semitones,
  // 12 * log2(shift), rounded inwards: about -39.86..19.02.
  final List<double> _mins = const [0, 0.1, -39.863];
  final List<double> _maxs = const [1, 3, 19.0195];
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
  const _PitchShiftInternal(SoundHash? soundHash, int? busId)
    : super(FilterType.pitchShiftFilter, soundHash, busId);

  PitchShiftEnum get queryWet => PitchShiftEnum.wet;
  PitchShiftEnum get queryShift => PitchShiftEnum.shift;
  PitchShiftEnum get querySemitones => PitchShiftEnum.semitones;
}

class PitchShiftSingle extends _PitchShiftInternal {
  PitchShiftSingle(super.soundHash, super.busId);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam.ofSource(
    soundHandle,
    super.busId,
    filterType,
    PitchShiftEnum.wet.index,
    PitchShiftEnum.wet.min,
    PitchShiftEnum.wet.max,
  );

  /// The shift value of the pitch, where 1.0 means the pitch is not shifted.
  /// Its range is 0.1..3.
  ///
  /// Note that both the [shift] and [semitones] parameters are acting to modify
  /// the pitch value, but using different scales. Changing this value will
  /// therefore adjust the [semitones] parameter with:
  /// ```dart
  /// semitones = 12 * log(value) / ln2; // 12 * log2(value)
  /// ```
  FilterParam shift({SoundHandle? soundHandle}) => FilterParam.ofSource(
    soundHandle,
    super.busId,
    filterType,
    PitchShiftEnum.shift.index,
    PitchShiftEnum.shift.min,
    PitchShiftEnum.shift.max,
  );

  /// The number of semitones that the pitch is shifted. Its range is the
  /// [shift] range in semitones, about -39.86..19.02.
  ///
  /// Note that both the [shift] and [semitones] parameters are acting to modify
  /// the pitch value, but using different scales. Changing this value will
  /// therefore adjust the [shift] parameter with:
  /// ```dart
  /// shift = pow(2, value / 12);
  /// ```
  FilterParam semitones({SoundHandle? soundHandle}) => FilterParam.ofSource(
    soundHandle,
    super.busId,
    filterType,
    PitchShiftEnum.semitones.index,
    PitchShiftEnum.semitones.min,
    PitchShiftEnum.semitones.max,
  );

  /// Adjust the play speed of a sound without changing the pitch of the audio.
  ///
  /// This is done by counteracting the change in pitch caused by changing the
  /// speed using the [shift] parameter.
  void timeStretch(SoundHandle soundHandle, double value) {
    // Adjust the play speed
    SoLoud.instance.setRelativePlaySpeed(soundHandle!, value);
    shift(soundHandle: soundHandle).value = 1.0 / value;
  }
}

class PitchShiftGlobal extends _PitchShiftInternal {
  const PitchShiftGlobal() : super(null, null);

  FilterParam get wet => FilterParam(
    null,
    null,
    filterType,
    PitchShiftEnum.wet.index,
    PitchShiftEnum.wet.min,
    PitchShiftEnum.wet.max,
  );

  /// The shift value of the pitch, where 1.0 means the pitch is not shifted.
  /// Its range is 0.1..3.
  ///
  /// Note that both the [shift] and [semitones] parameters are acting to modify
  /// the pitch value, but using different scales. Changing this value will
  /// therefore adjust the [semitones] parameter with:
  /// ```dart
  /// semitones = 12 * log(value) / ln2; // 12 * log2(value)
  /// ```
  FilterParam get shift => FilterParam(
    null,
    null,
    filterType,
    PitchShiftEnum.shift.index,
    PitchShiftEnum.shift.min,
    PitchShiftEnum.shift.max,
  );

  /// The number of semitones that the pitch is shifted. Its range is the
  /// [shift] range in semitones, about -39.86..19.02.
  ///
  /// Note that both the [shift] and [semitones] parameters are acting to modify
  /// the pitch value, but using different scales. Changing this value will
  /// therefore adjust the [shift] parameter with:
  /// ```dart
  /// shift = pow(2, value / 12);
  /// ```
  FilterParam get semitones => FilterParam(
    null,
    null,
    filterType,
    PitchShiftEnum.semitones.index,
    PitchShiftEnum.semitones.min,
    PitchShiftEnum.semitones.max,
  );
}

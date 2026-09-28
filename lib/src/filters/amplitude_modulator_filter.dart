// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum AmplitudeModulatorEnum {
  wet,
  frequency;

  final List<double> _mins = const [0, 0.1];
  final List<double> _maxs = const [1, 20000];
  final List<double> _defs = const [1, 440];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
    AmplitudeModulatorEnum.wet => 'Wet',
    AmplitudeModulatorEnum.frequency => 'Frequency',
  };
}

abstract class _AmplitudeModulatorInternal extends FilterBase {
  const _AmplitudeModulatorInternal(SoundHash? soundHash, int? busId)
    : super(FilterType.amplitudeModulatorFilter, soundHash, busId);

  AmplitudeModulatorEnum get queryWet => AmplitudeModulatorEnum.wet;
  AmplitudeModulatorEnum get queryFrequency => AmplitudeModulatorEnum.frequency;
}

class AmplitudeModulatorSingle extends _AmplitudeModulatorInternal {
  AmplitudeModulatorSingle(super.soundHash, super.busId);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
    soundHandle,
    super.busId,
    filterType,
    AmplitudeModulatorEnum.wet.index,
    AmplitudeModulatorEnum.wet.min,
    AmplitudeModulatorEnum.wet.max,
  );

  /// The carrier frequency in Hz.
  ///
  /// Frequencies at or above the Nyquist limit of the sample rate the filter
  /// runs at are clamped just below it while processing; the value set here
  /// is still the one returned when querying the parameter.
  FilterParam frequency({SoundHandle? soundHandle}) => FilterParam(
    soundHandle,
    super.busId,
    filterType,
    AmplitudeModulatorEnum.frequency.index,
    AmplitudeModulatorEnum.frequency.min,
    AmplitudeModulatorEnum.frequency.max,
  );
}

class AmplitudeModulatorGlobal extends _AmplitudeModulatorInternal {
  const AmplitudeModulatorGlobal() : super(null, null);

  FilterParam get wet => FilterParam(
    null,
    null,
    filterType,
    AmplitudeModulatorEnum.wet.index,
    AmplitudeModulatorEnum.wet.min,
    AmplitudeModulatorEnum.wet.max,
  );

  /// The carrier frequency in Hz.
  ///
  /// Frequencies at or above the Nyquist limit of the output sample rate are
  /// clamped just below it while processing; the value set here is still the
  /// one returned when querying the parameter.
  FilterParam get frequency => FilterParam(
    null,
    null,
    filterType,
    AmplitudeModulatorEnum.frequency.index,
    AmplitudeModulatorEnum.frequency.min,
    AmplitudeModulatorEnum.frequency.max,
  );
}

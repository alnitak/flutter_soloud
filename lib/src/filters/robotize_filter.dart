// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum RobotizeEnum {
  wet,
  frequency,
  waveform;

  final List<double> _mins = const [0, 0.1, 0];
  final List<double> _maxs = const [1, 100, 6];
  final List<double> _defs = const [1, 30, 0];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        RobotizeEnum.wet => 'Wet',
        RobotizeEnum.frequency => 'Frequency',
        RobotizeEnum.waveform => 'Waveform',
      };
}

abstract class _RobotizeInternal extends FilterBase {
  const _RobotizeInternal(SoundHash? soundHash, int? busId)
      : super(FilterType.robotizeFilter, soundHash, busId);

  RobotizeEnum get queryWet => RobotizeEnum.wet;
  RobotizeEnum get queryFrequency => RobotizeEnum.frequency;
  RobotizeEnum get queryWaveform => RobotizeEnum.waveform;
}

class RobotizeSingle extends _RobotizeInternal {
  RobotizeSingle(super.soundHash, super.busId);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        RobotizeEnum.wet.index,
        RobotizeEnum.wet.min,
        RobotizeEnum.wet.max,
      );

  FilterParam frequency({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        RobotizeEnum.frequency.index,
        RobotizeEnum.frequency.min,
        RobotizeEnum.frequency.max,
      );

  FilterParam waveform({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        RobotizeEnum.waveform.index,
        RobotizeEnum.waveform.min,
        RobotizeEnum.waveform.max,
      );
}

class RobotizeGlobal extends _RobotizeInternal {
  const RobotizeGlobal() : super(null, null);

  FilterParam get wet => FilterParam(
        null,
        null,
        filterType,
        RobotizeEnum.wet.index,
        RobotizeEnum.wet.min,
        RobotizeEnum.wet.max,
      );

  FilterParam get frequency => FilterParam(
        null,
        null,
        filterType,
        RobotizeEnum.frequency.index,
        RobotizeEnum.frequency.min,
        RobotizeEnum.frequency.max,
      );

  FilterParam get waveform => FilterParam(
        null,
        null,
        filterType,
        RobotizeEnum.waveform.index,
        RobotizeEnum.waveform.min,
        RobotizeEnum.waveform.max,
      );
}

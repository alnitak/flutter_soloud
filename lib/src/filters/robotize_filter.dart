// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum RobotizeEnum {
  wet,
  frequency,
  waveform;

  /// use iterables?
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

abstract class RobotizeInternal {
  const RobotizeInternal(SoundHash? soundHash) : _soundHash = soundHash;

  final SoundHash? _soundHash;
  FilterType get filterType => FilterType.robotizeFilter;
  RobotizeEnum get queryWet => RobotizeEnum.wet;
  RobotizeEnum get queryFrequency => RobotizeEnum.frequency;
  RobotizeEnum get queryWaveform => RobotizeEnum.waveform;

  void activate() => filterType.activate(_soundHash);

  void deactivate() => filterType.deactivate(_soundHash);
}

class RobotizeSingle extends RobotizeInternal {
  RobotizeSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        RobotizeEnum.wet.index,
        RobotizeEnum.wet.min,
        RobotizeEnum.wet.max,
      );

  FilterParam frequency({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        RobotizeEnum.frequency.index,
        RobotizeEnum.frequency.min,
        RobotizeEnum.frequency.max,
      );

  FilterParam waveform({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        RobotizeEnum.waveform.index,
        RobotizeEnum.waveform.min,
        RobotizeEnum.waveform.max,
      );
}

class RobotizeGlobal extends RobotizeInternal {
  const RobotizeGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        RobotizeEnum.wet.index,
        RobotizeEnum.wet.min,
        RobotizeEnum.wet.max,
      );

  FilterParam get frequency => FilterParam(
        null,
        filterType,
        RobotizeEnum.frequency.index,
        RobotizeEnum.frequency.min,
        RobotizeEnum.frequency.max,
      );

  FilterParam get waveform => FilterParam(
        null,
        filterType,
        RobotizeEnum.waveform.index,
        RobotizeEnum.waveform.min,
        RobotizeEnum.waveform.max,
      );
}

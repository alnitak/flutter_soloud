// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum FlangerEnum {
  wet,
  delay,
  freq;

  final List<double> _mins = const [0, 0, -48];
  final List<double> _maxs = const [1, 3, 48];
  final List<double> _defs = const [1, 1, 0];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        FlangerEnum.wet => 'Wet',
        FlangerEnum.delay => 'Delay',
        FlangerEnum.freq => 'Freq',
      };
}

abstract class _FlangerInternal extends FilterBase {
  const _FlangerInternal(SoundHash? soundHash)
      : super(FilterType.flangerFilter, soundHash);

  FlangerEnum get queryWet => FlangerEnum.wet;
  FlangerEnum get queryDelay => FlangerEnum.delay;
  FlangerEnum get queryFreq => FlangerEnum.freq;
}

class FlangerSingle extends _FlangerInternal {
  FlangerSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FlangerEnum.wet.index,
        FlangerEnum.wet.min,
        FlangerEnum.wet.max,
      );

  FilterParam delay({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FlangerEnum.delay.index,
        FlangerEnum.delay.min,
        FlangerEnum.delay.max,
      );

  FilterParam freq({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FlangerEnum.freq.index,
        FlangerEnum.freq.min,
        FlangerEnum.freq.max,
      );
}

class FlangerGlobal extends _FlangerInternal {
  const FlangerGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        FlangerEnum.wet.index,
        FlangerEnum.wet.min,
        FlangerEnum.wet.max,
      );

  FilterParam get delay => FilterParam(
        null,
        filterType,
        FlangerEnum.delay.index,
        FlangerEnum.delay.min,
        FlangerEnum.delay.max,
      );

  FilterParam get freq => FilterParam(
        null,
        filterType,
        FlangerEnum.freq.index,
        FlangerEnum.freq.min,
        FlangerEnum.freq.max,
      );
}

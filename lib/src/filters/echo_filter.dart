// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum EchoEnum {
  wet,
  delay,
  decay,
  filter;

  final List<double> _mins = const [0, 0.001, 0.001, 0];
  final List<double> _maxs = const [1, double.maxFinite, 1, 1];
  final List<double> _defs = const [1, 0.3, 0.7, 0];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        EchoEnum.wet => 'Wet',
        EchoEnum.delay => 'Delay',
        EchoEnum.decay => 'Decay',
        EchoEnum.filter => 'Filter',
      };
}

abstract class _EchoInternal extends FilterBase {
  const _EchoInternal(SoundHash? soundHash)
      : super(FilterType.echoFilter, soundHash);

  EchoEnum get queryWet => EchoEnum.wet;
  EchoEnum get queryDelay => EchoEnum.delay;
  EchoEnum get queryDecay => EchoEnum.decay;
  EchoEnum get queryFilter => EchoEnum.filter;
}

class EchoSingle extends _EchoInternal {
  EchoSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EchoEnum.wet.index,
        EchoEnum.wet.min,
        EchoEnum.wet.max,
      );

  FilterParam delay({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EchoEnum.delay.index,
        EchoEnum.delay.min,
        EchoEnum.delay.max,
      );

  FilterParam decay({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EchoEnum.decay.index,
        EchoEnum.decay.min,
        EchoEnum.decay.max,
      );

  FilterParam filter({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EchoEnum.filter.index,
        EchoEnum.filter.min,
        EchoEnum.filter.max,
      );
}

class EchoGlobal extends _EchoInternal {
  const EchoGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        EchoEnum.wet.index,
        EchoEnum.wet.min,
        EchoEnum.wet.max,
      );

  FilterParam get delay => FilterParam(
        null,
        filterType,
        EchoEnum.delay.index,
        EchoEnum.delay.min,
        EchoEnum.delay.max,
      );

  FilterParam get decay => FilterParam(
        null,
        filterType,
        EchoEnum.decay.index,
        EchoEnum.decay.min,
        EchoEnum.decay.max,
      );

  FilterParam get filter => FilterParam(
        null,
        filterType,
        EchoEnum.filter.index,
        EchoEnum.filter.min,
        EchoEnum.filter.max,
      );
}

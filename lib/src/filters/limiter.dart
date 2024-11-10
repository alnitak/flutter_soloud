// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum Limiter {
  wet,
  threshold,
  attackTime,
  releaseTime,
  makeupGain;

  final List<double> _mins = const [0, -24, 0.001, 0.01, 0];
  final List<double> _maxs = const [1, 0, 0.1, 1, 4];
  final List<double> _defs = const [1, -6, 0.01, 0.1, 1.0];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        Limiter.wet => 'Wet',
        Limiter.threshold => 'Threshold',
        Limiter.attackTime => 'Attack Time',
        Limiter.releaseTime => 'Release Time',
        Limiter.makeupGain => 'Makeup Gain',
      };
}

abstract class _LimiterInternal extends FilterBase {
  const _LimiterInternal(SoundHash? soundHash)
      : super(FilterType.limiterFilter, soundHash);

  Limiter get queryWet => Limiter.wet;
  Limiter get queryThreshold => Limiter.threshold;
  Limiter get queryAttackTime => Limiter.attackTime;
  Limiter get queryReleaseTime => Limiter.releaseTime;
  Limiter get queryMakeupGain => Limiter.makeupGain;
}

class LimiterSingle extends _LimiterInternal {
  LimiterSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.wet.index,
        Limiter.wet.min,
        Limiter.wet.max,
      );

  FilterParam threshold({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.threshold.index,
        Limiter.threshold.min,
        Limiter.threshold.max,
      );

  FilterParam attackTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.attackTime.index,
        Limiter.attackTime.min,
        Limiter.attackTime.max,
      );

  FilterParam releaseTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.releaseTime.index,
        Limiter.releaseTime.min,
        Limiter.releaseTime.max,
      );

  FilterParam makeupGain({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.makeupGain.index,
        Limiter.makeupGain.min,
        Limiter.makeupGain.max,
      );
}

class LimiterGlobal extends _LimiterInternal {
  const LimiterGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        Limiter.wet.index,
        Limiter.wet.min,
        Limiter.wet.max,
      );

  FilterParam get threshold => FilterParam(
        null,
        filterType,
        Limiter.threshold.index,
        Limiter.threshold.min,
        Limiter.threshold.max,
      );

  FilterParam get attackTime => FilterParam(
        null,
        filterType,
        Limiter.attackTime.index,
        Limiter.attackTime.min,
        Limiter.attackTime.max,
      );

  FilterParam get releaseTime => FilterParam(
        null,
        filterType,
        Limiter.releaseTime.index,
        Limiter.releaseTime.min,
        Limiter.releaseTime.max,
      );

  FilterParam get makeupGain => FilterParam(
        null,
        filterType,
        Limiter.makeupGain.index,
        Limiter.makeupGain.min,
        Limiter.makeupGain.max,
      );
}

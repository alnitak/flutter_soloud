// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum Limiter {
  wet,
  threshold,
  outputCeiling,
  kneeWidth,
  releaseTime,
  attackTime;

  final List<double> _mins = const [0, -60, -60, 0, 1, 0.1];
  final List<double> _maxs = const [1, 0, 0, 30, 1000, 200];
  final List<double> _defs = const [1, -6, -1, 2, 100, 1];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        Limiter.wet => 'Wet',
        Limiter.threshold => 'Threshold',
        Limiter.outputCeiling => 'Output Ceiling',
        Limiter.kneeWidth => 'Knee Width',
        Limiter.releaseTime => 'Release Time',
        Limiter.attackTime => 'Attack Time',
      };
}

abstract class _LimiterInternal extends FilterBase {
  const _LimiterInternal(SoundHash? soundHash)
      : super(FilterType.limiterFilter, soundHash);

  Limiter get queryWet => Limiter.wet;
  Limiter get queryThreshold => Limiter.threshold;
  Limiter get queryOutputCeiling => Limiter.outputCeiling;
  Limiter get queryKneeWidth => Limiter.kneeWidth;
  Limiter get queryReleaseTime => Limiter.releaseTime;
  Limiter get queryAttackTime => Limiter.attackTime;
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

  FilterParam outputCeiling({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.outputCeiling.index,
        Limiter.outputCeiling.min,
        Limiter.outputCeiling.max,
      );

  FilterParam kneeWidth({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.kneeWidth.index,
        Limiter.kneeWidth.min,
        Limiter.kneeWidth.max,
      );

  FilterParam releaseTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.releaseTime.index,
        Limiter.releaseTime.min,
        Limiter.releaseTime.max,
      );

  FilterParam attackTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.attackTime.index,
        Limiter.attackTime.min,
        Limiter.attackTime.max,
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

  FilterParam get outputCeiling => FilterParam(
        null,
        filterType,
        Limiter.outputCeiling.index,
        Limiter.outputCeiling.min,
        Limiter.outputCeiling.max,
      );

  FilterParam get kneeWidth => FilterParam(
        null,
        filterType,
        Limiter.kneeWidth.index,
        Limiter.kneeWidth.min,
        Limiter.kneeWidth.max,
      );

  FilterParam get releaseTime => FilterParam(
        null,
        filterType,
        Limiter.releaseTime.index,
        Limiter.releaseTime.min,
        Limiter.releaseTime.max,
      );

  FilterParam get attackTime => FilterParam(
        null,
        filterType,
        Limiter.attackTime.index,
        Limiter.attackTime.min,
        Limiter.attackTime.max,
      );
}

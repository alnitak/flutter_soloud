// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum Limiter {
  wet,
  threshold,
  makeupGain,
  kneeWidth,
  lookahead,
  releaseTime;

  final List<double> _mins = const [0, -60, -30, 0, 0, 1];
  final List<double> _maxs = const [1, 0, 30, 30, 10, 1000];
  final List<double> _defs = const [1, -6, 0, 6, 1, 100];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        Limiter.wet => 'Wet',
        Limiter.threshold => 'Threshold',
        Limiter.makeupGain => 'Makeup Gain',
        Limiter.kneeWidth => 'Knee Width',
        Limiter.lookahead => 'Lookahead',
        Limiter.releaseTime => 'Release Time',
      };
}

abstract class _LimiterInternal extends FilterBase {
  const _LimiterInternal(SoundHash? soundHash)
      : super(FilterType.limiterFilter, soundHash);

  Limiter get queryWet => Limiter.wet;
  Limiter get queryThreshold => Limiter.threshold;
  Limiter get queryMakeupGain => Limiter.makeupGain;
  Limiter get queryKneeWidth => Limiter.kneeWidth;
  Limiter get queryLookahead => Limiter.lookahead;
  Limiter get queryReleaseTime => Limiter.releaseTime;
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

  FilterParam makeupGain({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.makeupGain.index,
        Limiter.makeupGain.min,
        Limiter.makeupGain.max,
      );

  FilterParam kneeWidth({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.kneeWidth.index,
        Limiter.kneeWidth.min,
        Limiter.kneeWidth.max,
      );

  FilterParam lookahead({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.lookahead.index,
        Limiter.lookahead.min,
        Limiter.lookahead.max,
      );

  FilterParam releaseTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        Limiter.releaseTime.index,
        Limiter.releaseTime.min,
        Limiter.releaseTime.max,
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

  FilterParam get makeupGain => FilterParam(
        null,
        filterType,
        Limiter.makeupGain.index,
        Limiter.makeupGain.min,
        Limiter.makeupGain.max,
      );

  FilterParam get kneeWidth => FilterParam(
        null,
        filterType,
        Limiter.kneeWidth.index,
        Limiter.kneeWidth.min,
        Limiter.kneeWidth.max,
      );

  FilterParam get lookahead => FilterParam(
        null,
        filterType,
        Limiter.lookahead.index,
        Limiter.lookahead.min,
        Limiter.lookahead.max,
      );

  FilterParam get releaseTime => FilterParam(
        null,
        filterType,
        Limiter.releaseTime.index,
        Limiter.releaseTime.min,
        Limiter.releaseTime.max,
      );
}

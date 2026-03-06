// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum Compressor {
  wet,
  threshold,
  makeupGain,
  kneeWidth,
  ratio,
  attackTime,
  releaseTime;

  final List<double> _mins = const [0, -80, -40, 0, 1, 0, 0];
  final List<double> _maxs = const [1, 0, 40, 40, 10, 100, 1000];
  final List<double> _defs = const [1, -6, 0, 2, 3, 10, 100];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        Compressor.wet => 'Wet',
        Compressor.threshold => 'Threshold',
        Compressor.makeupGain => 'Makeup Gain',
        Compressor.kneeWidth => 'Knee Width',
        Compressor.ratio => 'Ratio',
        Compressor.attackTime => 'Attack Time',
        Compressor.releaseTime => 'Release Time',
      };
}

abstract class _CompressorInternal extends FilterBase {
  const _CompressorInternal(SoundHash? soundHash, int? busId)
      : super(FilterType.compressorFilter, soundHash, busId);

  Compressor get queryWet => Compressor.wet;
  Compressor get queryThreshold => Compressor.threshold;
  Compressor get queryMakeupGain => Compressor.makeupGain;
  Compressor get queryKneeWidth => Compressor.kneeWidth;
  Compressor get queryRatio => Compressor.ratio;
  Compressor get queryAttackTime => Compressor.attackTime;
  Compressor get queryReleaseTime => Compressor.releaseTime;
}

class CompressorSingle extends _CompressorInternal {
  CompressorSingle(super.soundHash, super.busId);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.wet.index,
        Compressor.wet.min,
        Compressor.wet.max,
      );

  FilterParam threshold({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.threshold.index,
        Compressor.threshold.min,
        Compressor.threshold.max,
      );

  FilterParam makeupGain({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.makeupGain.index,
        Compressor.makeupGain.min,
        Compressor.makeupGain.max,
      );

  FilterParam kneeWidth({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.kneeWidth.index,
        Compressor.kneeWidth.min,
        Compressor.kneeWidth.max,
      );

  FilterParam ratio({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.ratio.index,
        Compressor.ratio.min,
        Compressor.ratio.max,
      );

  FilterParam attackTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.attackTime.index,
        Compressor.attackTime.min,
        Compressor.attackTime.max,
      );

  FilterParam releaseTime({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        super.busId,
        filterType,
        Compressor.releaseTime.index,
        Compressor.releaseTime.min,
        Compressor.releaseTime.max,
      );
}

class CompressorGlobal extends _CompressorInternal {
  const CompressorGlobal() : super(null, null);

  FilterParam get wet => FilterParam(
        null,
        null,
        filterType,
        Compressor.wet.index,
        Compressor.wet.min,
        Compressor.wet.max,
      );

  FilterParam get threshold => FilterParam(
        null,
        null,
        filterType,
        Compressor.threshold.index,
        Compressor.threshold.min,
        Compressor.threshold.max,
      );

  FilterParam get makeupGain => FilterParam(
        null,
        null,
        filterType,
        Compressor.makeupGain.index,
        Compressor.makeupGain.min,
        Compressor.makeupGain.max,
      );

  FilterParam get kneeWidth => FilterParam(
        null,
        null,
        filterType,
        Compressor.kneeWidth.index,
        Compressor.kneeWidth.min,
        Compressor.kneeWidth.max,
      );

  FilterParam get ratio => FilterParam(
        null,
        null,
        filterType,
        Compressor.ratio.index,
        Compressor.ratio.min,
        Compressor.ratio.max,
      );

  FilterParam get attackTime => FilterParam(
        null,
        null,
        filterType,
        Compressor.attackTime.index,
        Compressor.attackTime.min,
        Compressor.attackTime.max,
      );

  FilterParam get releaseTime => FilterParam(
        null,
        null,
        filterType,
        Compressor.releaseTime.index,
        Compressor.releaseTime.min,
        Compressor.releaseTime.max,
      );
}

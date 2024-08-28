// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum EqualizerEnum {
  wet,
  band1,
  band2,
  band3,
  band4,
  band5,
  band6,
  band7,
  band8;

  final List<double> _mins = const [0, 0, 0, 0, 0, 0, 0, 0, 0];
  final List<double> _maxs = const [1, 4, 4, 4, 4, 4, 4, 4, 4];
  final List<double> _defs = const [1, 1, 1, 1, 1, 1, 1, 1, 1];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        EqualizerEnum.wet => 'Wet',
        EqualizerEnum.band1 => 'Band 1',
        EqualizerEnum.band2 => 'Band 2',
        EqualizerEnum.band3 => 'Band 3',
        EqualizerEnum.band4 => 'Band 4',
        EqualizerEnum.band5 => 'Band 5',
        EqualizerEnum.band6 => 'Band 6',
        EqualizerEnum.band7 => 'Band 7',
        EqualizerEnum.band8 => 'Band 8',
      };
}

abstract class _EqualizerInternal extends FilterBase {
  const _EqualizerInternal(SoundHash? soundHash)
      : super(FilterType.eqFilter, soundHash);

  EqualizerEnum get queryWet => EqualizerEnum.wet;
  EqualizerEnum get queryBand1 => EqualizerEnum.band1;
  EqualizerEnum get queryBand2 => EqualizerEnum.band2;
  EqualizerEnum get queryBand3 => EqualizerEnum.band3;
  EqualizerEnum get queryBand4 => EqualizerEnum.band4;
  EqualizerEnum get queryBand5 => EqualizerEnum.band5;
  EqualizerEnum get queryBand6 => EqualizerEnum.band6;
  EqualizerEnum get queryBand7 => EqualizerEnum.band7;
  EqualizerEnum get queryBand8 => EqualizerEnum.band8;
}

class EqualizerSingle extends _EqualizerInternal {
  EqualizerSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.wet.index,
        EqualizerEnum.wet.min,
        EqualizerEnum.wet.max,
      );

  FilterParam band1({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band1.index,
        EqualizerEnum.band1.min,
        EqualizerEnum.band1.max,
      );

  FilterParam band2({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band2.index,
        EqualizerEnum.band2.min,
        EqualizerEnum.band2.max,
      );

  FilterParam band3({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band3.index,
        EqualizerEnum.band3.min,
        EqualizerEnum.band3.max,
      );

  FilterParam band4({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band4.index,
        EqualizerEnum.band4.min,
        EqualizerEnum.band4.max,
      );

  FilterParam band5({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band5.index,
        EqualizerEnum.band5.min,
        EqualizerEnum.band5.max,
      );

  FilterParam band6({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band6.index,
        EqualizerEnum.band6.min,
        EqualizerEnum.band6.max,
      );

  FilterParam band7({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band7.index,
        EqualizerEnum.band7.min,
        EqualizerEnum.band7.max,
      );

  FilterParam band8({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        EqualizerEnum.band8.index,
        EqualizerEnum.band8.min,
        EqualizerEnum.band8.max,
      );
}

class EqualizerGlobal extends _EqualizerInternal {
  const EqualizerGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        EqualizerEnum.wet.index,
        EqualizerEnum.wet.min,
        EqualizerEnum.wet.max,
      );

  FilterParam get band1 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band1.index,
        EqualizerEnum.band1.min,
        EqualizerEnum.band1.max,
      );

  FilterParam get band2 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band2.index,
        EqualizerEnum.band2.min,
        EqualizerEnum.band2.max,
      );

  FilterParam get band3 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band3.index,
        EqualizerEnum.band3.min,
        EqualizerEnum.band3.max,
      );

  FilterParam get band4 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band4.index,
        EqualizerEnum.band4.min,
        EqualizerEnum.band4.max,
      );

  FilterParam get band5 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band5.index,
        EqualizerEnum.band5.min,
        EqualizerEnum.band5.max,
      );

  FilterParam get band6 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band6.index,
        EqualizerEnum.band6.min,
        EqualizerEnum.band6.max,
      );

  FilterParam get band7 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band7.index,
        EqualizerEnum.band7.min,
        EqualizerEnum.band7.max,
      );

  FilterParam get band8 => FilterParam(
        null,
        filterType,
        EqualizerEnum.band8.index,
        EqualizerEnum.band8.min,
        EqualizerEnum.band8.max,
      );
}

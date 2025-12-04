// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum ParametricEqEnum {
  wet,
  bass,
  mid,
  treble;

  final List<double> _mins = const [0, 0, 0, 0];
  final List<double> _maxs = const [1, 4, 4, 4];
  final List<double> _defs = const [1, 1, 1, 1];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        ParametricEqEnum.wet => 'Wet',
        ParametricEqEnum.bass => 'Bass',
        ParametricEqEnum.mid => 'Mid',
        ParametricEqEnum.treble => 'Treble',
      };
}

abstract class _ParametricEqInternal extends FilterBase {
  const _ParametricEqInternal(SoundHash? soundHash)
      : super(FilterType.parametricEq, soundHash);

  ParametricEqEnum get queryWet => ParametricEqEnum.wet;
  ParametricEqEnum get queryBass => ParametricEqEnum.bass;
  ParametricEqEnum get queryMid => ParametricEqEnum.mid;
  ParametricEqEnum get queryTreble => ParametricEqEnum.treble;
}

class ParametricEqSingle extends _ParametricEqInternal {
  ParametricEqSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqEnum.wet.index,
        ParametricEqEnum.wet.min,
        ParametricEqEnum.wet.max,
      );

  FilterParam bass({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqEnum.bass.index,
        ParametricEqEnum.bass.min,
        ParametricEqEnum.bass.max,
      );

  FilterParam mid({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqEnum.mid.index,
        ParametricEqEnum.mid.min,
        ParametricEqEnum.mid.max,
      );

  FilterParam treble({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqEnum.treble.index,
        ParametricEqEnum.treble.min,
        ParametricEqEnum.treble.max,
      );
}

class ParametricEqGlobal extends _ParametricEqInternal {
  const ParametricEqGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        ParametricEqEnum.wet.index,
        ParametricEqEnum.wet.min,
        ParametricEqEnum.wet.max,
      );

  FilterParam get bass => FilterParam(
        null,
        filterType,
        ParametricEqEnum.bass.index,
        ParametricEqEnum.bass.min,
        ParametricEqEnum.bass.max,
      );

  FilterParam get mid => FilterParam(
        null,
        filterType,
        ParametricEqEnum.mid.index,
        ParametricEqEnum.mid.min,
        ParametricEqEnum.mid.max,
      );

  FilterParam get treble => FilterParam(
        null,
        filterType,
        ParametricEqEnum.treble.index,
        ParametricEqEnum.treble.min,
        ParametricEqEnum.treble.max,
      );
}

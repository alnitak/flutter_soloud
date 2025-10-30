// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum ParametricEq3Enum {
  wet,
  bass,
  mid,
  treble;

  final List<double> _mins = const [0, 0, 0, 0];
  final List<double> _maxs = const [1, 2, 2, 2];
  final List<double> _defs = const [1, 1, 1, 1];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        ParametricEq3Enum.wet => 'Wet',
        ParametricEq3Enum.bass => 'Bass',
        ParametricEq3Enum.mid => 'Mid',
        ParametricEq3Enum.treble => 'Treble',
      };
}

abstract class _ParametricEq3Internal extends FilterBase {
  const _ParametricEq3Internal(SoundHash? soundHash)
      : super(FilterType.echoFilter, soundHash);

  ParametricEq3Enum get queryWet => ParametricEq3Enum.wet;
  ParametricEq3Enum get queryBass => ParametricEq3Enum.bass;
  ParametricEq3Enum get queryMid => ParametricEq3Enum.mid;
  ParametricEq3Enum get queryTreble => ParametricEq3Enum.treble;
}

class ParametricEq3Single extends _ParametricEq3Internal {
  ParametricEq3Single(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEq3Enum.wet.index,
        ParametricEq3Enum.wet.min,
        ParametricEq3Enum.wet.max,
      );

  FilterParam bass({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEq3Enum.bass.index,
        ParametricEq3Enum.bass.min,
        ParametricEq3Enum.bass.max,
      );

  FilterParam mid({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEq3Enum.mid.index,
        ParametricEq3Enum.mid.min,
        ParametricEq3Enum.mid.max,
      );

  FilterParam treble({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEq3Enum.treble.index,
        ParametricEq3Enum.treble.min,
        ParametricEq3Enum.treble.max,
      );
}

class ParametricEq3Global extends _ParametricEq3Internal {
  const ParametricEq3Global() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        ParametricEq3Enum.wet.index,
        ParametricEq3Enum.wet.min,
        ParametricEq3Enum.wet.max,
      );

  FilterParam get bass => FilterParam(
        null,
        filterType,
        ParametricEq3Enum.bass.index,
        ParametricEq3Enum.bass.min,
        ParametricEq3Enum.bass.max,
      );

  FilterParam get mid => FilterParam(
        null,
        filterType,
        ParametricEq3Enum.mid.index,
        ParametricEq3Enum.mid.min,
        ParametricEq3Enum.mid.max,
      );

  FilterParam get treble => FilterParam(
        null,
        filterType,
        ParametricEq3Enum.treble.index,
        ParametricEq3Enum.treble.min,
        ParametricEq3Enum.treble.max,
      );
}

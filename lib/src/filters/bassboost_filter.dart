// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum BassBoostEnum {
  wet,
  boost;

  final List<double> _mins = const [0, 0];
  final List<double> _maxs = const [1, 10];
  final List<double> _defs = const [1, 2];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        BassBoostEnum.wet => 'Wet',
        BassBoostEnum.boost => 'Boost',
      };
}

abstract class _BassBoostInternal extends FilterBase {
  const _BassBoostInternal(SoundHash? soundHash)
      : super(FilterType.bassboostFilter, soundHash);

  BassBoostEnum get queryWet => BassBoostEnum.wet;
  BassBoostEnum get queryBoost => BassBoostEnum.boost;
}

class BassBoostSingle extends _BassBoostInternal {
  BassBoostSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        BassBoostEnum.wet.index,
        BassBoostEnum.wet.min,
        BassBoostEnum.wet.max,
      );

  FilterParam boost({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        BassBoostEnum.boost.index,
        BassBoostEnum.boost.min,
        BassBoostEnum.boost.max,
      );
}

class BassBoostGlobal extends _BassBoostInternal {
  const BassBoostGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        BassBoostEnum.wet.index,
        BassBoostEnum.wet.min,
        BassBoostEnum.wet.max,
      );

  FilterParam get boost => FilterParam(
        null,
        filterType,
        BassBoostEnum.boost.index,
        BassBoostEnum.boost.min,
        BassBoostEnum.boost.max,
      );
}

// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum FreeverbEnum {
  wet,
  freeze,
  roomSize,
  damp,
  width;

  final List<double> _mins = const [0, 0, 0, 0, 0];
  final List<double> _maxs = const [1, 1, 1, 1, 1];
  final List<double> _defs = const [1, 0, 0.5, 0.5, 1];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        FreeverbEnum.wet => 'Wet',
        FreeverbEnum.freeze => 'Freeze',
        FreeverbEnum.roomSize => 'Room Size',
        FreeverbEnum.damp => 'Damp',
        FreeverbEnum.width => 'Width',
      };
}

abstract class _FreeverbInternal extends FilterBase {
  const _FreeverbInternal(SoundHash? soundHash)
      : super(FilterType.freeverbFilter, soundHash);

  FreeverbEnum get queryWet => FreeverbEnum.wet;
  FreeverbEnum get queryFreeze => FreeverbEnum.freeze;
  FreeverbEnum get queryRoomSize => FreeverbEnum.roomSize;
  FreeverbEnum get queryDamp => FreeverbEnum.damp;
  FreeverbEnum get queryWidth => FreeverbEnum.width;
}

class FreeverbSingle extends _FreeverbInternal {
  FreeverbSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FreeverbEnum.wet.index,
        FreeverbEnum.wet.min,
        FreeverbEnum.wet.max,
      );

  FilterParam freeze({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FreeverbEnum.freeze.index,
        FreeverbEnum.freeze.min,
        FreeverbEnum.freeze.max,
      );

  FilterParam roomSize({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FreeverbEnum.roomSize.index,
        FreeverbEnum.roomSize.min,
        FreeverbEnum.roomSize.max,
      );

  FilterParam damp({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FreeverbEnum.damp.index,
        FreeverbEnum.damp.min,
        FreeverbEnum.damp.max,
      );

  FilterParam width({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        FreeverbEnum.width.index,
        FreeverbEnum.width.min,
        FreeverbEnum.width.max,
      );
}

class FreeverbGlobal extends _FreeverbInternal {
  const FreeverbGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        FreeverbEnum.wet.index,
        FreeverbEnum.wet.min,
        FreeverbEnum.wet.max,
      );

  FilterParam get freeze => FilterParam(
        null,
        filterType,
        FreeverbEnum.freeze.index,
        FreeverbEnum.freeze.min,
        FreeverbEnum.freeze.max,
      );

  FilterParam get roomSize => FilterParam(
        null,
        filterType,
        FreeverbEnum.roomSize.index,
        FreeverbEnum.roomSize.min,
        FreeverbEnum.roomSize.max,
      );

  FilterParam get damp => FilterParam(
        null,
        filterType,
        FreeverbEnum.damp.index,
        FreeverbEnum.damp.min,
        FreeverbEnum.damp.max,
      );

  FilterParam get width => FilterParam(
        null,
        filterType,
        FreeverbEnum.width.index,
        FreeverbEnum.width.min,
        FreeverbEnum.width.max,
      );
}

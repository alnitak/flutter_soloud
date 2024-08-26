// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum LofiEnum {
  wet,
  samplerate,
  bitdepth;

  final List<double> _mins = const [0, 100, 0.5];
  final List<double> _maxs = const [1, 22000, 16];
  final List<double> _defs = const [1, 4000, 3];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        LofiEnum.wet => 'Wet',
        LofiEnum.samplerate => 'Samplerate',
        LofiEnum.bitdepth => 'Bitdepth',
      };
}

abstract class LofiInternal {
  const LofiInternal(SoundHash? soundHash) : _soundHash = soundHash;

  final SoundHash? _soundHash;
  FilterType get filterType => FilterType.lofiFilter;
  LofiEnum get queryWet => LofiEnum.wet;
  LofiEnum get querySamplerate => LofiEnum.samplerate;
  LofiEnum get queryBitdepth => LofiEnum.bitdepth;

  void activate() => filterType.activate(_soundHash);

  void deactivate() => filterType.deactivate(_soundHash);

  int isActive() => filterType.isActive(_soundHash);
}

class LofiSingle extends LofiInternal {
  LofiSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        LofiEnum.wet.index,
        LofiEnum.wet.min,
        LofiEnum.wet.max,
      );

  FilterParam samplerate({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        LofiEnum.samplerate.index,
        LofiEnum.samplerate.min,
        LofiEnum.samplerate.max,
      );

  FilterParam bitdepth({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        LofiEnum.bitdepth.index,
        LofiEnum.bitdepth.min,
        LofiEnum.bitdepth.max,
      );
}

class LofiGlobal extends LofiInternal {
  const LofiGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        LofiEnum.wet.index,
        LofiEnum.wet.min,
        LofiEnum.wet.max,
      );

  FilterParam get samplerate => FilterParam(
        null,
        filterType,
        LofiEnum.samplerate.index,
        LofiEnum.samplerate.min,
        LofiEnum.samplerate.max,
      );

  FilterParam get bitdepth => FilterParam(
        null,
        filterType,
        LofiEnum.bitdepth.index,
        LofiEnum.bitdepth.min,
        LofiEnum.bitdepth.max,
      );
}

// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum LofiFilterEnum {
  wet,
  samplerate,
  bitdepth;

  /// use iterables?
  final List<double> _mins = const [0, 100, 0.5];
  final List<double> _maxs = const [1, 22000, 16];
  final List<double> _defs = const [1, 4000, 3];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        LofiFilterEnum.wet => 'Wet',
        LofiFilterEnum.samplerate => 'Samplerate',
        LofiFilterEnum.bitdepth => 'Bitdepth',
      };
}

abstract class LofiFilterInternal {
  const LofiFilterInternal(SoundHash? soundHash) : _soundHash = soundHash;

  final SoundHash? _soundHash;
  FilterType get type => FilterType.lofiFilter;
  LofiFilterEnum get queryWet => LofiFilterEnum.wet;
  LofiFilterEnum get querySamplerate => LofiFilterEnum.samplerate;
  LofiFilterEnum get queryBitdepth => LofiFilterEnum.bitdepth;

  void activate() => type.activate(_soundHash);

  void deactivate() => type.deactivate(_soundHash);
}

class LofiFilterSingle extends LofiFilterInternal {
  LofiFilterSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        type,
        LofiFilterEnum.wet.index,
        LofiFilterEnum.wet.min,
        LofiFilterEnum.wet.max,
      );

  FilterParam samplerate({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        type,
        LofiFilterEnum.samplerate.index,
        LofiFilterEnum.samplerate.min,
        LofiFilterEnum.samplerate.max,
      );

  FilterParam bitdepth({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        type,
        LofiFilterEnum.bitdepth.index,
        LofiFilterEnum.bitdepth.min,
        LofiFilterEnum.bitdepth.max,
      );
}

class LofiFilterGlobal extends LofiFilterInternal {
  const LofiFilterGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        type,
        LofiFilterEnum.wet.index,
        LofiFilterEnum.wet.min,
        LofiFilterEnum.wet.max,
      );

  FilterParam get samplerate => FilterParam(
        null,
        type,
        LofiFilterEnum.samplerate.index,
        LofiFilterEnum.samplerate.min,
        LofiFilterEnum.samplerate.max,
      );

  FilterParam get bitdepth => FilterParam(
        null,
        type,
        LofiFilterEnum.bitdepth.index,
        LofiFilterEnum.bitdepth.min,
        LofiFilterEnum.bitdepth.max,
      );
}

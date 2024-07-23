// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

enum WaveShaperEnum {
  wet,
  amount;

  /// use iterables?
  final List<double> _mins = const [0, -1];
  final List<double> _maxs = const [1, 1];
  final List<double> _defs = const [1, 0];

  double get min => _mins[index];
  double get max => _maxs[index];
  double get def => _defs[index];

  @override
  String toString() => switch (this) {
        WaveShaperEnum.wet => 'Wet',
        WaveShaperEnum.amount => 'Amount',
      };
}

abstract class WaveShaperInternal {
  const WaveShaperInternal(SoundHash? soundHash) : _soundHash = soundHash;

  final SoundHash? _soundHash;
  FilterType get filterType => FilterType.waveShaperFilter;
  WaveShaperEnum get queryWet => WaveShaperEnum.wet;
  WaveShaperEnum get queryAmount => WaveShaperEnum.amount;

  void activate() => filterType.activate(_soundHash);

  void deactivate() => filterType.deactivate(_soundHash);
}

class WaveShaperSingle extends WaveShaperInternal {
  WaveShaperSingle(super.soundHash);

  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        WaveShaperEnum.wet.index,
        WaveShaperEnum.wet.min,
        WaveShaperEnum.wet.max,
      );

  FilterParam amount({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        WaveShaperEnum.amount.index,
        WaveShaperEnum.amount.min,
        WaveShaperEnum.amount.max,
      );
}

class WaveShaperGlobal extends WaveShaperInternal {
  const WaveShaperGlobal() : super(null);

  FilterParam get wet => FilterParam(
        null,
        filterType,
        WaveShaperEnum.wet.index,
        WaveShaperEnum.wet.min,
        WaveShaperEnum.wet.max,
      );

  FilterParam get amount => FilterParam(
        null,
        filterType,
        WaveShaperEnum.amount.index,
        WaveShaperEnum.amount.min,
        WaveShaperEnum.amount.max,
      );
}

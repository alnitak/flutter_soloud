// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

/// Parameter indices for the parametric equalizer filter.
///
/// The parametric EQ has the following parameters:
/// - Index 0: wet (0-1, default 1)
/// - Index 1: STFT window size (32-4096, power of 2, default 1024)
/// - Index 2: number of bands (1-64, default 3)
/// - Index 3+: gain for each band (0-4, default 1)
class ParametricEqParam {
  /// Wet parameter index - controls wet/dry mix (0-1, default 1)
  static const int wet = 0;

  /// STFT window size parameter index (32-4096, power of 2, default 1024)
  static const int stftWindowSize = 1;

  /// Number of bands parameter index (1-64, default 3)
  static const int numBands = 2;

  /// Starting index for band gains (0-4, default 1 per band)
  /// Band N gain is at index: bandGainOffset + N
  static const int bandGainOffset = 3;

  /// Maximum number of bands supported
  static const int maxBands = 64;

  /// Get the parameter index for a specific band's gain
  /// [bandIndex] should be 0-63
  static int bandGain(int bandIndex) {
    if (bandIndex < 0 || bandIndex >= maxBands) {
      throw ArgumentError('Band index must be between 0 and ${maxBands - 1}');
    }
    return bandGainOffset + bandIndex;
  }

  /// Get min value for a parameter at the given index
  static double getMin(int paramIndex) {
    if (paramIndex == wet) return 0;
    if (paramIndex == stftWindowSize) return 32;
    if (paramIndex == numBands) return 1;
    // Band gains
    return 0;
  }

  /// Get max value for a parameter at the given index
  static double getMax(int paramIndex) {
    if (paramIndex == wet) return 1;
    if (paramIndex == stftWindowSize) return 4096;
    if (paramIndex == numBands) return 64;
    // Band gains
    return 4;
  }

  /// Get default value for a parameter at the given index
  static double getDefault(int paramIndex) {
    if (paramIndex == wet) return 1;
    if (paramIndex == stftWindowSize) return 1024;
    if (paramIndex == numBands) return 3;
    // Band gains
    return 1;
  }

  /// Get the name of a parameter at the given index
  static String getName(int paramIndex) {
    if (paramIndex == wet) return 'Wet';
    if (paramIndex == stftWindowSize) return 'STFT Window Size';
    if (paramIndex == numBands) return 'Number of Bands';
    if (paramIndex >= bandGainOffset &&
        paramIndex < bandGainOffset + maxBands) {
      return 'Band ${paramIndex - bandGainOffset} Gain';
    }
    return 'Unknown';
  }
}

abstract class _ParametricEqInternal extends FilterBase {
  const _ParametricEqInternal(SoundHash? soundHash)
      : super(FilterType.parametricEq, soundHash);
}

class ParametricEqSingle extends _ParametricEqInternal {
  ParametricEqSingle(super.soundHash);

  /// Get the wet parameter (0-1, default 1)
  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqParam.wet,
        ParametricEqParam.getMin(ParametricEqParam.wet),
        ParametricEqParam.getMax(ParametricEqParam.wet),
      );

  /// Get the STFT window size parameter (32-4096, power of 2, default 1024)
  FilterParam stftWindowSize({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqParam.stftWindowSize,
        ParametricEqParam.getMin(ParametricEqParam.stftWindowSize),
        ParametricEqParam.getMax(ParametricEqParam.stftWindowSize),
      );

  /// Get the number of bands parameter (1-64, default 3)
  FilterParam numBands({SoundHandle? soundHandle}) => FilterParam(
        soundHandle,
        filterType,
        ParametricEqParam.numBands,
        ParametricEqParam.getMin(ParametricEqParam.numBands),
        ParametricEqParam.getMax(ParametricEqParam.numBands),
      );

  /// Get the gain parameter for a specific band (0-4, default 1)
  /// [bandIndex] should be 0-63
  FilterParam bandGain(int bandIndex, {SoundHandle? soundHandle}) {
    final paramIndex = ParametricEqParam.bandGain(bandIndex);
    return FilterParam(
      soundHandle,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }
}

class ParametricEqGlobal extends _ParametricEqInternal {
  const ParametricEqGlobal() : super(null);

  /// Get the wet parameter (0-1, default 1)
  FilterParam get wet => FilterParam(
        null,
        filterType,
        ParametricEqParam.wet,
        ParametricEqParam.getMin(ParametricEqParam.wet),
        ParametricEqParam.getMax(ParametricEqParam.wet),
      );

  /// Get the STFT window size parameter (32-4096, power of 2, default 1024)
  FilterParam get stftWindowSize => FilterParam(
        null,
        filterType,
        ParametricEqParam.stftWindowSize,
        ParametricEqParam.getMin(ParametricEqParam.stftWindowSize),
        ParametricEqParam.getMax(ParametricEqParam.stftWindowSize),
      );

  /// Get the number of bands parameter (1-64, default 3)
  FilterParam get numBands => FilterParam(
        null,
        filterType,
        ParametricEqParam.numBands,
        ParametricEqParam.getMin(ParametricEqParam.numBands),
        ParametricEqParam.getMax(ParametricEqParam.numBands),
      );

  /// Get the gain parameter for a specific band (0-4, default 1)
  /// [bandIndex] should be 0-63
  FilterParam bandGain(int bandIndex) {
    final paramIndex = ParametricEqParam.bandGain(bandIndex);
    return FilterParam(
      null,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }
}

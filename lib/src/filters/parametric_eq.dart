// ignore_for_file: public_member_api_docs

import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

/// Parameter indices for the parametric equalizer filter.
///
/// The parametric EQ has the following parameters:
/// - Index 0: wet (0-1, default 1)
/// - Index 1: STFT window size (32-4096, power of 2, default 1024)
/// - Index 2: number of bands (1-64, default 3)
/// - Index 3-66: gain for each band (0-4, default 1)
/// - Index 67-130: center frequency for each band (10-24000 Hz, default
///   geometric 30-12000 Hz)
/// - Index 131-194: Q factor for each band (0.1-20, default based on band
///   count / 1.0)
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

  /// Starting index for band center frequencies (10-24000 Hz)
  /// Band N frequency is at index: bandFreqOffset + N
  static const int bandFreqOffset = 3 + maxBands; // 67

  /// Starting index for band Q factors (0.1-20)
  /// Band N Q factor is at index: bandQOffset + N
  static const int bandQOffset = 3 + maxBands * 2; // 131

  /// Total number of parameters
  static const int maxParams = 3 + maxBands * 3; // 195

  /// Get the parameter index for a specific band's gain
  /// [bandIndex] should be 0-63
  static int bandGain(int bandIndex) {
    if (bandIndex < 0 || bandIndex >= maxBands) {
      throw ArgumentError('Band index must be between 0 and ${maxBands - 1}');
    }
    return bandGainOffset + bandIndex;
  }

  /// Get the parameter index for a specific band's center frequency
  /// [bandIndex] should be 0-63
  static int bandFreq(int bandIndex) {
    if (bandIndex < 0 || bandIndex >= maxBands) {
      throw ArgumentError('Band index must be between 0 and ${maxBands - 1}');
    }
    return bandFreqOffset + bandIndex;
  }

  /// Get the parameter index for a specific band's Q factor
  /// [bandIndex] should be 0-63
  static int bandQ(int bandIndex) {
    if (bandIndex < 0 || bandIndex >= maxBands) {
      throw ArgumentError('Band index must be between 0 and ${maxBands - 1}');
    }
    return bandQOffset + bandIndex;
  }

  /// Get min value for a parameter at the given index
  static double getMin(int paramIndex) {
    if (paramIndex == wet) return 0;
    if (paramIndex == stftWindowSize) return 32;
    if (paramIndex == numBands) return 1;
    if (paramIndex >= bandGainOffset && paramIndex < bandFreqOffset) return 0;
    if (paramIndex >= bandFreqOffset && paramIndex < bandQOffset) return 10;
    if (paramIndex >= bandQOffset && paramIndex < maxParams) return 0.1;
    return 0;
  }

  /// Get max value for a parameter at the given index
  static double getMax(int paramIndex) {
    if (paramIndex == wet) return 1;
    if (paramIndex == stftWindowSize) return 4096;
    if (paramIndex == numBands) return 64;
    if (paramIndex >= bandGainOffset && paramIndex < bandFreqOffset) return 4;
    if (paramIndex >= bandFreqOffset && paramIndex < bandQOffset) return 24000;
    if (paramIndex >= bandQOffset && paramIndex < maxParams) return 20;
    return 1;
  }

  /// Get default value for a parameter at the given index
  static double getDefault(int paramIndex) {
    if (paramIndex == wet) return 1;
    if (paramIndex == stftWindowSize) return 1024;
    if (paramIndex == numBands) return 3;
    if (paramIndex >= bandGainOffset && paramIndex < bandFreqOffset) return 1;
    if (paramIndex >= bandFreqOffset && paramIndex < bandQOffset) {
      final bandIndex = paramIndex - bandFreqOffset;
      if (bandIndex == 0) return 30;
      if (bandIndex == 1) return 600;
      if (bandIndex == 2) return 12000;
      return 1000;
    }
    if (paramIndex >= bandQOffset && paramIndex < maxParams) return 1;
    return 0;
  }

  /// Get the name of a parameter at the given index
  static String getName(int paramIndex) {
    if (paramIndex == wet) return 'Wet';
    if (paramIndex == stftWindowSize) return 'STFT Window Size';
    if (paramIndex == numBands) return 'Number of Bands';
    if (paramIndex >= bandGainOffset && paramIndex < bandFreqOffset) {
      return 'Band ${paramIndex - bandGainOffset} Gain';
    }
    if (paramIndex >= bandFreqOffset && paramIndex < bandQOffset) {
      return 'Band ${paramIndex - bandFreqOffset} Frequency';
    }
    if (paramIndex >= bandQOffset && paramIndex < maxParams) {
      return 'Band ${paramIndex - bandQOffset} Q';
    }
    return 'Unknown';
  }
}

abstract class _ParametricEqInternal extends FilterBase {
  const _ParametricEqInternal(SoundHash? soundHash, int? busId)
    : super(FilterType.parametricEq, soundHash, busId);

  /// Get the current number of bands from the filter.
  ///
  /// [soundHandle] is the handle of the playing sound for single sound filters,
  /// or `null` for global/bus filters.
  ///
  /// Returns the configured number of bands, or 0 if the filter
  /// is not active or the value cannot be read.
  @protected
  int getNumBands(SoundHandle? soundHandle) {
    if (kIsWeb && soundHash != null) {
      // Web doesn't support single sound filters
      return 0;
    }
    final ret = SoLoudController().soLoudFFI.getFilterParams(
      handle: soundHandle,
      busId: busId,
      FilterType.parametricEq,
      ParametricEqParam.numBands,
    );
    if (ret.error != PlayerErrors.noError || ret.value > 64) {
      return 0;
    }
    return ret.value.toInt();
  }

  /// Calculate the default center frequency (in Hz) for a specific band.
  ///
  /// [bandIndex] should be 0 to [nBands]-1.
  /// [nBands] is the total number of bands.
  ///
  /// Frequencies are distributed logarithmically (geometrically) between
  /// 30 Hz and 12,000 Hz to match human auditory perception.
  @protected
  @visibleForTesting
  double calculateBandFrequency(int bandIndex, int nBands) {
    if (bandIndex < 0 || bandIndex >= nBands) {
      throw ArgumentError('Band index must be between 0 and ${nBands - 1}');
    }
    if (nBands < 1 || nBands > ParametricEqParam.maxBands) {
      throw ArgumentError(
        'Number of bands must be between 1 and ${ParametricEqParam.maxBands}',
      );
    }

    const f0 = 30.0; // Lower bound: 30 Hz
    const f1 = 12000.0; // Upper bound: 12,000 Hz

    if (nBands == 1) {
      return 1000; // Special case: single band at 1 kHz
    }

    final t = bandIndex / (nBands - 1);
    return f0 * pow(f1 / f0, t);
  }
}

class ParametricEqSingle extends _ParametricEqInternal {
  ParametricEqSingle(super.soundHash, super.busId);

  /// Get the wet parameter (0-1, default 1)
  FilterParam wet({SoundHandle? soundHandle}) => FilterParam(
    soundHandle,
    super.busId,
    filterType,
    ParametricEqParam.wet,
    ParametricEqParam.getMin(ParametricEqParam.wet),
    ParametricEqParam.getMax(ParametricEqParam.wet),
  );

  /// Get the STFT window size parameter (32-4096, power of 2, default 1024)
  FilterParam stftWindowSize({SoundHandle? soundHandle}) => FilterParam(
    soundHandle,
    super.busId,
    filterType,
    ParametricEqParam.stftWindowSize,
    ParametricEqParam.getMin(ParametricEqParam.stftWindowSize),
    ParametricEqParam.getMax(ParametricEqParam.stftWindowSize),
  );

  /// Get the number of bands parameter (1-64, default 3)
  FilterParam numBands({SoundHandle? soundHandle}) => FilterParam(
    soundHandle,
    super.busId,
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
      super.busId,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }

  /// Get the center frequency parameter for a specific band (10-24000 Hz)
  /// [bandIndex] should be 0-63
  FilterParam bandFreq(int bandIndex, {SoundHandle? soundHandle}) {
    final paramIndex = ParametricEqParam.bandFreq(bandIndex);
    return FilterParam(
      soundHandle,
      super.busId,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }

  /// Alias for [bandFreq] to get the center frequency parameter.
  FilterParam bandFrequencyParam(int bandIndex, {SoundHandle? soundHandle}) =>
      bandFreq(bandIndex, soundHandle: soundHandle);

  /// Get the Q factor parameter for a specific band (0.1-20, default ~1.0)
  /// [bandIndex] should be 0-63
  FilterParam bandQ(int bandIndex, {SoundHandle? soundHandle}) {
    final paramIndex = ParametricEqParam.bandQ(bandIndex);
    return FilterParam(
      soundHandle,
      super.busId,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }

  /// Get the center frequency (in Hz) for a specific band.
  ///
  /// [bandIndex] should be 0 to nBands-1.
  /// [soundHandle] is the handle of the playing sound, or `null` for
  /// bus filters.
  ///
  /// Reads the active filter's configured frequency for this band, falling back
  /// to the default geometric spacing if not readable.
  double bandFrequency(int bandIndex, {SoundHandle? soundHandle}) {
    if (!kIsWeb || soundHash == null) {
      final ret = SoLoudController().soLoudFFI.getFilterParams(
        handle: soundHandle,
        busId: busId,
        FilterType.parametricEq,
        ParametricEqParam.bandFreq(bandIndex),
      );
      if (ret.error == PlayerErrors.noError && ret.value > 0) {
        return ret.value;
      }
    }
    final nBands = getNumBands(soundHandle);
    return calculateBandFrequency(bandIndex, nBands);
  }
}

class ParametricEqGlobal extends _ParametricEqInternal {
  const ParametricEqGlobal() : super(null, null);

  /// Get the wet parameter (0-1, default 1)
  FilterParam get wet => FilterParam(
    null,
    null,
    filterType,
    ParametricEqParam.wet,
    ParametricEqParam.getMin(ParametricEqParam.wet),
    ParametricEqParam.getMax(ParametricEqParam.wet),
  );

  /// Get the STFT window size parameter (32-4096, power of 2, default 1024)
  FilterParam get stftWindowSize => FilterParam(
    null,
    null,
    filterType,
    ParametricEqParam.stftWindowSize,
    ParametricEqParam.getMin(ParametricEqParam.stftWindowSize),
    ParametricEqParam.getMax(ParametricEqParam.stftWindowSize),
  );

  /// Get the number of bands parameter (1-64, default 3)
  FilterParam get numBands => FilterParam(
    null,
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
      null,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }

  /// Get the center frequency parameter for a specific band (10-24000 Hz)
  /// [bandIndex] should be 0-63
  FilterParam bandFreq(int bandIndex) {
    final paramIndex = ParametricEqParam.bandFreq(bandIndex);
    return FilterParam(
      null,
      null,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }

  /// Alias for [bandFreq] to get the center frequency parameter.
  FilterParam bandFrequencyParam(int bandIndex) => bandFreq(bandIndex);

  /// Get the Q factor parameter for a specific band (0.1-20, default ~1.0)
  /// [bandIndex] should be 0-63
  FilterParam bandQ(int bandIndex) {
    final paramIndex = ParametricEqParam.bandQ(bandIndex);
    return FilterParam(
      null,
      null,
      filterType,
      paramIndex,
      ParametricEqParam.getMin(paramIndex),
      ParametricEqParam.getMax(paramIndex),
    );
  }

  /// Get the center frequency (in Hz) for a specific band.
  ///
  /// [bandIndex] should be 0 to nBands-1.
  ///
  /// Reads the active filter's configured frequency for this band, falling back
  /// to the default geometric spacing if not readable.
  double bandFrequency(int bandIndex) {
    final ret = SoLoudController().soLoudFFI.getFilterParams(
      FilterType.parametricEq,
      ParametricEqParam.bandFreq(bandIndex),
    );
    if (ret.error == PlayerErrors.noError && ret.value > 0) {
      return ret.value;
    }
    final nBands = getNumBands(null);
    return calculateBandFrequency(bandIndex, nBands);
  }
}

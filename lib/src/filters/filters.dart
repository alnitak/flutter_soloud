import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/bassboost_filter.dart';
import 'package:flutter_soloud/src/filters/biquad_resonant_filter.dart';
import 'package:flutter_soloud/src/filters/compressor.dart';
import 'package:flutter_soloud/src/filters/echo_filter.dart';
import 'package:flutter_soloud/src/filters/equalizer_filter.dart';
import 'package:flutter_soloud/src/filters/flanger_filter.dart';
import 'package:flutter_soloud/src/filters/freeverb_filter.dart';
import 'package:flutter_soloud/src/filters/limiter.dart';
import 'package:flutter_soloud/src/filters/lofi_filter.dart';
import 'package:flutter_soloud/src/filters/pitchshift_filter.dart';
import 'package:flutter_soloud/src/filters/robotize_filter.dart';
import 'package:flutter_soloud/src/filters/wave_shaper_filter.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

/// This class serves as a base for all audio filter methods.
abstract class FilterBase {
  /// The base class common to all filters. It can be used to [activate],
  /// [deactivate] or query its status and its index in the filter list.
  const FilterBase(FilterType ft, SoundHash? soundHash)
      : filterType = ft,
        _soundHash = soundHash;

  final SoundHash? _soundHash;

  /// The type of this filter. It can be used to get the number of its
  /// parameters or the name of the filter.
  final FilterType filterType;

  /// Activate this filter.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// a filter for a single sound on the Web platform.
  void activate() => filterType.activate(_soundHash);

  /// Deactivate this filter.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// a filter for a single sound on the Web platform.
  void deactivate() => filterType.deactivate(_soundHash);

  /// Returns `-1` if the filter is not active. Otherwise, returns
  /// the index of this filter.
  int get index => filterType.isActive(_soundHash);

  /// Checks whether this filter is active.
  bool get isActive => index >= 0;
}

/// These classes are not exposed to the APIs. They are used internally in
/// [AudioSource.filters] and [SoLoud.filters].

/// Filters instance used in [AudioSource.filters]. This differentiate from the
/// below [FiltersGlobal] class for the required [SoundHash] parameter used to
/// add a filter to that specific [AudioSource].
final class FiltersSingle {
  /// The class to get access to all the filters available to sounds.
  const FiltersSingle({required this.soundHash});

  /// The unique hash code of the sound.
  final SoundHash soundHash;

  /// The `Bass Boost` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/bassboostfilter.html)
  BassBoostSingle get bassBoostFilter => BassBoostSingle(soundHash);

  /// The `Biquad Resonant` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/biquadfilter.html)
  BiquadResonantSingle get biquadFilter => BiquadResonantSingle(soundHash);

  /// The `Echo` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/echofilter.html)
  EchoSingle get echoFilter => EchoSingle(soundHash);

  /// The `Equalizer` filter for this sound.
  ///
  /// This filter is not documented in the SoLoud C++ lib, the source code is
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/soloud/src/filter/soloud_eqfilter.cpp)
  EqualizerSingle get equalizerFilter => EqualizerSingle(soundHash);

  /// The `Flanger` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/flangerfilter.html)
  FlangerSingle get flangerFilter => FlangerSingle(soundHash);

  /// The `Freeverb` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/freeverbfilter.html)
  ///
  /// **IMPORTANT**: freeverb supports only 2 channels. So using freeverb as
  /// global filter, the engine should be inited using the default 2 channels
  /// and when using it as sound filter the sound must be with 2 channels.
  FreeverbSingle get freeverbFilter => FreeverbSingle(soundHash);

  /// The `Lofi` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/lofifilter.html)
  LofiSingle get lofiFilter => LofiSingle(soundHash);

  /// The `Robotize` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/robotizefilter.html)
  RobotizeSingle get robotizeFilter => RobotizeSingle(soundHash);

  /// The `Wave Shaper` filter for this sound.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/waveshaperfilter.html)
  WaveShaperSingle get waveShaperFilter => WaveShaperSingle(soundHash);

  /// The `Pitch Shift` filter for this sound.
  ///
  /// This filter is not part of SoLoud C++ lib and the source code is
  /// experimental and can be found
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/filters/pitch_shift_filter.cpp#L16)
  @experimental
  PitchShiftSingle get pitchShiftFilter => PitchShiftSingle(soundHash);

  /// The `Limiter` filter for this sound.
  ///
  /// **Parameters**:
  /// - `wet`: Wet/dry mix ratio, 1.0 means fully wet, 0.0 means fully dry
  ///
  /// - `threshold`: The threshold in dB. Signals above this level are reduced
  /// in gain. A lower value means more aggressive limiting.
  ///
  /// - `outputCeiling`: The maximum output level in dB (should be < 0dB to
  /// prevent clipping)
  ///
  /// - `kneeWidth`: The width of the knee in dB. A larger value results in a
  /// softer transition into limiting.
  ///
  /// - `attackTime`: The attack time in milliseconds. Determines how quickly
  /// the gain reduction recovers after a signal peaks above the threshold.
  ///
  /// - `releaseTime`: The release time in milliseconds. Determines how quickly
  /// the gain reduction recovers after a signal drops below the threshold.
  LimiterSingle get limiterFilter => LimiterSingle(soundHash);

  /// The `Compressor` filter for this sound.
  ///
  /// **Parameters**:
  /// 'wet`: Mix between original (dry) and compressed (wet) signal. 0.0 = 100%
  /// dry, 1.0 = 100% wet.
  ///
  /// `threshold`: The threshold in dB at which compression starts. Values
  /// lower than the threshold will be compressed.
  ///
  /// `makeupGain`: The make-up gain in dB applied to the compressed signal
  /// to compensate for loss in volume due to compression.
  ///
  /// `kneeWidth`: The width in dB of the soft knee where compression smoothly
  /// begins to take effect. A larger value smooths compression.
  ///
  /// `ratio`: The compression ratio. The amount by which input exceeding the
  /// threshold will be reduced. For example, 4:1 reduces 4 dB of input to 1 dB.
  ///
  /// `attackTime`: The time in ms for the compressor to react to a sudden
  /// increase in input level.
  ///
  /// `releaseTime`: The time in ms for the compressor to release the gain
  /// reduction after the input level falls below the threshold.
  ///
  /// This filter is not part of SoLoud C++ lib and the source code is
  /// experimental and can be found
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/filters/compressor.cpp#L24)
  CompressorSingle get compressorFilter => CompressorSingle(soundHash);
}

/// Filters instance used in [SoLoud.filters]. This differentiate from the
/// above [FiltersSingle] class for the unneeded [SoundHash] parameter because
/// the filter is managed globally.
final class FiltersGlobal {
  /// The class to get access to all the filters available globally.
  const FiltersGlobal();

  /// The `Bass Boost` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/bassboostfilter.html)
  BassBoostGlobal get bassBoostFilter => const BassBoostGlobal();

  /// The `Biquad Resonant` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/biquadfilter.html)
  BiquadResonantGlobal get biquadResonantFilter => const BiquadResonantGlobal();

  /// The `Echo` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/echofilter.html)
  EchoGlobal get echoFilter => const EchoGlobal();

  /// The `Equalizer` filter used globally.
  ///
  /// This filter is not documented in the SoLoud C++ lib, the source code is
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/soloud/src/filter/soloud_eqfilter.cpp)
  EqualizerGlobal get equalizerFilter => const EqualizerGlobal();

  /// The `Flanger` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/flangerfilter.html)
  FlangerGlobal get flangerFilter => const FlangerGlobal();

  /// The `Freeverb` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/freeverbfilter.html)
  ///
  /// **IMPORTANT**: freeverb supports only 2 channels. So using freeverb as
  /// global filter, the engine should be inited using the default 2 channels
  /// and when using it as sound filter the sound must be with 2 channels.
  FreeverbGlobal get freeverbFilter => const FreeverbGlobal();

  /// The `Lofi` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/lofifilter.html)
  LofiGlobal get lofiFilter => const LofiGlobal();

  /// The `Robotize` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/robotizefilter.html)
  RobotizeGlobal get robotizeFilter => const RobotizeGlobal();

  /// The `Wave Shaper` filter used globally.
  ///
  /// This filter is documented in the SoLoud C++ lib docs web page
  /// [here](https://solhsa.com/soloud/waveshaperfilter.html)
  WaveShaperGlobal get waveShaperFilter => const WaveShaperGlobal();

  /// The `Pitch Shift` filter used globally.
  ///
  /// This filter is not part of SoLoud C++ lib and the source code is
  /// experimental and can be found
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/filters/pitch_shift_filter.cpp#L16)
  @experimental
  PitchShiftGlobal get pitchShiftFilter => const PitchShiftGlobal();

  /// The `Limiter` filter used globally.
  ///
  /// **Parameters**:
  /// - `wet`: Wet/dry mix ratio, 1.0 means fully wet, 0.0 means fully dry
  ///
  /// - `threshold`: The threshold in dB. Signals above this level are reduced
  /// in gain. A lower value means more aggressive limiting.
  ///
  /// - `outputCeiling`: The maximum output level in dB (should be < 0dB to
  /// prevent clipping)
  ///
  /// - `kneeWidth`: The width of the knee in dB. A larger value results in a
  /// softer transition into limiting.
  ///
  /// - `releaseTime`: The release time in milliseconds. Determines how quickly
  /// the gain reduction recovers after a signal drops below the threshold.
  ///
  /// This filter is not part of SoLoud C++ lib and the source code is
  /// experimental and can be found
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/filters/limiter.cpp#L20)
  @experimental
  LimiterGlobal get limiterFilter => const LimiterGlobal();

  /// The `Compressor` filter used globally.
  ///
  /// **Parameters**:
  /// 'wet`: Mix between original (dry) and compressed (wet) signal. 0.0 = 100%
  /// dry, 1.0 = 100% wet.
  ///
  /// `threshold`: The threshold in dB at which compression starts. Values
  /// lower than the threshold will be compressed.
  ///
  /// `makeupGain`: The make-up gain in dB applied to the compressed signal
  /// to compensate for loss in volume due to compression.
  ///
  /// `kneeWidth`: The width in dB of the soft knee where compression smoothly
  /// begins to take effect. A larger value smooths compression.
  ///
  /// `ratio`: The compression ratio. The amount by which input exceeding the
  /// threshold will be reduced. For example, 4:1 reduces 4 dB of input to 1 dB.
  ///
  /// `attackTime`: The time in ms for the compressor to react to a sudden
  /// increase in input level.
  ///
  /// `releaseTime`: The time in ms for the compressor to release the gain
  /// reduction after the input level falls below the threshold.
  ///
  /// This filter is not part of SoLoud C++ lib and the source code is
  /// experimental and can be found
  /// [here](https://github.com/alnitak/flutter_soloud/blob/main/src/filters/compressor.cpp#L24)
  @experimental
  CompressorGlobal get compressorFilter => const CompressorGlobal();
}

/// Common class for single and global filters.
class FilterParam {
  /// Every filter parameter values can be set/get/fade/oscillate.
  FilterParam(
    this._soundHandle,
    this._type,
    this._attributeId,
    this._min,
    this._max,
  );

  final SoundHandle? _soundHandle;
  final FilterType _type;
  final int _attributeId;
  final double _min;
  final double _max;

  /// Get the parameter value.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// [FiltersSingle] on the Web platform.
  double get value {
    if (kIsWeb && _soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final ret = SoLoudController().soLoudFFI.getFilterParams(
          handle: _soundHandle,
          _type,
          _attributeId,
        );

    if (ret.error != PlayerErrors.noError) {
      Logger('flutter_soloud.${_type.name}Filter')
          .severe(() => 'get value: ${ret.error}');
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.value;
  }

  /// Set the parameter value.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// [FiltersSingle] on the Web platform.
  set value(double val) {
    if (kIsWeb && _soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    if (val < _min || val > _max) {
      Logger('flutter_soloud.${_type.name}Filter')
          .warning(() => 'value [$val] out of accepted range [$_min, $_max]');
      return;
    }
    final error = SoLoudController().soLoudFFI.setFilterParams(
          handle: _soundHandle,
          _type,
          _attributeId,
          val,
        );
    if (error != PlayerErrors.noError) {
      Logger('flutter_soloud.${_type.name}Filter')
          .severe(() => 'set value: $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Fade a parameter value to a new value [to] in [time] time duration.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// [FiltersSingle] on the Web platform.
  void fadeFilterParameter({
    required double to,
    required Duration time,
  }) =>
      _type.fadeFilterParameter(_soundHandle, _attributeId, to, time);

  /// Oscillate a parameter value from [from] value to a new value [to]
  /// in [time] time duration.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// [FiltersSingle] on the Web platform.
  void oscillateFilterParameter({
    required double from,
    required double to,
    required Duration time,
  }) =>
      _type.oscillateFilterParameter(
        _soundHandle,
        _attributeId,
        from,
        to,
        time,
      );
}

/// The different types of audio filters.
enum FilterType {
  /// A biquad resonant filter.
  biquadResonantFilter,

  /// An equalizer filter.
  eqFilter,

  /// An echo filter.
  echoFilter,

  /// A lo-fi filter.
  lofiFilter,

  /// A flanger filter.
  flangerFilter,

  /// A bass-boost filter.
  bassboostFilter,

  /// A wave shaper filter.
  waveShaperFilter,

  /// A robotize filter.
  robotizeFilter,

  /// A reverb filter.
  freeverbFilter,

  /// A pitch shift filter.
  pitchShiftFilter,

  /// A limiter filter.
  limiterFilter,

  /// A compressor filter.
  compressorFilter;

  @override
  String toString() => switch (this) {
        FilterType.biquadResonantFilter => 'Biquad Resonant',
        FilterType.eqFilter => 'Equalizer',
        FilterType.echoFilter => 'Echo',
        FilterType.lofiFilter => 'Lofi',
        FilterType.flangerFilter => 'Flanger',
        FilterType.bassboostFilter => 'Bassboost',
        FilterType.waveShaperFilter => 'Wave Shaper',
        FilterType.robotizeFilter => 'Robotize',
        FilterType.freeverbFilter => 'Freeverb',
        FilterType.pitchShiftFilter => 'Pitchshift',
        FilterType.limiterFilter => 'Limiter',
        FilterType.compressorFilter => 'Compressor',
      };

  /// The number of parameter this filter owns.
  int get numParameters => switch (this) {
        FilterType.biquadResonantFilter => 4,
        FilterType.eqFilter => 9,
        FilterType.echoFilter => 4,
        FilterType.lofiFilter => 3,
        FilterType.flangerFilter => 3,
        FilterType.bassboostFilter => 2,
        FilterType.waveShaperFilter => 2,
        FilterType.robotizeFilter => 3,
        FilterType.freeverbFilter => 5,
        FilterType.pitchShiftFilter => 3,
        FilterType.limiterFilter => 5,
        FilterType.compressorFilter => 8,
      };

  /// Activate this filter. If [soundHash] is null this filter is applied
  /// globally, else to the given [soundHash].
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// a filter for a single sound on the Web platform.
  @internal
  void activate(SoundHash? soundHash) {
    if (kIsWeb && soundHash != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final error = SoLoudController().soLoudFFI.addFilter(
          this,
          soundHash: soundHash ?? const SoundHash.invalid(),
        );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(
        () => '$name activate() '
            '${soundHash == null ? 'global ' : 'single '} filter: $error',
      );
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Checks whether this filter is active.
  ///
  /// Returns `-1` if the filter is not active. Otherwise, returns
  /// the index of the given filter.
  @internal
  int isActive(SoundHash? soundHash) => SoLoudController()
      .soLoudFFI
      .isFilterActive(this, soundHash: soundHash)
      .index;

  /// Deactivate this filter. If [soundHash] is null this filter is removed
  /// globally, else from the given [soundHash].
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// a filter for a single sound on the Web platform.
  @internal
  void deactivate(SoundHash? soundHash) {
    if (kIsWeb && soundHash != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final error = SoLoudController().soLoudFFI.removeFilter(
          this,
          soundHash: soundHash ?? const SoundHash.invalid(),
        );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(
        () => '$name deactivate() '
            '${soundHash == null ? 'global ' : 'single '} filter: $error',
      );
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Fade a parameter with index [attributeId], to a value [to] in [time] time.
  /// If [soundHandle] is null the fade is applied to this global filter else
  /// to the given sound handle.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// a filter for a single sound on the Web platform.
  @internal
  void fadeFilterParameter(
    SoundHandle? soundHandle,
    int attributeId,
    double to,
    Duration time,
  ) {
    if (kIsWeb && soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    if (!SoLoud.instance.isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = SoLoudController().soLoudFFI.fadeFilterParameter(
          this,
          attributeId,
          to,
          time.toDouble(),
          handle: soundHandle,
        );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(() => 'fadeFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Oscillate a parameter with index [attributeId], to a value [to] in
  /// [time] time.
  /// If [soundHandle] is null the fade is applied to this global filter else
  /// to the given sound handle.
  ///
  /// Throws [SoLoudFilterForSingleSoundOnWebDartException] if trying to use
  /// a filter for a single sound on the Web platform.
  @internal
  void oscillateFilterParameter(
    SoundHandle? soundHandle,
    int attributeId,
    double from,
    double to,
    Duration time,
  ) {
    if (kIsWeb && soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    if (!SoLoud.instance.isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = SoLoudController().soLoudFFI.oscillateFilterParameter(
          this,
          attributeId,
          from,
          to,
          time.toDouble(),
          handle: soundHandle,
        );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(() => 'oscillateFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }
}

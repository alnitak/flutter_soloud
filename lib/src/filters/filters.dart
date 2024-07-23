import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/bassboost_filter.dart';
import 'package:flutter_soloud/src/filters/biquad_resonant_filter.dart';
import 'package:flutter_soloud/src/filters/echo_filter.dart';
import 'package:flutter_soloud/src/filters/equalizer_filter.dart';
import 'package:flutter_soloud/src/filters/flanger_filter.dart';
import 'package:flutter_soloud/src/filters/freeverb_filter.dart';
import 'package:flutter_soloud/src/filters/lofi_filter.dart';
import 'package:flutter_soloud/src/filters/pitchshift_filter.dart';
import 'package:flutter_soloud/src/filters/robotize_filter.dart';
import 'package:flutter_soloud/src/filters/wave_shaper_filter.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

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
  BassBoostSingle get bassBoostFilter => BassBoostSingle(soundHash);

  /// The `Biquad Resonant` filter for this sound.
  BiquadResonantSingle get biquadFilter => BiquadResonantSingle(soundHash);

  /// The `Echo` filter for this sound.
  EchoSingle get echoFilter => EchoSingle(soundHash);

  /// The `Equalizer` filter for this sound.
  EqualizerSingle get equalizerFilter => EqualizerSingle(soundHash);

  /// The `Flanger` filter for this sound.
  FlangerSingle get flangerFilter => FlangerSingle(soundHash);

  /// The `Freeverb` filter for this sound.
  FreeverbSingle get freeverbFilter => FreeverbSingle(soundHash);

  /// The `Lofi` filter for this sound.
  LofiSingle get lofiFilter => LofiSingle(soundHash);

  /// The `Pitch Shift` filter for this sound.
  PitchShiftSingle get pitchShiftFilter => PitchShiftSingle(soundHash);

  /// The `Robotize` filter for this sound.
  RobotizeSingle get robotizeFilter => RobotizeSingle(soundHash);

  /// The `Wave Shaper` filter for this sound.
  WaveShaperSingle get waveShaperFilter => WaveShaperSingle(soundHash);
}

/// Filters instance used in [SoLoud.filters]. This differentiate from the
/// above [FiltersSingle] class for the unneeded [SoundHash] parameter because
/// the filter is managed globally.
final class FiltersGlobal {
  /// The class to get access to all the filters available globally.
  const FiltersGlobal();

  /// The `Bass Boost` filter used globally.
  BassBoostGlobal get bassBoostFilter => const BassBoostGlobal();

  /// The `Biquad Resonant` filter used globally.
  BiquadResonantGlobal get biquadResonantFilter => const BiquadResonantGlobal();

  /// The `Echo` filter used globally.
  EchoGlobal get echoFilter => const EchoGlobal();

  /// The `Equalizer` filter used globally.
  EqualizerGlobal get equalizerFilter => const EqualizerGlobal();

  /// The `Flanger` filter used globally.
  FlangerGlobal get flangerFilter => const FlangerGlobal();

  /// The `Freeverb` filter used globally.
  FreeverbGlobal get freeverbFilter => const FreeverbGlobal();

  /// The `Lofi` filter used globally.
  LofiGlobal get lofiFilter => const LofiGlobal();

  /// The `Pitch Shift` filter used globally.
  PitchShiftGlobal get pitchShiftFilter => const PitchShiftGlobal();

  /// The `Robotize` filter used globally.
  RobotizeGlobal get robotizeFilter => const RobotizeGlobal();

  /// The `Wave Shaper` filter used globally.
  WaveShaperGlobal get waveShaperFilter => const WaveShaperGlobal();
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
  double get value {
    final ret = SoLoudController().soLoudFFI.getFilterParams(
          handle: _soundHandle ?? const SoundHandle.error(),
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
  set value(double val) {
    if (val < _min || val > _max) {
      Logger('flutter_soloud.${_type.name}Filter')
          .warning(() => 'value [$val] out of accepted range [$_min, $_max]');
      return;
    }
    final error = SoLoudController().soLoudFFI.setFilterParams(
          handle: _soundHandle ?? const SoundHandle.error(),
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
  void fadeFilterParameter({
    required double to,
    required Duration time,
  }) =>
      _type.fadeFilterParameter(_soundHandle, _attributeId, to, time);

  /// Oscillate a parameter value from [from] value to a new value [to]
  /// in [time] time duration.
  void oscillateFilterParameter({
    required double from,
    required double to,
    required Duration time,
  }) =>
      _type.oscillateFilterParameter(
          _soundHandle, _attributeId, from, to, time);
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
  bassBoostFilter,

  /// A wave shaper filter.
  waveShaperFilter,

  /// A robotize filter.
  robotizeFilter,

  /// A reverb filter.
  freeverbFilter,

  /// A pitch shift filter.
  pitchShiftFilter;

  @override
  String toString() => switch (this) {
        FilterType.biquadResonantFilter => 'Biquad Resonant',
        FilterType.eqFilter => 'Equalizer',
        FilterType.echoFilter => 'Echo',
        FilterType.lofiFilter => 'Lofi',
        FilterType.flangerFilter => 'Flanger',
        FilterType.bassBoostFilter => 'Bassboost',
        FilterType.waveShaperFilter => 'Wave Shaper',
        FilterType.robotizeFilter => 'Robotize',
        FilterType.freeverbFilter => 'Freeverb',
        FilterType.pitchShiftFilter => 'Pitchshift',
      };

  /// The number of parameter this filter owns.
  int get numParameters => switch (this) {
        FilterType.biquadResonantFilter => 4,
        FilterType.eqFilter => 9,
        FilterType.echoFilter => 4,
        FilterType.lofiFilter => 3,
        FilterType.flangerFilter => 3,
        FilterType.bassBoostFilter => 2,
        FilterType.waveShaperFilter => 2,
        FilterType.robotizeFilter => 3,
        FilterType.freeverbFilter => 5,
        FilterType.pitchShiftFilter => 3,
      };

  @internal

  /// Activate this filter. If [soundHash] is null this filter is applied
  /// globally, else to the given [soundHash].
  void activate(SoundHash? soundHash) {
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

  @internal

  /// Deactivate this filter. If [soundHash] is null this filter is removed
  /// globally, else from the given [soundHash].
  void deactivate(SoundHash? soundHash) {
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

  @internal

  /// Fade a parameter with index [attributeId], to a value [to] in [time] time.
  /// If [soundHandle] is null the fade is applied to this global filter else
  /// to the given sound handle.
  void fadeFilterParameter(
    SoundHandle? soundHandle,
    int attributeId,
    double to,
    Duration time,
  ) {
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

  @internal

  /// Oscillate a parameter with index [attributeId], to a value [to] in
  /// [time] time.
  /// If [soundHandle] is null the fade is applied to this global filter else
  /// to the given sound handle.
  void oscillateFilterParameter(
    SoundHandle? soundHandle,
    int attributeId,
    double from,
    double to,
    Duration time,
  ) {
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

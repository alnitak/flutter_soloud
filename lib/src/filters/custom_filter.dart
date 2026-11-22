import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/helpers/argument_checks.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

const bool _kIsWeb = bool.fromEnvironment('dart.library.js_interop');

/// Parameter definition for a [CustomFilterSlot].
///
/// Parameter `0` is always reserved for `Wet` (`0.0..1.0`, default `1.0`),
/// which controls the hardcoded 3-branch dry/wet blending and hard bypass
/// (`wet == 0.0`) inside the C++ audio thread. User-defined parameters occupy
/// indices `1..15` ([CustomFilterSlot.maxUserParams]).
@experimental
@immutable
class CustomFilterParamSpec {
  /// Creates a custom filter parameter specification.
  @experimental
  const CustomFilterParamSpec({
    required this.name,
    required this.min,
    required this.max,
    required this.defaultValue,
  }) : assert(min <= max, 'min must be <= max'),
       assert(
         defaultValue >= min && defaultValue <= max,
         'defaultValue must be within [min, max]',
       );

  /// Display name of the parameter.
  final String name;

  /// Minimum allowed value.
  final double min;

  /// Maximum allowed value.
  final double max;

  /// Default initial value.
  final double defaultValue;
}

/// A live parameter handle for a [CustomFilterSlot].
///
/// Supports reading/writing [value], [fadeFilterParameter], and
/// [oscillateFilterParameter] on the specific [slotIndex] (`0..7`).
@experimental
class CustomFilterParam extends FilterParam {
  /// Creates a parameter handle for custom filter slot [slotIndex] and
  /// parameter [attributeId] (`0..15`).
  @experimental
  CustomFilterParam(
    this.soundHandle,
    this.busId,
    this.slotIndex,
    int attributeId,
    double min,
    double max,
  ) : assert(
        slotIndex >= 0 && slotIndex < CustomFilterSlot.maxSlots,
        'slotIndex must be in 0..7',
      ),
      assert(
        attributeId >= 0 && attributeId < CustomFilterSlot.maxParams,
        'attributeId must be in 0..15',
      ),
      super(soundHandle, busId, FilterType.customFilter, attributeId, min, max);

  /// A parameter of a custom filter slot of a sound or of a bus: it throws
  /// [ArgumentError] when used without a voice handle or a bus (see
  /// [FilterParam.ofSource]).
  ///
  /// Used by [CustomFilterSingle], not by app code.
  @internal
  CustomFilterParam.ofSource(
    this.soundHandle,
    this.busId,
    this.slotIndex,
    int attributeId,
    double min,
    double max,
  ) : assert(
        slotIndex >= 0 && slotIndex < CustomFilterSlot.maxSlots,
        'slotIndex must be in 0..7',
      ),
      assert(
        attributeId >= 0 && attributeId < CustomFilterSlot.maxParams,
        'attributeId must be in 0..15',
      ),
      super.ofSource(
        soundHandle,
        busId,
        FilterType.customFilter,
        attributeId,
        min,
        max,
      );

  /// Optional voice handle when targeting a single-sound filter instance.
  final SoundHandle? soundHandle;

  /// Optional mixing bus ID when targeting a bus filter instance.
  final int? busId;

  /// The custom filter slot index (`0..7`).
  final int slotIndex;

  @override
  double get value {
    checkTarget();
    if (_kIsWeb && soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final ret = SoLoudController().soLoudFFI.getCustomFilterParams(
      slotIndex,
      attributeId,
      handle: soundHandle,
      busId: busId,
    );
    if (ret.error != PlayerErrors.noError) {
      Logger(
        'flutter_soloud.CustomFilter[$slotIndex]',
      ).severe(() => 'get value: ${ret.error}');
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.value;
  }

  @override
  set value(double val) {
    checkTarget();
    if (_kIsWeb && soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    if (val.isNaN || val < min || val > max) {
      Logger(
        'flutter_soloud.CustomFilter[$slotIndex]',
      ).warning(() => 'value [$val] out of accepted range [$min, $max]');
      return;
    }
    final error = SoLoudController().soLoudFFI.setCustomFilterParams(
      slotIndex,
      attributeId,
      val,
      handle: soundHandle,
      busId: busId,
    );
    if (error != PlayerErrors.noError) {
      Logger(
        'flutter_soloud.CustomFilter[$slotIndex]',
      ).severe(() => 'set value: $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  @override
  void fadeFilterParameter({required double to, required Duration time}) {
    checkTarget();
    if (_kIsWeb && soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    if (!SoLoud.instance.isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    checkFinite(to, 'to');
    final error = SoLoudController().soLoudFFI.fadeCustomFilterParameter(
      slotIndex,
      attributeId,
      to,
      time.toDouble(),
      handle: soundHandle,
      busId: busId,
    );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(() => 'fadeCustomFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  @override
  void oscillateFilterParameter({
    required double from,
    required double to,
    required Duration time,
  }) {
    checkTarget();
    if (_kIsWeb && soundHandle != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    if (!SoLoud.instance.isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    checkFinite(from, 'from');
    checkFinite(to, 'to');
    final error = SoLoudController().soLoudFFI.oscillateCustomFilterParameter(
      slotIndex,
      attributeId,
      from,
      to,
      time.toDouble(),
      handle: soundHandle,
      busId: busId,
    );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(() => 'oscillateCustomFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }
}

/// Base class for a user-programmable real-time DSP filter slot (`0..7`).
///
/// Up to [maxSlots] (8) independent custom filters can be chained on the same
/// scope (Global, Mixing Bus, or Single Sound).
///
/// Parameter `0` is always `Wet` (`0.0..1.0`). The C++ filter instance
/// automatically performs a hard bypass (skipping the DSP callback completely)
/// when `wet <= 0.0`, passes the buffer in-place with zero copies when
/// `wet >= 1.0`, and blends the dry/wet signals when `0.0 < wet < 1.0`.
@experimental
abstract class CustomFilterSlot extends FilterBase {
  /// Creates a custom filter slot wrapper for [slotIndex] (`0..7`).
  @experimental
  const CustomFilterSlot(this.slotIndex, SoundHash? soundHash, int? busId)
    : assert(
        slotIndex >= 0 && slotIndex < maxSlots,
        'slotIndex must be in 0..7',
      ),
      super(FilterType.customFilter, soundHash, busId);

  /// Maximum number of custom filter slots per scope (`FILTERS_PER_STREAM`).
  static const int maxSlots = 8;

  /// Total number of parameters per custom filter (`Wet` + 15 user
  /// parameters), the same as `CustomFilter::kMaxParams` in the native code.
  static const int maxParams = 16;

  /// Maximum number of user-defined parameters (`param[1]..param[15]`).
  static const int maxUserParams = 15;

  /// The slot index (`0..7`) on the target filter chain.
  final int slotIndex;

  @override
  void activate() {
    if (_kIsWeb && soundHash != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final error = SoLoudController().soLoudFFI.addCustomFilter(
      slotIndex,
      soundHash: soundHash ?? const SoundHash.invalid(),
      busId: busId,
    );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(
        () => 'CustomFilter[$slotIndex] activate() error: $error',
      );
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Removes this slot from its filter chain.
  ///
  /// The slot's parameter definitions are kept, but its native/JS callback is
  /// cleared: after a later [activate], call [setNativeCallback] again.
  @override
  void deactivate() {
    if (_kIsWeb && soundHash != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final error = SoLoudController().soLoudFFI.removeCustomFilter(
      slotIndex,
      soundHash: soundHash ?? const SoundHash.invalid(),
      busId: busId ?? 0,
    );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(
        () => 'CustomFilter[$slotIndex] deactivate() error: $error',
      );
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  @override
  int get index => SoLoudController().soLoudFFI
      .isCustomFilterActive(slotIndex, soundHash: soundHash, busId: busId)
      .index;

  /// Binds a native C/C++/Rust DSP callback address (or a Web JS function on
  /// `globalThis`) to this custom filter slot.
  ///
  /// The filter slot must be [activate]d before calling [setNativeCallback].
  ///
  /// - On native platforms (Android, iOS, macOS, Windows, Linux), pass the
  ///   native function pointer address as [callbackAddress] (having signature
  ///   `SoLoudFilterDspCallback_t`) and an optional native state pointer
  ///   address as [userdataAddress].
  /// - On Web, pass [webJsFunctionName] (the name of a function attached to
  ///   `globalThis`) or a Wasm table function index in [callbackAddress].
  ///   The JS function receives these 11 arguments, in this order:
  ///   `(bufferPtr, samples, bufferSize, channels, sampleRate, time,
  ///   paramsPtr, paramChangedMaskLo, paramChangedMaskHi, numParams,
  ///   userData)`:
  ///   - `bufferPtr`: byte offset in the WASM heap of the `float` buffer to
  ///     process in place (index `Module_soloud.HEAPF32` with
  ///     `bufferPtr >> 2`). The buffer is not interleaved: channel `ch`
  ///     starts at `ch * bufferSize` floats.
  ///   - `samples`: the number of samples to process for each channel.
  ///   - `bufferSize`: the stride in floats between two channels.
  ///   - `channels`: the number of channels.
  ///   - `sampleRate`: the sample rate in Hz.
  ///   - `time`: the stream time in seconds.
  ///   - `paramsPtr`: byte offset in the WASM heap of the `float` parameter
  ///     values (`paramsPtr >> 2` in `HEAPF32`); index `0` is `Wet`.
  ///   - `paramChangedMaskLo`, `paramChangedMaskHi`: the low and high 32 bits
  ///     of the mask of the parameters changed since the previous call (bit
  ///     `n` is parameter `n`).
  ///   - `numParams`: the number of values at `paramsPtr`.
  ///   - `userData`: the [userdataAddress] given here.
  ///   A [webJsFunctionName] works only with the single-threaded WASM build:
  ///   the multi-threaded build renders in an AudioWorklet, which does not
  ///   see the page's `globalThis`, so the filter has no effect there (a
  ///   warning is logged). Set `self.flutter_soloud_force_single_threaded =
  ///   true` before loading `init_soloud.js` to use the single-threaded build.
  @experimental
  void setNativeCallback({
    int callbackAddress = 0,
    int userdataAddress = 0,
    String? webJsFunctionName,
  }) {
    if (_kIsWeb && soundHash != null) {
      throw const SoLoudFilterForSingleSoundOnWebDartException();
    }
    final error = SoLoudController().soLoudFFI.setCustomFilterCallback(
      slotIndex,
      soundHash: soundHash ?? const SoundHash.invalid(),
      busId: busId,
      callbackAddress: callbackAddress,
      userdataAddress: userdataAddress,
      webJsFunctionName: webJsFunctionName,
    );
    if (error != PlayerErrors.noError) {
      Logger.root.severe(
        () => 'CustomFilter[$slotIndex] setNativeCallback() error: $error',
      );
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Configures up to [maxUserParams] (15) user parameter definitions
  /// (`1..15`) on this custom filter slot.
  ///
  /// Parameter `0` is always `Wet` (`0.0..1.0`, default `1.0`). The first item
  /// in [specs] configures parameter `1`, the second configures parameter `2`,
  /// and so on.
  @experimental
  void configureParameters(List<CustomFilterParamSpec> specs) {
    if (specs.length > maxUserParams) {
      throw ArgumentError.value(
        specs.length,
        'specs.length',
        'A CustomFilterSlot supports at most $maxUserParams user parameters '
            '(indices 1..15; index 0 is reserved for Wet).',
      );
    }
    for (var i = 0; i < specs.length; i++) {
      final spec = specs[i];
      final paramIdx = i + 1;
      final error = SoLoudController().soLoudFFI.setCustomFilterParamDefinition(
        slotIndex,
        paramIdx,
        spec.name,
        spec.min,
        spec.max,
        spec.defaultValue,
        soundHash: soundHash ?? const SoundHash.invalid(),
        busId: busId,
      );
      if (error != PlayerErrors.noError) {
        Logger.root.severe(
          () => 'CustomFilter[$slotIndex] configureParameters() error: $error',
        );
        throw SoLoudCppException.fromPlayerError(error);
      }
    }
  }
}

/// Custom filter slot bound to a single `AudioSource` or a mixing `Bus`.
@experimental
class CustomFilterSingle extends CustomFilterSlot {
  /// Creates a single-sound or bus custom filter slot (`0..7`).
  @experimental
  const CustomFilterSingle(super.slotIndex, super.soundHash, super.busId);

  /// Parameter `0`: Dry/wet mix (`0.0` = hard bypass, `1.0` = 100% wet).
  @experimental
  CustomFilterParam wet({SoundHandle? soundHandle}) =>
      CustomFilterParam.ofSource(soundHandle, busId, slotIndex, 0, 0, 1);

  /// Accesses parameter [paramIndex] (`0..15`) on this custom filter slot.
  ///
  /// Optional [min] and [max] bounds are used for client-side range validation
  /// when setting `.value`.
  @experimental
  CustomFilterParam param(
    int paramIndex, {
    SoundHandle? soundHandle,
    double min = -1e9,
    double max = 1e9,
  }) => CustomFilterParam.ofSource(
    soundHandle,
    busId,
    slotIndex,
    paramIndex,
    paramIndex == 0 ? 0 : min,
    paramIndex == 0 ? 1 : max,
  );
}

/// Custom filter slot bound to the global mixer.
@experimental
class CustomFilterGlobal extends CustomFilterSlot {
  /// Creates a global custom filter slot (`0..7`).
  @experimental
  const CustomFilterGlobal(int slotIndex) : super(slotIndex, null, null);

  /// Parameter `0`: Dry/wet mix (`0.0` = hard bypass, `1.0` = 100% wet).
  @experimental
  CustomFilterParam get wet =>
      CustomFilterParam(null, null, slotIndex, 0, 0, 1);

  /// Accesses parameter [paramIndex] (`0..15`) on this global custom filter
  /// slot.
  ///
  /// Optional [min] and [max] bounds are used for client-side range validation
  /// when setting `.value`.
  @experimental
  CustomFilterParam param(
    int paramIndex, {
    double min = -1e9,
    double max = 1e9,
  }) => CustomFilterParam(
    null,
    null,
    slotIndex,
    paramIndex,
    paramIndex == 0 ? 0 : min,
    paramIndex == 0 ? 1 : max,
  );
}

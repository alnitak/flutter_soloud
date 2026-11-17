import 'package:meta/meta.dart';

/// Metadata for a sub-plugin class discovered inside a `.vst3` bundle.
@experimental
@immutable
class Vst3ClassInfo {
  /// Creates a [Vst3ClassInfo] descriptor.
  const Vst3ClassInfo({
    required this.classUid,
    required this.name,
    required this.subCategories,
  });

  /// 32-character hex Steinberg `FUID` string.
  final String classUid;

  /// Human-readable plugin name (e.g. `"flutter_reverb"` or `"Surge XT"`).
  final String name;

  /// VST3 category string (e.g. `"Fx|EQ"`, `"Instrument|Synth"`).
  final String subCategories;

  /// Whether this class is a virtual instrument / synthesizer.
  bool get isInstrument => subCategories.contains('Instrument');

  /// Whether this class is an audio effect processor.
  bool get isEffect => subCategories.contains('Fx') || !isInstrument;

  @override
  String toString() =>
      'Vst3ClassInfo(name: $name, subCategories: $subCategories, '
      'classUid: $classUid)';
}

/// Metadata for an individual VST3 parameter discovered at runtime.
@experimental
@immutable
class Vst3ParamInfo {
  /// Creates a [Vst3ParamInfo] descriptor.
  const Vst3ParamInfo({
    required this.paramId,
    required this.title,
    required this.units,
    required this.defaultNormalized,
    required this.stepCount,
    required this.canAutomate,
    required this.isBypass,
  });

  /// The 32-bit Steinberg `ParamID` (may be arbitrary, not necessarily
  /// `0..N-1`).
  final int paramId;

  /// Parameter name (e.g. `"Cutoff"`, `"Mix"`, `"Threshold"`).
  final String title;

  /// Unit label (e.g. `"Hz"`, `"dB"`, `"%"`, `"ms"`).
  final String units;

  /// Default normalized value in `[0.0, 1.0]`.
  final double defaultNormalized;

  /// `0` for continuous parameters, `1` for boolean toggles, `> 1` for
  /// discrete steps.
  final int stepCount;

  /// Whether the parameter supports real-time host automation (`kCanAutomate`).
  final bool canAutomate;

  /// Whether this parameter is the plugin's primary bypass switch
  /// (`kIsBypass`).
  final bool isBypass;

  @override
  String toString() =>
      'Vst3ParamInfo(id: $paramId, title: $title, units: $units, '
      'default: $defaultNormalized, steps: $stepCount)';
}

/// Event emitted when the user adjusts a knob in the native VST3 editor GUI.
@experimental
@immutable
class Vst3ParamEditEvent {
  /// Creates a [Vst3ParamEditEvent].
  const Vst3ParamEditEvent({
    required this.pluginHandle,
    required this.paramId,
    required this.normalizedValue,
  });

  /// Internal native plugin handle.
  final int pluginHandle;

  /// The 32-bit Steinberg `ParamID`.
  final int paramId;

  /// New normalized parameter value in `[0.0, 1.0]`.
  final double normalizedValue;

  @override
  String toString() =>
      'Vst3ParamEditEvent(handle: $pluginHandle, param: $paramId, '
      'val: $normalizedValue)';
}

/// Event emitted when a VST3 plugin calls `IComponentHandler::restartComponent`
/// to notify the host that parameters, titles, latency, or buses have changed.
@experimental
@immutable
class Vst3RestartComponentEvent {
  /// Creates a [Vst3RestartComponentEvent].
  const Vst3RestartComponentEvent({
    required this.pluginHandle,
    required this.flags,
  });

  /// Internal native plugin handle.
  final int pluginHandle;

  /// Bitmask of Steinberg `RestartFlags`.
  final int flags;

  /// Whether parameter titles or parameter count changed (`kParamTitlesChanged`
  /// or `kReloadComponent`).
  bool get hasParamTitlesChanged =>
      (flags & (1 << 4)) != 0 || (flags & (1 << 0)) != 0;

  /// Whether parameter values changed (`kParamValuesChanged`).
  bool get hasParamValuesChanged => (flags & (1 << 2)) != 0;

  /// Whether latency changed (`kLatencyChanged`).
  bool get hasLatencyChanged => (flags & (1 << 3)) != 0;

  /// Whether I/O bus configuration changed (`kIoChanged`).
  bool get hasIoChanged => (flags & (1 << 1)) != 0;

  @override
  String toString() =>
      'Vst3RestartComponentEvent(pluginHandle: $pluginHandle, flags: $flags)';
}

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/custom_filter.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/vst3/vst3_types.dart';
import 'package:meta/meta.dart';

export 'package:flutter_soloud/src/vst3/vst3_types.dart';

/// Web stub for [Vst3Host] (VST3 plugin hosting is only supported on native
/// desktop platforms: macOS, Windows, and Linux).
@experimental
class Vst3Host {
  /// Creates a [Vst3Host] instance.
  @experimental
  Vst3Host();

  /// Always `false` on the Web platform.
  @experimental
  bool get isSupported => false;

  /// Always empty on the Web platform.
  @experimental
  List<String> get standardSearchPaths => const [];

  /// Always returns an empty list on the Web platform.
  @experimental
  List<Vst3ClassInfo> scanBundle(String bundlePath) => const [];

  /// Throws [SoLoudInvalidParameterException] on the Web platform.
  @experimental
  Vst3EffectPlugin loadEffect({
    required String bundlePath,
    required CustomFilterSlot slot,
    String? classUid,
    int sampleRate = 48000,
    int maxBlockSize = 2048,
    Channels channels = Channels.stereo,
  }) {
    throw SoLoudCppException.fromPlayerError(PlayerErrors.invalidParameter);
  }

  /// Throws [SoLoudInvalidParameterException] on the Web platform.
  @experimental
  Vst3InstrumentPlugin loadInstrument({
    required String bundlePath,
    String? classUid,
    int sampleRate = 48000,
    int maxBlockSize = 2048,
    Channels channels = Channels.stereo,
  }) {
    throw SoLoudCppException.fromPlayerError(PlayerErrors.invalidParameter);
  }

  /// No-op on the Web platform.
  @experimental
  void unloadAll() {}
}

/// Base class for a loaded VST3 plugin instance.
@experimental
abstract class Vst3PluginBase {
  /// Internal native handle.
  int get pluginHandle => 0;

  /// Whether this plugin instance has been disposed.
  bool get isDisposed => true;

  /// Lookahead latency in samples.
  int get latencySamples => 0;

  /// All parameters discovered via `IEditController::getParameterInfo`.
  List<Vst3ParamInfo> get parameters => const [];

  /// Re-queries parameter count and parameter metadata.
  List<Vst3ParamInfo> refreshParameters() => const [];

  /// Stream of restart events emitted by this plugin instance.
  Stream<Vst3RestartComponentEvent> get onRestartComponent =>
      const Stream.empty();

  /// Reads the current normalized `[0.0, 1.0]` value of [paramId].
  double getParameterNormalized(int paramId) => 0;

  /// Returns the human-readable formatted value string.
  String getParameterDisplay(int paramId) => '';

  /// Sets the normalized `[0.0, 1.0]` value of [paramId].
  void setParameterNormalized(int paramId, double value) {}

  /// Configures the musical transport (`ProcessContext`) sent to the plugin.
  void setTransport({
    required double bpm,
    int timeSigNumerator = 4,
    int timeSigDenominator = 4,
    bool isPlaying = true,
  }) {}

  /// Exports the binary state of the processor and controller.
  Uint8List saveState() => Uint8List(0);

  /// Restores a previously saved binary state.
  void restoreState(Uint8List stateBytes) {}

  /// Whether this plugin provides a custom native GUI editor.
  bool get hasEditor => false;

  /// Whether the native GUI editor window is currently open.
  bool get isEditorOpen => false;

  /// Opens the plugin's native GUI in a standalone floating OS window.
  bool openEditorWindow({String? title}) => false;

  /// Closes the native GUI editor window if open.
  void closeEditorWindow() {}

  /// Stream of parameter changes made by the user inside the native GUI window.
  Stream<Vst3ParamEditEvent> get onGuiParameterChanged => const Stream.empty();

  /// Disposes the plugin instance.
  void dispose() {}
}

/// Web stub for [Vst3EffectPlugin].
@experimental
class Vst3EffectPlugin extends Vst3PluginBase {
  /// Creates a stub [Vst3EffectPlugin].
  @experimental
  Vst3EffectPlugin(this.slot);

  /// The [CustomFilterSlot] hosting this VST3 effect.
  final CustomFilterSlot slot;

  /// Maps a [CustomFilterSlot] parameter index (`1..7`) to a VST3
  /// [vst3ParamId].
  @experimental
  CustomFilterParam mapToSlotParameter({
    required int slotParamIndex,
    required int vst3ParamId,
    SoundHandle? soundHandle,
  }) => CustomFilterParam(
    soundHandle,
    slot.busId,
    slot.slotIndex,
    slotParamIndex,
    0,
    1,
  );
}

/// Web stub for [Vst3InstrumentPlugin].
@experimental
class Vst3InstrumentPlugin extends Vst3PluginBase {
  /// Creates a stub [Vst3InstrumentPlugin].
  @experimental
  Vst3InstrumentPlugin(this.audioSource);

  /// The playable SoLoud [AudioSource].
  final AudioSource audioSource;

  /// Sends a real-time MIDI Note-On event to the VST3 instrument.
  @experimental
  void noteOn({
    required int pitch,
    double velocity = 0.8,
    int channel = 0,
    int sampleOffset = 0,
  }) {}

  /// Sends a real-time MIDI Note-Off event to the VST3 instrument.
  @experimental
  void noteOff({
    required int pitch,
    double velocity = 0.0,
    int channel = 0,
    int sampleOffset = 0,
  }) {}
}

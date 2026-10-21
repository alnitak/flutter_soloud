import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/bindings/flutter_soloud_ffigen.dart'
    as ffigen;
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/custom_filter.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/vst3/vst3_types.dart';
import 'package:meta/meta.dart';

export 'package:flutter_soloud/src/vst3/vst3_types.dart';

final StreamController<Vst3ParamEditEvent> _guiEditController =
    StreamController<Vst3ParamEditEvent>.broadcast();

ffi.NativeCallable<ffigen.dartVst3ParameterEditCallback_tFunction>?
_guiEditNativeCallable;

void _ensureGuiEditCallbackRegistered() {
  if (_guiEditNativeCallable != null) return;
  _guiEditNativeCallable =
      ffi.NativeCallable<
        ffigen.dartVst3ParameterEditCallback_tFunction
      >.listener((int pluginHandle, int paramId, double normalizedValue) {
        if (!_guiEditController.isClosed) {
          _guiEditController.add(
            Vst3ParamEditEvent(
              pluginHandle: pluginHandle,
              paramId: paramId,
              normalizedValue: normalizedValue,
            ),
          );
        }
      });
  ffigen.soloud_vst3SetParameterEditCallback(
    _guiEditNativeCallable!.nativeFunction,
  );
}

final StreamController<Vst3RestartComponentEvent> _restartComponentController =
    StreamController<Vst3RestartComponentEvent>.broadcast();

ffi.NativeCallable<ffigen.dartVst3RestartComponentCallback_tFunction>?
_restartComponentNativeCallable;

void _ensureRestartComponentCallbackRegistered() {
  if (_restartComponentNativeCallable != null) return;
  _restartComponentNativeCallable =
      ffi.NativeCallable<
        ffigen.dartVst3RestartComponentCallback_tFunction
      >.listener((int pluginHandle, int flags) {
        if (!_restartComponentController.isClosed) {
          _restartComponentController.add(
            Vst3RestartComponentEvent(
              pluginHandle: pluginHandle,
              flags: flags,
            ),
          );
        }
      });
  ffigen.soloud_vst3SetRestartComponentCallback(
    _restartComponentNativeCallable!.nativeFunction,
  );
}

/// Manages scanning and instantiating native VST3 effects and instruments on
/// desktop platforms (`macOS`, `Windows`, `Linux`).
///
/// Requires `enable_vst3: true` under `hooks.user_defines.flutter_soloud` in
/// the application's `pubspec.yaml`.
@experimental
class Vst3Host {
  /// Creates a [Vst3Host] instance.
  @experimental
  Vst3Host();

  /// Whether VST3 hosting is compiled and supported on the current platform.
  @experimental
  bool get isSupported => ffigen.soloud_vst3IsSupported() != 0;

  /// Standard OS directories where `.vst3` bundles are installed:
  /// - **macOS**: `/Library/Audio/Plug-Ins/VST3`,
  ///   `~/Library/Audio/Plug-Ins/VST3`
  /// - **Windows**: `%LOCALAPPDATA%\Programs\Common\VST3`
  /// - **Linux**: `/usr/lib/vst3`, `/usr/local/lib/vst3`, `~/.vst3`
  @experimental
  List<String> get standardSearchPaths {
    if (Platform.isMacOS) {
      final home = Platform.environment['HOME'];
      return [
        '/Library/Audio/Plug-Ins/VST3',
        if (home != null && home.isNotEmpty)
          '$home/Library/Audio/Plug-Ins/VST3',
      ];
    } else if (Platform.isWindows) {
      final localAppData = Platform.environment['LOCALAPPDATA'];
      final userProfile = Platform.environment['USERPROFILE'];
      final base = (localAppData != null && localAppData.isNotEmpty)
          ? localAppData
          : (userProfile != null && userProfile.isNotEmpty)
              ? '$userProfile\\AppData\\Local'
              : r'C:\Users\Default\AppData\Local';
      return ['$base\\Programs\\Common\\VST3'];
    } else if (Platform.isLinux) {
      final home = Platform.environment['HOME'];
      return [
        '/usr/lib/vst3',
        '/usr/local/lib/vst3',
        if (home != null && home.isNotEmpty) '$home/.vst3',
      ];
    }
    return const [];
  }

  /// Scans a `.vst3` bundle and returns all audio plugin classes inside it.
  @experimental
  List<Vst3ClassInfo> scanBundle(String bundlePath) {
    if (!isSupported) return const [];
    final pathPtr = bundlePath.toNativeUtf8().cast<ffi.Char>();
    final outCount = calloc<ffi.Int>();
    final outUids = calloc<ffi.Pointer<ffi.Pointer<ffi.Char>>>();
    final outNames = calloc<ffi.Pointer<ffi.Pointer<ffi.Char>>>();
    final outSubCats = calloc<ffi.Pointer<ffi.Pointer<ffi.Char>>>();

    try {
      final errCode = ffigen.soloud_vst3ScanBundle(
        pathPtr,
        outCount,
        outUids,
        outNames,
        outSubCats,
      );
      final err = PlayerErrors.values[errCode.value];
      if (err != PlayerErrors.noError) {
        throw SoLoudCppException.fromPlayerError(err);
      }

      final count = outCount.value;
      final uidsPtr = outUids.value;
      final namesPtr = outNames.value;
      final subCatsPtr = outSubCats.value;

      final results = <Vst3ClassInfo>[];
      try {
        for (var i = 0; i < count; i++) {
          final uid = (uidsPtr + i).value.cast<Utf8>().toDartString();
          final name = (namesPtr + i).value.cast<Utf8>().toDartString();
          final subCat = (subCatsPtr + i).value.cast<Utf8>().toDartString();
          results.add(
            Vst3ClassInfo(classUid: uid, name: name, subCategories: subCat),
          );
        }
      } finally {
        ffigen.soloud_vst3FreeScanResults(count, uidsPtr, namesPtr, subCatsPtr);
      }
      return results;
    } finally {
      calloc
        ..free(pathPtr)
        ..free(outCount)
        ..free(outUids)
        ..free(outNames)
        ..free(outSubCats);
    }
  }

  /// Unloads all active VST3 plugin instances and cached modules.
  ///
  /// Useful during teardown or before application exit to ensure clean release
  /// of all native plugin libraries and resources.
  @experimental
  void unloadAll() {
    if (!isSupported) return;
    ffigen.soloud_vst3HostUnloadAll();
  }

  /// Loads a VST3 **Audio Effect (`Fx`)** and attaches it to [slot]
  /// (`CustomFilterSlot` `0..7` on Global or Bus filters).
  @experimental
  Vst3EffectPlugin loadEffect({
    required String bundlePath,
    required CustomFilterSlot slot,
    String? classUid,
    int sampleRate = 48000,
    int maxBlockSize = 2048,
    Channels channels = Channels.stereo,
  }) {
    if (!isSupported) {
      throw SoLoudCppException.fromPlayerError(PlayerErrors.invalidParameter);
    }
    _ensureGuiEditCallbackRegistered();

    final handle = _loadNativePlugin(
      bundlePath: bundlePath,
      classUid: classUid,
      sampleRate: sampleRate,
      maxBlockSize: maxBlockSize,
      channels: channels.count,
    );

    try {
      if (slot.index < 0) {
        slot.activate();
      }
      final cbPtr = ffigen.soloud_vst3GetFilterCallbackPtr();
      final userDataPtr = ffigen.soloud_vst3GetPluginUserData(handle);
      slot.setNativeCallback(
        callbackAddress: cbPtr.address,
        userdataAddress: userDataPtr.address,
      );
      return Vst3EffectPlugin._(pluginHandle: handle, slot: slot);
    } catch (_) {
      ffigen.soloud_vst3UnloadPlugin(handle);
      rethrow;
    }
  }

  /// Loads a VST3 **Virtual Instrument (`Instrument|Synth`)** and wraps it in
  /// a playable SoLoud [AudioSource] via [SoLoud.createCustomAudioSource].
  @experimental
  Vst3InstrumentPlugin loadInstrument({
    required String bundlePath,
    String? classUid,
    int sampleRate = 48000,
    int maxBlockSize = 2048,
    Channels channels = Channels.stereo,
  }) {
    if (!isSupported) {
      throw SoLoudCppException.fromPlayerError(PlayerErrors.invalidParameter);
    }
    _ensureGuiEditCallbackRegistered();

    final handle = _loadNativePlugin(
      bundlePath: bundlePath,
      classUid: classUid,
      sampleRate: sampleRate,
      maxBlockSize: maxBlockSize,
      channels: channels.count,
    );

    try {
      final cbPtr = ffigen.soloud_vst3GetSourceCallbackPtr();
      final userDataPtr = ffigen.soloud_vst3GetPluginUserData(handle);
      final source = SoLoud.instance.createCustomAudioSource(
        sampleRate: sampleRate,
        channels: channels,
        dspCallbackAddress: cbPtr.address,
        userdataAddress: userDataPtr.address,
      );
      return Vst3InstrumentPlugin._(pluginHandle: handle, audioSource: source);
    } catch (_) {
      ffigen.soloud_vst3UnloadPlugin(handle);
      rethrow;
    }
  }

  int _loadNativePlugin({
    required String bundlePath,
    required String? classUid,
    required int sampleRate,
    required int maxBlockSize,
    required int channels,
  }) {
    final pathPtr = bundlePath.toNativeUtf8().cast<ffi.Char>();
    final uidPtr = (classUid != null && classUid.isNotEmpty)
        ? classUid.toNativeUtf8().cast<ffi.Char>()
        : ffi.nullptr.cast<ffi.Char>();
    final outHandle = calloc<ffi.Int>();

    try {
      final errCode = ffigen.soloud_vst3LoadPlugin(
        pathPtr,
        uidPtr,
        sampleRate,
        maxBlockSize,
        channels,
        outHandle,
      );
      final err = PlayerErrors.values[errCode.value];
      if (err != PlayerErrors.noError || outHandle.value <= 0) {
        throw SoLoudCppException.fromPlayerError(
          err == PlayerErrors.noError ? PlayerErrors.fileLoadFailed : err,
        );
      }
      return outHandle.value;
    } finally {
      calloc.free(pathPtr);
      if (uidPtr != ffi.nullptr) {
        calloc.free(uidPtr);
      }
      calloc.free(outHandle);
    }
  }
}

/// Base class for a loaded VST3 plugin instance (shared by effects & synths).
@experimental
abstract class Vst3PluginBase {
  Vst3PluginBase._(this.pluginHandle)
    : _parameters = _queryParameters(pluginHandle) {
    _ensureRestartComponentCallbackRegistered();
    _restartSub = onRestartComponent.listen((evt) {
      if (evt.hasParamTitlesChanged || evt.hasParamValuesChanged) {
        refreshParameters();
      }
    });
  }

  /// Internal native handle.
  final int pluginHandle;

  List<Vst3ParamInfo> _parameters;
  StreamSubscription<Vst3RestartComponentEvent>? _restartSub;
  bool? _hasEditorCached;
  bool _disposed = false;

  /// Whether this plugin instance has been disposed.
  bool get isDisposed => _disposed;

  /// Lookahead latency in samples reported by
  /// `IAudioProcessor::getLatencySamples()`.
  int get latencySamples =>
      _disposed ? 0 : ffigen.soloud_vst3GetLatencySamples(pluginHandle);

  /// All parameters discovered via `IEditController::getParameterInfo`.
  List<Vst3ParamInfo> get parameters => _parameters;

  /// Re-queries parameter count and parameter metadata from the VST3
  /// edit controller and updates [parameters].
  List<Vst3ParamInfo> refreshParameters() {
    if (_disposed) return const [];
    return _parameters = _queryParameters(pluginHandle);
  }

  /// Stream of restart events emitted by this plugin instance when
  /// `IComponentHandler::restartComponent` is called.
  Stream<Vst3RestartComponentEvent> get onRestartComponent =>
      _restartComponentController.stream.where(
        (e) => e.pluginHandle == pluginHandle,
      );

  static List<Vst3ParamInfo> _queryParameters(int handle) {
    final count = ffigen.soloud_vst3GetParameterCount(handle);
    if (count <= 0) return const [];

    const bufSize = 256;
    final outId = calloc<ffi.UnsignedInt>();
    final outTitle = calloc<ffi.Char>(bufSize);
    final outUnits = calloc<ffi.Char>(bufSize);
    final outDefault = calloc<ffi.Double>();
    final outSteps = calloc<ffi.Int>();
    final outFlags = calloc<ffi.UnsignedInt>();

    final list = <Vst3ParamInfo>[];
    try {
      for (var i = 0; i < count; i++) {
        final res = ffigen.soloud_vst3GetParameterInfo(
          handle,
          i,
          outId,
          outTitle,
          bufSize,
          outUnits,
          bufSize,
          outDefault,
          outSteps,
          outFlags,
        );
        if (PlayerErrors.values[res.value] == PlayerErrors.noError) {
          final flags = outFlags.value;
          list.add(
            Vst3ParamInfo(
              paramId: outId.value,
              title: outTitle.cast<Utf8>().toDartString(),
              units: outUnits.cast<Utf8>().toDartString(),
              defaultNormalized: outDefault.value,
              stepCount: outSteps.value,
              canAutomate: (flags & 1) != 0, // kCanAutomate = 1 << 0
              isBypass: (flags & (1 << 16)) != 0, // kIsBypass = 1 << 16
            ),
          );
        }
      }
      return List.unmodifiable(list);
    } finally {
      calloc
        ..free(outId)
        ..free(outTitle)
        ..free(outUnits)
        ..free(outDefault)
        ..free(outSteps)
        ..free(outFlags);
    }
  }

  /// Reads the current normalized `[0.0, 1.0]` value of [paramId].
  double getParameterNormalized(int paramId) {
    if (_disposed) return 0;
    return ffigen.soloud_vst3GetParameterNormalized(
      pluginHandle,
      paramId,
      ffi.nullptr,
      0,
    );
  }

  /// Returns the human-readable formatted value string (e.g. `"-4.2 dB"`).
  String getParameterDisplay(int paramId) {
    if (_disposed) return '';
    const bufSize = 256;
    final buf = calloc<ffi.Char>(bufSize);
    try {
      ffigen.soloud_vst3GetParameterNormalized(
        pluginHandle,
        paramId,
        buf,
        bufSize,
      );
      return buf.cast<Utf8>().toDartString();
    } finally {
      calloc.free(buf);
    }
  }

  /// Sets the normalized `[0.0, 1.0]` value of [paramId] in a real-time
  /// thread-safe manner (and updates the native GUI window if open).
  void setParameterNormalized(int paramId, double value) {
    if (_disposed) return;
    final errCode = ffigen.soloud_vst3SetParameterNormalized(
      pluginHandle,
      paramId,
      value.clamp(0.0, 1.0),
    );
    final err = PlayerErrors.values[errCode.value];
    if (err != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(err);
    }
  }

  /// Configures the musical transport (`ProcessContext`) sent to the plugin.
  void setTransport({
    required double bpm,
    int timeSigNumerator = 4,
    int timeSigDenominator = 4,
    bool isPlaying = true,
  }) {
    if (_disposed) return;
    ffigen.soloud_vst3SetTransport(
      pluginHandle,
      bpm,
      timeSigNumerator,
      timeSigDenominator,
      isPlaying ? 1 : 0,
    );
  }

  /// Exports the binary state of the processor and controller.
  Uint8List saveState() {
    if (_disposed) return Uint8List(0);
    final outData = calloc<ffi.Pointer<ffi.UnsignedChar>>();
    final outSize = calloc<ffi.UnsignedInt>();
    try {
      final errCode = ffigen.soloud_vst3GetState(
        pluginHandle,
        outData,
        outSize,
      );
      final err = PlayerErrors.values[errCode.value];
      if (err != PlayerErrors.noError) {
        throw SoLoudCppException.fromPlayerError(err);
      }
      final size = outSize.value;
      final ptr = outData.value;
      if (size == 0 || ptr == ffi.nullptr) {
        return Uint8List(0);
      }
      try {
        return Uint8List.fromList(ptr.cast<ffi.Uint8>().asTypedList(size));
      } finally {
        ffigen.nativeFree(ptr.cast());
      }
    } finally {
      calloc
        ..free(outData)
        ..free(outSize);
    }
  }

  /// Restores a previously saved binary state.
  void restoreState(Uint8List stateBytes) {
    if (_disposed || stateBytes.isEmpty) return;
    final buf = calloc<ffi.UnsignedChar>(stateBytes.length);
    try {
      buf
          .cast<ffi.Uint8>()
          .asTypedList(stateBytes.length)
          .setAll(0, stateBytes);
      final errCode = ffigen.soloud_vst3SetState(
        pluginHandle,
        buf,
        stateBytes.length,
      );
      final err = PlayerErrors.values[errCode.value];
      if (err != PlayerErrors.noError) {
        throw SoLoudCppException.fromPlayerError(err);
      }
    } finally {
      calloc.free(buf);
    }
  }

  // --- Native Desktop GUI Editor Window ---

  /// Whether this plugin provides a custom native GUI editor (`IPlugView`).
  bool get hasEditor =>
      !_disposed &&
      (_hasEditorCached ??= ffigen.soloud_vst3HasEditor(pluginHandle) != 0);

  /// Whether the native GUI editor window is currently open.
  bool get isEditorOpen =>
      !_disposed && ffigen.soloud_vst3IsEditorOpen(pluginHandle) != 0;

  /// Opens the plugin's native GUI in a standalone floating OS window
  /// (`NSWindow` on macOS, `HWND` on Windows, `X11 Window` on Linux).
  bool openEditorWindow({String? title}) {
    if (_disposed) return false;
    final titlePtr = title != null
        ? title.toNativeUtf8().cast<ffi.Char>()
        : ffi.nullptr.cast<ffi.Char>();
    try {
      final errCode = ffigen.soloud_vst3OpenEditor(pluginHandle, titlePtr);
      return PlayerErrors.values[errCode.value] == PlayerErrors.noError;
    } finally {
      if (titlePtr != ffi.nullptr) {
        calloc.free(titlePtr);
      }
    }
  }

  /// Closes the native GUI editor window if open.
  void closeEditorWindow() {
    if (_disposed) return;
    ffigen.soloud_vst3CloseEditor(pluginHandle);
  }

  /// Stream of parameter changes made by the user inside the native GUI window.
  Stream<Vst3ParamEditEvent> get onGuiParameterChanged => _guiEditController
      .stream
      .where((evt) => evt.pluginHandle == pluginHandle);

  /// Closes any open editor window, detaches from SoLoud, and unloads the
  /// `.vst3` module.
  @mustCallSuper
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _restartSub?.cancel();
    _restartSub = null;
    ffigen.soloud_vst3CloseEditor(pluginHandle);
    ffigen.soloud_vst3UnloadPlugin(pluginHandle);
  }
}

/// A loaded VST3 Audio Effect bound to a [CustomFilterSlot] (`0..7`).
@experimental
class Vst3EffectPlugin extends Vst3PluginBase {
  Vst3EffectPlugin._({required int pluginHandle, required this.slot})
    : super._(pluginHandle);

  /// The [CustomFilterSlot] (`0..7`) hosting this VST3 effect.
  final CustomFilterSlot slot;

  /// Maps a [CustomFilterSlot] parameter index (`1..7`) to a VST3 [vst3ParamId]
  /// so you can call `.fadeFilterParameter()` and `.oscillateFilterParameter()`
  /// on the returned [CustomFilterParam].
  @experimental
  CustomFilterParam mapToSlotParameter({
    required int slotParamIndex,
    required int vst3ParamId,
    SoundHandle? soundHandle,
  }) {
    if (slotParamIndex < 1 || slotParamIndex > CustomFilterSlot.maxUserParams) {
      throw ArgumentError.value(
        slotParamIndex,
        'slotParamIndex',
        'slotParamIndex must be between 1 and '
            '${CustomFilterSlot.maxUserParams} (index 0 is reserved for Wet).',
      );
    }
    final errCode = ffigen.soloud_vst3MapSlotParameter(
      pluginHandle,
      slotParamIndex,
      vst3ParamId,
    );
    final err = PlayerErrors.values[errCode.value];
    if (err != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(err);
    }
    return CustomFilterParam(
      soundHandle,
      slot.busId,
      slot.slotIndex,
      slotParamIndex,
      0,
      1,
    );
  }

  @override
  void dispose() {
    if (isDisposed) return;
    try {
      if (SoLoud.instance.isInitialized && slot.index >= 0) {
        slot
          ..setNativeCallback()
          ..deactivate();
      }
    } catch (_) {}
    super.dispose();
  }
}

/// A loaded VST3 Virtual Instrument wrapped in a playable SoLoud [AudioSource].
@experimental
class Vst3InstrumentPlugin extends Vst3PluginBase {
  Vst3InstrumentPlugin._({required int pluginHandle, required this.audioSource})
    : super._(pluginHandle);

  /// The playable SoLoud [AudioSource] created via `createCustomAudioSource()`.
  /// Pass this to `SoLoud.instance.play(instrument.audioSource)` or `play3d()`.
  final AudioSource audioSource;

  /// Sends a real-time MIDI Note-On event to the VST3 instrument.
  @experimental
  void noteOn({
    required int pitch,
    double velocity = 0.8,
    int channel = 0,
    int sampleOffset = 0,
  }) {
    if (isDisposed) return;
    final errCode = ffigen.soloud_vst3SendMidiNote(
      pluginHandle,
      1,
      channel,
      pitch,
      velocity,
      sampleOffset,
    );
    final err = PlayerErrors.values[errCode.value];
    if (err != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(err);
    }
  }

  /// Sends a real-time MIDI Note-Off event to the VST3 instrument.
  @experimental
  void noteOff({
    required int pitch,
    double velocity = 0.0,
    int channel = 0,
    int sampleOffset = 0,
  }) {
    if (isDisposed) return;
    final errCode = ffigen.soloud_vst3SendMidiNote(
      pluginHandle,
      0,
      channel,
      pitch,
      velocity,
      sampleOffset,
    );
    final err = PlayerErrors.values[errCode.value];
    if (err != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(err);
    }
  }

  @override
  void dispose() {
    if (isDisposed) return;
    try {
      if (SoLoud.instance.isInitialized) {
        unawaited(SoLoud.instance.disposeSource(audioSource));
      }
    } catch (_) {}
    super.dispose();
  }
}

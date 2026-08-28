// Hand-written wrapper around the ffigen-generated @Native bindings in
// `package:flutter_soloud/src/bindings/flutter_soloud_ffigen.dart` (generated
// from `src/bindings.h` — regenerate with
// `dart run ffigen --config ffigen.yaml`).
// ignore_for_file: avoid_positional_boolean_parameters,require_trailing_commas
// ignore_for_file: omit_local_variable_types,public_member_api_docs

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:ffi/ffi.dart';
import 'package:flutter_soloud/src/audio_visualization_data.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/darwin_engine_lifecycle.dart';
import 'package:flutter_soloud/src/bindings/flutter_soloud_ffigen.dart'
    as native;
import 'package:flutter_soloud/src/bindings/native_metadata_ffi.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/helpers/playback_device.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

typedef OnMetadataCallbackTFunction = void Function(NativeAudioMetadata);

typedef OnAudioDurationCallbackTFunction = void Function(double duration);

typedef OnMoreDataIsNeededCallbackTFunction = void Function(int offset);

final class _BufferStreamNativeCallbacks {
  _BufferStreamNativeCallbacks({
    this.onBuffering,
    this.onMetadata,
    this.onMoreDataIsNeeded,
    this.onAudioDuration,
  });

  final ffi.NativeCallable<native.dartOnBufferingCallback_tFunction>?
  onBuffering;
  final ffi.NativeCallable<ffi.Void Function(NativeAudioMetadata)>? onMetadata;
  final ffi.NativeCallable<native.dartOnMoreDataIsNeededCallback_tFunction>?
  onMoreDataIsNeeded;
  final ffi.NativeCallable<native.dartOnAudioDurationCallback_tFunction>?
  onAudioDuration;

  bool get hasCallbacks =>
      onBuffering != null ||
      onMetadata != null ||
      onMoreDataIsNeeded != null ||
      onAudioDuration != null;

  void close() {
    onBuffering?.close();
    onMetadata?.close();
    onMoreDataIsNeeded?.close();
    onAudioDuration?.close();
  }
}

final class _IsolateLifecycleToken implements ffi.Finalizable {}

/// FFI bindings to SoLoud
@internal
class FlutterSoLoudFfi extends FlutterSoLoud {
  static final Logger _log = Logger('flutter_soloud.FlutterSoLoudFfi');

  /// Sentinel used where no FlutterEngine lifecycle is available. Must match
  /// `kNoEngineId` in `src/bindings.cpp`.
  static const int noEngineId = -1;

  /// The FlutterEngine that owns this isolate, or [noEngineId] where the
  /// embedder does not expose one.
  ///
  /// Native code uses it to decide whether a detaching or hot-restarting
  /// FlutterEngine owns the registered callables and the native engine, and the
  /// Android plugin passes the same value down from
  /// `FlutterEngine.getEngineId()`.
  ///
  /// It must be read on the isolate the engine runs on:
  /// `PlatformDispatcher.instance.engineId` is set through an engine hook that
  /// only fires there, so a worker isolate would see `null`. Both call sites
  /// ([prepareEngineInit] and [setDartEventCallbacks]) run on that isolate,
  /// before any work is handed to `Isolate.run`.
  static int get currentEngineId =>
      ui.PlatformDispatcher.instance.engineId ?? noEngineId;

  // ////////////////////////////////////////////////
  // Callbacks impl
  // ////////////////////////////////////////////////

  ffi.NativeCallable<native.dartVoiceEndedCallback_tFunction>?
  nativeVoiceEndedCallable;
  ffi.NativeCallable<native.dartFileLoadedCallback_tFunction>?
  nativeFileLoadedCallable;
  ffi.NativeCallable<native.dartStateChangedCallback_tFunction>?
  nativeStateChangedCallable;
  ffi.NativeCallable<native.dartMixerOutputDataCallback_tFunction>?
  nativeMixerOutputDataCallable;
  ffi.NativeCallable<native.dartVisualizationCallback_tFunction>?
  nativeVisualizationCallable;

  void Function(AudioVisualizationData data)? _visualizationCallback;

  /// Controller that fires whenever new mixer output data is available.
  ///
  /// The event contains a pointer to the start of the contiguous unread
  /// region and the number of valid bytes. The pointer remains valid until
  /// the read position is advanced with [advanceMixerOutputReadPosition].
  late final StreamController<({ffi.Pointer<ffi.Uint8> pointer, int length})>
  mixerOutputDataAvailableController = StreamController.broadcast();

  /// Stream of notifications that new mixer output data is available.
  Stream<({ffi.Pointer<ffi.Uint8> pointer, int length})>
  get mixerOutputDataAvailableEvents =>
      mixerOutputDataAvailableController.stream;

  final Map<int, _BufferStreamNativeCallbacks> _bufferStreamNativeCallables =
      {};

  void _disposeBufferStreamCallbacks(SoundHash soundHash) {
    _bufferStreamNativeCallables.remove(soundHash.hash)?.close();
  }

  void _disposeAllBufferStreamCallbacks() {
    for (final callbacks in _bufferStreamNativeCallables.values) {
      callbacks.close();
    }
    _bufferStreamNativeCallables.clear();
  }

  void _voiceEndedCallback(ffi.Pointer<ffi.UnsignedInt> handle) {
    voiceEndedEventController.add(handle.value);
    // Must free a pointer made on cpp. On Windows this must be freed
    // there and cannot use `calloc.free(...)`
    nativeFree(handle.cast<ffi.Void>());
  }

  ///
  void _fileLoadedCallback(
    ffi.Pointer<ffi.UnsignedInt> error,
    ffi.Pointer<ffi.Char> completeFileName,
    ffi.Pointer<ffi.UnsignedInt> hash,
    ffi.Pointer<ffi.Uint64> counter,
  ) {
    _log.finest(
      () =>
          'FILE LOADED EVENT error: ${PlayerErrors.values[error.value].name}  '
          'hash: ${hash.value}  '
          'file: ${completeFileName.cast<Utf8>().toDartString()}  '
          'counter: ${counter.value}',
    );
    final result = <String, dynamic>{
      'error': error.value,
      'completeFileName': completeFileName.cast<Utf8>().toDartString(),
      'hash': hash.value,
      'counter': counter.value,
    };
    fileLoadedEventsController.add(result);
    // Must free a pointer made on cpp. On Windows this must be freed
    // there and cannot use `calloc.free(...)`
    nativeFree(error.cast<ffi.Void>());
    nativeFree(completeFileName.cast<ffi.Void>());
    nativeFree(hash.cast<ffi.Void>());
    nativeFree(counter.cast<ffi.Void>());
  }

  void _stateChangedCallback(ffi.Pointer<ffi.UnsignedInt> state) {
    final s = PlayerStateNotification.values[state.value];
    // Must free a pointer made on cpp. On Windows this must be freed
    // there and cannot use `calloc.free(state)`
    nativeFree(state.cast<ffi.Void>());
    _log.finest(() => 'STATE CHANGED EVENT state: $s');
    stateChangedController.add(s);
  }

  void _mixerOutputDataCallback(
    ffi.Pointer<ffi.UnsignedChar> data,
    int length,
  ) {
    if (mixerOutputDataAvailableController.hasListener) {
      mixerOutputDataAvailableController.add((
        pointer: data.cast<ffi.Uint8>(),
        length: length,
      ));
    }
    if (mixerOutputChunkController.hasListener) {
      final bytes = data.cast<ffi.Uint8>().asTypedList(length);
      mixerOutputChunkController.add(Uint8List.fromList(bytes));
      // In fixed PCM chunk mode the native side advances the read position
      // before invoking the callback, so Dart must not advance it again.
      if (!_mixerOutputChunkMode) {
        advanceMixerOutputReadPosition(length);
      }
    }
  }

  void _visualizationDataCallback(
    int channelCount,
    ffi.Pointer<ffi.Pointer<ffi.Float>> waveDataPerChannel,
    int waveSamples,
    ffi.Pointer<ffi.Pointer<ffi.Float>> fftDataPerChannel,
    int fftSamples,
  ) {
    if (_visualizationCallback == null) return;

    final waveList = <Float32List>[];
    if (waveSamples > 0 && waveDataPerChannel != ffi.nullptr) {
      for (var c = 0; c < channelCount; c++) {
        final ptr = waveDataPerChannel[c];
        if (ptr != ffi.nullptr) {
          waveList.add(Float32List.fromList(ptr.asTypedList(waveSamples)));
        }
      }
    }

    final fftList = <Float32List>[];
    if (fftSamples > 0 && fftDataPerChannel != ffi.nullptr) {
      for (var c = 0; c < channelCount; c++) {
        final ptr = fftDataPerChannel[c];
        if (ptr != ffi.nullptr) {
          fftList.add(Float32List.fromList(ptr.asTypedList(fftSamples)));
        }
      }
    }

    final packet = AudioVisualizationData(
      channelCount: channelCount,
      wave: waveList,
      fft: fftList,
    );

    _visualizationCallback?.call(packet);
  }

  @override
  void setVisualizationCallback(
    void Function(AudioVisualizationData data)? callback,
  ) {
    _visualizationCallback = callback;
    if (callback != null) {
      _registerVisualizationCallback();
    }
  }

  void _registerVisualizationCallback() {
    nativeVisualizationCallable ??=
        ffi.NativeCallable<
          native.dartVisualizationCallback_tFunction
        >.listener(_visualizationDataCallback);
    native.setVisualizationCallbackForEngine(
      nativeVisualizationCallable!.nativeFunction,
      currentEngineId,
    );
  }

  @override
  void disposeNativeCallables() {
    if (_lifecycleToken != null) {
      _isolateFinalizer.detach(_lifecycleToken!);
      _lifecycleToken = null;
    }
    _disposeAllBufferStreamCallbacks();
    nativeVoiceEndedCallable?.close();
    nativeVoiceEndedCallable = null;
    nativeFileLoadedCallable?.close();
    nativeFileLoadedCallable = null;
    nativeStateChangedCallable?.close();
    nativeStateChangedCallable = null;
    nativeMixerOutputDataCallable?.close();
    nativeMixerOutputDataCallable = null;
    nativeVisualizationCallable?.close();
    nativeVisualizationCallable = null;
  }

  @override
  void clearDartCallbackRegistrations() {
    native.clearDartCallbackRegistrations();
  }

  @override
  Future<void> setDartEventCallbacks() async {
    // Create a NativeCallable for the Dart functions
    nativeVoiceEndedCallable =
        ffi.NativeCallable<
          native.dartVoiceEndedCallback_tFunction
        >.listener(_voiceEndedCallback);
    nativeFileLoadedCallable =
        ffi.NativeCallable<
          native.dartFileLoadedCallback_tFunction
        >.listener(_fileLoadedCallback);
    nativeStateChangedCallable =
        ffi.NativeCallable<
          native.dartStateChangedCallback_tFunction
        >.listener(_stateChangedCallback);
    nativeMixerOutputDataCallable ??=
        ffi.NativeCallable<
          native.dartMixerOutputDataCallback_tFunction
        >.listener(_mixerOutputDataCallback);
    nativeVisualizationCallable ??=
        ffi.NativeCallable<
          native.dartVisualizationCallback_tFunction
        >.listener(_visualizationDataCallback);

    native.setDartEventCallback(
      nativeVoiceEndedCallable!.nativeFunction,
      nativeFileLoadedCallable!.nativeFunction,
      nativeStateChangedCallable!.nativeFunction,
      currentEngineId,
    );
    native.setMixerOutputCallbackForEngine(
      nativeMixerOutputDataCallable!.nativeFunction,
      currentEngineId,
    );
    native.setVisualizationCallbackForEngine(
      nativeVisualizationCallable!.nativeFunction,
      currentEngineId,
    );

    if (_lifecycleToken != null) {
      _isolateFinalizer.detach(_lifecycleToken!);
    }
    _lifecycleToken = _IsolateLifecycleToken();
    _isolateFinalizer.attach(
      _lifecycleToken!,
      ffi.Pointer.fromAddress(currentEngineId),
      detach: _lifecycleToken,
    );
  }

  @override
  void registerMixerOutputCallback() {
    nativeMixerOutputDataCallable ??=
        ffi.NativeCallable<
          native.dartMixerOutputDataCallback_tFunction
        >.listener(_mixerOutputDataCallback);
    // Called from whichever isolate owns the capture, including a worker one
    // via `SoLoudIsolate.startMixerOutputStream()`. On a worker,
    // [currentEngineId] is the no-engine sentinel and native code joins this
    // callable to whatever registration is live.
    //
    // A refusal means there is no live registration to join: the engine was
    // never initialized, has been deinitialized, or its FlutterEngine is being
    // destroyed. Publishing anyway would arm a callable belonging to an isolate
    // that is going away, so the capture simply produces nothing -- worth a
    // line in the log, since the stream stays silent rather than throwing.
    final published = native.setMixerOutputCallbackForEngine(
      nativeMixerOutputDataCallable!.nativeFunction,
      currentEngineId,
    );
    if (!published) {
      _log.warning(
        'The mixer output callback was refused: no live engine callback '
        'registration to join. The capture stream will not receive chunks. '
        'Initialize the engine before starting a mixer output capture, and '
        'stop the capture before deinitializing it.',
      );
    }
  }

  late final ffi.NativeFinalizer _isolateFinalizer = ffi.NativeFinalizer(
    ffi.Native.addressOf(native.retireDartCallbacksFinalizer),
  );
  _IsolateLifecycleToken? _lifecycleToken;

  // ////////////////////////////////////////////////
  // Mixer output capture bindings
  // ////////////////////////////////////////////////

  /// Whether the current mixer output capture is using fixed-size PCM chunks.
  /// When true, the native side advances the circular buffer read position
  /// before invoking the callback, so Dart must not advance it again.
  bool _mixerOutputChunkMode = false;

  @override
  PlayerErrors startMixerOutputCapture(
    MixerOutputFormat format,
    int sampleRate,
    int channels,
    int bufferSizeBytes,
    int notificationThresholdBytes,
    int chunkPCMFrames,
  ) {
    _mixerOutputChunkMode = format.isPcm && chunkPCMFrames > 0;
    final ret = native.startMixerCapture(
      format.value,
      sampleRate,
      channels,
      bufferSizeBytes,
      notificationThresholdBytes,
      chunkPCMFrames,
    );
    return PlayerErrors.values[ret.value];
  }

  @override
  void stopMixerOutputCapture() {
    _mixerOutputChunkMode = false;
    native.stopMixerCapture();
  }

  @override
  bool isMixerOutputCaptureRunning() {
    return native.isMixerCaptureRunning() != 0;
  }

  @override
  int getMixerOutputBufferSize() {
    return native.getMixerCaptureBufferSize();
  }

  @override
  int getMixerOutputAvailableBytes() {
    return native.getMixerCaptureAvailableBytes();
  }

  @override
  int getMixerOutputReadOffset() {
    return native.getMixerCaptureReadOffset();
  }

  @override
  void advanceMixerOutputReadPosition(int bytes) {
    native.advanceMixerCaptureReadPosition(bytes);
  }

  int getMixerOutputBufferPointer() {
    return native.getMixerCaptureBufferPointer().address;
  }

  @override
  Uint8List copyMixerOutputBuffer(int offset, int length) {
    if (length <= 0) {
      return Uint8List(0);
    }
    final ptr = native.getMixerCaptureBufferPointer();
    if (ptr == ffi.nullptr) {
      return Uint8List(0);
    }
    return Uint8List.fromList(
      (ptr + offset).cast<ffi.Uint8>().asTypedList(length),
    );
  }

  @override
  Uint8List getMixerOutputWavHeader() {
    final ptr = native.getMixerOutputWavHeader();
    if (ptr == ffi.nullptr) {
      return Uint8List(0);
    }
    final bytes = Uint8List.fromList(ptr.cast<ffi.Uint8>().asTypedList(44));
    nativeFree(ptr.cast<ffi.Void>());
    return bytes;
  }

  // ////////////////////////////////////////////////
  // Navtive bindings
  // ////////////////////////////////////////////////

  @override
  bool areXiphLibsAvailable() {
    return native.areXiphLibsAvailable();
  }

  /// When allocating memory in C code, more attention must be given when
  /// we are on Windows OS. It's not good to call `calloc.free()` because
  /// Windows could use different allocating methods for this and the same
  /// must be used freeing it. `calloc.free()` use the standard `free()` and
  /// doesn't have problems using it in other OSes.
  void nativeFree(ffi.Pointer<ffi.Void> pointer) {
    return native.nativeFree(pointer);
  }

  @override
  FutureOr<PlayerErrors> initEngine(
    int deviceId,
    int sampleRate,
    int bufferSize,
    Channels channels,
    bool lowLatency, {
    int devicePeriodFrames = 0,
    int renderAheadFrames = 0,
  }) async {
    // Run the blocking native engine/device initialization off the UI isolate
    // so it does not freeze the app (it can take seconds on Android/AAudio,
    // tripping the ANR watchdog — see #481). @Native external functions can
    // be called from any isolate, so the worker calls the generated binding
    // directly.
    final channelCount = channels.count;
    final lowLatencyValue = lowLatency ? 1 : 0;
    final result = await Isolate.run(
      () => native.initEngine(
        deviceId,
        sampleRate,
        bufferSize,
        channelCount,
        lowLatencyValue,
        devicePeriodFrames,
        renderAheadFrames,
      ),
    );
    return PlayerErrors.values[result.value];
  }

  @override
  void setAndroidAAudioAttributes(bool managed) {
    native.setAndroidAAudioAttributes(managed ? 1 : 0);
  }

  @override
  void setAudioDeviceIdleTimeout(Duration? timeout) {
    // Map the Dart Duration to the native signed-millisecond convention: null
    // (keep alive indefinitely) -> -1, and any finite duration to its
    // milliseconds, clamping negatives to 0 so only null means indefinite.
    final int timeoutMs = timeout == null
        ? -1
        : (timeout.inMilliseconds < 0 ? 0 : timeout.inMilliseconds);
    // The native call stores the policy and posts any required lifecycle
    // request. It does not start/stop the device or inspect SoLoud voice state
    // inline.
    native.setAudioDeviceIdleTimeout(timeoutMs);
  }

  @override
  Future<PlayerErrors> stopAudioDevice({bool force = false}) async {
    // Run the blocking native ma_device_stop() off the UI isolate.
    final ret = await Isolate.run(() => native.stopAudioDevice(force ? 1 : 0));
    return PlayerErrors.values[ret.value];
  }

  @override
  Future<PlayerErrors> startAudioDevice() async {
    // Run the blocking native ma_device_start() off the UI isolate so the app
    // stays responsive (it can take tens of ms while the OS restarts the
    // device).
    final ret = await Isolate.run(native.startAudioDevice);
    return PlayerErrors.values[ret.value];
  }

  @override
  AudioDeviceState getAudioDeviceState() {
    // Reading the device state is a cheap, non-blocking atomic load, so call
    // it directly on the UI isolate.
    return AudioDeviceState.fromValue(native.getAudioDeviceState().value);
  }

  /// Test-only interruption injection through the native notification path.
  void debugTriggerAudioInterruption({required bool began}) {
    native.debugTriggerAudioInterruption(began ? 1 : 0);
  }

  @override
  Future<PlayerErrors> changeDevice(int deviceId) async {
    final ret = await Isolate.run(() => native.changeDevice(deviceId));
    return PlayerErrors.values[ret.value];
  }

  @override
  List<PlaybackDevice> listPlaybackDevices() {
    final ret = <PlaybackDevice>[];
    final ffi.Pointer<ffi.Pointer<ffi.Char>> deviceNames = calloc(
      ffi.sizeOf<ffi.Pointer<ffi.Pointer<ffi.Char>>>() * 255,
    );
    final ffi.Pointer<ffi.Pointer<ffi.Int>> deviceIds = calloc(
      ffi.sizeOf<ffi.Pointer<ffi.Pointer<ffi.Int>>>() * 50,
    );
    final ffi.Pointer<ffi.Pointer<ffi.Int>> deviceIsDefault = calloc(
      ffi.sizeOf<ffi.Pointer<ffi.Pointer<ffi.Int>>>() * 50,
    );
    final ffi.Pointer<ffi.Int> nDevices = calloc();

    native.listPlaybackDevices(
      deviceNames,
      deviceIds,
      deviceIsDefault,
      nDevices,
    );

    final ndev = nDevices.value;
    for (var i = 0; i < ndev; i++) {
      final s1 = (deviceNames + i).value;
      final s = s1.cast<Utf8>().toDartString();
      final id1 = (deviceIds + i).value;
      final id = id1.value;
      final n1 = (deviceIsDefault + i).value;
      final n = n1.value;
      ret.add(PlaybackDevice(id, n == 1, s));
    }

    /// Free allocated memory is done in C.
    /// This work on all platforms but not on win.
    // for (int i = 0; i < ndev; i++) {
    //   calloc.free(devices.elementAt(i).value.ref.name);
    //   calloc.free(devices.elementAt(i).value);
    // }
    native.freeListPlaybackDevices(
      deviceNames,
      deviceIds,
      deviceIsDefault,
      ndev,
    );

    calloc
      ..free(deviceNames)
      ..free(deviceIds)
      ..free(nDevices);
    return ret;
  }

  void freeListPlaybackDevices(
    ffi.Pointer<ffi.Pointer<ffi.Char>> devicesName,
    ffi.Pointer<ffi.Pointer<ffi.Int>> deviceId,
    ffi.Pointer<ffi.Pointer<ffi.Int>> isDefault,
    int nDevices,
  ) {
    return native.freeListPlaybackDevices(
      devicesName,
      deviceId,
      isDefault,
      nDevices,
    );
  }

  @override
  void deinit() {
    return native.dispose();
  }

  @override
  Future<void> deinitAsync() async {
    await Isolate.run(native.dispose);
  }

  @override
  void prepareEngineInit() => native.prepareEngineInit(currentEngineId);

  @override
  bool get usesAsyncEnginePrepare => DarwinEngineLifecycle.isSupported;

  @override
  Future<void> prepareEngineInitAsync() async {
    final engineId = currentEngineId;

    // Read before the request goes out: `deinit()` can run while this is
    // suspended, and the claim must not land on the far side of the teardown
    // that superseded it. Native refuses a request whose epoch has moved.
    final shutdownEpoch = native.currentEngineShutdownEpoch();

    final result = await _iosEngineLifecycle.prepareEngineInit(
      engineId,
      shutdownEpoch,
    );

    switch (result) {
      case DarwinEnginePrepareResult.claimed:
        return;
      case DarwinEnginePrepareResult.unavailable:
        // Nothing was sent, so nothing was claimed: claim directly, exactly as
        // every other platform does. Automatic teardown is simply not armed.
        native.prepareEngineInit(engineId);
      case DarwinEnginePrepareResult.refused:
        // Either the platform said no, or a sent request's outcome is unknown.
        // Claiming again here could take the claim a second time on top of one
        // the platform may already have committed.
        throw const SoLoudInitializationStoppedByDeinitException();
    }
  }

  static const DarwinEngineLifecycle _iosEngineLifecycle =
      DarwinEngineLifecycle();

  @override
  void requestEngineShutdown() => native.requestEngineShutdown();

  @override
  bool isInited() {
    return native.isInited() == 1;
  }

  /// After loading the file, the [_fileLoadedCallback] will call the
  /// Dart function defined with [setDartEventCallbacks] which gives back
  /// the error and the new hash.
  @override
  void loadFile(String completeFileName, LoadMode mode, int counter) {
    final ffi.Pointer<Utf8> cString = completeFileName.toNativeUtf8();
    native.loadFile(
      cString.cast<ffi.Char>(),
      mode == LoadMode.memory,
      counter,
    );
    calloc.free(cString);
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadMem(
    String uniqueName,
    Uint8List buffer,
    LoadMode mode,
  ) {
    final ffi.Pointer<ffi.UnsignedInt> hash = calloc(
      ffi.sizeOf<ffi.UnsignedInt>(),
    );
    final ffi.Pointer<ffi.Uint8> bufferPtr = calloc(buffer.length);
    for (var i = 0; i < buffer.length; i++) {
      bufferPtr[i] = buffer[i];
    }

    final ffi.Pointer<Utf8> cString = uniqueName.toNativeUtf8();
    final e = native.loadMem(
      cString.cast<ffi.Char>(),
      bufferPtr.cast<ffi.UnsignedChar>(),
      buffer.length,
      mode == LoadMode.memory ? 1 : 0,
      hash,
    );
    final soundHash = SoundHash(hash.value);
    final ret = (error: PlayerErrors.values[e.value], soundHash: soundHash);
    calloc
      ..free(hash)
      ..free(cString);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) joinTwoSources(
    String uniqueName,
    Uint8List bufferLeft,
    Uint8List bufferRight,
  ) {
    final ffi.Pointer<ffi.UnsignedInt> hash = calloc(
      ffi.sizeOf<ffi.UnsignedInt>(),
    );
    final ffi.Pointer<ffi.Uint8> bufferLeftPtr = calloc(bufferLeft.length);
    for (var i = 0; i < bufferLeft.length; i++) {
      bufferLeftPtr[i] = bufferLeft[i];
    }
    final ffi.Pointer<ffi.Uint8> bufferRightPtr = calloc(bufferRight.length);
    for (var i = 0; i < bufferRight.length; i++) {
      bufferRightPtr[i] = bufferRight[i];
    }

    final ffi.Pointer<Utf8> cString = uniqueName.toNativeUtf8();
    final e = native.joinTwoSources(
      cString.cast<ffi.Char>(),
      bufferLeftPtr.cast<ffi.UnsignedChar>(),
      bufferRightPtr.cast<ffi.UnsignedChar>(),
      bufferLeft.length,
      bufferRight.length,
      hash,
    );
    final soundHash = SoundHash(hash.value);
    final ret = (error: PlayerErrors.values[e.value], soundHash: soundHash);
    calloc
      ..free(hash)
      ..free(bufferLeftPtr)
      ..free(bufferRightPtr)
      ..free(cString);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) setBufferStream(
    int maxBufferSize,
    BufferingType bufferingType,
    double bufferingTimeNeeds,
    int sampleRate,
    int channels,
    int format,
    OnBufferingCallbackTFunction? onBuffering,
    OnMetadataCallbackTFunction? onMetadata,
  ) {
    final nativeCallbacks = _BufferStreamNativeCallbacks(
      onBuffering: onBuffering == null
          ? null
          : ffi.NativeCallable<
              native.dartOnBufferingCallback_tFunction
            >.listener(onBuffering),
      onMetadata: onMetadata == null
          ? null
          : ffi.NativeCallable<ffi.Void Function(NativeAudioMetadata)>.listener(
              onMetadata,
            ),
    );

    final ffi.Pointer<ffi.UnsignedInt> hash = calloc(
      ffi.sizeOf<ffi.UnsignedInt>(),
    );
    final e = native.setBufferStream(
      hash,
      maxBufferSize,
      bufferingType.index,
      bufferingTimeNeeds,
      sampleRate,
      channels,
      format,
      nativeCallbacks.onBuffering?.nativeFunction ?? ffi.nullptr,
      nativeCallbacks.onMetadata?.nativeFunction
              .cast<
                ffi.NativeFunction<native.dartOnMetadataCallback_tFunction>
              >() ??
          ffi.nullptr,
    );
    final soundHash = SoundHash(hash.value);
    final ret = (error: PlayerErrors.values[e.value], soundHash: soundHash);
    if (ret.error == PlayerErrors.noError && nativeCallbacks.hasCallbacks) {
      _bufferStreamNativeCallables[soundHash.hash] = nativeCallbacks;
    } else {
      nativeCallbacks.close();
    }
    calloc.free(hash);
    return ret;
  }

  @override
  PlayerErrors resetBufferStream(SoundHash soundHash) {
    final e = native.resetBufferStream(soundHash.hash);
    return PlayerErrors.values[e.value];
  }

  @override
  ({PlayerErrors error, double value}) getStreamTimeConsumed(
    SoundHash soundHash,
  ) {
    final ffi.Pointer<ffi.Float> paramValue = calloc();
    final error = native.getStreamTimeConsumed(soundHash.hash, paramValue);
    final ret = paramValue.value;
    calloc.free(paramValue);
    return (error: PlayerErrors.values[error.value], value: ret);
  }

  @override
  PlayerErrors setBufferIcyMetaInt(SoundHash soundHash, int icyMetaInt) {
    final e = native.setBufferIcyMetaInt(soundHash.hash, icyMetaInt);
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors addPullBufferDataStream(
    int hash,
    Uint8List audioChunk, {
    int offset = 0,
  }) {
    final ffi.Pointer<ffi.Uint8> audioChunkPtr = calloc(audioChunk.length);
    for (var i = 0; i < audioChunk.length; i++) {
      audioChunkPtr[i] = audioChunk[i];
    }
    final e = native.addPullBufferDataStream(
      hash,
      audioChunkPtr.cast<ffi.UnsignedChar>(),
      audioChunk.length,
      offset,
    );
    calloc.free(audioChunkPtr);
    return PlayerErrors.values[e.value];
  }

  @override
  ({PlayerErrors error, double startTime, double endTime})
  getPullBufferTimeRange(int hash) {
    final startTimePtr = calloc<ffi.Double>();
    final endTimePtr = calloc<ffi.Double>();
    final e = native.getPullBufferTimeRange(hash, startTimePtr, endTimePtr);
    final result = (
      error: PlayerErrors.values[e.value],
      startTime: startTimePtr.value,
      endTime: endTimePtr.value,
    );
    calloc
      ..free(startTimePtr)
      ..free(endTimePtr);
    return result;
  }

  @override
  PlayerErrors addAudioDataStream(int hash, Uint8List audioChunk) {
    final ffi.Pointer<ffi.Uint8> audioChunkPtr = calloc(audioChunk.length);
    for (var i = 0; i < audioChunk.length; i++) {
      audioChunkPtr[i] = audioChunk[i];
    }
    final e = native.addAudioDataStream(
      hash,
      audioChunkPtr.cast<ffi.UnsignedChar>(),
      audioChunk.length,
    );
    calloc.free(audioChunkPtr);
    return PlayerErrors.values[e.value];
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) setPullBufferStream(
    int bufferSizeBytes,
    double bufferTriggerPosition,
    int sampleRate,
    int channels,
    int format,
    int audioSizeBytes,
    OnBufferingCallbackTFunction? onBuffering,
    OnMetadataCallbackTFunction? onMetadata,
    OnMoreDataIsNeededCallbackTFunction? onMoreDataIsNeeded,
    OnAudioDurationCallbackTFunction? onAudioDuration,
  ) {
    final nativeCallbacks = _BufferStreamNativeCallbacks(
      onBuffering: onBuffering == null
          ? null
          : ffi.NativeCallable<
              native.dartOnBufferingCallback_tFunction
            >.listener(onBuffering),
      onMetadata: onMetadata == null
          ? null
          : ffi.NativeCallable<ffi.Void Function(NativeAudioMetadata)>.listener(
              onMetadata,
            ),
      onMoreDataIsNeeded: onMoreDataIsNeeded == null
          ? null
          : ffi.NativeCallable<
              native.dartOnMoreDataIsNeededCallback_tFunction
            >.listener((int offset) => onMoreDataIsNeeded(offset)),
      onAudioDuration: onAudioDuration == null
          ? null
          : ffi.NativeCallable<
              native.dartOnAudioDurationCallback_tFunction
            >.listener((double duration) => onAudioDuration(duration)),
    );

    final ffi.Pointer<ffi.UnsignedInt> hash = calloc(
      ffi.sizeOf<ffi.UnsignedInt>(),
    );
    final e = native.setPullBufferStream(
      hash,
      bufferSizeBytes,
      bufferTriggerPosition,
      sampleRate,
      channels,
      format,
      audioSizeBytes,
      nativeCallbacks.onBuffering?.nativeFunction ?? ffi.nullptr,
      nativeCallbacks.onMetadata?.nativeFunction
              .cast<
                ffi.NativeFunction<native.dartOnMetadataCallback_tFunction>
              >() ??
          ffi.nullptr,
      nativeCallbacks.onMoreDataIsNeeded?.nativeFunction ?? ffi.nullptr,
      nativeCallbacks.onAudioDuration?.nativeFunction ?? ffi.nullptr,
    );
    final soundHash = SoundHash(hash.value);
    final ret = (error: PlayerErrors.values[e.value], soundHash: soundHash);
    if (ret.error == PlayerErrors.noError && nativeCallbacks.hasCallbacks) {
      _bufferStreamNativeCallables[soundHash.hash] = nativeCallbacks;
    } else {
      nativeCallbacks.close();
    }
    calloc.free(hash);
    return ret;
  }

  @override
  PlayerErrors resetPullBufferStream(SoundHash soundHash) {
    final e = native.resetPullBufferStream(soundHash.hash);
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors setDataIsEnded(SoundHash soundHash) {
    final e = native.setDataIsEnded(soundHash.hash);
    return PlayerErrors.values[e.value];
  }

  @override
  ({PlayerErrors error, int sizeInBytes}) getBufferSize(SoundHash soundHash) {
    final ffi.Pointer<ffi.UnsignedInt> size = calloc(
      ffi.sizeOf<ffi.UnsignedInt>(),
    );
    final e = native.getBufferSize(soundHash.hash, size);
    final ret = (error: PlayerErrors.values[e.value], sizeInBytes: size.value);
    calloc.free(size);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadWaveform(
    WaveForm waveform,
    bool superWave,
    double scale,
    double detune,
  ) {
    final ffi.Pointer<ffi.UnsignedInt> h = calloc(
      ffi.sizeOf<ffi.UnsignedInt>(),
    );
    final e = native.loadWaveform(
      waveform.index,
      superWave,
      scale,
      detune,
      h,
    );
    final soundHash = SoundHash(h.value);
    final ret = (error: PlayerErrors.values[e.value], soundHash: soundHash);
    calloc.free(h);
    return ret;
  }

  @override
  void setWaveformScale(SoundHash hash, double newScale) {
    return native.setWaveformScale(hash.hash, newScale);
  }

  @override
  void setWaveformDetune(SoundHash hash, double newDetune) {
    return native.setWaveformDetune(hash.hash, newDetune);
  }

  @override
  void setWaveformFreq(SoundHash hash, double newFreq) {
    return native.setWaveformFreq(hash.hash, newFreq);
  }

  @override
  void setWaveformSuperWave(SoundHash hash, int superwave) {
    return native.setSuperWave(hash.hash, superwave != 0);
  }

  @override
  void setWaveform(SoundHash hash, WaveForm newWaveform) {
    return native.setWaveform(hash.hash, newWaveform.index);
  }

  @override
  ({PlayerErrors error, SoundHandle handle}) speechText(String textToSpeech) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final ffi.Pointer<Utf8> cString = textToSpeech.toNativeUtf8();
    final e = native.speechText(cString.cast<ffi.Char>(), handle);
    final ret = (
      error: PlayerErrors.values[e.value],
      handle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  PlayerErrors pauseSwitch(SoundHandle handle) {
    return PlayerErrors.values[native.pauseSwitch(handle.id).value];
  }

  @override
  PlayerErrors setPause(SoundHandle handle, int pause) {
    return PlayerErrors.values[native.setPause(handle.id, pause != 0).value];
  }

  @override
  bool getPause(SoundHandle handle) {
    return native.getPause(handle.id) == 1;
  }

  @override
  void setRelativePlaySpeed(SoundHandle handle, double speed) {
    return native.setRelativePlaySpeed(handle.id, speed);
  }

  @override
  double getRelativePlaySpeed(SoundHandle handle) {
    return native.getRelativePlaySpeed(handle.id);
  }

  @override
  double getApproximateVolume(int channel) {
    return native.getApproximateVolume(channel);
  }

  @override
  ({PlayerErrors error, SoundHandle newHandle}) play(
    SoundHash soundHash, {
    int busId = 0,
    double volume = 1,
    double pan = 0,
    bool paused = false,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
    Duration? loopingEndAt,
    int? loopingStartOffsetAt,
    int? loopingEndOffsetAt,
    double scale = 1,
  }) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final hash = soundHash.hash;
    final e = native.play(
      hash,
      busId,
      volume,
      pan,
      paused,
      looping,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      scale,
      handle,
    );
    final ret = (
      error: PlayerErrors.values[e.value],
      newHandle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHandle newHandle}) playClocked(
    SoundHash soundHash,
    Duration soundTime, {
    int busId = 0,
    double volume = 1,
    double pan = 0,
    double scale = 1,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
    Duration? loopingEndAt,
    int? loopingStartOffsetAt,
    int? loopingEndOffsetAt,
  }) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final e = native.playClocked(
      soundHash.hash,
      soundTime.toDouble(),
      busId,
      volume,
      pan,
      scale,
      looping,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      handle,
    );
    final ret = (
      error: PlayerErrors.values[e.value],
      newHandle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  void setDelaySamples(SoundHandle handle, int samples) {
    native.setDelaySamples(handle.id, samples);
  }

  @override
  Duration getStreamTime(SoundHandle handle) {
    return native.getStreamTime(handle.id).toDuration();
  }

  @override
  void resetStreamTime() {
    native.resetStreamTime();
  }

  @override
  Duration getEngineTime() {
    return native.getEngineTime().toDuration();
  }

  @override
  Duration getPlayheadTime() {
    return native.getPlayheadTime().toDuration();
  }

  @override
  Duration getOutputLatency() {
    return native.getOutputLatency().toDuration();
  }

  @override
  bool isRenderAheadEnabled() {
    return native.isRenderAheadEnabled() != 0;
  }

  @override
  ({PlayerErrors error, SoundHandle newHandle}) playScheduled(
    SoundHash soundHash,
    Duration atTime, {
    Duration duration = Duration.zero,
    int busId = 0,
    double volume = 1,
    double pan = 0,
    double scale = 1,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
    Duration? loopingEndAt,
    int? loopingStartOffsetAt,
    int? loopingEndOffsetAt,
  }) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final e = native.playScheduled(
      soundHash.hash,
      atTime.toDouble(),
      duration.toDouble(),
      busId,
      volume,
      pan,
      scale,
      looping,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      handle,
    );
    final ret = (
      error: PlayerErrors.values[e.value],
      newHandle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  void stopScheduled(SoundHandle handle, Duration atTime) {
    native.stopScheduled(handle.id, atTime.toDouble());
  }

  @override
  void fadeScheduled(
    SoundHandle handle,
    Duration atTime,
    double to,
    Duration time, {
    bool thenStop = false,
  }) {
    native.fadeScheduled(
      handle.id,
      atTime.toDouble(),
      to,
      time.toDouble(),
      thenStop,
    );
  }

  @override
  PlayerErrors stop(SoundHandle handle) {
    return PlayerErrors.values[native.stop(handle.id).value];
  }

  @override
  void stopAll() {
    native.stopAll();
  }

  @override
  void stopAudioSource(SoundHash soundHash) {
    native.stopAudioSource(soundHash.hash);
  }

  @override
  void disposeSound(SoundHash soundHash) {
    native.disposeSound(soundHash.hash);
    _disposeBufferStreamCallbacks(soundHash);
  }

  @override
  void disposeAllSound() {
    native.disposeAllSound();
    _disposeAllBufferStreamCallbacks();
  }

  @override
  bool getLooping(SoundHandle handle) {
    return native.getLooping(handle.id) == 1;
  }

  @override
  void setLooping(SoundHandle handle, bool enable) {
    return native.setLooping(handle.id, enable);
  }

  @override
  Duration getLoopPoint(SoundHandle handle) {
    return native.getLoopPoint(handle.id).toDuration();
  }

  @override
  void setLoopPoint(SoundHandle handle, Duration timestamp) {
    native.setLoopPoint(handle.id, timestamp.toDouble());
  }

  @override
  Duration? getLoopEndPoint(SoundHandle handle) {
    final seconds = native.getLoopEndPoint(handle.id);
    return seconds > 0 ? seconds.toDuration() : null;
  }

  @override
  void setLoopEndPoint(SoundHandle handle, Duration? timestamp) {
    native.setLoopEndPoint(handle.id, timestamp?.toDouble() ?? 0);
  }

  @override
  PlayerErrors setVisualizationEnabled(
    bool enabled, {
    int windowSize = 256,
    VisualizationKind kind = VisualizationKind.waveAndFft,
    int channel = VisualizationChannel.merged,
  }) {
    if (enabled && _visualizationCallback != null) {
      _registerVisualizationCallback();
    }
    final ret = native.setVisualizationEnabled(
      enabled,
      windowSize,
      kind.value,
      channel,
    );
    return PlayerErrors.values[ret.value];
  }

  @override
  bool getVisualizationEnabled() {
    return native.getVisualizationEnabled() == 1;
  }

  @override
  void setFftSmoothing(double smooth) {
    return native.setFftSmoothing(smooth);
  }

  @override
  Duration getLength(SoundHash soundHash) {
    return native.getLength(soundHash.hash).toDuration();
  }

  @override
  int seek(SoundHandle handle, Duration time) {
    return native.seek(handle.id, time.toDouble()).value;
  }

  @override
  Duration getPosition(SoundHandle handle) {
    return native.getPosition(handle.id).toDuration();
  }

  @override
  double getGlobalVolume() {
    return native.getGlobalVolume();
  }

  @override
  int setGlobalVolume(double volume) {
    return native.setGlobalVolume(volume).value;
  }

  @override
  double getVolume(SoundHandle handle) {
    return native.getVolume(handle.id);
  }

  @override
  int setVolume(SoundHandle handle, double volume) {
    return native.setVolume(handle.id, volume).value;
  }

  /// Get a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// Returns the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  @override
  double getPan(SoundHandle handle) {
    // Note that because of the float<=>double conversion precision error
    // (SoLoud lib uses floats), the returned value is not precise.
    return native.getPan(handle.id);
  }

  /// Set a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// [pan] the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  @override
  void setPan(SoundHandle handle, double pan) {
    return native.setPan(handle.id, pan);
  }

  /// Set the left/right volumes directly.
  /// Note that this does not affect the value returned by getPan.
  ///
  /// [handle] the sound handle.
  /// [panLeft] value for the left pan.
  /// [panRight] value for the right pan.
  @override
  void setPanAbsolute(SoundHandle handle, double panLeft, double panRight) {
    return native.setPanAbsolute(handle.id, panLeft, panRight);
  }

  /// Check if a handle is still valid.
  ///
  /// [handle] handle to check
  /// Return true if it still exists
  @override
  bool getIsValidVoiceHandle(SoundHandle handle) {
    return native.getIsValidVoiceHandle(handle.id) == 1;
  }

  @override
  int getActiveVoiceCount() {
    return native.getActiveVoiceCount();
  }

  @override
  int countAudioSource(SoundHash soundHash) {
    return native.countAudioSource(soundHash.hash);
  }

  @override
  int getVoiceCount() {
    return native.getVoiceCount();
  }

  @override
  bool getProtectVoice(SoundHandle handle) {
    return native.getProtectVoice(handle.id);
  }

  @override
  void setProtectVoice(SoundHandle handle, bool protect) {
    return native.setProtectVoice(handle.id, protect);
  }

  @override
  void setInaudibleBehavior(SoundHandle handle, bool mustTick, bool kill) {
    return native.setInaudibleBehavior(handle.id, mustTick, kill);
  }

  @override
  int getMaxActiveVoiceCount() {
    return native.getMaxActiveVoiceCount();
  }

  @override
  void setMaxActiveVoiceCount(int maxVoiceCount) {
    return native.setMaxActiveVoiceCount(maxVoiceCount);
  }

  /////////////////////////////////////////
  /// voice groups
  /////////////////////////////////////////

  @override
  SoundHandle createVoiceGroup() {
    final ret = native.createVoiceGroup();
    return SoundHandle(ret > 0 ? ret : -1);
  }

  @override
  void destroyVoiceGroup(SoundHandle handle) {
    return native.destroyVoiceGroup(handle.id);
  }

  @override
  void addVoicesToGroup(
    SoundHandle voiceGroupHandle,
    List<SoundHandle> voiceHandles,
  ) {
    for (final handle in voiceHandles) {
      native.addVoiceToGroup(voiceGroupHandle.id, handle.id);
    }
  }

  @override
  bool isVoiceGroup(SoundHandle handle) {
    return native.isVoiceGroup(handle.id) == 1;
  }

  @override
  bool isVoiceGroupEmpty(SoundHandle handle) {
    return native.isVoiceGroupEmpty(handle.id) == 1;
  }

  /////////////////////////////////////////
  /// faders
  /////////////////////////////////////////

  @override
  PlayerErrors fadeGlobalVolume(double to, Duration duration) {
    final e = native.fadeGlobalVolume(to, duration.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors fadeVolume(SoundHandle handle, double to, Duration duration) {
    final e = native.fadeVolume(handle.id, to, duration.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors fadePan(SoundHandle handle, double to, Duration duration) {
    final e = native.fadePan(handle.id, to, duration.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors fadeRelativePlaySpeed(
    SoundHandle handle,
    double to,
    Duration time,
  ) {
    final e = native.fadeRelativePlaySpeed(handle.id, to, time.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors schedulePause(SoundHandle handle, Duration duration) {
    final e = native.schedulePause(handle.id, duration.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors scheduleStop(SoundHandle handle, Duration duration) {
    final e = native.scheduleStop(handle.id, duration.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors oscillateVolume(
    SoundHandle handle,
    double from,
    double to,
    Duration time,
  ) {
    final e = native.oscillateVolume(handle.id, from, to, time.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors oscillatePan(
    SoundHandle handle,
    double from,
    double to,
    Duration time,
  ) {
    final e = native.oscillatePan(handle.id, from, to, time.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors oscillateRelativePlaySpeed(
    SoundHandle handle,
    double from,
    double to,
    Duration time,
  ) {
    final e = native.oscillateRelativePlaySpeed(
      handle.id,
      from,
      to,
      time.toDouble(),
    );
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors oscillateGlobalVolume(double from, double to, Duration time) {
    final e = native.oscillateGlobalVolume(from, to, time.toDouble());
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors fadeFilterParameter(
    FilterType filterType,
    int attributeId,
    double to,
    double time, {
    SoundHandle? handle,
    int? busId,
  }) {
    final e = native.fadeFilterParameter(
      handle?.id ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
      attributeId,
      to,
      time,
    );
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors oscillateFilterParameter(
    FilterType filterType,
    int attributeId,
    double from,
    double to,
    double time, {
    SoundHandle? handle,
    int? busId,
  }) {
    final e = native.oscillateFilterParameter(
      handle?.id ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
      attributeId,
      from,
      to,
      time,
    );
    return PlayerErrors.values[e.value];
  }

  // ///////////////////////////////////////
  //  Filters
  // ///////////////////////////////////////

  @override
  ({PlayerErrors error, int index}) isFilterActive(
    FilterType filterType, {
    SoundHash? soundHash,
    int? busId,
  }) {
    final ffi.Pointer<ffi.Int> id = calloc(ffi.sizeOf<ffi.Int>());
    final e = native.isFilterActive(
      soundHash?.hash ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
      id,
    );
    final ret = (error: PlayerErrors.values[e.value], index: id.value);
    calloc.free(id);
    return ret;
  }

  @override
  ({PlayerErrors error, List<String> names}) getFilterParamNames(
    FilterType filterType,
  ) {
    final ffi.Pointer<ffi.Int> paramsCount = calloc(ffi.sizeOf<ffi.Int>());
    final ffi.Pointer<ffi.Pointer<ffi.Char>> names = calloc(
      ffi.sizeOf<ffi.Char>() * 30,
    );
    _log.fine(
      () =>
          'PARAMS NAME paramsCount: ${paramsCount.address.toRadixString(16)}  '
          'names: ${names.address.toRadixString(16)}',
    );

    final e = native.getFilterParamNames(
      native.FilterType.fromValue(filterType.index),
      paramsCount,
      names,
    );
    final pNames = <String>[];
    for (var i = 0; i < paramsCount.value; i++) {
      _log.fine(
        () =>
            'PARAMS NAME $i ${names + i}   '
            '${names[i].cast<Utf8>().toDartString()}    '
            'names[i]: ${names[i].address.toRadixString(16)}',
      );
      pNames.add(names[i].cast<Utf8>().toDartString());
    }
    final ret = (error: PlayerErrors.values[e.value], names: pNames);
    calloc.free(paramsCount);
    for (var i = 0; i < pNames.length; i++) {
      calloc.free(names[i]);
    }
    calloc.free(names);
    return ret;
  }

  @override
  PlayerErrors addFilter(
    FilterType filterType, {
    SoundHash? soundHash,
    int? busId,
  }) {
    final e = native.addFilter(
      soundHash?.hash ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
    );
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors removeFilter(
    FilterType filterType, {
    SoundHash? soundHash,
    int? busId,
  }) {
    final e = native.removeFilter(
      soundHash?.hash ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
    );
    return PlayerErrors.values[e.value];
  }

  @override
  PlayerErrors setFilterParams(
    FilterType filterType,
    int attributeId,
    double value, {
    SoundHandle? handle,
    int? busId,
  }) {
    final e = native.setFilterParams(
      handle?.id ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
      attributeId,
      value,
    );
    return PlayerErrors.values[e.value];
  }

  @override
  ({PlayerErrors error, double value}) getFilterParams(
    FilterType filterType,
    int attributeId, {
    SoundHandle? handle,
    int? busId,
  }) {
    final ffi.Pointer<ffi.Float> paramValue = calloc();
    final error = native.getFilterParams(
      handle?.id ?? 0,
      busId ?? 0,
      native.FilterType.fromValue(filterType.index),
      attributeId,
      paramValue,
    );
    final ret = paramValue.value;
    calloc.free(paramValue);
    return (error: PlayerErrors.values[error.value], value: ret);
  }

  /////////////////////////////////////////
  /// 3D audio methods
  /////////////////////////////////////////

  @override
  ({PlayerErrors error, SoundHandle newHandle}) play3d(
    SoundHash soundHash,
    double posX,
    double posY,
    double posZ, {
    int busId = 0,
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    bool paused = false,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
    Duration? loopingEndAt,
    int? loopingStartOffsetAt,
    int? loopingEndOffsetAt,
    double scale = 1,
  }) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final e = native.play3dWithLoopPoints(
      soundHash.hash,
      busId,
      posX,
      posY,
      posZ,
      velX,
      velY,
      velZ,
      volume,
      paused,
      looping,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      scale,
      handle,
    );
    final ret = (
      error: PlayerErrors.values[e.value],
      newHandle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHandle newHandle}) play3dClocked(
    SoundHash soundHash,
    Duration soundTime,
    double posX,
    double posY,
    double posZ, {
    int busId = 0,
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    double scale = 1,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
    Duration? loopingEndAt,
    int? loopingStartOffsetAt,
    int? loopingEndOffsetAt,
  }) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final e = native.play3dClocked(
      soundHash.hash,
      soundTime.toDouble(),
      busId,
      posX,
      posY,
      posZ,
      velX,
      velY,
      velZ,
      volume,
      scale,
      looping,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      handle,
    );
    final ret = (
      error: PlayerErrors.values[e.value],
      newHandle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHandle newHandle}) play3dScheduled(
    SoundHash soundHash,
    Duration atTime,
    double posX,
    double posY,
    double posZ, {
    Duration duration = Duration.zero,
    int busId = 0,
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    double scale = 1,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
    Duration? loopingEndAt,
    int? loopingStartOffsetAt,
    int? loopingEndOffsetAt,
  }) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final e = native.play3dScheduled(
      soundHash.hash,
      atTime.toDouble(),
      duration.toDouble(),
      busId,
      posX,
      posY,
      posZ,
      velX,
      velY,
      velZ,
      volume,
      scale,
      looping,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      handle,
    );
    final ret = (
      error: PlayerErrors.values[e.value],
      newHandle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  @override
  void set3dSoundSpeed(double speed) {
    return native.set3dSoundSpeed(speed);
  }

  @override
  double get3dSoundSpeed() {
    return native.get3dSoundSpeed();
  }

  @override
  void set3dListenerParameters(
    double posX,
    double posY,
    double posZ,
    double atX,
    double atY,
    double atZ,
    double upX,
    double upY,
    double upZ,
    double velocityX,
    double velocityY,
    double velocityZ,
  ) {
    return native.set3dListenerParameters(
      posX,
      posY,
      posZ,
      atX,
      atY,
      atZ,
      upX,
      upY,
      upZ,
      velocityX,
      velocityY,
      velocityZ,
    );
  }

  @override
  void set3dListenerPosition(double posX, double posY, double posZ) {
    return native.set3dListenerPosition(posX, posY, posZ);
  }

  @override
  void set3dListenerAt(double atX, double atY, double atZ) {
    return native.set3dListenerAt(atX, atY, atZ);
  }

  @override
  void set3dListenerUp(double upX, double upY, double upZ) {
    return native.set3dListenerUp(upX, upY, upZ);
  }

  @override
  void set3dListenerVelocity(
    double velocityX,
    double velocityY,
    double velocityZ,
  ) {
    return native.set3dListenerVelocity(velocityX, velocityY, velocityZ);
  }

  @override
  void set3dSourceParameters(
    SoundHandle handle,
    double posX,
    double posY,
    double posZ,
    double velocityX,
    double velocityY,
    double velocityZ,
  ) {
    return native.set3dSourceParameters(
      handle.id,
      posX,
      posY,
      posZ,
      velocityX,
      velocityY,
      velocityZ,
    );
  }

  @override
  void set3dSourcePosition(
    SoundHandle handle,
    double posX,
    double posY,
    double posZ,
  ) {
    return native.set3dSourcePosition(handle.id, posX, posY, posZ);
  }

  @override
  void set3dSourceVelocity(
    SoundHandle handle,
    double velocityX,
    double velocityY,
    double velocityZ,
  ) {
    return native.set3dSourceVelocity(
      handle.id,
      velocityX,
      velocityY,
      velocityZ,
    );
  }

  @override
  void set3dSourceMinMaxDistance(
    SoundHandle handle,
    double minDistance,
    double maxDistance,
  ) {
    return native.set3dSourceMinMaxDistance(
      handle.id,
      minDistance,
      maxDistance,
    );
  }

  @override
  void set3dSourceAttenuation(
    SoundHandle handle,
    int attenuationModel,
    double attenuationRolloffFactor,
  ) {
    return native.set3dSourceAttenuation(
      handle.id,
      attenuationModel,
      attenuationRolloffFactor,
    );
  }

  @override
  void set3dSourceDopplerFactor(SoundHandle handle, double dopplerFactor) {
    return native.set3dSourceDopplerFactor(handle.id, dopplerFactor);
  }

  // ///////////////////////////////////////
  // waveform audio data
  // ///////////////////////////////////////
  @override
  Float32List readSamplesFromFile(
    String completeFileName,
    int numSamplesNeeded, {
    double startTime = 0,
    double endTime = -1,
    bool average = false,
  }) {
    final pSamples = calloc<ffi.Float>(
      numSamplesNeeded * ffi.sizeOf<ffi.Float>(),
    );
    final error = native.readSamplesFromFile(
      completeFileName.toNativeUtf8().cast<ffi.Char>(),
      startTime,
      endTime,
      numSamplesNeeded,
      average,
      pSamples,
    );
    final samples = pSamples.asTypedList(numSamplesNeeded).asUnmodifiableView();

    /// Seems freeing this pointer is not needed because "samples" gets
    /// undefined after using "free"!? It will be GC-ed.
    // calloc.free(pSamples);
    if (ReadSamplesErrors.fromValue(error.value) !=
        ReadSamplesErrors.readSamplesNoError) {
      throw SoLoudCppException.fromReadSampleError(
        ReadSamplesErrors.fromValue(error.value),
      );
    }
    return samples;
  }

  @override
  Float32List readSamplesFromMem(
    Uint8List buffer,
    int numSamplesNeeded, {
    double startTime = 0,
    double endTime = -1,
    bool average = false,
  }) {
    final pSamples = calloc<ffi.Float>(
      numSamplesNeeded * ffi.sizeOf<ffi.Float>(),
    );
    final ffi.Pointer<ffi.Uint8> bufferPtr = calloc(buffer.length);
    for (var i = 0; i < buffer.length; i++) {
      bufferPtr[i] = buffer[i];
    }
    final error = native.readSamplesFromMem(
      bufferPtr.cast<ffi.UnsignedChar>(),
      buffer.length,
      startTime,
      endTime,
      numSamplesNeeded,
      average,
      pSamples,
    );
    final samples = pSamples.asTypedList(numSamplesNeeded).asUnmodifiableView();

    /// Seems freeing this pointer is not needed because "samples" gets
    /// undefined after using "free"!? It will be GC-ed.
    // calloc.free(pSamples);
    if (ReadSamplesErrors.fromValue(error.value) !=
        ReadSamplesErrors.readSamplesNoError) {
      throw SoLoudCppException.fromReadSampleError(
        ReadSamplesErrors.fromValue(error.value),
      );
    }
    return samples;
  }

  /////////////////////////////////////////
  /// Mixing Bus
  /// https://solhsa.com/soloud/mixbus.html
  /// https://solhsa.com/soloud/soloud_20200207.html#mixing-bus
  ///
  /// A mixing bus is a special audio source that plays other audio sources
  /// through it. Useful for grouped volume control, per-bus filtering,
  /// and per-bus visualization (FFT/wave). Busses can also be nested.
  /// Only one instance of a bus can play at a time.
  /// Busses are protected by default and marked as "must tick".
  /////////////////////////////////////////

  /// Create a new mixing bus.
  /// Returns a unique bus ID (>0) to reference this bus in other calls.
  @override
  int createBus() {
    return native.createBus();
  }

  /// Destroy a mixing bus by its ID.
  /// Does not stop voices that were playing through the bus.
  @override
  void destroyBus(int busId) {
    return native.destroyBus(busId);
  }

  /// Play the bus itself on the main SoLoud engine so it becomes audible.
  /// You must call this before sounds routed through the bus can be heard.
  ///
  /// [busId] the bus ID returned by createBus.
  /// [volume] playback volume (1.0 = full).
  /// [paused] whether to start paused.
  /// When [paused] is false the output audio device is started off the UI
  /// thread after the bus voice has been created.
  ///
  /// Returns [PlayerErrors.noError] and the voice handle of the bus on
  /// success, or the error and a zeroed handle on failure.
  @override
  ({PlayerErrors error, SoundHandle handle}) busPlayOnEngine(
    int busId,
    double volume,
    bool paused,
  ) {
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc();
    final e = native.busPlayOnEngine(busId, volume, paused, handle);
    final ret = (
      error: PlayerErrors.values[e.value],
      handle: SoundHandle(handle.value),
    );
    calloc.free(handle);
    return ret;
  }

  /// Set the number of output channels for the bus (default is 2 = stereo).
  ///
  /// [busId] the bus ID.
  /// [channels] number of channels (1 = mono, 2 = stereo, etc.).
  @override
  void busSetChannels(int busId, int channels) {
    native.busSetChannels(busId, channels);
  }

  /// Get the approximate output volume for a specific channel of this bus.
  /// Useful for VU meters or level indicators.
  /// Visualization must be enabled first.
  ///
  /// [busId] the bus ID.
  /// [channel] the output channel index (0 = left, 1 = right, etc.).
  /// Returns the approximate volume, or 0 if the bus is not found.
  @override
  double busGetApproximateVolume(int busId, int channel) {
    return native.busGetApproximateVolume(busId, channel);
  }

  /// Move a live voice (identified by its handle) into this bus.
  /// The voice will be reparented so it plays through the bus.
  /// Useful for dynamically routing sounds in/out of filtered busses.
  ///
  /// [busId] the bus ID.
  /// [voiceHandle] handle of the voice to annex.
  @override
  void busAnnexSound(int busId, int voiceHandle) {
    return native.busAnnexSound(busId, voiceHandle);
  }

  /// Get the number of voices currently playing through this bus.
  ///
  /// [busId] the bus ID.
  /// Returns the active voice count, or 0 if the bus is not found.
  @override
  int busGetActiveVoiceCount(int busId) {
    return native.busGetActiveVoiceCount(busId);
  }
}

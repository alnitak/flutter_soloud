// ignore_for_file: avoid_positional_boolean_parameters

import 'dart:async';
import 'dart:convert';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/helpers/playback_device.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:flutter_soloud/src/worker/worker.dart';
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

/// https://kapadia.github.io/emscripten/2013/09/13/emscripten-pointers-and-pointers.html
/// https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#access-memory-from-javascript

/// https://github.com/isar/isar/blob/main/packages/isar/lib/src/web/web.dart
/// chromium --disable-web-security --disable-gpu --user-data-dir=~/chromeTemp
///
/// Call Dart method from JS in Flutter Web
/// https://stackoverflow.com/questions/65423861/call-dart-method-from-js-in-flutter-web

/// Callback set in `setBufferStream` for the `onMetadata` closure.
typedef OnMetadataCallbackTFunction = void Function(int metadataPtr);

/// JS/WASM bindings to SoLoud
@internal
class FlutterSoLoudWeb extends FlutterSoLoud {
  static final Logger _log = Logger('flutter_soloud.FlutterSoLoudFfi');

  WorkerController? workerController;
  bool _eventCallbacksSetUp = false;

  /// The engine session the currently initialized native engine belongs to.
  /// Read from the native side after every successful `initEngine` (the
  /// native counter is bumped at each engine teardown). `voiceEnded` events
  /// posted by a previous engine travel asynchronously and can arrive after
  /// re-initialization; since voice handle ids restart from scratch in the
  /// new engine, stale events would otherwise kill voices of the current
  /// session (see engineGeneration in bindings.cpp).
  int _engineGeneration = 0;

  /// Whether the current mixer output capture is using fixed-size PCM chunks.
  /// When true, the native side advances the circular buffer read position
  /// before invoking the callback, so Dart must not advance it again.
  bool _mixerOutputChunkMode = false;

  /// Serializes native engine lifecycle calls (initEngine / changeDevice /
  /// deinit / disposeAllSound).
  ///
  /// In the multi-threaded (AudioWorklet) build, initEngine and changeDevice
  /// run through ASYNCIFY: while the AudioWorklet thread starts up, the WASM
  /// call is suspended with the native side still holding the non-recursive
  /// `init_deinit_mutex`/`loadMutex`. A concurrent native call from the main
  /// thread (e.g. `deinit()` racing `init()`, see the AsynchronousDeinit
  /// test) would then lock a mutex the same thread already holds, and musl
  /// aborts with "pthread mutex deadlock detected". Engine ops therefore run
  /// through this queue, and deinit/disposeAllSound are deferred only while
  /// an op is actually in flight (otherwise they run synchronously, like on
  /// every other platform). On the single-threaded build ops complete
  /// synchronously and the queue is a no-op pass-through.
  Future<void> _engineOpQueue = Future<void>.value();

  /// Set when [deinit] has been called but the native dispose is still
  /// queued behind an in-flight engine op. Makes [isInited] report false
  /// right away, as the app considers the engine gone.
  bool _deinitQueued = false;

  /// Number of engine ops currently executing (including while suspended in
  /// an ASYNCIFY sleep). When zero, the native lifecycle locks are free and
  /// deinit/disposeAllSound can run synchronously.
  int _engineOpsInFlight = 0;

  /// Runs [op] after every previously queued engine op has completed.
  Future<T> _enqueueEngineOp<T>(Future<T> Function() op) {
    final result = _engineOpQueue.then((_) async {
      _engineOpsInFlight++;
      try {
        return await op();
      } finally {
        _engineOpsInFlight--;
      }
    });
    // Keep the queue itself error-transparent: a failing op must not wedge
    // the ops queued behind it.
    _engineOpQueue = result.then((_) {}, onError: (Object _) {});
    return result;
  }

  @override
  void disposeNativeCallables() {
    /// Nothing to do on web.
  }

  @override
  void clearDartCallbackRegistrations() {
    /// Nothing to do on web.
  }

  /// Create the worker in the WASM Module and listen for events coming
  /// from `web/worker.dart.js`
  @override
  Future<void> setDartEventCallbacks() async {
    // Prevent multiple listener setups
    if (_eventCallbacksSetUp) {
      return;
    }

    // This calls the native WASM `createWorkerInWasm()` in `bindings.cpp`.
    // The latter creates a web Worker using `EM_ASM` inlining JS code to
    // create the worker in the WASM `Module`.
    final result = wasmCreateWorkerInWasm();
    if (result == 0) {
      // The worker could not be created (the EM_ASM catch path). Don't mark
      // the callbacks as set up, so the next call retries: a transient
      // failure here would otherwise leave `Module_soloud.wasmWorker` null
      // forever and every voiceEnded event would log "Worker not found.".
      _log.warning(
        'The web event worker (worker.dart.js) could not be created. '
        'voiceEnded and mixer output events will not be delivered until '
        'it is created successfully. If you serve the app with COOP/COEP '
        'headers, check for duplicated or conflicting values.',
      );
      return;
    }

    // Here the `Module_soloud.wasmModule` binded to a local [WorkerController]
    // is used in the main isolate to listen for events coming from native.
    // From native the events can be sent from the main thread and even from
    // other threads like the audio thread.
    workerController = WorkerController();
    await workerController!.setWasmWorker(wasmWorker);

    // Register the native mixer output callback. On the web the callback does
    // not call Dart directly; it posts a message to the worker from the audio
    // thread with the buffer offset and length.
    wasmSetMixerOutputCallback(0);

    _eventCallbacksSetUp = true;
    workerController!.onReceive().listen((event) {
      /// The [event] coming from `web/worker.dart.js` is of String type.
      /// Only `voiceEndedCallback` event in web for now.
      switch (event) {
        case String():
          final decodedMap = jsonDecode(event) as Map;
          _handleWorkerMessage(decodedMap);
        case Map():
          _handleWorkerMessage(event);
      }
    });
  }

  @override
  void registerMixerOutputCallback() {
    // On the web there is no separate isolate boundary, so we just ensure the
    // worker-based event plumbing (including the mixer output callback) is set
    // up. This is idempotent and safe to call from any place that needs to
    // listen to [mixerOutputChunkEvents].
    setDartEventCallbacks();
  }

  void _handleWorkerMessage(Map<dynamic, dynamic> message) {
    final msg = message['message'] as String?;
    if (msg == null) return;

    switch (msg) {
      case 'voiceEndedCallback':
        // Drop events emitted by a previous engine session: they can arrive
        // after re-initialization and would otherwise invalidate a voice of
        // the current session that happens to reuse the same handle id.
        final generation = (message['generation'] as num?)?.toInt();
        if (generation != null && generation != _engineGeneration) {
          _log.finest(
            () =>
                'VOICE ENDED EVENT from stale engine session '
                '(generation $generation, current $_engineGeneration); '
                'dropping handle ${(message['value'] as num).toInt()}',
          );
          return;
        }
        _log.finest(
          () =>
              'VOICE ENDED EVENT handle: '
              '${(message['value'] as num).toInt()}\n',
        );
        voiceEndedEventController.add((message['value'] as num).toInt());
      case 'mixerOutputData':
        final offset = (message['offset'] as num).toInt();
        final length = (message['length'] as num).toInt();
        _log.finest(
          () => 'MIXER OUTPUT DATA EVENT offset: $offset length: $length',
        );
        if (length <= 0) return;

        // Copy the data out of the WASM memory buffer and advance the native
        // read position so the circular buffer can reuse the memory. In fixed
        // PCM chunk mode the native side already advances the read position,
        // so Dart must not advance it again.
        final heapU8 = wasmHeapU8;
        final bytes = Uint8List.sublistView(
          heapU8.toDart,
          offset,
          offset + length,
        );
        mixerOutputChunkController.add(Uint8List.fromList(bytes));
        if (!_mixerOutputChunkMode) {
          advanceMixerOutputReadPosition(length);
        }
    }
  }

  /// If we will need to send messages to the native. Not used now.
  void sendMessageToWasmWorker(String message, int value) {
    final messagePtr = wasmMalloc(message.length);
    for (var i = 0; i < message.length; i++) {
      wasmSetValue(messagePtr + i, message.codeUnits[i], 'i8');
    }
    wasmSendToWorker(messagePtr, value);
    wasmFree(messagePtr);
  }

  @override
  bool areXiphLibsAvailable() => wasmAreXiphLibsAvailable() == 1;

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
    final ret = wasmStartMixerCapture(
      format.value,
      sampleRate,
      channels,
      bufferSizeBytes,
      notificationThresholdBytes,
      chunkPCMFrames,
    );
    return PlayerErrors.values[ret];
  }

  @override
  void stopMixerOutputCapture() {
    _mixerOutputChunkMode = false;
    wasmStopMixerCapture();
  }

  @override
  bool isMixerOutputCaptureRunning() {
    // A deinit can arrive while the WASM module is still instantiating
    // (init and deinit raced at startup): there is no capture running yet,
    // and the exports are not callable anyway.
    if (!_isModuleInstantiated()) {
      return false;
    }
    return wasmIsMixerCaptureRunning() == 1;
  }

  @override
  int getMixerOutputBufferSize() => wasmGetMixerCaptureBufferSize();

  @override
  int getMixerOutputAvailableBytes() => wasmGetMixerCaptureAvailableBytes();

  @override
  int getMixerOutputReadOffset() => wasmGetMixerCaptureReadOffset();

  @override
  void advanceMixerOutputReadPosition(int bytes) {
    if (bytes > 0) {
      wasmAdvanceMixerCaptureReadPosition(bytes);
    }
  }

  @override
  Uint8List getMixerOutputWavHeader() {
    final ptr = wasmGetMixerOutputWavHeader();
    if (ptr == 0) {
      return Uint8List(0);
    }
    final heapU8 = wasmHeapU8;
    final bytes = Uint8List.sublistView(heapU8.toDart, ptr, ptr + 44);
    final copy = Uint8List.fromList(bytes);
    wasmFree(ptr);
    return copy;
  }

  @override
  Uint8List copyMixerOutputBuffer(int offset, int length) {
    if (length <= 0) {
      return Uint8List(0);
    }
    final basePointer = wasmGetMixerCaptureBufferPointer();
    final heapU8 = wasmHeapU8;
    final bytes = Uint8List.sublistView(
      heapU8.toDart,
      basePointer + offset,
      basePointer + offset + length,
    );
    return Uint8List.fromList(bytes);
  }

  /// Calls `initEngine`/`changeDevice`. In the multi-threaded (AudioWorklet)
  /// build these can reach `emscripten_sleep` (miniaudio spin-waits while the
  /// worklet thread starts up), and the ASYNCIFY build requires them to be
  /// called with `{async: true}`. The single-threaded build has no ASYNCIFY,
  /// so there they are plain synchronous calls.
  Future<PlayerErrors> _callEngineAsync(String fn, List<int> args) async {
    await _ensureModuleReady();
    if (flutterSoloudHasAsyncify != true) {
      final ret = fn == 'initEngine'
          ? wasmInitEngine(args[0], args[1], args[2], args[3], args[4])
          : wasmChangeDevice(args[0]);
      return PlayerErrors.values[ret];
    }
    final promise = wasmCcallAsync(
      fn.toJS,
      'number'.toJS,
      List.filled(args.length, 'number'.toJS).toJS,
      args.map((a) => a.toJS).toList().toJS,
      <String, Object>{'async': true}.jsify()! as JSObject,
    );
    final ret = (await promise.toDart).toDartInt;
    return PlayerErrors.values[ret];
  }

  /// Waits for the WASM module to finish loading. `SoLoud.init()` can be
  /// called by the app while `init_module.dart.js` is still instantiating
  /// the module (especially with the larger multi-threaded build); without
  /// this the first bindings call would hit a not-yet-defined `Module_soloud`.
  Future<void> _ensureModuleReady() async {
    if (_isModuleInstantiated()) return;
    final ready = flutterSoloudReady;
    if (ready != null) {
      await ready.toDart.timeout(
        const Duration(seconds: 15),
        onTimeout: () => throw TimeoutException(
          'flutter_soloud: the WASM module did not finish initializing in '
          '15 seconds. If you serve the app with COOP/COEP headers, make '
          'sure they are not duplicated or conflicting (e.g. '
          'Cross-Origin-Embedder-Policy: credentialless, require-corp), '
          'which blocks the worker threads the module needs.',
        ),
      );
      return;
    }
    // init_module.dart.js has not started yet (or is not included in the
    // page); poll briefly for it to appear and finish.
    for (var i = 0; i < 100 && !_isModuleInstantiated(); i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
      final r = flutterSoloudReady;
      if (r != null) {
        await r.toDart;
        return;
      }
    }
  }

  /// Whether `self.Module_soloud` is the fully instantiated WASM module.
  ///
  /// After the glue script loads but before the MODULARIZE factory promise
  /// resolves, `self.Module_soloud` is the factory function (not the module
  /// instance): it has no `ccall`/`_malloc`/exports yet, so it must be
  /// treated as "not ready". The instantiated module is a plain object.
  bool _isModuleInstantiated() {
    final instance = moduleSoloudInstance;
    return instance != null && !instance.isA<JSFunction>();
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
    // Web is single-threaded (no isolates), so call the wasm function directly.
    // [lowLatency] only affects the native miniaudio backends (it selects the
    // AAudio/CoreAudio performance profile); the Web Audio backend ignores it.
    // [devicePeriodFrames]/[renderAheadFrames] (the render-ahead ring) are
    // native-only for now; the web backend keeps direct-to-device mixing.
    return _enqueueEngineOp(() async {
      _deinitQueued = false;
      final error = await _callEngineAsync('initEngine', [
        deviceId,
        sampleRate,
        bufferSize,
        channels.count,
        if (lowLatency) 1 else 0,
      ]);
      if (error == PlayerErrors.noError) {
        // Adopt the current engine session counter so voiceEnded events
        // posted by the previous engine (still in flight in the worker
        // pipeline) are recognized as stale and dropped.
        _engineGeneration = wasmGetEngineGeneration();
      }
      return error;
    });
  }

  @override
  Future<void> deinitAsync() async => deinit();

  @override
  void prepareEngineInit() {
    // The C++ `dispose()` latches `engine_shutdown_requested`; the next
    // `initEngine` short-circuits with `backendNotInited` unless this resets
    // the flag first (same protocol as the native bindings). It must run
    // through the engine-op queue: `dispose()` itself executes inside the
    // queue, so resetting the flag synchronously here would be overwritten
    // by the queued teardown before the queued `initEngine` runs.
    if (!_isModuleInstantiated()) {
      return;
    }
    unawaited(
      _enqueueEngineOp(() async {
        // kNoEngineId (-1): the web has no FlutterEngine lifecycle hooks.
        wasmPrepareEngineInit(wasmBigInt('-1'));
      }),
    );
  }

  @override
  bool get usesAsyncEnginePrepare => false;

  @override
  Future<void> prepareEngineInitAsync() async {}

  @override
  void requestEngineShutdown() {
    if (!_isModuleInstantiated()) {
      return;
    }
    wasmRequestEngineShutdown();
  }

  @override
  void setAndroidAAudioAttributes(bool managed) {
    // No-op on web: AAudio stream attributes are Android-only.
  }

  @override
  void setAudioDeviceIdleTimeout(Duration? timeout) {
    // No-op on web: the device is always kept running there (the idle-pause
    // is disabled on web to avoid stale-buffer glitches), so the idle timeout
    // has no effect.
  }

  @override
  Future<PlayerErrors> stopAudioDevice({bool force = false}) async {
    // Web is single-threaded (no isolates) and the device change is instant,
    // so call the wasm function directly.
    final ret = wasmStopAudioDevice(force ? 1 : 0);
    return PlayerErrors.values[ret];
  }

  @override
  Future<PlayerErrors> startAudioDevice() async {
    // Web is single-threaded (no isolates) and the device change is instant,
    // so call the wasm function directly.
    final ret = wasmStartAudioDevice();
    return PlayerErrors.values[ret];
  }

  @override
  AudioDeviceState getAudioDeviceState() {
    return AudioDeviceState.fromValue(wasmGetAudioDeviceState());
  }

  /// Test-only no-op. Browser AudioContext interruptions are not driven by
  /// miniaudio notifications.
  void debugTriggerAudioInterruption({required bool began}) {}

  @override
  FutureOr<PlayerErrors> changeDevice(int deviceId) {
    return _enqueueEngineOp(() => _callEngineAsync('changeDevice', [deviceId]));
  }

  @override
  List<PlaybackDevice> listPlaybackDevices() {
    /// allocate 50 device strings
    final namesPtr = wasmMalloc(50 * 255);
    final deviceIdPtr = wasmMalloc(50 * 4);
    final isDefaultPtr = wasmMalloc(50 * 4);
    final nDevicesPtr = wasmMalloc(4); // 4 bytes for an int32

    wasmListPlaybackDevices(namesPtr, deviceIdPtr, isDefaultPtr, nDevicesPtr);

    final nDevices = wasmGetI32Value(nDevicesPtr, 'i32');
    final devices = <PlaybackDevice>[];
    for (var i = 0; i < nDevices; i++) {
      final namePtr = wasmGetI32Value(namesPtr + i * 4, 'i32');
      final name = wasmUtf8ToString(namePtr);
      final deviceId = wasmGetI32Value(
        wasmGetI32Value(deviceIdPtr + i * 4, 'i32'),
        'i32',
      );
      final isDefault = wasmGetI32Value(
        wasmGetI32Value(isDefaultPtr + i * 4, 'i32'),
        'i32',
      );

      devices.add(PlaybackDevice(deviceId, isDefault == 1, name));
    }

    wasmFreeListPlaybackDevices(namesPtr, deviceIdPtr, isDefaultPtr, nDevices);

    wasmFree(nDevicesPtr);
    wasmFree(deviceIdPtr);
    wasmFree(isDefaultPtr);
    wasmFree(namesPtr);

    return devices;
  }

  @override
  void deinit() {
    // Synchronous when no engine op is in flight (the native lifecycle locks
    // are free then). Deferred only while an ASYNCIFY-suspended
    // initEngine/changeDevice holds them.
    if (_engineOpsInFlight == 0) {
      _doDeinit();
      return;
    }
    _deinitQueued = true;
    unawaited(
      _enqueueEngineOp(() async {
        _doDeinit();
      }),
    );
  }

  void _doDeinit() {
    try {
      wasmDeinit();
    } on Object catch (e) {
      _log.warning('deinit() error: $e');
    }
  }

  @override
  bool isInited() {
    // A deinit was requested but is still queued behind an in-flight engine
    // op: the app already considers the engine gone.
    if (_deinitQueued) {
      return false;
    }
    // The module may still be loading (init_module.dart.js instantiates it
    // asynchronously); treat that as "not initialized" instead of crashing.
    // Note that between the glue load and the end of the instantiation
    // `Module_soloud` is the factory function, so also guard against calling
    // into a module whose exports are not there yet (e.g. when instantiation
    // hangs or fails).
    if (!_isModuleInstantiated()) {
      return false;
    }
    // The multi-threaded WASM build uses SharedArrayBuffer for its memory and
    // therefore needs cross-origin isolation. That combination cannot happen
    // when the flavor is picked automatically (init_module.dart.js loads the
    // MT build only when the page is isolated), but it can happen if the page
    // loads the MT glue script manually (`manual` flavor) without COOP/COEP
    // headers. Warn only in that case.
    if (flutterSoloudBuild != 'st' && isCrossOriginIsolated != true) {
      // ignore: avoid_print
      print(
        'flutter_soloud: WARNING! This web page is not cross-origin isolated. '
        'If you are loading the multi-threaded WASM build '
        '(libflutter_soloud_plugin_mt.js) manually, serve the app with '
        '`Cross-Origin-Opener-Policy: same-origin` and '
        '`Cross-Origin-Embedder-Policy: require-corp` headers, or let '
        'init_module.dart.js pick the build automatically.',
      );
    }
    try {
      return wasmIsInited() == 1;
    } on Object {
      return false;
    }
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadFile(
    String completeFileName,
    LoadMode mode,
    int counter,
  ) {
    throw UnimplementedError(
      '[loadFile] in not supported on the web platfom! '
      'Please use [loadMem].',
    );
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadMem(
    String uniqueName,
    Uint8List buffer,
    LoadMode mode,
  ) {
    final hashPtr = wasmMalloc(4); // 4 bytes for an int32
    final bytesPtr = wasmMalloc(buffer.length);
    final pathPtr = wasmMalloc(uniqueName.length);

    /// Copy the buffer into WASM memory using the HEAPU8 view.
    final heapU8 = wasmHeapU8;
    heapU8.toDart.setAll(bytesPtr, buffer);

    /// Copy the path string into WASM memory.
    for (var i = 0; i < uniqueName.length; i++) {
      wasmSetValue(pathPtr + i, uniqueName.codeUnits[i], 'i8');
    }

    final result = wasmLoadMem(
      pathPtr,
      bytesPtr,
      buffer.length,
      mode == LoadMode.memory ? 1 : 0,
      hashPtr,
    );

    /// "*" means unsigned int 32
    final hash = wasmGetI32Value(hashPtr, '*');
    final soundHash = SoundHash(hash);
    final ret = (error: PlayerErrors.values[result], soundHash: soundHash);

    wasmFree(hashPtr);
    wasmFree(bytesPtr);
    wasmFree(pathPtr);

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
    final hashPtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmSetBufferStream(
      hashPtr,
      maxBufferSize,
      bufferingType.index,
      bufferingTimeNeeds,
      sampleRate,
      channels,
      format,
      // not used on C side. The callback is set below on the JS side. Setting
      // this to 1 to tell C that we have a callback.
      onBuffering == null ? 0 : 1,
      onMetadata == null ? 0 : 1,
    );
    final hash = wasmGetI32Value(hashPtr, 'i32');
    final soundHash = SoundHash(hash);
    final ret = (error: PlayerErrors.values[result], soundHash: soundHash);
    wasmFree(hashPtr);

    if (onBuffering != null) {
      // Create a new JS function named `dartOnBufferingCallback_$hash`.
      // To the function name is added the hash of the sound to make it unique.
      // This is done to prevent collisions with other sounds.
      // On the C++ side, this new function is called in `audiobuffer.cpp`
      // within the `addData()` method (which then calls `onBuffering()`
      // callback) when a playing handle reach the end of the buffer or
      // there is enough audio data to start playing it again.
      // If you change `dartOnBufferingCallback_$hash` name, you need to
      // change it on the C++ side as well.
      globalThis.setProperty(
        'dartOnBufferingCallback_$hash'.toJS,
        onBuffering.toJS,
      );
    }

    if (onMetadata != null) {
      // For web, create a JS-interop compatible callback wrapper
      @JSExport()
      void webMetadataCallback(JSNumber metadataPtr) {
        onMetadata(metadataPtr.toDartInt);
      }

      // Register the callback with the JS runtime
      globalThis.setProperty(
        'dartOnMetadataCallback_$hash'.toJS,
        webMetadataCallback.toJS,
      );
    }

    return ret;
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
    final hashPtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmSetPullBufferStream(
      hashPtr,
      bufferSizeBytes,
      bufferTriggerPosition,
      sampleRate,
      channels,
      format,
      wasmBigInt(audioSizeBytes.toString()),
      onBuffering == null ? 0 : 1,
      onMetadata == null ? 0 : 1,
      onMoreDataIsNeeded == null ? 0 : 1,
      onAudioDuration == null ? 0 : 1,
    );
    final hash = wasmGetI32Value(hashPtr, 'i32');
    final soundHash = SoundHash(hash);
    final ret = (error: PlayerErrors.values[result], soundHash: soundHash);
    wasmFree(hashPtr);

    if (onBuffering != null) {
      globalThis.setProperty(
        'dartOnBufferingCallback_$hash'.toJS,
        onBuffering.toJS,
      );
    }

    if (onMetadata != null) {
      @JSExport()
      void webMetadataCallback(JSNumber metadataPtr) {
        onMetadata(metadataPtr.toDartInt);
      }

      globalThis.setProperty(
        'dartOnMetadataCallback_$hash'.toJS,
        webMetadataCallback.toJS,
      );
    }

    if (onMoreDataIsNeeded != null) {
      @JSExport()
      void webMoreDataIsNeededCallback(int offset) {
        // The C++ side passes the offset as a 64-bit integer, which the
        // web target represents as a JS BigInt. Accepting [int] here lets
        // the runtime convert the BigInt to a Dart integer automatically.
        onMoreDataIsNeeded(offset);
      }

      globalThis.setProperty(
        'dartOnMoreDataIsNeededCallback_$hash'.toJS,
        webMoreDataIsNeededCallback.toJS,
      );
      // The C++ side does not request the first chunk on the web because the
      // callback is registered only after the WASM call returns. Fire the
      // initial request after the current call stack so callers can finish
      // assigning the returned [SoundHash] before the callback runs.
      scheduleMicrotask(() => onMoreDataIsNeeded(0));
    }

    if (onAudioDuration != null) {
      @JSExport()
      void webAudioDurationCallback(JSNumber duration) {
        onAudioDuration(duration.toDartDouble);
      }

      globalThis.setProperty(
        'dartOnAudioDurationCallback_$hash'.toJS,
        webAudioDurationCallback.toJS,
      );
    }

    return ret;
  }

  @override
  PlayerErrors resetPullBufferStream(SoundHash soundHash) {
    final result = wasmResetPullBufferStream(soundHash.hash);
    return PlayerErrors.values[result];
  }

  @override
  PlayerErrors addPullBufferDataStream(
    int hash,
    Uint8List audioChunk, {
    int offset = 0,
  }) {
    // On the web, `onMoreDataIsNeeded` can be invoked synchronously from the
    // audio thread's `onaudioprocess` callback. Calling back into the WASM
    // module from that same thread re-enters the pull-buffer mutex and
    // deadlocks. Copy the chunk and defer the native call so it runs after the
    // audio callback has returned and released the mutex.
    final chunkCopy = Uint8List.fromList(audioChunk);

    scheduleMicrotask(() {
      final audioChunkPtr = wasmMalloc(chunkCopy.length);

      final heapU8 = wasmHeapU8;
      heapU8.toDart.setAll(audioChunkPtr, chunkCopy);

      try {
        wasmAddPullBufferDataStream(
          hash,
          audioChunkPtr,
          chunkCopy.length,
          wasmBigInt(offset.toString()),
        );
      } on Object catch (e, st) {
        _log.warning('addPullBufferDataStream failed: $e\n$st');
      } finally {
        wasmFree(audioChunkPtr);
      }
    });

    return PlayerErrors.noError;
  }

  @override
  ({PlayerErrors error, double startTime, double endTime})
  getPullBufferTimeRange(int hash) {
    final startTimePtr = wasmMalloc(8); // 8 bytes for a double
    final endTimePtr = wasmMalloc(8);

    final result = wasmGetPullBufferTimeRange(hash, startTimePtr, endTimePtr);

    final startTime = wasmGetF64Value(startTimePtr, 'double');
    final endTime = wasmGetF64Value(endTimePtr, 'double');

    wasmFree(startTimePtr);
    wasmFree(endTimePtr);

    return (
      error: PlayerErrors.values[result],
      startTime: startTime,
      endTime: endTime,
    );
  }

  @override
  PlayerErrors resetBufferStream(SoundHash soundHash) {
    final result = wasmResetBufferStream(soundHash.hash);
    return PlayerErrors.values[result];
  }

  @override
  ({PlayerErrors error, double value}) getStreamTimeConsumed(
    SoundHash soundHash,
  ) {
    final valuePtr = wasmMalloc(4); // 4 bytes for a float
    final result = wasmGetStreamTimeConsumed(soundHash.hash, valuePtr);
    final value = wasmGetF32Value(valuePtr, 'float');
    wasmFree(valuePtr);

    return (error: PlayerErrors.values[result], value: value);
  }

  @override
  PlayerErrors setBufferIcyMetaInt(SoundHash soundHash, int icyMetaInt) {
    final result = wasmSetBufferIcyMetaInt(soundHash.hash, icyMetaInt);
    return PlayerErrors.values[result];
  }

  @override
  PlayerErrors addAudioDataStream(int hash, Uint8List audioChunk) {
    final audioChunkPtr = wasmMalloc(audioChunk.length);

    /// Copy the audio chunk into WASM memory using the HEAPU8 view.
    final heapU8 = wasmHeapU8;
    heapU8.toDart.setAll(audioChunkPtr, audioChunk);

    final result = wasmAddAudioDataStream(
      hash,
      audioChunkPtr,
      audioChunk.length,
    );
    wasmFree(audioChunkPtr);

    return PlayerErrors.values[result];
  }

  @override
  PlayerErrors setDataIsEnded(SoundHash soundHash) {
    final result = wasmSetDataIsEnded(soundHash.hash);
    return PlayerErrors.values[result];
  }

  @override
  ({PlayerErrors error, int sizeInBytes}) getBufferSize(SoundHash soundHash) {
    final sizeInBytesPtr = wasmMalloc(4);
    final result = wasmGetBufferSize(soundHash.hash, sizeInBytesPtr);
    final sizeInBytes = wasmGetI32Value(sizeInBytesPtr, 'i32');
    wasmFree(sizeInBytesPtr);

    final ret = (error: PlayerErrors.values[result], sizeInBytes: sizeInBytes);
    return ret;
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadWaveform(
    WaveForm waveform,
    bool superWave,
    double scale,
    double detune,
  ) {
    final hashPtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmLoadWaveform(
      waveform.index,
      superWave,
      scale,
      detune,
      hashPtr,
    );

    /// "*" means unsigned int 32
    final hash = wasmGetI32Value(hashPtr, 'i32');
    final soundHash = SoundHash(hash);
    final ret = (error: PlayerErrors.values[result], soundHash: soundHash);
    wasmFree(hashPtr);

    return ret;
  }

  @override
  void setWaveformScale(SoundHash hash, double newScale) {
    return wasmSetWaveformScale(hash.hash, newScale);
  }

  @override
  void setWaveformDetune(SoundHash hash, double newDetune) {
    return wasmSetWaveformDetune(hash.hash, newDetune);
  }

  @override
  void setWaveformFreq(SoundHash hash, double newFreq) {
    return wasmSetWaveformFreq(hash.hash, newFreq);
  }

  @override
  void setWaveformSuperWave(SoundHash hash, int superwave) {
    return wasmSetSuperWave(hash.hash, superwave);
  }

  @override
  void setWaveform(SoundHash hash, WaveForm newWaveform) {
    return wasmSetWaveform(hash.hash, newWaveform.index);
  }

  @override
  ({PlayerErrors error, SoundHandle handle}) speechText(String textToSpeech) {
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final textToSpeechPtr = wasmMalloc(textToSpeech.length);
    final result = wasmSpeechText(textToSpeechPtr, handlePtr);

    final newHandle = wasmGetI32Value(handlePtr, 'i32');
    final ret = (
      error: PlayerErrors.values[result],
      handle: SoundHandle(newHandle),
    );
    wasmFree(textToSpeechPtr);
    wasmFree(handlePtr);

    return ret;
  }

  @override
  PlayerErrors pauseSwitch(SoundHandle handle) {
    return PlayerErrors.values[wasmPauseSwitch(handle.id)];
  }

  @override
  PlayerErrors setPause(SoundHandle handle, int pause) {
    return PlayerErrors.values[wasmSetPause(handle.id, pause)];
  }

  @override
  bool getPause(SoundHandle handle) {
    return wasmGetPause(handle.id) == 1;
  }

  @override
  void setRelativePlaySpeed(SoundHandle handle, double speed) {
    return wasmSetRelativePlaySpeed(handle.id, speed);
  }

  @override
  double getRelativePlaySpeed(SoundHandle handle) {
    return wasmGetRelativePlaySpeed(handle.id);
  }

  @override
  double getApproximateVolume(int channel) {
    return wasmGetApproximateVolume(channel);
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
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmPlay(
      soundHash.hash,
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
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, 'i32');
    final ret = (
      error: PlayerErrors.values[result],
      newHandle: SoundHandle(newHandle),
    );
    wasmFree(handlePtr);

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
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmPlayClocked(
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
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, 'i32');
    final ret = (
      error: PlayerErrors.values[result],
      newHandle: SoundHandle(newHandle),
    );
    wasmFree(handlePtr);

    return ret;
  }

  @override
  void setDelaySamples(SoundHandle handle, int samples) {
    wasmSetDelaySamples(handle.id, samples);
  }

  @override
  Duration getStreamTime(SoundHandle handle) {
    return wasmGetStreamTime(handle.id).toDuration();
  }

  @override
  void resetStreamTime() {
    wasmResetStreamTime();
  }

  @override
  Duration getEngineTime() {
    return wasmGetEngineTime().toDuration();
  }

  @override
  Duration getPlayheadTime() {
    // The render-ahead ring is native-only for now; on web the playhead is
    // the mix clock.
    return getEngineTime();
  }

  @override
  Duration getOutputLatency() {
    return Duration.zero;
  }

  @override
  bool isRenderAheadEnabled() {
    return false;
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
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmPlayScheduled(
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
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, 'i32');
    final ret = (
      error: PlayerErrors.values[result],
      newHandle: SoundHandle(newHandle),
    );
    wasmFree(handlePtr);

    return ret;
  }

  @override
  void stopScheduled(SoundHandle handle, Duration atTime) {
    wasmStopScheduled(handle.id, atTime.toDouble());
  }

  @override
  void fadeScheduled(
    SoundHandle handle,
    Duration atTime,
    double to,
    Duration time, {
    bool thenStop = false,
  }) {
    wasmFadeScheduled(
      handle.id,
      atTime.toDouble(),
      to,
      time.toDouble(),
      thenStop ? 1 : 0,
    );
  }

  @override
  PlayerErrors stop(SoundHandle handle) {
    return PlayerErrors.values[wasmStop(handle.id)];
  }

  @override
  void stopAll() {
    wasmStopAll();
  }

  @override
  void stopAudioSource(SoundHash soundHash) {
    wasmStopAudioSource(soundHash.hash);
  }

  @override
  void disposeSound(SoundHash soundHash) {
    try {
      wasmDisposeSound(soundHash.hash);
    } catch (e) {
      _log.warning('disposeSound() error: $e');
    }
  }

  @override
  void disposeAllSound() {
    // See deinit(): the native call takes the non-recursive init_deinit_mutex,
    // so it is deferred only while an engine op is in flight.
    if (_engineOpsInFlight == 0) {
      _doDisposeAllSound();
      return;
    }
    unawaited(
      _enqueueEngineOp(() async {
        _doDisposeAllSound();
      }),
    );
  }

  void _doDisposeAllSound() {
    try {
      wasmDisposeAllSound();
    } on Object catch (e) {
      _log.warning('disposeAllSound() error: $e');
    }
  }

  @override
  bool getLooping(SoundHandle handle) {
    return wasmGetLooping(handle.id) == 1;
  }

  @override
  void setLooping(SoundHandle handle, bool enable) {
    return wasmSetLooping(handle.id, enable ? 1 : 0);
  }

  @override
  Duration getLoopPoint(SoundHandle handle) {
    return wasmGetLoopPoint(handle.id).toDuration();
  }

  @override
  void setLoopPoint(SoundHandle handle, Duration timestamp) {
    wasmSetLoopPoint(handle.id, timestamp.toDouble());
  }

  @override
  Duration? getLoopEndPoint(SoundHandle handle) {
    final seconds = wasmGetLoopEndPoint(handle.id);
    return seconds > 0 ? seconds.toDuration() : null;
  }

  @override
  void setLoopEndPoint(SoundHandle handle, Duration? timestamp) {
    wasmSetLoopEndPoint(handle.id, timestamp?.toDouble() ?? 0);
  }

  @override
  void setVisualizationEnabled(bool enabled) {
    wasmSetVisualizationEnabled(enabled ? 1 : 0);
  }

  @override
  bool getVisualizationEnabled() {
    return wasmGetVisualizationEnabled() == 1;
  }

  @override
  bool getFft(AudioData fft) {
    final isTheSameAsBeforePtr = wasmMalloc(4);
    wasmGetWave(fft.ctrl.samplesPtr, isTheSameAsBeforePtr);
    final ret = wasmGetI32Value(isTheSameAsBeforePtr, 'i32');
    wasmFree(isTheSameAsBeforePtr);
    return ret == 1;
  }

  @override
  bool getWave(AudioData wave) {
    final isTheSameAsBeforePtr = wasmMalloc(4);
    wasmGetWave(wave.ctrl.samplesPtr, isTheSameAsBeforePtr);
    final ret = wasmGetI32Value(isTheSameAsBeforePtr, 'i32');
    wasmFree(isTheSameAsBeforePtr);
    return ret == 1;
  }

  @override
  void setFftSmoothing(double smooth) {
    wasmSetFftSmoothing(smooth);
  }

  @override
  bool getAudioTexture(AudioData samples) {
    final isTheSameAsBeforePtr = wasmMalloc(4);
    wasmGetAudioTexture(samples.ctrl.samplesPtr, isTheSameAsBeforePtr);
    final ret = wasmGetI32Value(isTheSameAsBeforePtr, 'i32');
    wasmFree(isTheSameAsBeforePtr);
    return ret == 1;
  }

  @override
  bool getAudioTexture2D(AudioData samples) {
    final isTheSameAsBeforePtr = wasmMalloc(4);
    wasmGetAudioTexture2D(samples.ctrl.samplesPtr, isTheSameAsBeforePtr);
    final ret = wasmGetI32Value(isTheSameAsBeforePtr, 'i32');
    wasmFree(isTheSameAsBeforePtr);
    return ret == 1;
  }

  @override
  double getTextureValue(int row, int column) {
    final e = wasmGetTextureValue(row, column);
    return e;
  }

  @override
  Duration getLength(SoundHash soundHash) {
    return wasmGetLength(soundHash.hash).toDuration();
  }

  @override
  int seek(SoundHandle handle, Duration time) {
    return wasmSeek(handle.id, time.toDouble());
  }

  @override
  Duration getPosition(SoundHandle handle) {
    return wasmGetPosition(handle.id).toDuration();
  }

  @override
  double getGlobalVolume() {
    return wasmGetGlobalVolume();
  }

  @override
  int setGlobalVolume(double volume) {
    return wasmSetGlobalVolume(volume);
  }

  @override
  double getVolume(SoundHandle handle) {
    return wasmGetVolume(handle.id);
  }

  @override
  int setVolume(SoundHandle handle, double volume) {
    return wasmSetVolume(handle.id, volume);
  }

  @override
  double getPan(SoundHandle handle) {
    return wasmGetPan(handle.id);
  }

  @override
  void setPan(SoundHandle handle, double pan) {
    return wasmSetPan(handle.id, pan);
  }

  @override
  void setPanAbsolute(SoundHandle handle, double panLeft, double panRight) {
    return wasmSetPanAbsolute(handle.id, panLeft, panRight);
  }

  @override
  bool getIsValidVoiceHandle(SoundHandle handle) {
    return wasmGetIsValidVoiceHandle(handle.id) == 1;
  }

  @override
  int getActiveVoiceCount() {
    return wasmGetActiveVoiceCount();
  }

  @override
  int countAudioSource(SoundHash soundHash) {
    return wasmCountAudioSource(soundHash.hash);
  }

  @override
  int getVoiceCount() {
    return wasmGetVoiceCount();
  }

  @override
  bool getProtectVoice(SoundHandle handle) {
    return wasmGetProtectVoice(handle.id) == 1;
  }

  @override
  void setProtectVoice(SoundHandle handle, bool protect) {
    return wasmSetProtectVoice(handle.id, protect ? 1 : 0);
  }

  @override
  void setInaudibleBehavior(SoundHandle handle, bool mustTick, bool kill) {
    return wasmSetInaudibleBehavior(handle.id, mustTick, kill);
  }

  @override
  int getMaxActiveVoiceCount() {
    return wasmGetMaxActiveVoiceCount();
  }

  @override
  void setMaxActiveVoiceCount(int maxVoiceCount) {
    return wasmSetMaxActiveVoiceCount(maxVoiceCount);
  }

  /////////////////////////////////////////
  /// voice groups
  /////////////////////////////////////////

  @override
  SoundHandle createVoiceGroup() {
    /// The group handle returned has the sign bit flagged because int returns
    /// a unsigned 32 bit value (0xfffff000 | index) but Dart int is signed.
    /// Since on the web the int is a signed 32 bit, a negative number
    /// will be returned.
    /// The handle returned from "handle Soloud::createVoiceGroup()" function
    /// in "soloud_core_voicegroup.cpp" is 0xfffff000 | index, which is
    /// a negative number when interpreted as a signed 32-bit integer. So we
    /// need to convert it to unsigned 32-bit.
    /// A return value of 0 means error.
    final raw = wasmCreateVoiceGroup().toUnsigned(32);
    if (raw == 0) return SoundHandle(-1);
    return SoundHandle(raw);
  }

  @override
  void destroyVoiceGroup(SoundHandle handle) {
    return wasmDestroyVoiceGroup(handle.id);
  }

  @override
  void addVoicesToGroup(
    SoundHandle voiceGroupHandle,
    List<SoundHandle> voiceHandles,
  ) {
    for (final handle in voiceHandles) {
      wasmAddVoiceToGroup(voiceGroupHandle.id, handle.id);
    }
  }

  @override
  bool isVoiceGroup(SoundHandle handle) {
    return wasmIsVoiceGroup(handle.id) == 1;
  }

  @override
  bool isVoiceGroupEmpty(SoundHandle handle) {
    return wasmIsVoiceGroupEmpty(handle.id) == 1;
  }

  // ///////////////////////////////////////
  //  faders
  // ///////////////////////////////////////

  @override
  PlayerErrors fadeGlobalVolume(double to, Duration duration) {
    final e = wasmFadeGlobalVolume(to, duration.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors fadeVolume(SoundHandle handle, double to, Duration duration) {
    final e = wasmFadeVolume(handle.id, to, duration.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors fadePan(SoundHandle handle, double to, Duration duration) {
    final e = wasmFadePan(handle.id, to, duration.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors fadeRelativePlaySpeed(
    SoundHandle handle,
    double to,
    Duration time,
  ) {
    final e = wasmFadeRelativePlaySpeed(handle.id, to, time.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors schedulePause(SoundHandle handle, Duration duration) {
    final e = wasmSchedulePause(handle.id, duration.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors scheduleStop(SoundHandle handle, Duration duration) {
    final e = wasmScheduleStop(handle.id, duration.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors oscillateVolume(
    SoundHandle handle,
    double from,
    double to,
    Duration time,
  ) {
    final e = wasmOscillateVolume(handle.id, from, to, time.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors oscillatePan(
    SoundHandle handle,
    double from,
    double to,
    Duration time,
  ) {
    final e = wasmOscillatePan(handle.id, from, to, time.toDouble());
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors oscillateRelativePlaySpeed(
    SoundHandle handle,
    double from,
    double to,
    Duration time,
  ) {
    final e = wasmOscillateRelativePlaySpeed(
      handle.id,
      from,
      to,
      time.toDouble(),
    );
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors oscillateGlobalVolume(double from, double to, Duration time) {
    final e = wasmOscillateGlobalVolume(from, to, time.toDouble());
    return PlayerErrors.values[e];
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
    final e = wasmFadeFilterParameter(
      handle?.id ?? 0,
      busId ?? 0,
      filterType.index,
      attributeId,
      to,
      time,
    );
    return PlayerErrors.values[e];
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
    final e = wasmOscillateFilterParameter(
      handle?.id ?? 0,
      busId ?? 0,
      filterType.index,
      attributeId,
      from,
      to,
      time,
    );
    return PlayerErrors.values[e];
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
    // ignore: omit_local_variable_types
    final idPtr = wasmMalloc(4); // 4 bytes for an int32
    final e = wasmIsFilterActive(
      soundHash?.hash ?? 0,
      busId ?? 0,
      filterType.index,
      idPtr,
    );
    final index = wasmGetI32Value(idPtr, 'i32');
    final ret = (error: PlayerErrors.values[e], index: index);
    wasmFree(idPtr);
    return ret;
  }

  @override
  ({PlayerErrors error, List<String> names}) getFilterParamNames(
    FilterType filterType,
  ) {
    final paramsCountPtr = wasmMalloc(4); // 4 bytes for an int32
    final namesPtr = wasmMalloc(30 * 20); // list of 30 String with 20 chars
    final e = wasmGetFilterParamNames(
      filterType.index,
      paramsCountPtr,
      namesPtr,
    );

    final pNames = <String>[];
    var offsetPtr = 0;
    final paramsCount = wasmGetI32Value(paramsCountPtr, 'i32');
    for (var i = 0; i < paramsCount; i++) {
      final namePtr = wasmGetI32Value(namesPtr + offsetPtr, 'i32');
      final name = wasmUtf8ToString(namePtr);
      offsetPtr += name.length;

      pNames.add(name);
    }

    final ret = (error: PlayerErrors.values[e], names: pNames);
    wasmFree(namesPtr);
    wasmFree(paramsCountPtr);
    return ret;
  }

  @override
  PlayerErrors addFilter(
    FilterType filterType, {
    SoundHash? soundHash,
    int? busId,
  }) {
    final e = wasmAddFilter(soundHash?.hash ?? 0, busId ?? 0, filterType.index);
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors removeFilter(
    FilterType filterType, {
    SoundHash? soundHash,
    int? busId,
  }) {
    final e = wasmRemoveFilter(
      soundHash?.hash ?? 0,
      busId ?? 0,
      filterType.index,
    );
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors setFilterParams(
    FilterType filterType,
    int attributeId,
    double value, {
    SoundHandle? handle,
    int? busId,
  }) {
    final e = wasmSetFilterParams(
      handle?.id ?? 0,
      busId ?? 0,
      filterType.index,
      attributeId,
      value,
    );
    return PlayerErrors.values[e];
  }

  @override
  ({PlayerErrors error, double value}) getFilterParams(
    FilterType filterType,
    int attributeId, {
    SoundHandle? handle,
    int? busId,
  }) {
    final paramValuePtr = wasmMalloc(4);
    final error = wasmGetFilterParams(
      handle?.id ?? 0,
      busId ?? 0,
      filterType.index,
      attributeId,
      paramValuePtr,
    );
    final ret = wasmGetF32Value(paramValuePtr, 'float');
    wasmFree(paramValuePtr);
    return (error: PlayerErrors.values[error], value: ret);
  }

  // //////////////////////////////////////
  // 3D audio methods
  // //////////////////////////////////////

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
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmPlay3d(
      soundHash.hash,
      busId,
      posX,
      posY,
      posZ,
      velX,
      velY,
      velZ,
      volume,
      paused ? 1 : 0,
      looping ? 1 : 0,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      scale,
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, 'i32');
    final ret = (
      error: PlayerErrors.values[result],
      newHandle: SoundHandle(newHandle),
    );
    wasmFree(handlePtr);

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
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmPlay3dClocked(
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
      looping ? 1 : 0,
      loopingStartAt.toDouble(),
      loopingEndAt?.toDouble() ?? 0,
      loopingStartOffsetAt ?? -1,
      loopingEndOffsetAt ?? -1,
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, 'i32');
    final ret = (
      error: PlayerErrors.values[result],
      newHandle: SoundHandle(newHandle),
    );
    wasmFree(handlePtr);

    return ret;
  }

  @override
  void set3dSoundSpeed(double speed) {
    return wasmSet3dSoundSpeed(speed);
  }

  @override
  double get3dSoundSpeed() {
    return wasmGet3dSoundSpeed();
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
    return wasmSet3dListenerParameters(
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
    return wasmSet3dListenerPosition(posX, posY, posZ);
  }

  @override
  void set3dListenerAt(double atX, double atY, double atZ) {
    return wasmSet3dListenerAt(atX, atY, atZ);
  }

  @override
  void set3dListenerUp(double upX, double upY, double upZ) {
    return wasmSet3dListenerUp(upX, upY, upZ);
  }

  @override
  void set3dListenerVelocity(
    double velocityX,
    double velocityY,
    double velocityZ,
  ) {
    return wasmSet3dListenerVelocity(velocityX, velocityY, velocityZ);
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
    return wasmSet3dSourceParameters(
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
    return wasmSet3dSourcePosition(handle.id, posX, posY, posZ);
  }

  @override
  void set3dSourceVelocity(
    SoundHandle handle,
    double velocityX,
    double velocityY,
    double velocityZ,
  ) {
    return wasmSet3dSourceVelocity(handle.id, velocityX, velocityY, velocityZ);
  }

  @override
  void set3dSourceMinMaxDistance(
    SoundHandle handle,
    double minDistance,
    double maxDistance,
  ) {
    return wasmSet3dSourceMinMaxDistance(handle.id, minDistance, maxDistance);
  }

  @override
  void set3dSourceAttenuation(
    SoundHandle handle,
    int attenuationModel,
    double attenuationRolloffFactor,
  ) {
    return wasmSet3dSourceAttenuation(
      handle.id,
      attenuationModel,
      attenuationRolloffFactor,
    );
  }

  @override
  void set3dSourceDopplerFactor(SoundHandle handle, double dopplerFactor) {
    return wasmSet3dSourceDopplerFactor(handle.id, dopplerFactor);
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
    throw UnimplementedError(
      '[readSamplesFromFile] in not supported on the '
      'web platfom! Please use [readSamplesFromMem].',
    );
  }

  @override
  Float32List readSamplesFromMem(
    Uint8List buffer,
    int numSamplesNeeded, {
    double startTime = 0,
    double endTime = -1,
    bool average = false,
  }) {
    final bufferPtr = wasmMalloc(buffer.length);

    /// Copy the buffer into WASM memory using the HEAPU8 view.
    final heapU8 = wasmHeapU8;
    heapU8.toDart.setAll(bufferPtr, buffer);

    final samplesPtr = wasmMalloc(numSamplesNeeded * 4);
    final error = wasmReadSamplesFromMem(
      bufferPtr,
      buffer.length,
      startTime,
      endTime,
      numSamplesNeeded,
      average,
      samplesPtr,
    );

    // Create a view of the WASM memory using JSFloat32Array first
    final jsHeapF32 = wasmHeapF32;
    // Convert the TypedArray view to a Dart Float32List
    final samples = Float32List.sublistView(
      jsHeapF32.toDart,
      samplesPtr ~/ 4, // divide by 4 because Float32 is 4 bytes
      (samplesPtr ~/ 4) + numSamplesNeeded,
    );

    wasmFree(samplesPtr);
    wasmFree(bufferPtr);

    if (ReadSamplesErrors.fromValue(error) !=
        ReadSamplesErrors.readSamplesNoError) {
      throw SoLoudCppException.fromReadSampleError(
        ReadSamplesErrors.fromValue(error),
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

  @override
  int createBus() {
    return wasmCreateBus();
  }

  @override
  void destroyBus(int busId) {
    return wasmDestroyBus(busId);
  }

  @override
  ({PlayerErrors error, SoundHandle handle}) busPlayOnEngine(
    int busId,
    double volume,
    bool paused,
  ) {
    final handlePtr = wasmMalloc(4); // 4 bytes for an int32
    final result = wasmBusPlayOnEngine(
      busId,
      volume,
      paused ? 1 : 0,
      handlePtr,
    );
    final ret = (
      error: PlayerErrors.values[result],
      handle: SoundHandle(wasmGetI32Value(handlePtr, 'i32')),
    );
    wasmFree(handlePtr);

    return ret;
  }

  @override
  void busSetChannels(int busId, int channels) {
    return wasmBusSetChannels(busId, channels);
  }

  @override
  double busGetApproximateVolume(int busId, int channel) {
    return wasmBusGetApproximateVolume(busId, channel);
  }

  @override
  void busAnnexSound(int busId, int voiceHandle) {
    return wasmBusAnnexSound(busId, voiceHandle);
  }

  @override
  int busGetActiveVoiceCount(int busId) {
    return wasmBusGetActiveVoiceCount(busId);
  }
}

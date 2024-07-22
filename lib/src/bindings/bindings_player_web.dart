import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/filter_params.dart';
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

/// JS/WASM bindings to SoLoud
@internal
class FlutterSoLoudWeb extends FlutterSoLoud {
  static final Logger _log = Logger('flutter_soloud.FlutterSoLoudFfi');

  WorkerController? workerController;

  /// Create the worker in the WASM Module and listen for events coming
  /// from `web/worker.dart.js`
  @override
  void setDartEventCallbacks() {
    // This calls the native WASM `createWorkerInWasm()` in `bindings.cpp`.
    // The latter creates a web Worker using `EM_ASM` inlining JS code to
    // create the worker in the WASM `Module`.
    wasmCreateWorkerInWasm();

    // Here the `Module.wasmModule` binded to a local [WorkerController]
    // is used in the main isolate to listen for events coming from native.
    // From native the events can be sent from the main thread and even from
    // other threads like the audio thread.
    workerController = WorkerController();
    workerController!.setWasmWorker(wasmWorker);
    workerController!.onReceive().listen(
      (event) {
        /// The [event] coming from `web/worker.dart.js` is of String type.
        /// Only `voiceEndedCallback` event in web for now.
        switch (event) {
          case String():
            final decodedMap = jsonDecode(event) as Map;
            if (decodedMap['message'] == 'voiceEndedCallback') {
              _log.finest(
                () => 'VOICE ENDED EVENT handle: ${decodedMap['value']}\n',
              );
              voiceEndedEventController.add(decodedMap['value'] as int);
            }
        }
      },
    );
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
  PlayerErrors initEngine(int sampleRate, int bufferSize, Channels channels) {
    final ret = wasmInitEngine(sampleRate, bufferSize, channels.count);
    return PlayerErrors.values[ret];
  }

  @override
  void deinit() => wasmDeinit();

  @override
  bool isInited() => wasmIsInited() == 1;

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadFile(
    String completeFileName,
    LoadMode mode,
  ) {
    throw UnimplementedError('[loadFile] in not supported on the web platfom! '
        'Please use [loadMem].');
  }

  @override
  ({PlayerErrors error, SoundHash soundHash}) loadMem(
    String uniqueName,
    Uint8List buffer,
    LoadMode mode,
  ) {
    final hashPtr = wasmMalloc(4); // 4 bytes for an int
    final bytesPtr = wasmMalloc(buffer.length);
    final pathPtr = wasmMalloc(uniqueName.length);
    // Is there a way to speed up this array copy?
    for (var i = 0; i < buffer.length; i++) {
      wasmSetValue(bytesPtr + i, buffer[i], 'i8');
    }
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
  ({PlayerErrors error, SoundHash soundHash}) loadWaveform(
    WaveForm waveform,
    bool superWave,
    double scale,
    double detune,
  ) {
    final hashPtr = wasmMalloc(4); // 4 bytes for an int
    final result = wasmLoadWaveform(
      waveform.index,
      superWave,
      scale,
      detune,
      hashPtr,
    );

    /// "*" means unsigned int 32
    final hash = wasmGetI32Value(hashPtr, '*');
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
    final handlePtr = wasmMalloc(4); // 4 bytes for an int
    final textToSpeechPtr = wasmMalloc(textToSpeech.length);
    final result = wasmSpeechText(
      textToSpeechPtr,
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, '*');
    final ret = (
      error: PlayerErrors.values[result],
      handle: SoundHandle(newHandle),
    );
    wasmFree(textToSpeechPtr);
    wasmFree(handlePtr);

    return ret;
  }

  @override
  void pauseSwitch(SoundHandle handle) {
    return wasmPauseSwitch(handle.id);
  }

  @override
  void setPause(SoundHandle handle, int pause) {
    return wasmSetPause(handle.id, pause);
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
  ({PlayerErrors error, SoundHandle newHandle}) play(
    SoundHash soundHash, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
  }) {
    final handlePtr = wasmMalloc(4); // 4 bytes for an int
    final result = wasmPlay(
      soundHash.hash,
      volume,
      pan,
      paused,
      looping,
      loopingStartAt.toDouble(),
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, '*');
    final ret =
        (error: PlayerErrors.values[result], newHandle: SoundHandle(newHandle));
    wasmFree(handlePtr);

    return ret;
  }

  @override
  void stop(SoundHandle handle) {
    return wasmStop(handle.id);
  }

  @override
  void disposeSound(SoundHash soundHash) {
    return wasmDisposeSound(soundHash.hash);
  }

  @override
  void disposeAllSound() {
    return wasmDisposeAllSound();
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
  void setVisualizationEnabled(bool enabled) {
    wasmSetVisualizationEnabled(enabled ? 1 : 0);
  }

  @override
  bool getVisualizationEnabled() {
    return wasmGetVisualizationEnabled() == 1;
  }

  @override
  void getFft(AudioData fft) {
    wasmGetWave(fft.ctrl.samplesPtr);
  }

  @override
  void getWave(AudioData wave) {
    wasmGetWave(wave.ctrl.samplesPtr);
  }

  @override
  void setFftSmoothing(double smooth) {
    wasmSetFftSmoothing(smooth);
  }

  @override
  void getAudioTexture(AudioData samples) {
    wasmGetAudioTexture(samples.ctrl.samplesPtr);
  }

  @override
  PlayerErrors getAudioTexture2D(AudioData samples) {
    final e = wasmGetAudioTexture2D(samples.ctrl.samplesPtr);
    return PlayerErrors.values[e];
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
    /// The group handle returned has the sign bit flagged. Since on the web
    /// the int is a signed 32 bit, a negative number will be returned.
    /// Fixing by ORing the result.
    final ret = wasmCreateVoiceGroup() | 0xfffff000;
    return SoundHandle(ret > 0 ? ret : -1);
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
    SoundHandle handle = const SoundHandle.error(),
  }) {
    final e = wasmFadeFilterParameter(
      handle.isError ? 0 : handle.id,
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
    SoundHandle handle = const SoundHandle.error(),
  }) {
    final e = wasmOscillateFilterParameter(
      handle.isError ? 0 : handle.id,
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
    SoundHash soundHash = const SoundHash.invalid(),
  }) {
    // ignore: omit_local_variable_types
    final idPtr = wasmMalloc(4); // 4 bytes for an int
    final e = wasmIsFilterActive(soundHash.hash, filterType.index, idPtr);
    final index = wasmGetI32Value(idPtr, 'i32');
    final ret = (error: PlayerErrors.values[e], index: index);
    wasmFree(idPtr);
    return ret;
  }

  @override
  ({PlayerErrors error, List<String> names}) getFilterParamNames(
    FilterType filterType,
  ) {
    final paramsCountPtr = wasmMalloc(4); // 4 bytes for an int
    final namesPtr = wasmMalloc(30 * 20); // list of 30 String with 20 chars
    final e =
        wasmGetFilterParamNames(filterType.index, paramsCountPtr, namesPtr);

    final pNames = <String>[];
    var offsetPtr = 0;
    final paramsCount = wasmGetI32Value(paramsCountPtr, '*');
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
    SoundHash soundHash = const SoundHash.invalid(),
  }) {
    final e = wasmAddFilter(soundHash.hash, filterType.index);
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors removeFilter(
    FilterType filterType, {
    SoundHash soundHash = const SoundHash.invalid(),
  }) {
    final e = wasmRemoveFilter(soundHash.hash, filterType.index);
    return PlayerErrors.values[e];
  }

  @override
  PlayerErrors setFilterParams(
    FilterType filterType,
    int attributeId,
    double value, {
    SoundHandle handle = const SoundHandle(0),
  }) {
    final e = wasmSetFilterParams(
      handle.id,
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
    SoundHandle handle = const SoundHandle(0),
  }) {
    final paramValuePtr = wasmMalloc(4);
    final error = wasmGetFilterParams(
      handle.id,
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
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    bool paused = false,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
  }) {
    final handlePtr = wasmMalloc(4); // 4 bytes for an int
    final result = wasmPlay3d(
      soundHash.hash,
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
      handlePtr,
    );

    /// "*" means unsigned int 32
    final newHandle = wasmGetI32Value(handlePtr, '*');
    final ret =
        (error: PlayerErrors.values[result], newHandle: SoundHandle(newHandle));
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
}

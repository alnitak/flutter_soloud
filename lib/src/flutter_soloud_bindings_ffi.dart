// this file is not the one generated by ffiGen.
// ffiGen will generate [flutter_soloud_bindings_ffi_TMP.dart]
// from [ffi_gen_tmp.h] file. Read notes in the latter
// ignore_for_file: require_trailing_commas, avoid_bool_literals_in_conditional_expressions

import 'dart:ffi' as ffi;
import 'package:ffi/ffi.dart';

/// Possible player errors
enum PlayerErrors {
  /// No error
  noError,

  /// Some parameter is invalid
  invalidParameter,

  /// File not found
  fileNotFound,

  /// File found, but could not be loaded
  fileLoadFailed,

  /// DLL not found, or wrong DLL
  dllNotFound,

  /// Out of memory
  outOfMemory,

  /// Feature not implemented
  notImplemented,

  /// Other error
  unknownError,

  /// null pointer. Could happens when passing a non initialized
  /// pointer (with calloc()) to retrieve FFT or wave data
  nullPointer,

  /// The sound with specified hash is not found
  soundHashNotFound,

  /// Player not initialized
  backendNotInited,

  /// Audio isolate already started
  isolateAlreadyStarted,

  /// Audio isolate not yet started
  isolateNotStarted,

  /// Engine not yet started
  engineNotInited,
}

/// FFI bindings to SoLoud
class FlutterSoLoudFfi {
  /// Holds the symbol lookup function.
  final ffi.Pointer<T> Function<T extends ffi.NativeType>(String symbolName)
      _lookup;

  /// The symbols are looked up in [dynamicLibrary].
  // ignore: sort_constructors_first
  FlutterSoLoudFfi(ffi.DynamicLibrary dynamicLibrary)
      : _lookup = dynamicLibrary.lookup;

  /// The symbols are looked up with [lookup].
  // ignore: sort_constructors_first
  FlutterSoLoudFfi.fromLookup(
      ffi.Pointer<T> Function<T extends ffi.NativeType>(String symbolName)
          lookup)
      : _lookup = lookup;

  /// FOR NOW THIS CALLBACK IS NOT USED
  ///
//   /// Since using the callback passed to [setPlayEndedCallback] will throw
//   /// ```Error: fromFunction expects a static function as parameter.
//   /// dart:ffi only supports calling static Dart functions from native code.
//   /// Closures and tear-offs are not supported because
//   /// they can capture context.```
//   static void Function(int)? _userPlayEndedCallback;
//   /// here the user callback given to [setPlayEndedCallback] will be temporarly
// /// saved into [_userPlayEndedCallback]. The [_playEndedCallback] will instead
// /// passed to C side to be called, which then call the user callback.
//   static void _playEndedCallback(int handle) {
//     if (_userPlayEndedCallback != null) {
//       // ignore: prefer_null_aware_method_calls
//       _userPlayEndedCallback!(handle);
//     }
//   }
//   /// @brief Set a dart function to call when the sound with [handle] handle ends
//   /// @param callback this is the dart function that will be called
//   ///     when the sound ends to play.
//   ///     Must be global or a static class member:
//   ///     ```@pragma('vm:entry-point')
//   ///        void playEndedCallback(int handle) {
//   ///             // here the sound with [handle] has ended.
//   ///             // you can play again
//   ///             soLoudController.soLoudFFI.play(handle);
//   ///             // or dispose it
//   ///             soLoudController.soLoudFFI.stop(handle);
//   ///        }
//   ///     ```
//   /// @param handle the handle to the sound
//   /// @return callback this is the dart function that will be called
//   ///         when the sound ends to play
//   /// @return true if success;
//   /// https://github.com/dart-lang/sdk/issues/37022
//   /// PS: NOT USED, maybe in another time
//   bool setPlayEndedCallback(
//     void Function(int) callback,
//     int handle,
//   ) {

//     _userPlayEndedCallback = callback;
//     final ret = _setPlayEndedCallback(
//       ffi.Pointer.fromFunction(_playEndedCallback),
//       handle,
//     );

//     return ret == 1 ? true : false;
//   }

//   late final _setPlayEndedCallbackPtr = _lookup<
//       ffi.NativeFunction<
//           ffi.Int Function(
//               ffi.Pointer<
//                   ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt)>>,
//               ffi.UnsignedInt)>>('setPlayEndedCallback');
//   late final _setPlayEndedCallback = _setPlayEndedCallbackPtr.asFunction<
//       int Function(
//           ffi.Pointer<ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt)>>,
//           int)>();

  /// Initialize the player. Must be called before any other player functions
  ///
  /// Returns [PlayerErrors.noError] if success
  PlayerErrors initEngine() {
    return PlayerErrors.values[_initEngine()];
  }

  late final _initEnginePtr =
      _lookup<ffi.NativeFunction<ffi.Int32 Function()>>('initEngine');
  late final _initEngine = _initEnginePtr.asFunction<int Function()>();

  /// Must be called when there is no more need of the player or when closing the app
  ///
  void dispose() {
    return _dispose();
  }

  late final _disposePtr =
      _lookup<ffi.NativeFunction<ffi.Void Function()>>('dispose');
  late final _dispose = _disposePtr.asFunction<void Function()>();

  /// Load a new sound to be played once or multiple times later
  ///
  /// [completeFileName] the complete file path
  /// [hash] return hash of the sound
  /// Returns [PlayerErrors.noError] if success
  ({PlayerErrors error, int soundHash}) loadFile(String completeFileName) {
    // ignore: omit_local_variable_types
    final ffi.Pointer<ffi.UnsignedInt> h =
        calloc(ffi.sizeOf<ffi.UnsignedInt>());
    final e = _loadFile(
      completeFileName.toNativeUtf8().cast<ffi.Char>(),
      h,
    );
    final ret = (error: PlayerErrors.values[e], soundHash: h.value);
    calloc.free(h);
    return ret;
  }

  late final _loadFilePtr = _lookup<
      ffi.NativeFunction<
          ffi.Int32 Function(ffi.Pointer<ffi.Char>,
              ffi.Pointer<ffi.UnsignedInt>)>>('loadFile');
  late final _loadFile = _loadFilePtr.asFunction<
      int Function(ffi.Pointer<ffi.Char>, ffi.Pointer<ffi.UnsignedInt>)>();

  /// Speech the text given
  ///
  /// [textToSpeech]
  /// Returns [PlayerErrors.noError] if success and [handle] sound identifier
  /// TODO(me): add other T2S parameters
  ({PlayerErrors error, int handle}) speechText(String textToSpeech) {
    // ignore: omit_local_variable_types
    final ffi.Pointer<ffi.UnsignedInt> handle = calloc(1);
    final e = _speechText(
      textToSpeech.toNativeUtf8().cast<ffi.Char>(),
      handle,
    );
    final ret = (error: PlayerErrors.values[e], handle: handle.value);
    calloc.free(handle);
    return ret;
  }

  late final _speechTextPtr = _lookup<
      ffi.NativeFunction<
          ffi.Int32 Function(ffi.Pointer<ffi.Char>,
              ffi.Pointer<ffi.UnsignedInt>)>>('speechText');
  late final _speechText = _speechTextPtr.asFunction<
      int Function(ffi.Pointer<ffi.Char>, ffi.Pointer<ffi.UnsignedInt>)>();

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  void pauseSwitch(int handle) {
    return _pauseSwitch(handle);
  }

  late final _pauseSwitchPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt)>>(
          'pauseSwitch');
  late final _pauseSwitch = _pauseSwitchPtr.asFunction<void Function(int)>();

  /// Gets the pause state
  ///
  /// [handle] the sound handle
  /// Return true if paused
  bool getPause(int handle) {
    return _getPause(handle) == 1 ? true : false;
  }

  late final _getPausePtr =
      _lookup<ffi.NativeFunction<ffi.Int Function(ffi.UnsignedInt)>>(
          'getPause');
  late final _getPause = _getPausePtr.asFunction<int Function(int)>();

  /// Play already loaded sound identified by [handle]
  ///
  /// [hash] the unique sound hash of a sound
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [paused] 0 not pause
  /// Return the handle of the sound, 0 if error
  int play(
    int soundHash, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
  }) {
    return _play(soundHash, volume, pan, paused ? 1 : 0);
  }

  late final _playPtr = _lookup<
      ffi.NativeFunction<
          ffi.UnsignedInt Function(
              ffi.UnsignedInt, ffi.Float, ffi.Float, ffi.Int)>>('play');
  late final _play =
      _playPtr.asFunction<int Function(int, double, double, int)>();

  /// Stop already loaded sound identified by [handle] and clear it
  ///
  /// [handle]
  void stop(int handle) {
    return _stop(handle);
  }

  late final _stopPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt)>>('stop');
  late final _stop = _stopPtr.asFunction<void Function(int)>();

  /// Stop all handles of the already loaded sound identified
  /// by [soundHash] and clear it
  ///
  /// [soundHash]
  void stopSound(int soundHash) {
    return _stopSound(soundHash);
  }

  late final _stopSoundPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt)>>(
          'stopSound');
  late final _stopSound = _stopSoundPtr.asFunction<void Function(int)>();

  /// This function can be used to set a sample to play on repeat,
  /// instead of just playing once
  ///
  /// [handle]
  /// [enable]
  void setLooping(int handle, bool enable) {
    return _setLooping(handle, enable ? 1 : 0);
  }

  late final _setLoopingPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt, ffi.Int)>>(
          'setLooping');
  late final _setLooping = _setLoopingPtr.asFunction<void Function(int, int)>();

  /// Enable or disable visualization
  ///
  /// [enabled] enable or disable it
  void setVisualizationEnabled(bool enabled) {
    return _setVisualizationEnabled(
      enabled ? 1 : 0,
    );
  }

  late final _setVisualizationEnabledPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Int)>>(
          'setVisualizationEnabled');
  late final _setVisualizationEnabled =
      _setVisualizationEnabledPtr.asFunction<void Function(int)>();

  /// Returns valid data only if VisualizationEnabled is true
  ///
  /// [fft]
  /// Return a 256 float array containing FFT data.
  void getFft(ffi.Pointer<ffi.Float> fft) {
    return _getFft(fft);
  }

  late final _getFftPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Pointer<ffi.Float>)>>(
          'getFft');
  late final _getFft =
      _getFftPtr.asFunction<void Function(ffi.Pointer<ffi.Float>)>();

  /// Returns valid data only if VisualizationEnabled is true
  ///
  /// fft
  /// Return a 256 float array containing wave data.
  void getWave(ffi.Pointer<ffi.Float> wave) {
    return _getWave(wave);
  }

  late final _getWavePtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Pointer<ffi.Float>)>>(
          'getWave');
  late final _getWave =
      _getWavePtr.asFunction<void Function(ffi.Pointer<ffi.Float>)>();

  /// Smooth FFT data.
  /// When new data is read and the values are decreasing, the new value will be
  /// decreased with an amplitude between the old and the new value.
  /// This will result on a less shaky visualization.
  ///
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  void setFftSmoothing(double smooth) {
    return _setFftSmoothing(smooth);
  }

  late final _setFftSmoothingPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Float)>>(
          'setFftSmoothing');
  late final _setFftSmoothing =
      _setFftSmoothingPtr.asFunction<void Function(double)>();

  /// Return in [samples] a 512 float array.
  /// The first 256 floats represent the FFT frequencies data [0.0~1.0].
  /// The other 256 floats represent the wave data (amplitude) [-1.0~1.0].
  ///
  /// [samples] should be allocated and freed in dart side
  void getAudioTexture(ffi.Pointer<ffi.Float> samples) {
    return _getAudioTexture(samples);
  }

  late final _getAudioTexturePtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Pointer<ffi.Float>)>>(
          'getAudioTexture');
  late final _getAudioTexture =
      _getAudioTexturePtr.asFunction<void Function(ffi.Pointer<ffi.Float>)>();

  /// Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up (the last one will be lost).
  ///
  /// [samples]
  PlayerErrors getAudioTexture2D(ffi.Pointer<ffi.Pointer<ffi.Float>> samples) {
    if (samples == ffi.nullptr) return PlayerErrors.nullPointer;
    final ret = _getAudioTexture2D(samples);
    return PlayerErrors.values[ret];
  }

  late final _getAudioTexture2DPtr = _lookup<
      ffi.NativeFunction<
          ffi.Int32 Function(
              ffi.Pointer<ffi.Pointer<ffi.Float>>)>>('getAudioTexture2D');
  late final _getAudioTexture2D = _getAudioTexture2DPtr
      .asFunction<int Function(ffi.Pointer<ffi.Pointer<ffi.Float>>)>();

  /// Get the sound length in seconds
  ///
  /// [soundHash] the sound hash
  /// Returns sound length in seconds
  double getLength(int soundHash) {
    return _getLength(soundHash);
  }

  late final _getLengthPtr =
      _lookup<ffi.NativeFunction<ffi.Double Function(ffi.UnsignedInt)>>(
          'getLength');
  late final _getLength = _getLengthPtr.asFunction<double Function(int)>();

  /// Seek playing in [time] seconds
  /// [time]
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  int seek(int handle, double time) {
    return _seek(handle, time);
  }

  late final _seekPtr = _lookup<
          ffi.NativeFunction<ffi.Int32 Function(ffi.UnsignedInt, ffi.Float)>>(
      'seek');
  late final _seek = _seekPtr.asFunction<int Function(int, double)>();

  /// Get current sound position  in seconds
  ///
  /// [handle] the sound handle
  /// Returns time in seconds
  double getPosition(int handle) {
    return _getPosition(handle);
  }

  late final _getPositionPtr =
      _lookup<ffi.NativeFunction<ffi.Double Function(ffi.UnsignedInt)>>(
          'getPosition');
  late final _getPosition = _getPositionPtr.asFunction<double Function(int)>();

  /// Check if a handle is still valid.
  ///
  /// [handle] handle to check
  /// Return true if it still exists
  bool getIsValidVoiceHandle(int handle) {
    return _getIsValidVoiceHandle(handle) == 1 ? true : false;
  }

  late final _getIsValidVoiceHandlePtr =
      _lookup<ffi.NativeFunction<ffi.Int Function(ffi.UnsignedInt)>>(
          'getIsValidVoiceHandle');
  late final _getIsValidVoiceHandle =
      _getIsValidVoiceHandlePtr.asFunction<int Function(int)>();

  /////////////////////////////////////////
  /// 3D audio methods
  /////////////////////////////////////////

  /// play3d() is the 3d version of the play() call
  ///
  /// Returns the handle of the sound, 0 if error
  int play3d(int soundHash, double posX, double posY, double posZ,
      {double velX = 0,
      double velY = 0,
      double velZ = 0,
      double volume = 1,
      bool paused = false}) {
    return _play3d(
        soundHash, posX, posY, posZ, velX, velY, velZ, volume, paused ? 1 : 0);
  }

  late final _play3dPtr = _lookup<
      ffi.NativeFunction<
          ffi.UnsignedInt Function(
              ffi.UnsignedInt,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Int)>>('play3d');
  late final _play3d = _play3dPtr.asFunction<
      int Function(
          int, double, double, double, double, double, double, double, int)>();

  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  void set3dSoundSpeed(
    double speed,
  ) {
    return _set3dSoundSpeed(speed);
  }

  late final _set3dSoundSpeedPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Float)>>(
          'set3dSoundSpeed');
  late final _set3dSoundSpeed =
      _set3dSoundSpeedPtr.asFunction<void Function(double)>();

  /// Get the sound speed
  double get3dSoundSpeed() {
    return _get3dSoundSpeed();
  }

  late final _get3dSoundSpeedPtr =
      _lookup<ffi.NativeFunction<ffi.Float Function()>>('get3dSoundSpeed');
  late final _get3dSoundSpeed =
      _get3dSoundSpeedPtr.asFunction<double Function()>();

  /// You can set the position, at-vector, up-vector and velocity
  /// parameters of the 3d audio listener with one call
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
      double velocityZ) {
    return _set3dListenerParameters(posX, posY, posZ, atX, atY, atZ, upX, upY,
        upZ, velocityX, velocityY, velocityZ);
  }

  late final _set3dListenerParametersPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float,
              ffi.Float)>>('set3dListenerParameters');
  late final _set3dListenerParameters = _set3dListenerParametersPtr.asFunction<
      void Function(double, double, double, double, double, double, double,
          double, double, double, double, double)>();

  /// You can set the position parameter of the 3d audio listener
  void set3dListenerPosition(double posX, double posY, double posZ) {
    return _set3dListenerPosition(posX, posY, posZ);
  }

  late final _set3dListenerPositionPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Float, ffi.Float, ffi.Float)>>('set3dListenerPosition');
  late final _set3dListenerPosition = _set3dListenerPositionPtr
      .asFunction<void Function(double, double, double)>();

  /// You can set the "at" vector parameter of the 3d audio listener
  void set3dListenerAt(double atX, double atY, double atZ) {
    return _set3dListenerAt(atX, atY, atZ);
  }

  late final _set3dListenerAtPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Float, ffi.Float, ffi.Float)>>('set3dListenerAt');
  late final _set3dListenerAt =
      _set3dListenerAtPtr.asFunction<void Function(double, double, double)>();

  /// You can set the "up" vector parameter of the 3d audio listener
  void set3dListenerUp(double upX, double upY, double upZ) {
    return _set3dListenerUp(upX, upY, upZ);
  }

  late final _set3dListenerUpPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Float, ffi.Float, ffi.Float)>>('set3dListenerUp');
  late final _set3dListenerUp =
      _set3dListenerUpPtr.asFunction<void Function(double, double, double)>();

  /// You can set the listener's velocity vector parameter
  void set3dListenerVelocity(
      double velocityX, double velocityY, double velocityZ) {
    return _set3dListenerVelocity(velocityX, velocityY, velocityZ);
  }

  late final _set3dListenerVelocityPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Float, ffi.Float, ffi.Float)>>('set3dListenerVelocity');
  late final _set3dListenerVelocity = _set3dListenerVelocityPtr
      .asFunction<void Function(double, double, double)>();

  /// You can set the position and velocity parameters of a live
  /// 3d audio source with one call
  void set3dSourceParameters(int handle, double posX, double posY, double posZ,
      double velocityX, double velocityY, double velocityZ) {
    return _set3dSourceParameters(
        handle, posX, posY, posZ, velocityX, velocityY, velocityZ);
  }

  late final _set3dSourceParametersPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(ffi.UnsignedInt, ffi.Float, ffi.Float, ffi.Float,
              ffi.Float, ffi.Float, ffi.Float)>>('set3dSourceParameters');
  late final _set3dSourceParameters = _set3dSourceParametersPtr.asFunction<
      void Function(int, double, double, double, double, double, double)>();

  /// You can set the position parameters of a live 3d audio source
  void set3dSourcePosition(int handle, double posX, double posY, double posZ) {
    return _set3dSourcePosition(handle, posX, posY, posZ);
  }

  late final _set3dSourcePositionPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(ffi.UnsignedInt, ffi.Float, ffi.Float,
              ffi.Float)>>('set3dSourcePosition');
  late final _set3dSourcePosition = _set3dSourcePositionPtr
      .asFunction<void Function(int, double, double, double)>();

  /// You can set the velocity parameters of a live 3d audio source
  void set3dSourceVelocity(
      int handle, double velocityX, double velocityY, double velocityZ) {
    return _set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
  }

  late final _set3dSourceVelocityPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(ffi.UnsignedInt, ffi.Float, ffi.Float,
              ffi.Float)>>('set3dSourceVelocity');
  late final _set3dSourceVelocity = _set3dSourceVelocityPtr
      .asFunction<void Function(int, double, double, double)>();

  /// You can set the minimum and maximum distance parameters
  /// of a live 3d audio source
  void set3dSourceMinMaxDistance(
      int handle, double minDistance, double maxDistance) {
    return _set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
  }

  late final _set3dSourceMinMaxDistancePtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(ffi.UnsignedInt, ffi.Float,
              ffi.Float)>>('set3dSourceMinMaxDistance');
  late final _set3dSourceMinMaxDistance = _set3dSourceMinMaxDistancePtr
      .asFunction<void Function(int, double, double)>();

  /// You can change the attenuation model and rolloff factor parameters of
  /// a live 3d audio source.
  ///
  /// NO_ATTENUATION 	      No attenuation
  /// INVERSE_DISTANCE 	    Inverse distance attenuation model
  /// LINEAR_DISTANCE 	    Linear distance attenuation model
  /// EXPONENTIAL_DISTANCE 	Exponential distance attenuation model
  ///
  /// see https://solhsa.com/soloud/concepts3d.html
  void set3dSourceAttenuation(
    int handle,
    int attenuationModel,
    double attenuationRolloffFactor,
  ) {
    return _set3dSourceAttenuation(
      handle,
      attenuationModel,
      attenuationRolloffFactor,
    );
  }

  late final _set3dSourceAttenuationPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(ffi.UnsignedInt, ffi.UnsignedInt,
              ffi.Float)>>('set3dSourceAttenuation');
  late final _set3dSourceAttenuation =
      _set3dSourceAttenuationPtr.asFunction<void Function(int, int, double)>();

  /// You can change the doppler factor of a live 3d audio source
  void set3dSourceDopplerFactor(int handle, double dopplerFactor) {
    return _set3dSourceDopplerFactor(handle, dopplerFactor);
  }

  late final _set3dSourceDopplerFactorPtr = _lookup<
          ffi.NativeFunction<ffi.Void Function(ffi.UnsignedInt, ffi.Float)>>(
      'set3dSourceDopplerFactor');
  late final _set3dSourceDopplerFactor =
      _set3dSourceDopplerFactorPtr.asFunction<void Function(int, double)>();

  /// internal test. Does nothing now
  void test() {
    return _test();
  }

  late final _testPtr =
      _lookup<ffi.NativeFunction<ffi.Void Function()>>('test');
  late final _test = _testPtr.asFunction<void Function()>();
}

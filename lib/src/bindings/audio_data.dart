import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/audio_data_ffi.dart'
    if (dart.library.js_interop) 'audio_data_web.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:meta/meta.dart';

/// The way the audio data should be acquired.
///
/// Every time [AudioData.updateSamples] is called it is possible to query the
/// acquired new audio data using [AudioData.getAudioData]. The latter method
/// returns a [Float32List] containing the audio data in the way specified by
/// [GetSamplesKind] enum.
enum GetSamplesKind {
  /// Get the 256 float of wave audio data.
  wave,

  /// Get data in a linear manner: the first 256 floats are audio FFI values,
  /// the other 256 are audio wave samples.
  linear,

  /// Get data in a 2D way. The resulting data will be a matrix of 512
  /// [linear] rows. Each time the [AudioData.updateSamples] method is called,
  /// the last row is discarded and the new one will be the first.
  texture,
}

/// Class to manage audio samples.
///
/// The `visualization` must be enabled to be able to acquire data from the
/// player. You can achieve this by calling
/// `SoLoud.instance.setVisualizationEnabled(true);`.
///
/// Audio samples can be get from the player in three ways. See [GetSamplesKind]
/// for more information.
///
/// IMPORTANT: remember to call [dispose] method when there is no more need
/// to acquire audio.
///
/// After calling [updateSamples] it's possible to call [AudioData.getAudioData]
/// to have back the audio samples. For example, using a "Ticker"
/// in a Widget that needs the audio data to be displayed:
/// ```dart
/// ...
/// late final Ticker ticker;
/// late final AudioData audioData;
/// late final double waveData;
/// late final double fftData;
///
/// @override
/// void initState() {
///   super.initState();
///   audioData = AudioData(GetSamplesKind.linear);
///   ticker = createTicker(_tick);
///   ticker.start();
/// }
///
/// @override
/// void dispose() {
///   ticker.stop();
///   audioData.dispose();
///   super.dispose();
/// }
///
/// void _tick(Duration elapsed) {
///   if (context.mounted) {
///     try {
///       audioData.updateSamples();
///       setState(() {});
///     } on Exception catch (e) {
///       debugPrint('$e');
///     }
///   }
/// }
/// ```
/// Then in your "build" method, you can read the audio data:
/// ```dart
/// try {
///   /// Since we are used [GetSamplesKind.linear], `samples` will contain
///   /// 512 floats: the first 256 are FFT values, the other 256 are wave values
///   final samples = audioData.getAudioData();
///   Float32List ffiData = samples.sublist(0, 256);
///   Float32List waveData = samples.sublist(256, 512);
///   /// Do something with `ffiData` and `waveData`
/// } on Exception catch (e) {
///   debugPrint('$e');
/// }
/// ```
///
/// To smooth FFT values use [SoLoud.setFftSmoothing].
class AudioData {
  /// Initialize the way the audio data should be acquired.
  AudioData(
    this._getSamplesKind,
  ) : ctrl = AudioDataCtrl() {
    _init();
    ctrl.allocSamples(this);
  }

  void _init() {
    switch (_getSamplesKind) {
      case GetSamplesKind.wave:
        _updateCallback = ctrl.waveCallback;
      case GetSamplesKind.linear:
        _updateCallback = ctrl.textureCallback;
      case GetSamplesKind.texture:
        _updateCallback = ctrl.texture2DCallback;
    }
  }

  /// The controller used to allocate, dispose and get audio data.
  @internal
  final AudioDataCtrl ctrl;

  /// Kind of audio samples. See [GetSamplesKind].
  GetSamplesKind _getSamplesKind;

  /// The current type of data to acquire.
  GetSamplesKind get getSamplesKind => _getSamplesKind;

  /// The callback used to get new audio samples.
  /// This callback is used in [updateSamples] to avoid to
  /// do [GetSamplesKind] checks on every calls.
  late bool Function(AudioData) _updateCallback;

  /// Update the content of samples memory to be read later
  /// using [getAudioData].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  /// Throws [SoLoudVisualizationNotEnabledException] if the visualization
  /// flag is not enableb. Please, Use `setVisualizationEnabled(true)`
  /// when needed.
  /// Throws [SoLoudNullPointerException] something is going wrong with the
  /// player engine. Please, open an issue on
  /// [GitHub](https://github.com/alnitak/flutter_soloud/issues) providing
  /// a simple working example.
  void updateSamples() {
    ctrl.dataIsTheSameAsBefore = _updateCallback(this);
  }

  /// Changes the input device from which to retrieve audio data and its kind.
  void changeSamplesKind(GetSamplesKind newKind) {
    _getSamplesKind = newKind;
    _init();
  }

  /// Dispose the memory allocated to acquire audio data.
  /// Must be called when there is no more need of [AudioData] otherwise memory
  /// leaks will occur.
  void dispose() {
    ctrl.dispose(_getSamplesKind);
  }

  /// Get audio data only for visualization purposes. Don't use this method
  /// for audio processing or for saving audio data.
  ///
  /// Depending on the [GetSamplesKind] used to initialize [AudioData],
  /// the returned data will be a [Float32List]. See [GetSamplesKind] for
  /// more information.
  ///
  /// [alwaysReturnData] if true, the audio data list will be returned even if
  /// it is the same as the previous one.
  /// When this method is callled, the returned audio data is the current
  /// audio buffer shrinked to 256 samples currently playing. For example,
  /// when initializing the engine with a buffer of 2048, this will return
  /// the average of that data grouped by 8 samples. For a good visualization
  /// should be better to use a buffer size of 1024 or maybe 512.
  /// When calling this method 2 times and before all the buffer is played,
  /// the returned data will be the same. The purpose of [alwaysReturnData] is
  /// to avoid this and when this happens, the returned data will be an
  /// empty list when [alwaysReturnData] is false.
  Float32List getAudioData({bool alwaysReturnData = true}) {
    /// Non blocking condition.
    if (!SoLoudController().soLoudFFI.isInited()) {
      return Float32List(0);
    }
    if (!SoLoudController().soLoudFFI.getVisualizationEnabled()) {
      throw const SoLoudVisualizationNotEnabledException();
    }
    switch (_getSamplesKind) {
      case GetSamplesKind.wave:
        return ctrl.getWave(alwaysReturnData: alwaysReturnData);
      case GetSamplesKind.linear:
        return ctrl.getFftAndWave(alwaysReturnData: alwaysReturnData);
      case GetSamplesKind.texture:
        return ctrl.get2DTexture(alwaysReturnData: alwaysReturnData);
    }
  }
}

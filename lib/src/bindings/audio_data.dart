import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';
import 'package:flutter_soloud/src/bindings/audio_data_ffi.dart'
    if (dart.library.js_interop) 'audio_data_web.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:meta/meta.dart';

/// The way the audio data should be acquired.
///
/// Every time [AudioData.updateSamples] is called it is possible to query the
/// acquired new audio data using [AudioData.getLinearFft],
/// [AudioData.getLinearWave], [AudioData.getTexture] or [AudioData.getWave].
enum GetSamplesKind {
  /// Get data in a linear manner: the first 256 floats are audio FFI values,
  /// the other 256 are audio wave samples.
  /// To get the audio data use [AudioData.getLinearFft] or
  /// [AudioData.getLinearWave].
  linear,

  /// Get data in a 2D way. The resulting data will be a matrix of 256
  /// [linear] rows. Each time the [AudioData.updateSamples] method is called,
  /// the last row is discarded and the new one will be the first.
  /// To get the audio data use [AudioData.getTexture].
  texture,

  /// Get the 256 float of wave audio data.
  /// To get the audio data use [AudioData.getWave].
  wave,
}

/// Class to manage audio samples.
///
/// The `visualization` must be enabled to be able to acquire data from the
/// player. You can achieve this by calling
/// `SoLoud.instance.setVisualizationEnabled(true);`.
///
/// Audio samples can be get from the player or from the microphone, and
/// in a texture matrix or a linear array way.
///
/// IMPORTANT: remember to call [dispose] method when there is no more need
/// to acquire audio.
///
/// After calling [updateSamples] it's possible to call the proper getter
/// to have back the audio samples. For example, using a "Ticker"
/// in a Widget that needs the audio data to be displayed:
/// ```
/// ...
/// late final Ticker ticker;
/// late final AudioData audioData;
/// late final double waveData;
/// late final double fftData;
///
/// @override
/// void initState() {
///   super.initState();
///   audioData = AudioData(GetSamplesFrom.player, GetSamplesKind.linear);
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
///     } on Exception {
///       debugPrint('Player not initialized or visualization is not enabled!');
///     }
///   }
/// }
/// ```
/// Then in your "build" method, you can read the audio data:
/// ```
/// try {
///   /// Use [getTexture] if you have inizialized [AudioData]
///   /// with [GetSamplesKind.texture]
///   ffiData = audioData.getLinearFft(i);
///   waveData = audioData.getLinearWave(i);
/// } on Exception {
///   ffiData = 0;
///   waveData = 0;
/// }
/// ```
///
/// To smooth FFT values use [SoLoud.instance.setFftSmoothing].
///
// TODO(all): make AudioData singleton?
@experimental
class AudioData {
  /// Initialize the way the audio data should be acquired.
  AudioData(
    this._getSamplesKind,
  ) : ctrl = AudioDataCtrl() {
    _init();
    ctrl.allocSamples();
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
  late void Function(AudioData) _updateCallback;

  /// Update the content of samples memory to be get with [getWave],
  /// [getLinearFft], [getLinearWave] or [getTexture].
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
    _updateCallback(this);
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

  /// Get the wave data at offset [offset].
  ///
  /// Use this method to get data when using [GetSamplesKind.wave].
  /// The data is composed of 256 floats.
  double getWave(SampleWave offset) {
    if (_getSamplesKind != GetSamplesKind.wave) {
      return 0;
    }

    if (!SoLoudController().soLoudFFI.getVisualizationEnabled()) {
      throw const SoLoudVisualizationNotEnabledException();
    }
    return ctrl.getWave(offset);
  }

  /// Get the FFT audio data at offset [offset].
  ///
  /// Use this method to get FFT data when using [GetSamplesKind.linear].
  /// The data is composed of 256 floats.
  double getLinearFft(SampleLinear offset) {
    if (_getSamplesKind != GetSamplesKind.linear) {
      return 0;
    }

    if (!SoLoudController().soLoudFFI.getVisualizationEnabled()) {
      throw const SoLoudVisualizationNotEnabledException();
    }
    return ctrl.getLinearFft(offset);
  }

  /// Get the wave audio data at offset [offset].
  ///
  /// Use this method to get wave data when using [GetSamplesKind.linear].
  /// The data is composed of 256 floats.
  double getLinearWave(SampleLinear offset) {
    if (_getSamplesKind != GetSamplesKind.linear) {
      return 0;
    }

    if (!SoLoudController().soLoudFFI.getVisualizationEnabled()) {
      throw const SoLoudVisualizationNotEnabledException();
    }
    return ctrl.getLinearWave(offset);
  }

  /// Get the audio data at row [row] and column [column].
  /// Use this method to get data when using [GetSamplesKind.texture].
  /// This matrix represents 256 rows. Each rows is represented by 256 floats
  /// of FFT data and 256 floats of wave data.
  /// Each time the [AudioData.updateSamples] method is called,
  /// the last row is discarded and the new one will be the first.
  double getTexture(SampleRow row, SampleColumn column) {
    if (_getSamplesKind != GetSamplesKind.texture) {
      return 0;
    }

    if (!SoLoudController().soLoudFFI.getVisualizationEnabled()) {
      throw const SoLoudVisualizationNotEnabledException();
    }
    return ctrl.getTexture(row, column);
  }
}

// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';

class AudioDataCtrl {
  late final int _samplesPtr;
  int get samplesPtr => _samplesPtr;

  final void Function(AudioData) waveCallback =
      SoLoudController().soLoudFFI.getWave;

  final void Function(AudioData) texture2DCallback =
      SoLoudController().soLoudFFI.getAudioTexture2D;

  final void Function(AudioData) textureCallback =
      SoLoudController().soLoudFFI.getAudioTexture;

  final void Function(AudioData) captureWaveCallback =
      SoLoudController().captureFFI.getCaptureWave;

  final CaptureErrors Function(AudioData) captureTexture2DCallback =
      SoLoudController().captureFFI.getCaptureAudioTexture2D;

  final void Function(AudioData) captureAudioTextureCallback =
      SoLoudController().captureFFI.getCaptureAudioTexture;

  void allocSamples() {
    _samplesPtr = wasmMalloc(512 * 256 * 4);
  }

  void dispose(
    GetSamplesKind getSamplesKind,
  ) {
    if (_samplesPtr != 0) {
      wasmFree(_samplesPtr);
    }
  }

  double getWave(SampleWave offset) {
    final samplePtr = wasmGetI32Value(_samplesPtr, '*');
    final data = wasmGetF32Value(samplePtr + offset.value * 4, 'float');
    return data;
  }

  double getLinearFft(SampleLinear offset) {
    final data = wasmGetF32Value(_samplesPtr + offset.value * 4, 'float');
    return data;
  }

  double getLinearWave(SampleLinear offset) {
    final data =
        wasmGetF32Value(_samplesPtr + offset.value * 4 + 256 * 4, 'float');
    return data;
  }

  double getTexture(
    GetSamplesFrom getSamplesFrom,
    SampleRow row,
    SampleColumn column,
  ) {
    // final offset = samplesPtr + ((row.value * 256 + column.value) * 4);
    // final data = wasmGetF32Value(offset, 'float');
    final double data;
    if (getSamplesFrom == GetSamplesFrom.player) {
      data = wasmGetTextureValue(row.value, column.value);
    } else {
      data = wasmGetCaptureTextureValue(row.value, column.value);
    }
    return data;
  }
}

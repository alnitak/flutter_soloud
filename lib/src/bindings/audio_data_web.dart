// ignore_for_file: public_member_api_docs

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:meta/meta.dart';

typedef SampleFormat2D = Float32List;
typedef SampleFormat1D = Float32List;

@internal
@immutable
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

  SampleFormat2D allocSample2D() {
    _samplesPtr = wasmMalloc(4);
    return Float32List(512 * 256);
  }

  SampleFormat1D allocSample1D() {
    _samplesPtr = wasmMalloc(4);
    return Float32List(512);
  }

  SampleFormat1D allocSampleWave() {
    _samplesPtr = wasmMalloc(256 * 4);
    return Float32List(256);
  }

  void dispose(
    GetSamplesKind getSamplesKind,
    SampleFormat2D? sWave,
    SampleFormat1D? s1D,
    SampleFormat2D? s2D,
  ) {
    wasmFree(_samplesPtr);
  }

  double getWave(SampleFormat2D s2D, SampleWave offset) {
    final samplePtr = wasmGetI32Value(_samplesPtr, '*');
    final data = wasmGetF32Value(samplePtr + offset.value * 4, 'float');
    return data;
  }

  double getLinearFft(SampleFormat1D s1D, SampleLinear offset) {
    final data = wasmGetF32Value(_samplesPtr + offset.value * 4, 'float');
    return data;
  }

  double getLinearWave(SampleFormat1D s1D, SampleLinear offset) {
    final data =
        wasmGetF32Value(_samplesPtr + offset.value * 4 + 256 * 4, 'float');
    return data;
  }

  double getTexture(
    SampleFormat2D s2D,
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

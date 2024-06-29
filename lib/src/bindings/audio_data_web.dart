import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:meta/meta.dart';
import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';

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
    _samplesPtr = wasmMalloc(512 * 256 * 4);
    return Float32List(512 * 256);
  }

  SampleFormat1D allocSample1D() {
    _samplesPtr = wasmMalloc(512 * 4);
    return Float32List(512);
  }

  SampleFormat1D allocSampleWave() {
    _samplesPtr = wasmMalloc(256 * 4);
    return Float32List(256);
  }

  void dispose(SampleFormat1D s1D, SampleFormat2D s2D) {
    wasmFree(_samplesPtr);
  }

  double getWave(SampleFormat2D s2D, SampleWave offset) {
    final samplePtr = wasmGetI32Value(_samplesPtr, '*');
    return wasmGetF32Value(samplePtr + offset.value * 4, 'float');
  }

  double getLinear(SampleFormat1D s1D, SampleLinear offset) {
    final data = wasmGetF32Value(_samplesPtr + offset.value * 4, 'float');
    return data;
  }

  double getTexture(SampleFormat2D s2D, SampleRow row, SampleColumn column) {
    final rowPtr = wasmGetI32Value(_samplesPtr + row.value * 4, '*');
    return wasmGetF32Value(rowPtr + column.value * 4, 'float');
  }

  bool isEmptyLinear(SampleFormat1D s1D) => s1D.isEmpty;

  bool isEmptyTexture(SampleFormat1D s2D) => s2D.isEmpty;

  bool isEmptyWave(SampleFormat1D s2D) => s2D.isEmpty;
}

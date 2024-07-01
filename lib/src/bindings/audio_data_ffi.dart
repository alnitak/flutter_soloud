import 'dart:ffi';

import 'package:ffi/ffi.dart' show calloc;
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:meta/meta.dart';
import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';

typedef SampleFormat2D = Pointer<Pointer<Float>>;
typedef SampleFormat1D = Pointer<Float>;

@internal
class AudioDataCtrl {
  /// To reflect [AudioDataCtrl] for web. Not used with `dart:ffi`
  final int _samplesPtr = 0;
  int get samplesPtr => _samplesPtr;

  final void Function(AudioData) waveCallback =
      SoLoudController().soLoudFFI.getWave;

  final PlayerErrors Function(AudioData) texture2DCallback =
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
    return calloc();
  }

  SampleFormat1D allocSample1D() {
    return calloc(512 * 4);
  }

  SampleFormat2D allocSampleWave() {
    return calloc();
  }

  void dispose(
    GetSamplesKind getSamplesKind,
    SampleFormat2D? sWave,
    SampleFormat1D? s1D,
    SampleFormat2D? s2D,
  ) {
    if (getSamplesKind == GetSamplesKind.wave &&
        sWave != null &&
        sWave != nullptr) {
      calloc.free(sWave);
    }
    if (getSamplesKind == GetSamplesKind.linear &&
        s1D != null &&
        s1D != nullptr) {
      calloc.free(s1D);
    }
    if (getSamplesKind == GetSamplesKind.texture &&
        s2D != null &&
        s2D != nullptr) {
      calloc.free(s2D);
    }
  }

  double getWave(SampleFormat2D s2D, SampleWave offset) {
    final val = Pointer<Float>.fromAddress(s2D.value.address);
    if (val == nullptr) return 0;
    return val[offset.value];
  }

  double getLinearFft(SampleFormat1D s1D, SampleLinear offset) {
    return s1D[offset.value];
  }

  double getLinearWave(SampleFormat1D s1D, SampleLinear offset) {
    return s1D[offset.value + 256];
  }

  double getTexture(
    SampleFormat2D s2D,
    GetSamplesFrom getSamplesFrom,
    SampleRow row,
    SampleColumn column,
  ) {
    const stride = 512;
    final val = s2D.value;
    if (val == nullptr) return 0;
    return val[stride * row.value + column.value];
  }
}

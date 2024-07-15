// ignore_for_file: public_member_api_docs

import 'dart:ffi';

import 'package:ffi/ffi.dart' show calloc;
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';

class AudioDataCtrl {
  /// To reflect [AudioDataCtrl] for web. Not used with `dart:ffi`
  final int _samplesPtr = 0;
  int get samplesPtr => _samplesPtr;

  /// Where the FFT or wave data is stored.
  late Pointer<Pointer<Float>> samplesWave;

  /// Where the audio 2D data is stored.
  late Pointer<Pointer<Float>> samples2D;

  /// Where the audio 1D data is stored.
  late Pointer<Float> samples1D;

  final void Function(AudioData) waveCallback =
      SoLoudController().soLoudFFI.getWave;

  final PlayerErrors Function(AudioData) texture2DCallback =
      SoLoudController().soLoudFFI.getAudioTexture2D;

  final void Function(AudioData) textureCallback =
      SoLoudController().soLoudFFI.getAudioTexture;

  void allocSamples() {
    samples2D = calloc();
    samples1D = calloc(512 * 4);
    samplesWave = calloc();
  }

  void dispose(
    GetSamplesKind getSamplesKind,
  ) {
    if (samplesWave != nullptr) calloc.free(samplesWave);
    if (samples1D != nullptr) calloc.free(samples1D);
    if (samples2D != nullptr) calloc.free(samples2D);
  }

  double getWave(SampleWave offset) {
    final val = Pointer<Float>.fromAddress(samplesWave.value.address);
    if (val == nullptr) return 0;
    return val[offset.value];
  }

  double getLinearFft(SampleLinear offset) {
    return samples1D[offset.value];
  }

  double getLinearWave(SampleLinear offset) {
    return samples1D[offset.value + 256];
  }

  double getTexture(
    SampleRow row,
    SampleColumn column,
  ) {
    const stride = 512;
    final val = samples2D.value;
    if (val == nullptr) return 0;
    return val[stride * row.value + column.value];
  }
}

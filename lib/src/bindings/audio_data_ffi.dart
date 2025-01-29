// ignore_for_file: public_member_api_docs

import 'dart:ffi';

import 'package:ffi/ffi.dart' show calloc;
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/audio_data.dart';
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

  final void Function(AudioData) textureCallback =
      SoLoudController().soLoudFFI.getAudioTexture;

  final PlayerErrors Function(AudioData) texture2DCallback =
      SoLoudController().soLoudFFI.getAudioTexture2D;

  void allocSamples(AudioData audioData) {
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

  Float32List getWave() {
    final val = Pointer<Float>.fromAddress(samplesWave.value.address);
    if (val == nullptr) return Float32List(0);
    return Float32List.view(
      val.cast<Uint8>().asTypedList(256 * 4).buffer,
      0,
      256,
    );
  }

  Float32List getFftAndWave() {
    final val = Pointer<Float>.fromAddress(samples1D.address);
    if (val == nullptr) return Float32List(0);
    return Float32List.view(
      val.cast<Uint8>().asTypedList(512 * 4).buffer,
      0,
      512,
    );
  }

  Float32List get2DTexture() {
    final val = Pointer<Float>.fromAddress(samples1D.address);
    if (val == nullptr) return Float32List(0);
    return Float32List.view(
      val.cast<Uint8>().asTypedList(512 * 256 * 4).buffer,
      0,
      512 * 256,
    );
  }
}

// ignore_for_file: public_member_api_docs

import 'dart:ffi';

import 'package:ffi/ffi.dart' show calloc;
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

class AudioDataCtrl {
  /// To reflect [AudioDataCtrl] for web. Not used with `dart:ffi`
  final int _samplesPtr = 0;
  int get samplesPtr => _samplesPtr;

  /// Where the FFT or wave data is stored.
  late Pointer<Pointer<Float>> samplesWave;

  /// Where the audio 2D data is stored.
  late Pointer<Pointer<Float>> samples2D;

  /// Where the audio 1D data is stored.
  late Pointer<Pointer<Float>> samples1D;

  final bool Function(AudioData) waveCallback =
      SoLoudController().soLoudFFI.getWave;

  final bool Function(AudioData) textureCallback =
      SoLoudController().soLoudFFI.getAudioTexture;

  final bool Function(AudioData) texture2DCallback =
      SoLoudController().soLoudFFI.getAudioTexture2D;

  late bool dataIsTheSameAsBefore;

  void allocSamples(AudioData audioData) {
    dataIsTheSameAsBefore = false;
    samples2D = calloc();
    samples1D = calloc();
    samplesWave = calloc();
  }

  void dispose(
    GetSamplesKind getSamplesKind,
  ) {
    if (samplesWave != nullptr) calloc.free(samplesWave);
    if (samples1D != nullptr) calloc.free(samples1D);
    if (samples2D != nullptr) calloc.free(samples2D);
    samplesWave = nullptr;
    samples1D = nullptr;
    samples2D = nullptr;
  }

  Float32List getWave({bool alwaysReturnData = true}) {
    final wavePtr = samplesWave.value;
    if (!alwaysReturnData && dataIsTheSameAsBefore || wavePtr == nullptr) {
      return Float32List(0);
    }

    return Float32List.view(
      wavePtr.cast<Uint8>().asTypedList(256 * 4).buffer,
      0,
      256,
    );
  }

  Float32List getFftAndWave({bool alwaysReturnData = true}) {
    final texturePtr = samples1D.value;

    if (!alwaysReturnData && dataIsTheSameAsBefore || texturePtr == nullptr) {
      return Float32List(0);
    }
    return Float32List.view(
      texturePtr.cast<Uint8>().asTypedList(512 * 4).buffer,
      0,
      512,
    );
  }

  Float32List get2DTexture({bool alwaysReturnData = true}) {
    final texture2DPtr = samples2D.value;

    if (!alwaysReturnData && dataIsTheSameAsBefore || texture2DPtr == nullptr) {
      return Float32List(0);
    }

    final ret = Float32List.view(
      texture2DPtr.cast<Uint8>().asTypedList(512 * 256 * 4).buffer,
      0,
      512 * 256,
    );

    return ret;
  }
}

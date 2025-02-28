// ignore_for_file: public_member_api_docs

import 'dart:js_interop';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

class AudioDataCtrl {
  late final int _samplesPtr;
  int get samplesPtr => _samplesPtr;

  late int _samplePtrPtr;

  final void Function(AudioData) waveCallback =
      SoLoudController().soLoudFFI.getWave;

  final void Function(AudioData) texture2DCallback =
      SoLoudController().soLoudFFI.getAudioTexture2D;

  final void Function(AudioData) textureCallback =
      SoLoudController().soLoudFFI.getAudioTexture;

  void allocSamples(AudioData audioData) {
    /// This is the max amount of memory [_samplePtr] may need. This number
    /// is needed when acquiring data with [get2DTexture] which is a matrix of
    /// 256 rows and 512 columns of floats (4 bytes each).
    _samplesPtr = wasmMalloc(512 * 256 * 4);

    /// Initialize the pointer to the pointer of the samples. This can be done
    /// only after calling once the get* method.
    if (audioData.getSamplesKind == GetSamplesKind.wave) {
      SoLoudController().soLoudFFI.getWave(audioData);
      _samplePtrPtr = wasmGetI32Value(_samplesPtr, '*');
    }

    if (audioData.getSamplesKind == GetSamplesKind.texture) {
      SoLoudController().soLoudFFI.getAudioTexture2D(audioData);
      _samplePtrPtr = wasmGetI32Value(_samplesPtr, '*');
    }
  }

  void dispose(
    GetSamplesKind getSamplesKind,
  ) {
    if (_samplesPtr != 0) {
      wasmFree(_samplesPtr);
    }
  }

  Float32List getWave() {
    // final samplesPtrPtr = wasmGetI32Value(_samplesPtr, '*');
    // Convert the JSArrayBuffer to a Dart Float32List
    return wasmHeapU8Buffer.toDart.asFloat32List(_samplePtrPtr, 256);
  }

  Float32List getFftAndWave() {
    // Convert the JSArrayBuffer to a Dart Float32List
    return wasmHeapU8Buffer.toDart.asFloat32List(_samplesPtr, 512);
  }

  Float32List get2DTexture() {
    // Convert the JSArrayBuffer to a Dart Float32List
    return wasmHeapU8Buffer.toDart.asFloat32List(_samplePtrPtr, 512 * 256);
  }
}

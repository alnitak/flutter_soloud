import 'dart:typed_data';
import 'dart:js_interop';

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/bindings_capture.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/bindings/js_extension.dart';

class FlutterCaptureWeb extends FlutterCapture {
  @override
  List<CaptureDevice> listCaptureDevices() {
    /// allocate 50 device strings
    final namesPtr = wasmMalloc(50 * 150);
    final isDefaultPtr = wasmMalloc(50 * 4);
    final nDevicesPtr = wasmMalloc(4); // 4 bytes for an int

    wasmListCaptureDevices(
      namesPtr,
      isDefaultPtr,
      nDevicesPtr,
    );

    final nDevices = wasmGetI32Value(nDevicesPtr, '*');
    final devices = <CaptureDevice>[];
    for (var i = 0; i < nDevices; i++) {
      final namePtr = wasmGetI32Value(namesPtr + i * 4, '*');
      final name = wasmUtf8ToString(namePtr);
      final isDefault = wasmGetI32Value(
          wasmGetI32Value(isDefaultPtr + i * 4, '*'), '*');

      devices.add(CaptureDevice(name, isDefault == 1));
    }

    wasmFreeListCaptureDevices(namesPtr, isDefaultPtr, nDevices);

    wasmFree(nDevicesPtr);
    wasmFree(isDefaultPtr);
    wasmFree(namesPtr);

    return devices;
  }

  @override
  CaptureErrors initCapture(int deviceID) {
    final e = wasmInitCapture(deviceID);
    return CaptureErrors.values[e];
  }

  @override
  void disposeCapture() {
    return wasmDisposeCapture();
  }

  @override
  bool isCaptureInited() {
    return wasmIsCaptureInited() == 1 ? true : false;
  }

  @override
  bool isCaptureStarted() {
    return wasmIsCaptureStarted() == 1 ? true : false;
  }

  @override
  CaptureErrors startCapture() {
    return CaptureErrors.values[wasmStartCapture()];
  }

  @override
  CaptureErrors stopCapture() {
    return CaptureErrors.values[wasmStopCapture()];
  }

  @override
  void getCaptureFft(AudioData fft) {
    return wasmGetCaptureFft(fft.ctrl.samplesPtr);
  }

  @override
  void getCaptureWave(AudioData wave) {
    return wasmGetCaptureWave(wave.ctrl.samplesPtr);
  }

  @override
  void getCaptureAudioTexture(AudioData samples) {
    wasmGetCaptureAudioTexture(samples.ctrl.samplesPtr);
  }

  @override
  CaptureErrors getCaptureAudioTexture2D(AudioData samples) {
    final e = wasmGetCaptureAudioTexture2D(samples.ctrl.samplesPtr);
    return CaptureErrors.values[e];
  }

  @override
  CaptureErrors setCaptureFftSmoothing(double smooth) {
    final e = wasmSetCaptureFftSmoothing(smooth);
    return CaptureErrors.values[e];
  }
}

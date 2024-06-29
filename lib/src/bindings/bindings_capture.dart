// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:meta/meta.dart';

export 'package:flutter_soloud/src/bindings/bindings_capture_ffi.dart'
    if (dart.library.html) 'package:flutter_soloud/src/bindings/bindings_capture_web.dart';

/// The experimenta functionality to use the microphone used by "SoLoudCapture".
@experimental
abstract class FlutterCapture {
  @mustBeOverridden
  List<CaptureDevice> listCaptureDevices();

  @mustBeOverridden
  CaptureErrors initCapture(int deviceID);

  @mustBeOverridden
  void disposeCapture(); 

  @mustBeOverridden
  bool isCaptureInited();

  @mustBeOverridden
  bool isCaptureStarted();

  @mustBeOverridden
  CaptureErrors startCapture();

  @mustBeOverridden
  CaptureErrors stopCapture();

  @mustBeOverridden
  void getCaptureFft(AudioData fft);

  @mustBeOverridden
  void getCaptureWave(AudioData wave);

  @mustBeOverridden
  void getCaptureAudioTexture(AudioData samples);

  @mustBeOverridden
  CaptureErrors getCaptureAudioTexture2D(AudioData samples);

  @mustBeOverridden
  CaptureErrors setCaptureFftSmoothing(double smooth);
}

import 'package:flutter_soloud/src/bindings/bindings_capture_web.dart';

import 'bindings_player_web.dart';

/// Controller that expose method channel and FFI
class SoLoudController {
  factory SoLoudController() => _instance ??= SoLoudController._();

  SoLoudController._();

  static SoLoudController? _instance;

  final FlutterSoLoudWeb _soLoudFFI = FlutterSoLoudWeb();

  FlutterSoLoudWeb get soLoudFFI => _soLoudFFI;

  final FlutterCaptureWeb _captureFFI = FlutterCaptureWeb();

  FlutterCaptureWeb get captureFFI => _captureFFI;
}

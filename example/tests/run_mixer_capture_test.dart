import 'dart:io' if (dart.library.js_interop) 'dart_io_stub.dart';

import 'package:flutter/material.dart';

import 'tests/mixer_output_capture.dart'
    if (dart.library.js_interop) 'tests/mixer_output_capture_web.dart';
import 'web_runner.dart'
    if (dart.library.js_interop) 'web_runner_web.dart';

/// One-shot test runner for mixer output capture.
///
/// Run with: flutter run -d macos tests/run_mixer_capture_test.dart
/// Run on web with: flutter run -d chrome --wasm -t tests/run_mixer_capture_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await runWithUserGesture(() async {
    try {
      final output = await testMixerOutputCapture();
      // ignore: avoid_print
      print('MIXER_CAPTURE_TEST_PASSED');
      // ignore: avoid_print
      print(output);
      exit(0);
    } catch (e, st) {
      // ignore: avoid_print
      print('MIXER_CAPTURE_TEST_FAILED: $e');
      // ignore: avoid_print
      print(st);
      exit(1);
    }
  });
}

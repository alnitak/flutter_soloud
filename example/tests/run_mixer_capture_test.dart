import 'dart:io';

import 'package:flutter/material.dart';

import 'tests/mixer_output_capture.dart';

/// One-shot test runner for mixer output capture.
///
/// Run with: flutter run -d macos tests/run_mixer_capture_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

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
}

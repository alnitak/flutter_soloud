import 'dart:io';

import 'package:flutter/material.dart';

import 'tests/mixer_capture_save_file.dart';

/// One-shot test runner that writes Opus/Vorbis capture files to /tmp.
///
/// Run with: flutter run -d macos tests/run_mixer_capture_save_file.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await testMixerCaptureSaveFile();
    // ignore: avoid_print
    print('MIXER_CAPTURE_SAVE_FILE_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('MIXER_CAPTURE_SAVE_FILE_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    exit(1);
  }
}

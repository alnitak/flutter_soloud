import 'dart:io' if (dart.library.js_interop) 'dart_io_stub.dart';

import 'package:flutter/material.dart';

import 'tests/in_buffer_seek_test.dart' as in_buffer_seek;

/// One-shot runner for the in-buffer pull-buffer seek test.
///
/// Run with: flutter run -d macos tests/run_in_buffer_seek_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await in_buffer_seek.testInBufferSeek();
    // ignore: avoid_print
    print('IN_BUFFER_SEEK_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('IN_BUFFER_SEEK_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    exit(1);
  }
}

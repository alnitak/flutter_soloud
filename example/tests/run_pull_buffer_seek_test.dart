import 'dart:io' if (dart.library.js_interop) 'dart_io_stub.dart';

import 'package:flutter/material.dart';

import 'tests/pull_buffer_seek_test.dart' as pull_buffer_seek;

/// One-shot runner for the out-of-buffer pull-buffer seek test.
///
/// Run with: flutter run -d macos tests/run_pull_buffer_seek_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await pull_buffer_seek.testPullBufferSeek();
    // ignore: avoid_print
    print('PULL_BUFFER_SEEK_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('PULL_BUFFER_SEEK_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    exit(1);
  }
}

import 'dart:io' if (dart.library.js_interop) 'dart_io_stub.dart';

import 'package:flutter/material.dart';

import 'tests/pull_buffer_file_stream_test.dart' as pull_buffer_file_stream;

/// One-shot test runner for pull-buffer file streaming.
///
/// Run with: flutter run -d macos tests/run_pull_buffer_file_stream_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await pull_buffer_file_stream.testPullBufferFileStream();
    // ignore: avoid_print
    print('PULL_BUFFER_FILE_STREAM_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('PULL_BUFFER_FILE_STREAM_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    exit(1);
  }
}

import 'dart:io' if (dart.library.js_interop) 'dart_io_stub.dart';

import 'package:flutter/material.dart';

import 'tests/pull_buffer_tiny_test.dart'
    if (dart.library.js_interop) 'tests/pull_buffer_tiny_test_web.dart'
    as pull_buffer_tiny;

/// One-shot test runner for tiny pull-buffer streaming.
///
/// Run with: flutter run -d macos tests/run_pull_buffer_tiny_test.dart
/// Run on web with: flutter run -d chrome --wasm -t tests/run_pull_buffer_tiny_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await pull_buffer_tiny.testPullBufferTiny();
    // ignore: avoid_print
    print('PULL_BUFFER_TINY_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('PULL_BUFFER_TINY_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    exit(1);
  }
}

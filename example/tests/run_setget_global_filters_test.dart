import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'tests/global_filters.dart';

/// One-shot test runner for set/get global filters.
///
/// Run with: flutter run -d macos tests/run_setget_global_filters_test.dart
/// Web: flutter run -d chrome --wasm tests/run_setget_global_filters_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await testSetGetGlobalFilters();
    // ignore: avoid_print
    print('SETGET_GLOBAL_FILTERS_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    if (!kIsWeb) exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('SETGET_GLOBAL_FILTERS_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    if (!kIsWeb) exit(1);
  }
}

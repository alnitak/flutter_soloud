import 'package:flutter/material.dart';

import 'exit_app.dart';
import 'tests/play_clocked.dart';
import 'tests/play_scheduled.dart';

/// One-shot test runner for the playClocked and playScheduled accuracy
/// tests.
///
/// Run with: flutter run -d macos tests/run_scheduled_tests.dart
/// Run on web with: flutter run -d chrome --wasm -t tests/run_scheduled_tests.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  var failed = false;
  for (final (name, run) in [
    ('PLAY_CLOCKED', testPlayClocked),
    ('PLAY_SCHEDULED', testPlayScheduled),
  ]) {
    try {
      final output = await run();
      // ignore: avoid_print
      print('${name}_TEST_PASSED');
      // ignore: avoid_print
      print(output);
    } catch (e, st) {
      failed = true;
      // ignore: avoid_print
      print('${name}_TEST_FAILED: $e');
      // ignore: avoid_print
      print(st);
    }
  }
  exitApp(failed ? 1 : 0);
}

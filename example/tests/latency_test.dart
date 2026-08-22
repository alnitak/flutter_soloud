import 'package:flutter/widgets.dart';

import 'exit_app.dart';
import 'tests/latency_test.dart';

/// Standalone entry point for latency_test.
///
/// Run with:
///   flutter run -d macos -t tests/latency_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await testLatency();
    debugPrint(output.toString());
    debugPrint('TEST_PASSED LatencyTest');
    exitApp(0);
  } catch (e, st) {
    debugPrint('$st');
    debugPrint('TEST_FAILED LatencyTest: $e');
    exitApp(1);
  }
}

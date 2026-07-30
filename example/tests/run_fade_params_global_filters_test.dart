import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'tests/global_filters.dart';

/// One-shot test runner for fading global filter parameters.
///
/// Run with: flutter run -d macos tests/run_fade_params_global_filters_test.dart
/// Web: flutter run -d chrome --wasm tests/run_fade_params_global_filters_test.dart
void main() {
  WidgetsFlutterBinding.ensureInitialized();

  if (kIsWeb) {
    // On the Web the browser suspends the AudioContext until a user
    // gesture and SoLoud advances the fades inside the audio callback.
    // Hence a tap is needed before running the test.
    runApp(const _RunnerApp());
    return;
  }

  _runTest();
}

Future<void> _runTest() async {
  try {
    final output = await testFadeParamsGlobalFilters();
    // ignore: avoid_print
    print('FADE_PARAMS_GLOBAL_FILTERS_TEST_PASSED');
    // ignore: avoid_print
    print(output);
    if (!kIsWeb) exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('FADE_PARAMS_GLOBAL_FILTERS_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    if (!kIsWeb) exit(1);
  }
}

/// Minimal app that waits for a tap before running the test on the Web.
class _RunnerApp extends StatefulWidget {
  const _RunnerApp();

  @override
  State<_RunnerApp> createState() => _RunnerAppState();
}

class _RunnerAppState extends State<_RunnerApp> {
  String _status = 'Tap to run the fade test';

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        body: Center(
          child: ElevatedButton(
            onPressed: () async {
              setState(() => _status = 'Running...');
              await _runTest();
              setState(() => _status = 'Done. See the browser console.');
            },
            child: Text(_status),
          ),
        ),
      ),
    );
  }
}

// ignore_for_file: avoid_print

import 'package:flutter/material.dart';

import 'exit_app.dart';
import 'tests/all_tests.dart';
import 'wait_module.dart';

/// One-shot test runner
///
/// Run with:
///   flutter run -d chrome -t tests/run_tests.dart --wasm \
///       --dart-define=TEST_ARG=[<test_name>|<test_index>] \
///      --web-browser-flag="--autoplay-policy=no-user-gesture-required"
///
void main(List<String> args) async {
  const dartDefineArg = String.fromEnvironment('TEST_ARG');
  final effectiveArgs = args.isNotEmpty
      ? args
      : (dartDefineArg.isEmpty ? const <String>[] : <String>[dartDefineArg]);

  print('********* args: $effectiveArgs *********');
  if (effectiveArgs.isEmpty) {
    print('Usage: flutter run -d <device> -t tests/run_tests.dart '
        '-a [<test_name>|<test_index>]');
    print('Usage: flutter run -d chrome -t tests/run_tests.dart '
        '--wasm --dart-define=TEST_ARG=[<test_name>|<test_index>] '
        '--web-browser-flag="--autoplay-policy=no-user-gesture-required"');
    return;
  }
  final arg = effectiveArgs.first;

  WidgetsFlutterBinding.ensureInitialized();

  // On web the WASM module loads asynchronously; wait for it so tests that
  // touch the raw bindings before `SoLoud.init()` don't race instantiation.
  // No-op on native.
  await waitForModule();

  final requested = arg == 'ALL'
      ? List<int>.generate(allTests.length, (i) => i)
      : arg.split(',').map((token) {
          final trimmed = token.trim();
          final parsed = int.tryParse(trimmed);
          if (parsed != null) return parsed;
          return allTests.indexWhere((test) => test.name == trimmed);
        }).toList();

  var passed = 0;
  var failed = 0;
  for (final index in requested) {
    if (index < 0 || index >= allTests.length) {
      print('TEST_FAILED unknown_index($index)');
      failed++;
      continue;
    }
    final entry = allTests[index];
    print('=== RUNNING_TEST ${entry.name} ===');
    try {
      final output = await entry.run();
      print(output);
      print('TEST_PASSED ${entry.name}');
      passed++;
    } on Object catch (e, st) {
      print(st);
      print('TEST_FAILED ${entry.name}: $e');
      failed++;
    }
  }

  final total = passed + failed;
  print('BATCH_SUMMARY: passed=$passed failed=$failed total=$total');
  await Future<void>.delayed(const Duration(milliseconds: 500));
  exitApp(failed == 0 ? 0 : 1);
}

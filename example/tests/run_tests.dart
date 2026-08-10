// ignore_for_file: avoid_print

import 'package:flutter/material.dart';

import 'exit_app.dart';
import 'tests/all_tests.dart';

/// One-shot test runner
///
/// On desktop, pass the test name or index with `-a`:
///   flutter run -d <device> -t tests/run_tests.dart -a [<test_name>|<test_index>]
///
/// On web (where `-a` is not supported), use `--dart-define` instead:
///   flutter run -d chrome -t tests/run_tests.dart --wasm \
///       --dart-define=TEST_ARG=[<test_name>|<test_index>]
///
void main(List<String> args) async {
  const dartDefineArg = String.fromEnvironment('TEST_ARG');
  final effectiveArgs =
      args.isNotEmpty ? args : (dartDefineArg.isEmpty ? const <String>[] : <String>[dartDefineArg]);

  print('********* args: $effectiveArgs *********');
  if (effectiveArgs.isEmpty) {
    print('Usage: flutter run -d <device> -t tests/run_tests.dart '
        '-a [<test_name>|<test_index>]');
    print('Web usage: flutter run -d chrome -t tests/run_tests.dart '
        '--wasm --dart-define=TEST_ARG=[<test_name>|<test_index>]');
    return;
  }
  final arg = effectiveArgs.first;

  final value = int.tryParse(arg);
  var index = 0;

  if (value != null) {
    print('Testing with index: $value');
    index = value;
  } else {
    print('Testing with string: $arg');
    index = allTests.indexWhere((test) => test.name == arg);
  }

  WidgetsFlutterBinding.ensureInitialized();

  try {
    final output = await allTests[index].run();
    print(output);
    print('TEST_PASSED ${allTests[index].name}');
    exitApp(0);
  } catch (e, st) {
    print(st);
    print('TEST_FAILED ${allTests[index].name}: $e');
    exitApp(1);
  }
}

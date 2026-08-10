// ignore_for_file: avoid_print

import 'package:flutter/material.dart';

import 'exit_app.dart';
import 'tests/all_tests.dart';

/// One-shot test runner
///
void main(List<String> args) async {
  print('********* args: $args *********');
  if (args.isEmpty) {
    print('Usage: flutter run -d <device> -t tests/run_tests.dart '
        '-a [<test_name>|<test_index>]');
    return;
  }
  final arg = args.first;

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

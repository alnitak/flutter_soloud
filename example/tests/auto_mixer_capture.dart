import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'tests/all_tests.dart';

/// Auto-run the mixer output capture test and exit.
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  final test = allTests.firstWhere((t) => t.name == 'MixerOutputCapture');
  final output = await test.run();
  // ignore: avoid_print
  print(output.toString());

  SoLoud.instance.deinit();
  // ignore: avoid_print
  print('Mixer output capture test completed.');
}

// ignore_for_file: avoid_print

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'tests/all_tests.dart';
import 'wait_module.dart';

/// Headless equivalent of the GUI runner's "Run All Tests" button
/// (tests.dart): every test runs in the same engine session, with the same
/// cleanup deinit between tests. Catches lifecycle bugs that only show up
/// when re-initializing the engine within one page session.
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await waitForModule();

  var failures = 0;

  for (var index = 0; index < allTests.length; index++) {
    print('>>>>> STARTING "${allTests[index].name}"');
    // Same cleanup as tests.dart _runTestByIndex.
    try {
      if (SoLoud.instance.isInitialized) {
        SoLoud.instance.deinit();
      }
    } catch (_) {}
    try {
      await allTests[index].run();
      print('>>>>> OK "${allTests[index].name}"');
    } catch (e) {
      failures++;
      print('>>>>> ERROR "${allTests[index].name}": $e');
    }
  }

  print('>>>>> SUMMARY: ${allTests.length - failures}/${allTests.length} OK');
  print(failures == 0 ? 'TEST_PASSED repro' : 'TEST_FAILED repro');
}

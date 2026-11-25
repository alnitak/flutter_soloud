// Runs before every test file in this folder (a flutter_test convention).
// In the browser it loads the WASM module first, so the tests can start the
// real engine like on the other platforms.
import 'dart:async';

import '../support/web_module/web_module.dart';

Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  await loadSoloudWebModule();
  await testMain();
}

import 'dart:js_interop';

import 'package:flutter_soloud/src/bindings/js_extension.dart';

/// Waits for the WASM module to be fully instantiated. Awaits the readiness
/// promise exposed by init_module.dart.js, which resolves only after
/// `self.Module_soloud` has been replaced with the instantiated module.
/// Falls back to polling while the script has not started yet.
Future<void> waitForModule() async {
  for (var i = 0; i < 200; i++) {
    final ready = flutterSoloudReady;
    if (ready != null) {
      await ready.toDart;
      return;
    }
    await Future<void>.delayed(const Duration(milliseconds: 50));
  }
}

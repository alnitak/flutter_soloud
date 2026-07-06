import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

import 'common.dart';

/// Test asynchronous `init()`-`deinit()`.
Future<OutputBuffer> testAsynchronousDeinit() async {
  /// test asynchronous init-deinit looping with a short decreasing time
  for (var t = 10; t >= 0; t--) {
    var error = '';

    /// Initialize the player
    unawaited(
      SoLoud.instance.init().then(
        (_) {},
        onError: (Object e) {
          deinit();
          if (e is SoLoudInitializationStoppedByDeinitException) {
            // This is to be expected.
            debugPrint('$e\n');
            return;
          }
          debugPrint('TEST FAILED delay: $t. Player starting error: $e\n');
          error = e.toString();
        },
      ),
    );

    assert(error.isEmpty, error);

    /// wait for [t] ms and deinit()
    await delay(t);
    deinit();
    final after = SoLoudController().soLoudFFI.isInited();

    assert(
      after == false,
      'TEST FAILED delay: $t. The player has not been deinited correctly!',
    );

    debugPrint('------------- awaited init delay $t passed\n');
  }
  return OutputBuffer();
}

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

import 'common.dart';

/// Test asynchronous `init()`-`deinit()`.
Future<StringBuffer> testAsynchronousDeinit() async {
  // Repeat the shortest delays because they specifically exercise workers
  // reaching the native init/deinit serialization mutex in reverse order.
  final delays = <int>[
    for (var t = 10; t >= 0; t--) t,
    2,
    0,
    2,
    0,
    2,
    0,
  ];
  for (final t in delays) {
    Object? initializationError;

    // Attach the error handler immediately, but retain the Future so every
    // initialization completion is joined before the next iteration/test.
    final initialization = SoLoud.instance.init().then<void>(
      (_) {},
      onError: (Object error, StackTrace stackTrace) {
        initializationError = error;
      },
    );

    /// wait for [t] ms and deinit()
    await delay(t);
    await SoLoud.instance.deinitAsync();
    await initialization;

    final error = initializationError;
    assert(
      error == null || error is SoLoudInitializationStoppedByDeinitException,
      'TEST FAILED delay: $t. Player starting error: $error',
    );
    if (error is SoLoudInitializationStoppedByDeinitException) {
      debugPrint('$error\n');
    }

    final after = SoLoudController().soLoudFFI.isInited();

    assert(
      after == false,
      'TEST FAILED delay: $t. The player has not been deinited correctly!',
    );

    debugPrint('------------- awaited init delay $t passed\n');
  }
  return StringBuffer();
}

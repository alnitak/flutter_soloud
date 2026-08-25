import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

import 'common.dart';

/// Test asynchronous `init()`-`deinit()`.
Future<OutputBuffer> testAsynchronousDeinit() async {
  /// test asynchronous init-deinit looping with a short decreasing time.
  /// The shortest delays are repeated because they specifically exercise
  /// workers reaching the native init/deinit serialization mutex in reverse
  /// order.
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
    final initResult =
        SoLoud.instance.init().then<({Object? error, StackTrace? stack})>(
              (_) => (error: null, stack: null),
              onError: (Object error, StackTrace stack) =>
                  (error: error, stack: stack),
            );

    await delay(t);
    await SoLoud.instance.deinitAsync();

    final result = await initResult;
    final error = result.error;

    if (error is SoLoudInitializationStoppedByDeinitException) {
      debugPrint('$error\n');
    } else if (error != null) {
      Error.throwWithStackTrace(error, result.stack!);
    }

    final after = SoLoudController().soLoudFFI.isInited();

    assert(
      after == false,
      'TEST FAILED delay: $t. The player has not been deinited correctly!',
    );

    debugPrint('------------- awaited init delay $t passed\n');
  }
  return OutputBuffer();
}

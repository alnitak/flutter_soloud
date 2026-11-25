// init() raced by deinitAsync() after a short, decreasing delay, with the real
// engine. Each round must leave the native engine deinitialized; init() may
// fail only with SoLoudInitializationStoppedByDeinitException. The shortest
// delays are repeated: they make the workers reach the native init/deinit
// serialization mutex in reverse order.
//
// A copy runs in the example app (example/tests/tests/asynchronous_deinit.dart)
// for the web and mobile lifecycles.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  prepareEngineHost();
  tearDown(deinitTestEngine);

  test('deinitAsync during init always leaves the engine deinited', () async {
    final delays = <int>[for (var t = 10; t >= 0; t--) t, 2, 0, 2, 0, 2, 0];
    for (final t in delays) {
      final initResult = SoLoud.instance.init().then<Object?>(
        (_) => null,
        onError: (Object error) => error,
      );
      await Future<void>.delayed(Duration(milliseconds: t));
      await SoLoud.instance.deinitAsync();

      final error = await initResult;
      expect(
        error,
        anyOf(isNull, isA<SoLoudInitializationStoppedByDeinitException>()),
        reason: 'delay $t ms',
      );
      expect(
        SoLoudController().soLoudFFI.isInited(),
        isFalse,
        reason: 'delay $t ms: the engine was not deinited',
      );
      expect(SoLoud.instance.isInitialized, isFalse, reason: 'delay $t ms');
    }
  });
}

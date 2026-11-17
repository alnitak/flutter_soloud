import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/bindings/android_engine_lifecycle.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:logging/logging.dart';

/// `SoLoud.init()` has never required
/// `WidgetsFlutterBinding.ensureInitialized()` first. Without it there is no
/// messenger to reach the Android plugin, and the signal must then degrade to
/// an info message, never to a warning or a failed init.
///
/// Kept apart from android_engine_lifecycle_test.dart because that file has
/// to initialize the binding, and a binding cannot be uninitialized.
void main() {
  late List<LogRecord> records;
  late StreamSubscription<LogRecord> logSubscription;

  setUp(() {
    records = <LogRecord>[];
    Logger.root.level = Level.ALL;
    logSubscription = Logger.root.onRecord
        .where((record) => record.level >= Level.INFO)
        .listen(records.add);
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    AndroidEngineLifecycle.debugReset();
  });

  tearDown(() async {
    await logSubscription.cancel();
    debugDefaultTargetPlatformOverride = null;
    AndroidEngineLifecycle.debugReset();
  });

  test(
    'no messenger logs info, points at ensureInitialized, and does not throw',
    () async {
      const bridge = AndroidEngineLifecycle();

      expect(await bridge.markNativeLibraryInUse(), isFalse);

      expect(records.single.level, Level.INFO);
      expect(records.single.message, contains('Flutter messaging'));
      expect(
        records.single.message,
        contains('WidgetsFlutterBinding.ensureInitialized()'),
      );
      // Not remembered: an init after the binding exists sends it.
      expect(AndroidEngineLifecycle.isPending, isTrue);
    },
  );
}

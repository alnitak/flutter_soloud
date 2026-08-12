import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/src/bindings/darwin_engine_lifecycle.dart';
import 'package:flutter_test/flutter_test.dart';

/// The iOS lifecycle handshake decides, from what comes back over the channel,
/// whether Dart may claim the native engine itself. Getting that wrong in
/// either direction is a real defect: falling back after the platform already
/// claimed takes the claim twice, and *not* falling back when the channel was
/// never usable would newly require `WidgetsFlutterBinding.ensureInitialized()`
/// before `SoLoud.init()`, which this package has never demanded.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const bridge = DarwinEngineLifecycle();
  const engineId = 1234;
  const shutdownEpoch = 7;

  late List<MethodCall> calls;

  setUp(() {
    calls = <MethodCall>[];
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
  });

  tearDown(() {
    debugDefaultTargetPlatformOverride = null;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          const MethodChannel(DarwinEngineLifecycle.channelName),
          null,
        );
  });

  void handleWith(Future<Object?>? Function(MethodCall call) handler) {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          const MethodChannel(DarwinEngineLifecycle.channelName),
          (call) {
            calls.add(call);
            return handler(call);
          },
        );
  }

  test('a successful claim reports claimed and carries the epoch', () async {
    handleWith((call) async => true);

    final result = await bridge.prepareEngineInit(engineId, shutdownEpoch);

    expect(result, DarwinEnginePrepareResult.claimed);
    expect(calls.single.method, 'prepareEngineInit');
    // The epoch has to reach the platform: it is what lets native refuse a
    // request that a deinit() superseded while this was in flight.
    expect(calls.single.arguments, <String, Object?>{
      'engineId': engineId,
      'shutdownEpoch': shutdownEpoch,
    });
  });

  test(
    'an explicit plugin refusal never falls back to a direct claim',
    () async {
      // What the plugin sends when a shutdown superseded this request, or when
      // the engine is already being destroyed.
      for (final code in <String>[
        'stale_prepare',
        'engine_detached',
        'invalid_engine_id',
        'invalid_arguments',
      ]) {
        calls.clear();
        handleWith((call) async => throw PlatformException(code: code));

        final result = await bridge.prepareEngineInit(engineId, shutdownEpoch);

        expect(
          result,
          DarwinEnginePrepareResult.refused,
          reason: '$code must not be mistaken for an unusable channel',
        );
      }
    },
  );

  test('a reply that is not true is a refusal, not a fallback', () async {
    handleWith((call) async => false);

    expect(
      await bridge.prepareEngineInit(engineId, shutdownEpoch),
      DarwinEnginePrepareResult.refused,
    );
  });

  test('an unknown failure after sending is refused, not retried', () async {
    // The ambiguous case: the handler may have committed the claim and only
    // the reply was lost. Claiming again would advance the native lifecycle
    // generation a second time.
    handleWith((call) async => throw StateError('the reply went missing'));

    expect(
      await bridge.prepareEngineInit(engineId, shutdownEpoch),
      DarwinEnginePrepareResult.refused,
    );
  });

  test('an unregistered plugin is unavailable, so init still works', () async {
    // No handler registered at all, which is what an app without the plugin
    // looks like: nothing received the message, so nothing was claimed and
    // claiming directly is safe.
    expect(
      await bridge.prepareEngineInit(engineId, shutdownEpoch),
      DarwinEnginePrepareResult.unavailable,
    );
  });

  test('macOS uses the same handshake as iOS', () async {
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    handleWith((call) async => true);

    expect(
      await bridge.prepareEngineInit(engineId, shutdownEpoch),
      DarwinEnginePrepareResult.claimed,
    );
    expect(calls.single.arguments, <String, Object?>{
      'engineId': engineId,
      'shutdownEpoch': shutdownEpoch,
    });
  });

  test('platforms without the plugin never touch the channel', () async {
    for (final platform in <TargetPlatform>[
      TargetPlatform.android,
      TargetPlatform.linux,
      TargetPlatform.windows,
    ]) {
      calls.clear();
      debugDefaultTargetPlatformOverride = platform;
      handleWith((call) async => true);

      expect(
        await bridge.prepareEngineInit(engineId, shutdownEpoch),
        DarwinEnginePrepareResult.unavailable,
        reason: '$platform has no lifecycle plugin to hand the engine id to',
      );
      expect(calls, isEmpty, reason: '$platform must not send anything');
    }
  });
}

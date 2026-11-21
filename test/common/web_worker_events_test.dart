// The web event worker with the real WASM module: worker.dart.js carries the
// voice-ended events and the mixer output packets from the WASM side
// (bindings.cpp createWorkerInWasm / postMessageToEventWorker) to Dart
// (bindings_player_web.dart). Checks:
//   * a voice that ends on its own is reported (soundEvents,
//     allInstancesFinished) and its handle becomes invalid; many short voices
//     are each reported once;
//   * mixer output packets arrive while capturing and stop after stop;
//   * a worker that cannot be created (new Worker throws) makes init log a
//     warning and the next init retries and works;
//   * voice-ended events of a previous session are dropped after deinit/init
//     (a new voice that reuses the handle id is not invalidated by them).
//
// Browser only: ./test/support/web_tests.sh test/common/web_worker_events_test.dart [--wasm]
@TestOn('browser')
@Tags(['device'])
library;

import 'dart:js_interop';
import 'dart:js_interop_unsafe';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:logging/logging.dart';

import '../support/sounds.dart';

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  tearDown(deinitTestEngine);

  // First: the worker is created once per page, by the first init.
  test(
    'a worker that cannot be created: warning, then the next init retries',
    () async {
      final records = <LogRecord>[];
      Logger.root.level = Level.ALL;
      final logSub = Logger.root.onRecord.listen(records.add);
      final worker = globalContext['Worker'];
      globalContext['Worker'] = _blockedWorker.toJS;
      try {
        await soloud.init();
      } finally {
        globalContext['Worker'] = worker;
      }
      expect(
        records.any(
          (r) => r.level >= Level.WARNING && r.message.contains('worker'),
        ),
        isTrue,
        reason: 'init must warn that the event worker could not be created',
      );
      soloud.deinit();

      // The real Worker is back: a new init creates it and events flow.
      await initTestEngine(globalVolume: 0);
      final s = await loadSine(duration: const Duration(milliseconds: 150));
      final h = soloud.play(s);
      await s.allInstancesFinished.first.timeout(const Duration(seconds: 3));
      expect(soloud.getIsValidVoiceHandle(h), isFalse);
      await logSub.cancel();
    },
  );

  test('a voice that ends on its own is reported once', () async {
    await initTestEngine(globalVolume: 0);
    final s = await loadSine(duration: const Duration(milliseconds: 200));
    final events = <SoundHandle>[];
    final sub = s.soundEvents.listen((e) => events.add(e.handle));
    final finished = s.allInstancesFinished.first;
    final h = soloud.play(s);
    await finished.timeout(const Duration(seconds: 3));
    await Future<void>.delayed(const Duration(milliseconds: 100));
    expect(events, [h]);
    expect(soloud.getIsValidVoiceHandle(h), isFalse);
    await sub.cancel();
  });

  test('twenty short voices: twenty events, each handle once', () async {
    await initTestEngine(globalVolume: 0);
    final s = await loadSine(duration: const Duration(milliseconds: 100));
    final events = <SoundHandle>[];
    final sub = s.soundEvents.listen((e) => events.add(e.handle));
    final handles = <SoundHandle>[];
    for (var i = 0; i < 20; i++) {
      handles.add(soloud.play(s));
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    await waitUntil(
      () => events.length >= 20,
      timeout: const Duration(seconds: 5),
    );
    await Future<void>.delayed(const Duration(milliseconds: 200));
    expect(events.toSet(), handles.toSet());
    expect(events.length, 20);
    await sub.cancel();
  });

  test('mixer output packets arrive while capturing, not after', () async {
    await initTestEngine();
    final tone = await loadTestWaveform();
    soloud.play(tone);
    var bytes = 0;
    final sub = soloud.startMixerOutputStream().listen(
      (c) => bytes += c.length,
    );
    await waitUntil(() => bytes > 16384);
    expect(bytes, greaterThan(16384));
    soloud.stopMixerOutputStream();
    await Future<void>.delayed(const Duration(milliseconds: 200));
    final after = bytes;
    await Future<void>.delayed(const Duration(milliseconds: 500));
    expect(bytes, after, reason: 'no packets after stop');
    await sub.cancel();
  });

  test("a previous session's voice-ended events are dropped", () async {
    await initTestEngine(globalVolume: 0);
    // Many voices about to end when the engine goes away.
    final s = await loadSine(duration: const Duration(milliseconds: 60));
    for (var i = 0; i < 10; i++) {
      soloud.play(s);
    }
    await Future<void>.delayed(const Duration(milliseconds: 40));
    soloud.deinit();
    await initTestEngine(globalVolume: 0);
    // New voices, which can get the same handle ids, must survive whatever
    // the old session still had in flight.
    final tone = await loadTestWaveform();
    final handles = [for (var i = 0; i < 10; i++) soloud.play(tone)];
    await Future<void>.delayed(const Duration(milliseconds: 500));
    for (final h in handles) {
      expect(soloud.getIsValidVoiceHandle(h), isTrue, reason: '$h');
    }
  });
}

/// Stands in for `Worker`: creating a worker fails, as with a bad URL or a
/// conflicting COEP policy.
JSAny? _blockedWorker() => throw StateError('Worker blocked by the test');

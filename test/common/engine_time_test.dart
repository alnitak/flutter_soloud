// The engine clock and the session boundary with the real engine and
// generated sounds (ported from example/tests/tests/
// engine_time_across_sessions.dart and hot_restart_lifecycle.dart): the
// engine time advances in every one of many init/deinit sessions and starts
// again from zero in each; init() while running (the hot-restart path) gives
// a working engine; no event of a previous session reaches the next one.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  tearDown(deinitTestEngine);

  /// Samples the engine time [count] times, [every] apart.
  Future<List<Duration>> sampleEngineTime({
    int count = 6,
    Duration every = const Duration(milliseconds: 150),
  }) async {
    final times = <Duration>[];
    for (var i = 0; i < count; i++) {
      await Future<void>.delayed(every);
      times.add(soloud.getEngineTime());
    }
    return times;
  }

  test(
    'engine time advances and restarts in each of 8 sessions',
    tags: ['slow'],
    () async {
      var previousEnd = Duration.zero;
      for (var session = 1; session <= 8; session++) {
        await soloud.init(bufferSize: 4096, channels: Channels.mono);
        soloud.setGlobalVolume(0.1);
        final start = soloud.getEngineTime();
        if (session > 1) {
          expect(
            start,
            lessThan(previousEnd),
            reason: 'session $session did not restart the clock',
          );
        }
        expect(start, lessThan(const Duration(milliseconds: 500)));

        soloud.play(await loadTestWaveform());
        final times = await sampleEngineTime();
        var frozen = 0;
        for (var i = 1; i < times.length; i++) {
          expect(
            times[i],
            greaterThanOrEqualTo(times[i - 1]),
            reason: 'session $session: the clock went back',
          );
          if (times[i] == times[i - 1]) frozen++;
        }
        expect(
          frozen,
          lessThanOrEqualTo(2),
          reason: 'session $session: engine time froze ($times)',
        );
        // About as fast as the wall clock (generous for slow CI).
        final elapsed = times.last - times.first;
        expect(elapsed, greaterThan(const Duration(milliseconds: 300)));
        expect(elapsed, lessThan(const Duration(milliseconds: 1500)));
        previousEnd = times.last;
        soloud.deinit();
      }
    },
  );

  test('engine time restarts after deinitAsync too', () async {
    await initTestEngine();
    soloud.play(await loadTestWaveform());
    await Future<void>.delayed(const Duration(milliseconds: 600));
    final before = soloud.getEngineTime();
    expect(before, greaterThan(const Duration(milliseconds: 300)));
    await soloud.deinitAsync();
    await initTestEngine();
    expect(soloud.getEngineTime(), lessThan(before));
  });

  test('init() while running re-initializes (hot-restart path)', () async {
    await initTestEngine();
    final old = await loadTestWaveform();
    final oldHandle = soloud.play(old);
    expect(soloud.isInitialized, isTrue);

    await soloud.init();
    soloud.setGlobalVolume(0.1);
    expect(soloud.isInitialized, isTrue);
    expect(soloud.activeSounds, isEmpty);
    expect(soloud.getIsValidVoiceHandle(oldHandle), isFalse);

    // Callbacks work in the new session: stop() completes on the
    // voice-ended event.
    final sound = await loadTestSound();
    final handle = soloud.play(sound);
    expect(soloud.getIsValidVoiceHandle(handle), isTrue);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    await soloud.stop(handle).timeout(const Duration(seconds: 3));
    expect(soloud.getIsValidVoiceHandle(handle), isFalse);
    expect(sound.handles, isEmpty);
  });

  test('rapid init/deinit cycles', () async {
    for (var i = 0; i < 5; i++) {
      await soloud.init();
      expect(soloud.isInitialized, isTrue, reason: 'cycle $i');
      soloud.deinit();
      expect(soloud.isInitialized, isFalse, reason: 'cycle $i');
    }
    await initTestEngine();
    final sound = await loadTestSound(
      duration: const Duration(milliseconds: 150),
    );
    soloud.play(sound);
    expect(await waitUntil(() => sound.handles.isEmpty), isTrue);
  });

  group('no event crosses sessions', () {
    test('voices cut by deinit send no event to the next session', () async {
      await initTestEngine();
      final oldSound = await loadTestSound(
        duration: const Duration(milliseconds: 300),
      );
      final oldEvents = <StreamSoundEvent>[];
      oldSound.soundEvents.listen(oldEvents.add);
      final oldFinished = <void>[];
      oldSound.allInstancesFinished.listen(oldFinished.add);
      final oldHandles = [for (var i = 0; i < 4; i++) soloud.play(oldSound)];
      final oldCount = oldEvents.length;
      soloud.deinit();

      await initTestEngine();
      final ended = <int>[];
      final subscription = SoLoudController().soLoudFFI.voiceEndedEvents.listen(
        ended.add,
      );
      addTearDown(subscription.cancel);
      // Longer than the old sound would have played.
      await Future<void>.delayed(const Duration(milliseconds: 600));
      expect(ended, isEmpty, reason: 'voice-ended events of old voices');
      expect(
        oldEvents
            .skip(oldCount)
            .where((e) => e.event == SoundEventType.handleIsNoMoreValid),
        isEmpty,
      );
      expect(oldFinished, isEmpty);
      for (final h in oldHandles) {
        expect(soloud.getIsValidVoiceHandle(h), isFalse);
      }
    });

    test('a new voice ends with only its own event', () async {
      await initTestEngine();
      final old = await loadTestSound();
      soloud
        ..play(old)
        ..deinit();

      await initTestEngine();
      final ended = <int>[];
      final subscription = SoLoudController().soLoudFFI.voiceEndedEvents.listen(
        ended.add,
      );
      addTearDown(subscription.cancel);
      final sound = await loadTestSound(
        duration: const Duration(milliseconds: 200),
      );
      final events = <StreamSoundEvent>[];
      sound.soundEvents.listen(events.add);
      final handle = soloud.play(sound);
      expect(await waitUntil(() => sound.handles.isEmpty), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 1200));
      expect(ended, [handle.id]);
      expect(events.map((e) => e.handle), [handle]);
    });

    test('a deinit during a voice fade sends nothing later', () async {
      await initTestEngine();
      final sound = await loadTestWaveform();
      final h = soloud.play(sound);
      soloud
        ..fadeVolume(h, 0, const Duration(milliseconds: 300))
        ..scheduleStop(h, const Duration(milliseconds: 300))
        ..deinit();

      await initTestEngine();
      final ended = <int>[];
      final subscription = SoLoudController().soLoudFFI.voiceEndedEvents.listen(
        ended.add,
      );
      addTearDown(subscription.cancel);
      soloud.play(await loadTestWaveform(), volume: 0.01);
      await Future<void>.delayed(const Duration(milliseconds: 600));
      expect(ended, isEmpty);
    });
  });
}

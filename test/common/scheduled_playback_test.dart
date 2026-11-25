// Scheduling against the engine clock, with the real engine: playScheduled,
// stopScheduled and fadeScheduled act at the requested engine time (within
// a tolerance of a few mix blocks), times in the past act at once, invalid
// handles are ignored; setDelaySamples delays a voice; playClocked plays;
// getPlayheadTime follows the engine time; isRenderAheadEnabled reflects
// init.
@Tags(['device'])
library;

import 'dart:typed_data';

import 'package:flutter/foundation.dart' show kIsWeb;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

/// A handle no voice has ever had.
const _bogus = SoundHandle(0x0BAD0BAD);

/// How late (in engine time) an action may be observed: the engine clock
/// advances one mix block (2048 frames, ~46 ms) at a time, the device may
/// mix a couple of blocks at once, and the test polls.
const _tolerance = Duration(milliseconds: 160);

/// Index of the first sample of [data] that is not silent, or null.
int? _onset(Float32List data) {
  for (var i = 0; i < data.length; i++) {
    if (data[i].abs() > 1e-3) return i;
  }
  return null;
}

void main() {
  setUpEngineTests(globalVolume: 0.05);
  final soloud = SoLoud.instance;

  late AudioSource tone;
  setUp(() async {
    tone = await loadTestWaveform();
    // Keeps the device mixing, so the engine clock advances.
    soloud.play(tone, volume: 0);
    await Future<void>.delayed(const Duration(milliseconds: 100));
  });

  /// Polls until [condition] holds and returns the engine time then.
  Future<Duration> engineTimeWhen(
    bool Function() condition, {
    Duration timeout = const Duration(seconds: 4),
  }) async {
    expect(
      await waitUntil(
        condition,
        timeout: timeout,
        interval: const Duration(milliseconds: 5),
      ),
      isTrue,
      reason: 'never happened',
    );
    return soloud.getEngineTime();
  }

  test('the engine time and the playhead advance while mixing', () async {
    final t0 = soloud.getEngineTime();
    final p0 = soloud.getPlayheadTime();
    await Future<void>.delayed(const Duration(milliseconds: 300));
    final t1 = soloud.getEngineTime();
    final p1 = soloud.getPlayheadTime();
    expect(t1 - t0, greaterThan(const Duration(milliseconds: 150)));
    expect(p1 - p0, greaterThan(const Duration(milliseconds: 150)));
    // Without render-ahead the playhead is the mix clock.
    expect(soloud.isRenderAheadEnabled, isFalse);
    expect((p1 - t1).abs(), lessThan(const Duration(milliseconds: 50)));
  });

  group('playScheduled', () {
    test('starts at the requested engine time, sample-accurately', () async {
      // A reference on the left at t, the voice under test on the right at
      // t + 300 ms: their onsets on the output are 300 ms apart.
      final t = soloud.getEngineTime() + const Duration(milliseconds: 200);
      soloud
        ..playScheduled(tone, t, pan: -1)
        ..playScheduled(tone, t + const Duration(milliseconds: 300), pan: 1);
      final out = await captureOutput(frames: 44100, skipFrames: 0);
      final left = _onset(out.channel(0));
      final right = _onset(out.channel(1));
      expect(left, isNotNull, reason: 'the reference was captured');
      expect(right, isNotNull);
      expect(right! - left!, closeTo(0.3 * 44100, 2));
    });

    test('a pending voice is valid and silent', () async {
      final at = soloud.getEngineTime() + const Duration(seconds: 2);
      final h = soloud.playScheduled(tone, at);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      final out = await captureOutput();
      expect(out.peak(0), lessThan(1e-4));
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
    });

    test('with a duration stops at atTime + duration', () async {
      final at = soloud.getEngineTime() + const Duration(milliseconds: 200);
      final h = soloud.playScheduled(
        tone,
        at,
        duration: const Duration(milliseconds: 300),
      );
      final ended = await engineTimeWhen(
        () => !soloud.getIsValidVoiceHandle(h),
      );
      final end = at + const Duration(milliseconds: 300);
      expect(ended, greaterThanOrEqualTo(end));
      expect(ended - end, lessThan(_tolerance));
    });

    test('a time in the past plays at once', () async {
      final h = soloud.playScheduled(tone, Duration.zero);
      expect(
        await waitUntil(
          () => soloud.getStreamTime(h) > Duration.zero,
          timeout: const Duration(milliseconds: 500),
        ),
        isTrue,
      );
    });

    test('stop cancels a pending voice', () async {
      final at = soloud.getEngineTime() + const Duration(seconds: 1);
      final h = soloud.playScheduled(tone, at);
      await soloud.stop(h);
      expect(soloud.getIsValidVoiceHandle(h), isFalse);
    });
  });

  group('stopScheduled', () {
    test('stops at the requested engine time', () async {
      final h = soloud.play(tone);
      final at = soloud.getEngineTime() + const Duration(milliseconds: 400);
      soloud.stopScheduled(h, at);
      await Future<void>.delayed(const Duration(milliseconds: 150));
      expect(soloud.getIsValidVoiceHandle(h), isTrue, reason: 'not yet');
      final stopped = await engineTimeWhen(
        () => !soloud.getIsValidVoiceHandle(h),
      );
      expect(stopped, greaterThanOrEqualTo(at));
      expect(stopped - at, lessThan(_tolerance));
    });

    test('a time in the past stops at once', () async {
      final h = soloud.play(tone);
      soloud.stopScheduled(h, Duration.zero);
      expect(
        await waitUntil(
          () => !soloud.getIsValidVoiceHandle(h),
          timeout: const Duration(milliseconds: 500),
        ),
        isTrue,
      );
    });

    test('an invalid handle is ignored', () {
      expect(
        () => soloud.stopScheduled(_bogus, soloud.getEngineTime()),
        returnsNormally,
      );
    });
  });

  group('fadeScheduled', () {
    test('fades from the requested engine time', () async {
      final h = soloud.play(tone);
      final at = soloud.getEngineTime() + const Duration(milliseconds: 400);
      soloud.fadeScheduled(h, at, 0, const Duration(milliseconds: 200));
      await Future<void>.delayed(const Duration(milliseconds: 150));
      expect(soloud.getVolume(h), 1, reason: 'not started yet');
      final faded = await engineTimeWhen(() => soloud.getVolume(h) < 0.5);
      // Halfway through the fade.
      final mid = at + const Duration(milliseconds: 100);
      expect(faded, greaterThanOrEqualTo(at));
      expect((faded - mid).abs(), lessThan(_tolerance));
      expect(
        await waitUntil(() => soloud.getVolume(h) == 0),
        isTrue,
        reason: 'volume is ${soloud.getVolume(h)}',
      );
      expect(soloud.getIsValidVoiceHandle(h), isTrue, reason: 'no thenStop');
    });

    test('thenStop stops the voice when the fade ends', () async {
      final h = soloud.play(tone);
      final at = soloud.getEngineTime() + const Duration(milliseconds: 200);
      soloud.fadeScheduled(
        h,
        at,
        0,
        const Duration(milliseconds: 200),
        thenStop: true,
      );
      final stopped = await engineTimeWhen(
        () => !soloud.getIsValidVoiceHandle(h),
      );
      final end = at + const Duration(milliseconds: 200);
      expect(stopped, greaterThanOrEqualTo(end - _tolerance));
      expect(stopped - end, lessThan(_tolerance));
    });

    test('a time in the past fades at once, like fadeVolume', () async {
      final h = soloud.play(tone);
      soloud.fadeScheduled(
        h,
        Duration.zero,
        0.2,
        const Duration(milliseconds: 100),
      );
      expect(
        await waitUntil(
          () => (soloud.getVolume(h) - 0.2).abs() < 1e-3,
          timeout: const Duration(milliseconds: 800),
        ),
        isTrue,
        reason: 'volume is ${soloud.getVolume(h)}',
      );
    });

    test('an invalid handle is ignored', () {
      expect(
        () => soloud.fadeScheduled(
          _bogus,
          soloud.getEngineTime(),
          0,
          const Duration(milliseconds: 100),
        ),
        returnsNormally,
      );
    });
  });

  group('setDelaySamples', () {
    test('delays a voice by that many samples', () async {
      // Two paused voices, unpaused together through a voice group: the one
      // on the right is delayed by half a second.
      final left = soloud.play(tone, paused: true, pan: -1);
      final right = soloud.play(tone, paused: true, pan: 1);
      soloud.setDelaySamples(right, 44100 ~/ 2);
      final group = soloud.createVoiceGroup();
      addTearDown(() => soloud.destroyVoiceGroup(group));
      soloud
        ..addVoicesToGroup(group, [left, right])
        ..setPause(group, false);
      final out = await captureOutput(frames: 44100, skipFrames: 0);
      final l = _onset(out.channel(0));
      final r = _onset(out.channel(1));
      expect(l, isNotNull);
      expect(r, isNotNull);
      expect(r! - l!, closeTo(44100 / 2, 2));
    });

    test('the delay is silent on the output', () async {
      final h = soloud.play(tone, paused: true);
      soloud
        ..setDelaySamples(h, 44100 * 2)
        ..setPause(h, false);
      final out = await captureOutput(frames: 4096, skipFrames: 0);
      expect(out.peak(0), lessThan(1e-4));
    });

    test('an invalid handle is ignored', () {
      expect(() => soloud.setDelaySamples(_bogus, 100), returnsNormally);
    });
  });

  group('playClocked', () {
    test('keeps the spacing of the clock times', () async {
      soloud.resetStreamTime();
      final a = soloud.playClocked(tone, Duration.zero, pan: -1);
      final b = soloud.playClocked(
        tone,
        const Duration(milliseconds: 100),
        pan: 1,
      );
      expect(soloud.getIsValidVoiceHandle(a), isTrue);
      expect(soloud.getIsValidVoiceHandle(b), isTrue);
      final out = await captureOutput(frames: 22050, skipFrames: 0);
      final l = _onset(out.channel(0));
      final r = _onset(out.channel(1));
      expect(l, isNotNull);
      expect(r, isNotNull);
      expect(r! - l!, closeTo(0.1 * 44100, 2));
    });
  });

  group('render-ahead', () {
    test(
      'is enabled by init(renderAheadFrames:) on native, never on web',
      () async {
        soloud.deinit();
        await soloud.init(renderAheadFrames: 4096);
        expect(soloud.isRenderAheadEnabled, kIsWeb ? isFalse : isTrue);
        soloud.play(await loadTestWaveform(), volume: 0);
        await Future<void>.delayed(const Duration(milliseconds: 300));
        final playhead = soloud.getPlayheadTime();
        final engine = soloud.getEngineTime();
        expect(playhead, lessThanOrEqualTo(engine));
        if (!kIsWeb) {
          // The playhead trails the mix clock by the render-ahead depth.
          expect(engine - playhead, greaterThan(Duration.zero));
          expect(
            engine - playhead,
            lessThan(const Duration(milliseconds: 4096 * 1000 ~/ 44100 + 100)),
          );
        }
      },
    );

    test('is false once deinitialized', () {
      soloud.deinit();
      expect(soloud.isRenderAheadEnabled, isFalse);
    });
  });
}

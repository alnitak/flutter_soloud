// Voice stream time, loop point, approximate output volume and global volume
// oscillation, with the real engine: getStreamTime while playing, paused,
// looping and after a seek; resetStreamTime; setLoopPoint/getLoopPoint and
// where a looping voice restarts; getApproximateVolume; oscillateGlobalVolume.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

/// A handle no voice has ever had.
const _bogus = SoundHandle(0x0BAD0BAD);

const _ms = Duration(milliseconds: 1);

void main() {
  setUpEngineTests(globalVolume: 0.05);
  final soloud = SoLoud.instance;

  group('getStreamTime', () {
    test('advances with wall time while playing', () async {
      final sound = await loadSine(duration: const Duration(seconds: 5));
      final h = soloud.play(sound);
      final watch = Stopwatch()..start();
      await Future<void>.delayed(const Duration(milliseconds: 500));
      final t = soloud.getStreamTime(h);
      expect(
        (t - watch.elapsed).abs(),
        lessThan(_ms * 200),
        reason: 'stream time $t after ${watch.elapsed}',
      );
      // Without seeks, loops or speed changes it follows the position (they
      // can be a mix block, ~46 ms, apart).
      expect((t - soloud.getPosition(h)).abs(), lessThan(_ms * 60));
    });

    test('stops while paused', () async {
      final sound = await loadSine(duration: const Duration(seconds: 5));
      final h = soloud.play(sound);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      soloud.setPause(h, true);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      final paused = soloud.getStreamTime(h);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(soloud.getStreamTime(h), paused);
      soloud.setPause(h, false);
      expect(await waitUntil(() => soloud.getStreamTime(h) > paused), isTrue);
    });

    test('keeps growing across loops, unlike the position', () async {
      final sound = await loadSine(duration: const Duration(milliseconds: 200));
      final h = soloud.play(sound, looping: true);
      await Future<void>.delayed(const Duration(milliseconds: 700));
      expect(soloud.getStreamTime(h), greaterThan(_ms * 450));
      expect(soloud.getPosition(h), lessThan(_ms * 200));
    });

    test('is not moved by a seek, unlike the position', () async {
      final sound = await loadSine(duration: const Duration(seconds: 5));
      final h = soloud.play(sound);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      final before = soloud.getStreamTime(h);
      soloud.seek(h, const Duration(seconds: 3));
      expect(soloud.getPosition(h), greaterThanOrEqualTo(_ms * 3000));
      final after = soloud.getStreamTime(h);
      expect(after, greaterThanOrEqualTo(before));
      expect(after - before, lessThan(_ms * 200));
    });

    test(
      'counts real time while the position follows the play speed',
      () async {
        final sound = await loadSine(duration: const Duration(seconds: 5));
        final h = soloud.play(sound);
        soloud.setRelativePlaySpeed(h, 2);
        await Future<void>.delayed(const Duration(milliseconds: 500));
        final t = soloud.getStreamTime(h);
        final p = soloud.getPosition(h);
        expect(p.inMicroseconds / t.inMicroseconds, closeTo(2, 0.1));
      },
    );

    test('is zero for an invalid handle', () {
      expect(soloud.getStreamTime(_bogus), Duration.zero);
    });

    test('is zero once the voice ended', () async {
      final sound = await loadSine(duration: const Duration(milliseconds: 100));
      final h = soloud.play(sound);
      expect(await waitUntil(() => !soloud.getIsValidVoiceHandle(h)), isTrue);
      expect(soloud.getStreamTime(h), Duration.zero);
    });
  });

  group('resetStreamTime', () {
    test('re-anchors playClocked: a clock far ahead plays at once', () async {
      final tone = await loadTestWaveform();
      // Anchor the clock at 0, then jump far ahead after a reset.
      soloud
        ..playClocked(tone, Duration.zero, volume: 0)
        ..resetStreamTime();
      final h = soloud.playClocked(tone, const Duration(seconds: 100));
      final out = await captureOutput(skipFrames: 0);
      expect(out.peak(0), greaterThan(0.005), reason: 'it is heard at once');
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
    });
  });

  group('loop point', () {
    test('defaults to zero and round-trips', () async {
      final sound = await loadSine(duration: const Duration(seconds: 2));
      final h = soloud.play(sound, looping: true);
      expect(soloud.getLoopPoint(h), Duration.zero);
      soloud.setLoopPoint(h, const Duration(milliseconds: 750));
      expect((soloud.getLoopPoint(h) - _ms * 750).abs(), lessThan(_ms));
    });

    test('a looping voice restarts from it', () async {
      final sound = await loadSine(duration: const Duration(milliseconds: 400));
      final h = soloud.play(sound, looping: true);
      soloud.setLoopPoint(h, const Duration(milliseconds: 300));
      // Sample the position over a few loops: after the first pass it stays
      // between the loop point and the end.
      await Future<void>.delayed(const Duration(milliseconds: 500));
      for (var i = 0; i < 20; i++) {
        final p = soloud.getPosition(h);
        expect(p, greaterThanOrEqualTo(_ms * 299), reason: 'position $p');
        expect(p, lessThanOrEqualTo(_ms * 400), reason: 'position $p');
        await Future<void>.delayed(const Duration(milliseconds: 15));
      }
    });

    test('given to play() applies from the first loop', () async {
      final sound = await loadSine(duration: const Duration(milliseconds: 300));
      final h = soloud.play(
        sound,
        looping: true,
        loopingStartAt: const Duration(milliseconds: 200),
      );
      expect(soloud.getLoopPoint(h), _ms * 200);
    });

    test('past the loop end point throws', () async {
      final sound = await loadSine(duration: const Duration(seconds: 2));
      final h = soloud.play(
        sound,
        looping: true,
        loopingEndAt: const Duration(milliseconds: 500),
      );
      expect(
        () => soloud.setLoopPoint(h, const Duration(milliseconds: 800)),
        throwsA(anything),
      );
      expect(soloud.getLoopPoint(h), Duration.zero);
    });

    test('is zero for an invalid handle, and setting it is ignored', () {
      expect(soloud.getLoopPoint(_bogus), Duration.zero);
      expect(
        () => soloud.setLoopPoint(_bogus, const Duration(seconds: 1)),
        returnsNormally,
      );
    });
  });

  group('getApproximateVolume', () {
    test('reads 0 while visualization is disabled', () async {
      soloud.play(await loadTestWaveform());
      await Future<void>.delayed(const Duration(milliseconds: 200));
      expect(soloud.getApproximateVolume(0), 0);
      expect(soloud.getApproximateVolume(1), 0);
    });

    test('follows the output level with visualization enabled', () async {
      soloud
        ..setVisualizationEnabled(true)
        ..setGlobalVolume(0.1);
      final h = soloud.play(await loadTestWaveform());
      expect(await waitUntil(() => soloud.getApproximateVolume(0) > 0), isTrue);
      final loud = soloud.getApproximateVolume(0);
      soloud.setVolume(h, 0.25);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      final quiet = soloud.getApproximateVolume(0);
      expect(quiet / loud, closeTo(0.25, 0.05));
    });

    test('reads 0 for channels the output does not have', () async {
      soloud
        ..setVisualizationEnabled(true)
        ..play(await loadTestWaveform());
      await Future<void>.delayed(const Duration(milliseconds: 200));
      expect(soloud.getApproximateVolume(2), 0);
      expect(soloud.getApproximateVolume(1000), 0);
    });
  });

  group('oscillateGlobalVolume', () {
    test('stays within its range and moves', () async {
      soloud
        ..play(await loadTestWaveform())
        ..oscillateGlobalVolume(0.1, 0.3, const Duration(milliseconds: 100));
      await Future<void>.delayed(const Duration(milliseconds: 150));
      final seen = <double>{};
      for (var i = 0; i < 30; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 10));
        final v = soloud.getGlobalVolume();
        expect(v, inInclusiveRange(0.1 - 1e-3, 0.3 + 1e-3));
        seen.add(v);
      }
      expect(seen.length, greaterThan(2), reason: 'the volume oscillates');
    });

    test('setGlobalVolume stops it', () async {
      soloud
        ..play(await loadTestWaveform())
        ..oscillateGlobalVolume(0, 0.5, const Duration(milliseconds: 100));
      await Future<void>.delayed(const Duration(milliseconds: 100));
      soloud.setGlobalVolume(0.2);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      expect(soloud.getGlobalVolume(), closeTo(0.2, 1e-6));
    });

    test('is heard on the output', () async {
      soloud
        ..play(await loadTestWaveform())
        ..oscillateGlobalVolume(0, 0.2, const Duration(milliseconds: 100));
      final out = await captureOutput(frames: 22050);
      // Split into 10 ms blocks: the level swings between near 0 and the top.
      final left = out.channel(0);
      var low = double.infinity;
      var high = 0.0;
      for (var b = 0; b + 441 <= left.length; b += 441) {
        var peak = 0.0;
        for (var i = b; i < b + 441; i++) {
          peak = peak > left[i].abs() ? peak : left[i].abs();
        }
        low = peak < low ? peak : low;
        high = peak > high ? peak : high;
      }
      expect(high, greaterThan(0.04));
      expect(low, lessThan(high / 4));
    });
  });
}

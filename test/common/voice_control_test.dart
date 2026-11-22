// Per-voice controls with the real engine: volume, pan, relative speed,
// pause, stop, faders, oscillators, scheduled pause/stop, and what happens
// with handles that are invalid or whose sound has already ended.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// A handle no voice has ever had.
const _bogus = SoundHandle(0x0BAD0BAD);

void main() {
  setUpEngineTests(globalVolume: 0);
  final soloud = SoLoud.instance;

  late AudioSource tone;
  setUp(() async => tone = await loadTestWaveform());

  group('volume', () {
    test('play() sets the initial volume and setVolume changes it', () {
      final h = soloud.play(tone, volume: 0.3);
      expect(soloud.getVolume(h), closeTo(0.3, 1e-6));
      soloud.setVolume(h, 0.8);
      expect(soloud.getVolume(h), closeTo(0.8, 1e-6));
      soloud.setVolume(h, 0);
      expect(soloud.getVolume(h), 0);
    });

    test('global volume round-trips', () {
      soloud.setGlobalVolume(0.25);
      expect(soloud.getGlobalVolume(), closeTo(0.25, 1e-6));
      soloud.setGlobalVolume(0);
      expect(soloud.getGlobalVolume(), 0);
    });

    test('fadeVolume reaches the target', () async {
      final h = soloud.play(tone);
      soloud.fadeVolume(h, 0.2, const Duration(milliseconds: 100));
      expect(
        await waitUntil(() => (soloud.getVolume(h) - 0.2).abs() < 1e-3),
        isTrue,
        reason: 'volume is ${soloud.getVolume(h)}',
      );
    });

    test('fadeGlobalVolume reaches the target', () async {
      soloud
        ..setGlobalVolume(0.1)
        ..play(tone)
        ..fadeGlobalVolume(0, const Duration(milliseconds: 100));
      expect(await waitUntil(() => soloud.getGlobalVolume() == 0), isTrue);
    });

    test('oscillateVolume stays within its range and moves', () async {
      final h = soloud.play(tone);
      soloud.oscillateVolume(h, 0.2, 0.6, const Duration(milliseconds: 80));
      // Faders advance with the mixer, so let it run at least once.
      await Future<void>.delayed(const Duration(milliseconds: 150));
      final seen = <double>{};
      for (var i = 0; i < 30; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 10));
        final v = soloud.getVolume(h);
        expect(v, inInclusiveRange(0.2 - 1e-3, 0.6 + 1e-3));
        seen.add(v);
      }
      expect(seen.length, greaterThan(2), reason: 'the volume oscillates');
    });

    test('setVolume after a fade cancels it', () async {
      final h = soloud.play(tone);
      soloud
        ..fadeVolume(h, 0, const Duration(milliseconds: 200))
        ..setVolume(h, 0.5);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(soloud.getVolume(h), closeTo(0.5, 1e-6));
    });
  });

  group('pan', () {
    test('play() sets the initial pan and setPan changes it', () {
      final h = soloud.play(tone, pan: -0.5);
      expect(soloud.getPan(h), closeTo(-0.5, 1e-6));
      soloud.setPan(h, 1);
      expect(soloud.getPan(h), closeTo(1, 1e-6));
    });

    test('fadePan reaches the target', () async {
      final h = soloud.play(tone);
      soloud.fadePan(h, -1, const Duration(milliseconds: 100));
      expect(
        await waitUntil(() => (soloud.getPan(h) + 1).abs() < 1e-3),
        isTrue,
      );
    });

    test('oscillatePan stays within its range', () async {
      final h = soloud.play(tone);
      soloud.oscillatePan(h, -0.5, 0.5, const Duration(milliseconds: 80));
      await Future<void>.delayed(const Duration(milliseconds: 150));
      final seen = <double>{};
      for (var i = 0; i < 20; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 10));
        final p = soloud.getPan(h);
        expect(p, inInclusiveRange(-0.5 - 1e-3, 0.5 + 1e-3));
        seen.add(p);
      }
      expect(seen.length, greaterThan(2));
    });
  });

  group('relative play speed', () {
    test('round-trips', () {
      final h = soloud.play(tone);
      expect(soloud.getRelativePlaySpeed(h), 1);
      soloud.setRelativePlaySpeed(h, 2.5);
      expect(soloud.getRelativePlaySpeed(h), closeTo(2.5, 1e-6));
      soloud.setRelativePlaySpeed(h, 0.5);
      expect(soloud.getRelativePlaySpeed(h), closeTo(0.5, 1e-6));
    });

    test('play(scale:) sets it', () {
      final h = soloud.play(tone, scale: 1.5);
      expect(soloud.getRelativePlaySpeed(h), closeTo(1.5, 1e-6));
    });

    test('a speed of 0 is rejected and the voice keeps its speed', () {
      final h = soloud.play(tone);
      soloud.setRelativePlaySpeed(h, 0);
      expect(soloud.getRelativePlaySpeed(h), greaterThan(0));
    });

    test('fadeRelativePlaySpeed reaches the target', () async {
      final h = soloud.play(tone);
      soloud.fadeRelativePlaySpeed(h, 2, const Duration(milliseconds: 100));
      expect(
        await waitUntil(
          () => (soloud.getRelativePlaySpeed(h) - 2).abs() < 1e-3,
        ),
        isTrue,
      );
    });

    test('oscillateRelativePlaySpeed stays within its range', () async {
      final h = soloud.play(tone);
      soloud.oscillateRelativePlaySpeed(
        h,
        0.5,
        1.5,
        const Duration(milliseconds: 80),
      );
      await Future<void>.delayed(const Duration(milliseconds: 150));
      final seen = <double>{};
      for (var i = 0; i < 20; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 10));
        final s = soloud.getRelativePlaySpeed(h);
        expect(s, inInclusiveRange(0.5 - 1e-3, 1.5 + 1e-3));
        seen.add(s);
      }
      expect(seen.length, greaterThan(2));
    });

    test('a faster voice advances faster', () async {
      final slow = soloud.play(tone, scale: 0.5);
      final fast = soloud.play(tone, scale: 2);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      final s = soloud.getPosition(slow).inMicroseconds;
      final f = soloud.getPosition(fast).inMicroseconds;
      expect(s, greaterThan(0));
      expect(f / s, closeTo(4, 0.5));
    });
  });

  group('pause', () {
    test('play(paused: true) starts paused and does not advance', () async {
      final h = soloud.play(tone, paused: true);
      expect(soloud.getPause(h), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(soloud.getPosition(h), Duration.zero);
    });

    test('setPause and pauseSwitch toggle it', () async {
      final h = soloud.play(tone, paused: true);
      soloud.setPause(h, false);
      expect(soloud.getPause(h), isFalse);
      soloud.pauseSwitch(h);
      expect(soloud.getPause(h), isTrue);
      soloud.pauseSwitch(h);
      expect(soloud.getPause(h), isFalse);
      soloud.setPause(h, true);
      expect(soloud.getPause(h), isTrue);
    });

    test('a paused voice keeps its position', () async {
      final h = soloud.play(tone);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      soloud.setPause(h, true);
      final at = soloud.getPosition(h);
      expect(at, greaterThan(Duration.zero));
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(soloud.getPosition(h), at);
    });

    test('schedulePause pauses the voice later', () async {
      final h = soloud.play(tone);
      soloud.schedulePause(h, const Duration(milliseconds: 100));
      expect(soloud.getPause(h), isFalse);
      expect(await waitUntil(() => soloud.getPause(h)), isTrue);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
    });
  });

  group('stop', () {
    test('stop() ends the voice and emits handleIsNoMoreValid', () async {
      final h = soloud.play(tone);
      final event = tone.soundEvents.firstWhere(
        (e) => e.event == SoundEventType.handleIsNoMoreValid,
      );
      await soloud.stop(h);
      expect(soloud.getIsValidVoiceHandle(h), isFalse);
      expect(tone.handles, isNot(contains(h)));
      final e = await event.timeout(const Duration(seconds: 2));
      expect(e.handle, h);
    });

    test('scheduleStop stops the voice later', () async {
      final h = soloud.play(tone);
      soloud.scheduleStop(h, const Duration(milliseconds: 100));
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(await waitUntil(() => !soloud.getIsValidVoiceHandle(h)), isTrue);
    });

    test('stopAudioSource stops only the voices of that source', () async {
      final wav = await loadTestSound(duration: const Duration(seconds: 5));
      final a = soloud.play(wav);
      final b = soloud.play(wav);
      final c = soloud.play(tone);
      expect(soloud.countAudioSource(wav), 2);
      soloud.stopAudioSource(wav);
      expect(
        await waitUntil(
          () =>
              !soloud.getIsValidVoiceHandle(a) &&
              !soloud.getIsValidVoiceHandle(b),
        ),
        isTrue,
      );
      expect(soloud.getIsValidVoiceHandle(c), isTrue);
      expect(soloud.countAudioSource(wav), 0);
      expect(soloud.isValidAudioSource(wav), isTrue);
    });

    test('countAudioSource counts the voices of a waveform', () {
      soloud
        ..play(tone)
        ..play(tone);
      expect(soloud.countAudioSource(tone), 2);
    });

    test('stopAll stops every voice but keeps the sources', () async {
      soloud
        ..play(tone)
        ..play(tone, paused: true);
      expect(soloud.getVoiceCount(), 2);
      soloud.stopAll();
      expect(soloud.getVoiceCount(), 0);
      expect(soloud.isValidAudioSource(tone), isTrue);
      expect(soloud.getIsValidVoiceHandle(soloud.play(tone)), isTrue);
    });
  });

  group('a finished voice', () {
    late SoundHandle finished;

    setUp(() async {
      final short = await loadTestSound(
        duration: const Duration(milliseconds: 30),
      );
      finished = soloud.play(short);
      expect(
        await waitUntil(() => !soloud.getIsValidVoiceHandle(finished)),
        isTrue,
      );
    });

    test('is no longer valid', () {
      expect(soloud.getIsValidVoiceHandle(finished), isFalse);
    });

    test('can be stopped without error', () async {
      await expectLater(soloud.stop(finished), completes);
    });

    test('pause calls throw SoLoudSoundHandleNotFoundCppException', () {
      expect(
        () => soloud.setPause(finished, true),
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
      expect(
        () => soloud.pauseSwitch(finished),
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
    });

    test('getters report neutral values', () {
      expect(soloud.getPause(finished), isFalse);
      expect(soloud.getVolume(finished), 0);
    });
  });

  group('a handle that never existed', () {
    test('is not valid', () {
      expect(soloud.getIsValidVoiceHandle(_bogus), isFalse);
    });

    test('pause calls throw SoLoudSoundHandleNotFoundCppException', () {
      expect(
        () => soloud.setPause(_bogus, false),
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
      expect(
        () => soloud.pauseSwitch(_bogus),
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
    });

    test('setters, faders and schedulers do not touch live voices', () async {
      final h = soloud.play(tone, volume: 0.5, pan: 0.25);
      soloud
        ..setVolume(_bogus, 1)
        ..setPan(_bogus, -1)
        ..setRelativePlaySpeed(_bogus, 3)
        // Faders and schedulers ignore a handle that is not playing.
        ..fadeVolume(_bogus, 0, Duration.zero)
        ..fadePan(_bogus, 0, Duration.zero)
        ..fadeRelativePlaySpeed(_bogus, 2, Duration.zero)
        ..oscillateVolume(_bogus, 0, 1, Duration.zero)
        ..oscillatePan(_bogus, -1, 1, Duration.zero)
        ..oscillateRelativePlaySpeed(_bogus, 1, 2, Duration.zero)
        ..schedulePause(_bogus, Duration.zero)
        ..scheduleStop(_bogus, Duration.zero);
      await soloud.stop(_bogus);
      await Future<void>.delayed(const Duration(milliseconds: 50));
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(soloud.getVolume(h), closeTo(0.5, 1e-6));
      expect(soloud.getPan(h), closeTo(0.25, 1e-6));
      expect(soloud.getRelativePlaySpeed(h), 1);
      expect(soloud.getPause(h), isFalse);
    });

    test('getters report neutral values', () {
      expect(soloud.getVolume(_bogus), 0);
      expect(soloud.getPause(_bogus), isFalse);
      expect(soloud.getPosition(_bogus), Duration.zero);
    });
  });

  test('play() of a disposed source throws', () async {
    final sound = await loadTestWaveform();
    await soloud.disposeSource(sound);
    expect(soloud.isValidAudioSource(sound), isFalse);
    expect(() => soloud.play(sound), throwsA(isA<SoLoudException>()));
  });
}

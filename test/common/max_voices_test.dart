// setMaxActiveVoiceCount with the real engine and generated sounds (ported
// from example/tests/tests/max_voices.dart): at the limit, playing a sound
// again stops its oldest voice and another sound does not play; lowering the
// limit keeps the voices but only the loudest is heard; protected voices;
// out-of-range limits are ignored; voices are released when they stop.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  /// Plays [count] voices of [sound] at [volume], looping.
  List<SoundHandle> playMany(AudioSource sound, int count, {double? volume}) =>
      [
        for (var i = 0; i < count; i++)
          soloud.play(sound, looping: true, volume: volume ?? 0.01),
      ];

  /// Waits until the mixer has counted [expected] active voices.
  Future<void> expectActive(int expected) async {
    await waitUntil(() => soloud.getActiveVoiceCount() == expected);
    expect(soloud.getActiveVoiceCount(), expected);
  }

  test('the default limit is 16', () {
    expect(soloud.getMaxActiveVoiceCount(), 16);
  });

  for (final (max, played) in [(10, 15), (100, 110), (1000, 1020)]) {
    test(
      '$played plays of a sound, limit $max: the oldest are stopped',
      () async {
        final sound = await loadSine(duration: const Duration(seconds: 3));
        soloud.setMaxActiveVoiceCount(max);
        expect(soloud.getMaxActiveVoiceCount(), max);

        final handles = playMany(sound, played);
        expect(handles, everyElement(isNot(const SoundHandle.error())));
        expect(soloud.getActiveVoiceCount(), max);
        // The replaced voices are stopped by the mixer.
        await waitUntil(() => soloud.getVoiceCount() == max);
        expect(soloud.getVoiceCount(), max);
        expect(soloud.countAudioSource(sound), max);
        expect(
          handles.skip(played - max).every(soloud.getIsValidVoiceHandle),
          isTrue,
        );
        expect(
          handles.take(played - max).any(soloud.getIsValidVoiceHandle),
          isFalse,
        );
        // AudioSource.handles drops them on their voice-ended events.
        await waitUntil(() => sound.handles.length == max);
        expect(sound.handles.length, max);
      },
    );
  }

  test('another sound does not play once the limit is reached', () async {
    final first = await loadTestWaveform();
    final other = await loadTestWaveform(frequency: 880);
    soloud.setMaxActiveVoiceCount(3);
    final handles = playMany(first, 3);
    final h = soloud.play(other);
    expect(soloud.getIsValidVoiceHandle(h), isFalse);
    expect(other.handles, isEmpty);
    expect(soloud.getActiveVoiceCount(), 3);
    expect(handles.every(soloud.getIsValidVoiceHandle), isTrue);

    // A freed slot lets it play.
    await soloud.stop(handles.first);
    final h2 = soloud.play(other, volume: 0.01);
    expect(soloud.getIsValidVoiceHandle(h2), isTrue);
    expect(soloud.getActiveVoiceCount(), 3);
  });

  test('lowering the limit keeps the playing voices', () async {
    final sound = await loadTestWaveform();
    soloud.setMaxActiveVoiceCount(10);
    final handles = playMany(sound, 8);
    soloud.setMaxActiveVoiceCount(3);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    expect(soloud.getVoiceCount(), 8);
    expect(soloud.getActiveVoiceCount(), 8);
    expect(handles.every(soloud.getIsValidVoiceHandle), isTrue);
    // But no new voice: the limit is already exceeded.
    expect(
      soloud.getIsValidVoiceHandle(soloud.play(await loadSine())),
      isFalse,
    );
  });

  for (final loudFirst in [true, false]) {
    test('past the limit the loudest voice is heard '
        '(${loudFirst ? 'loud' : 'quiet'} one played first)', () async {
      final quiet = await loadTestWaveform(frequency: 300);
      final loud = await loadTestWaveform(frequency: 1200);
      soloud.setGlobalVolume(1);
      if (loudFirst) {
        soloud
          ..play(loud, volume: 0.6)
          ..play(quiet, volume: 0.05);
      } else {
        soloud
          ..play(quiet, volume: 0.05)
          ..play(loud, volume: 0.6);
      }
      soloud.setMaxActiveVoiceCount(1);
      expect(soloud.getVoiceCount(), 2);
      final out = await captureOutput();
      expect(out.frequency(0), closeTo(1200, 30));
    });
  }

  test('a protected voice of another sound survives the limit', () async {
    final background = await loadTestWaveform(frequency: 300);
    final effect = await loadTestWaveform(frequency: 1200);
    soloud.setMaxActiveVoiceCount(4);
    final music = soloud.play(background, volume: 0.01);
    soloud.setProtectVoice(music, true);
    playMany(effect, 10, volume: 0.5);
    expect(soloud.getIsValidVoiceHandle(music), isTrue);
    expect(await waitUntil(() => soloud.countAudioSource(effect) == 3), isTrue);
    expect(soloud.getActiveVoiceCount(), 4);
  });

  test(
    'replaying a sound past the limit does not stop its protected voice',
    () async {
      // setProtectVoice: "if you try to play more sounds than there are
      // voices, SoLoud will kill off the oldest playing sound [...]. You can
      // solve this by protecting the sound instance."
      final sound = await loadTestWaveform();
      soloud.setMaxActiveVoiceCount(3);
      final protectedHandle = soloud.play(sound, volume: 0.01);
      soloud.setProtectVoice(protectedHandle, true);
      playMany(sound, 5);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(soloud.getIsValidVoiceHandle(protectedHandle), isTrue);
    },
  );

  test('a limit of 0 or above 1023 is ignored', () {
    soloud.setMaxActiveVoiceCount(20);
    for (final invalid in [0, -1, 1024, 5000]) {
      soloud.setMaxActiveVoiceCount(invalid);
      expect(soloud.getMaxActiveVoiceCount(), 20, reason: '$invalid');
    }
    soloud.setMaxActiveVoiceCount(1023);
    expect(soloud.getMaxActiveVoiceCount(), 1023);
    soloud.setMaxActiveVoiceCount(1);
    expect(soloud.getMaxActiveVoiceCount(), 1);
  });

  test('voices are released when they end or are stopped', () async {
    final short = await loadSine(duration: const Duration(milliseconds: 150));
    final long = await loadTestWaveform();
    soloud.setMaxActiveVoiceCount(10);
    for (var i = 0; i < 6; i++) {
      soloud.play(short, volume: 0.01);
    }
    final longHandles = playMany(long, 3);
    expect(soloud.getVoiceCount(), 9);

    // The short ones end on their own.
    expect(await waitUntil(() => soloud.getVoiceCount() == 3), isTrue);
    expect(soloud.countAudioSource(short), 0);
    await expectActive(3);

    soloud.stopAll();
    expect(await waitUntil(() => soloud.getVoiceCount() == 0), isTrue);
    await expectActive(0);
    expect(longHandles.any(soloud.getIsValidVoiceHandle), isFalse);
  });

  test('disposeAllSources frees every voice and the limit survives', () async {
    final sound = await loadTestWaveform();
    soloud.setMaxActiveVoiceCount(10);
    playMany(sound, 15);
    expect(soloud.getActiveVoiceCount(), 10);
    await soloud.disposeAllSources();
    expect(soloud.getVoiceCount(), 0);
    await expectActive(0);
    expect(soloud.getMaxActiveVoiceCount(), 10);

    final again = await loadTestWaveform();
    playMany(again, 12);
    expect(soloud.getActiveVoiceCount(), 10);
  });

  test('1023 voices can play at once', () async {
    final sound = await loadTestWaveform();
    soloud.setMaxActiveVoiceCount(1023);
    final handles = playMany(sound, 1023, volume: 0.001);
    expect(soloud.getVoiceCount(), 1023);
    expect(handles.every(soloud.getIsValidVoiceHandle), isTrue);
    await expectActive(1023);
  });

  test('the limit resets to 16 after deinit and init', () async {
    soloud
      ..setMaxActiveVoiceCount(50)
      ..deinit();
    await initTestEngine();
    expect(soloud.getMaxActiveVoiceCount(), 16);
  });
}

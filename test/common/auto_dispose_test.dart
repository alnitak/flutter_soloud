// autoDispose and allInstancesFinished with the real engine and generated
// sounds (ported from example/tests/tests/auto_dispose.dart and
// all_instances_finished.dart): a source is disposed when its last voice
// ends, not while a voice is playing or paused; allInstancesFinished fires
// once each time the number of voices drops to zero; the same holds for a
// buffer stream.
@Tags(['device'])
library;

import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

const _short = Duration(milliseconds: 200);

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  /// Counts the events of [sound] by type.
  Map<SoundEventType, int> countEvents(AudioSource sound) {
    final counts = <SoundEventType, int>{};
    sound.soundEvents.listen(
      (e) => counts[e.event] = (counts[e.event] ?? 0) + 1,
    );
    return counts;
  }

  /// Counts the allInstancesFinished events of [sound].
  List<void> countFinished(AudioSource sound) {
    final events = <void>[];
    sound.allInstancesFinished.listen(events.add);
    return events;
  }

  group('autoDispose', () {
    test('disposes the source when its last voice ends', () async {
      final sound = await loadTestSound(duration: _short, autoDispose: true);
      final events = countEvents(sound);
      soloud.play(sound);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      soloud.play(sound);

      expect(
        await waitUntil(() => !soloud.isValidAudioSource(sound)),
        isTrue,
        reason: 'not disposed after its voices ended',
      );
      expect(soloud.activeSounds, isNot(contains(sound)));
      expect(events[SoundEventType.handleIsNoMoreValid], 2);
      expect(events[SoundEventType.soundDisposed], 1);
      expect(sound.handles, isEmpty);
    });

    test('keeps the source while another voice still plays', () async {
      final sound = await loadTestSound(duration: _short, autoDispose: true);
      final first = soloud.play(sound);
      final long = soloud.play(sound, looping: true);
      expect(
        await waitUntil(() => !soloud.getIsValidVoiceHandle(first)),
        isTrue,
      );
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(soloud.isValidAudioSource(sound), isTrue);
      expect(soloud.getIsValidVoiceHandle(long), isTrue);

      await soloud.stop(long);
      expect(await waitUntil(() => !soloud.isValidAudioSource(sound)), isTrue);
    });

    test('keeps the source while its voice is paused', () async {
      final sound = await loadTestSound(duration: _short, autoDispose: true);
      final h = soloud.play(sound, paused: true);
      await Future<void>.delayed(_short * 3);
      expect(soloud.isValidAudioSource(sound), isTrue);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);

      final h2 = soloud.play(sound);
      soloud.setPause(h2, true);
      await Future<void>.delayed(_short * 3);
      expect(soloud.isValidAudioSource(sound), isTrue);

      soloud
        ..setPause(h, false)
        ..setPause(h2, false);
      expect(await waitUntil(() => !soloud.isValidAudioSource(sound)), isTrue);
    });

    test('stop() of the last voice disposes the source', () async {
      final sound = await loadTestWaveform();
      sound.autoDispose = true;
      final events = countEvents(sound);
      final h = soloud.play(sound);
      await soloud.stop(h);
      expect(await waitUntil(() => !soloud.isValidAudioSource(sound)), isTrue);
      expect(events[SoundEventType.soundDisposed], 1);
    });

    test('a source that never played is not disposed', () async {
      final sound = await loadTestSound(duration: _short, autoDispose: true);
      await Future<void>.delayed(_short * 2);
      expect(soloud.isValidAudioSource(sound), isTrue);
    });

    test('without autoDispose the source stays loaded', () async {
      final sound = await loadTestSound(duration: _short);
      soloud.play(sound);
      expect(await waitUntil(() => sound.handles.isEmpty), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(soloud.isValidAudioSource(sound), isTrue);
      expect(soloud.getLength(sound), greaterThan(Duration.zero));
    });

    test('setting autoDispose after load applies to the next end', () async {
      final sound = await loadTestSound(duration: _short);
      soloud.play(sound);
      expect(await waitUntil(() => sound.handles.isEmpty), isTrue);
      expect(soloud.isValidAudioSource(sound), isTrue);

      sound.autoDispose = true;
      soloud.play(sound);
      expect(await waitUntil(() => !soloud.isValidAudioSource(sound)), isTrue);
    });

    test('a buffer stream is disposed when its voices end', () async {
      final sound = soloud.setBufferStream(
        format: BufferType.auto,
        bufferingTimeNeeds: 0.05,
        maxBufferSizeBytes: 1024 * 1024,
        autoDispose: true,
      );
      final events = countEvents(sound);
      soloud
        ..addAudioDataStream(sound, wavBytes(duration: _short))
        ..setDataIsEnded(sound)
        ..play(sound);
      await Future<void>.delayed(const Duration(milliseconds: 50));
      soloud.play(sound);
      expect(
        await waitUntil(() => !soloud.isValidAudioSource(sound)),
        isTrue,
        reason: 'buffer stream not disposed',
      );
      expect(events[SoundEventType.soundDisposed], 1);
    });

    test('several auto-disposed sources go independently', () async {
      final a = await loadTestSound(duration: _short, autoDispose: true);
      final b = await loadTestSound(
        duration: const Duration(milliseconds: 600),
        autoDispose: true,
      );
      soloud
        ..play(a)
        ..play(b);
      expect(await waitUntil(() => !soloud.isValidAudioSource(a)), isTrue);
      expect(soloud.isValidAudioSource(b), isTrue);
      expect(await waitUntil(() => !soloud.isValidAudioSource(b)), isTrue);
    });
  });

  group('allInstancesFinished', () {
    test('fires once when the last of several voices ends', () async {
      final sound = await loadTestSound(duration: _short);
      final finished = countFinished(sound);
      soloud
        ..play(sound)
        ..play(sound)
        ..play(sound);
      expect(await waitUntil(() => sound.handles.isEmpty), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(finished, hasLength(1));
    });

    test('fires again each time the voices drop to zero', () async {
      final sound = await loadTestSound(duration: _short);
      final finished = countFinished(sound);
      for (var round = 1; round <= 3; round++) {
        soloud
          ..play(sound)
          ..play(sound);
        expect(await waitUntil(() => finished.length == round), isTrue);
        await Future<void>.delayed(const Duration(milliseconds: 50));
        expect(finished, hasLength(round));
      }
    });

    test('does not fire while a voice is paused', () async {
      final sound = await loadTestSound(duration: _short);
      final finished = countFinished(sound);
      final paused = soloud.play(sound, paused: true);
      soloud.play(sound);
      await Future<void>.delayed(_short * 3);
      expect(finished, isEmpty);
      soloud.setPause(paused, false);
      expect(await waitUntil(() => finished.length == 1), isTrue);
    });

    test('fires when the voices are stopped', () async {
      final song = await loadTestWaveform();
      final finished = countFinished(song);
      final a = soloud.play(song);
      final b = soloud.play(song);
      await soloud.stop(a);
      await Future<void>.delayed(const Duration(milliseconds: 50));
      expect(finished, isEmpty);
      await soloud.stop(b);
      expect(await waitUntil(() => finished.length == 1), isTrue);
    });

    test('its first event can dispose the source', () async {
      // As in all_instances_finished.dart: dispose on the first event.
      final explosion = await loadTestSound(duration: _short);
      final song = await loadTestWaveform();
      var explosionDisposed = false;
      var songDisposed = false;
      unawaited(
        explosion.allInstancesFinished.first.then((_) async {
          await soloud.disposeSource(explosion);
          explosionDisposed = true;
        }),
      );
      unawaited(
        song.allInstancesFinished.first.then((_) async {
          await soloud.disposeSource(song);
          songDisposed = true;
        }),
      );
      soloud.play(explosion);
      final songHandle = soloud.play(song);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      soloud.play(explosion);

      expect(await waitUntil(() => explosionDisposed), isTrue);
      expect(songDisposed, isFalse);
      await soloud.stop(songHandle);
      expect(await waitUntil(() => songDisposed), isTrue);
      expect(soloud.activeSounds, isEmpty);
    });
  });
}

// Many loads in parallel with the real engine and generated sounds (ported
// from example/tests/tests/async_multi_load.dart and join_two_sources.dart):
// loadMem of different and of identical content at the same time, each load
// giving its own source with its own hash, deinit while loads are running,
// and joinTwoSources (also in parallel with other loads).
@Tags(['device'])
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  /// Duration of the i-th generated sound.
  Duration lengthOf(int i) => Duration(milliseconds: 100 + 50 * i);

  test('different sounds loaded in parallel', () async {
    const count = 8;
    final sounds = await Future.wait([
      for (var i = 0; i < count; i++)
        soloud.loadMem(
          'parallel_$i.wav',
          wavBytes(duration: lengthOf(i), frequency: 200.0 + 100 * i),
        ),
    ]);
    expect(sounds.map((s) => s.soundHash).toSet(), hasLength(count));
    expect(soloud.activeSounds, hasLength(count));
    for (var i = 0; i < count; i++) {
      expect(sounds[i].soundPath, 'parallel_$i.wav');
      expect(
        soloud.getLength(sounds[i]).inMilliseconds,
        closeTo(lengthOf(i).inMilliseconds, 2),
        reason: 'sound $i got the content of another load',
      );
    }
  });

  test('the same path loaded many times in parallel', () async {
    final bytes = wavBytes(duration: const Duration(milliseconds: 300));
    final sounds = await Future.wait([
      for (var i = 0; i < 6; i++) soloud.loadMem('same.wav', bytes),
    ]);
    // Every load is a source of its own (a warning, not an error).
    expect(sounds.map((s) => s.soundHash).toSet(), hasLength(6));
    expect(soloud.activeSounds, hasLength(6));
    for (final s in sounds) {
      expect(s.soundHash.isValid, isTrue);
      expect(soloud.getLength(s).inMilliseconds, closeTo(300, 2));
      final h = soloud.play(s, volume: 0.01);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(soloud.countAudioSource(s), 1);
    }
  });

  test('the same path loaded again one after the other', () async {
    final bytes = wavBytes(duration: const Duration(milliseconds: 300));
    final first = await soloud.loadMem('again.wav', bytes);
    final second = await soloud.loadMem('again.wav', bytes);
    final third = await soloud.loadMem('again.wav', bytes);
    expect({first.soundHash, second.soundHash, third.soundHash}, hasLength(3));
    expect(soloud.activeSounds, hasLength(3));
  });

  test('disposing one duplicate leaves the others playable', () async {
    final bytes = wavBytes();
    final sounds = await Future.wait([
      for (var i = 0; i < 3; i++) soloud.loadMem('dup.wav', bytes),
    ]);
    await soloud.disposeSource(sounds[1]);
    expect(soloud.isValidAudioSource(sounds[1]), isFalse);
    expect(soloud.isValidAudioSource(sounds[0]), isTrue);
    expect(soloud.isValidAudioSource(sounds[2]), isTrue);
    for (final s in [sounds[0], sounds[2]]) {
      final h = soloud.play(s, volume: 0.01);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
    }
    expect(soloud.activeSounds, hasLength(2));
  });

  test('identical content under different paths', () async {
    final bytes = wavBytes(duration: const Duration(milliseconds: 250));
    final sounds = await Future.wait([
      for (var i = 0; i < 4; i++) soloud.loadMem('content_$i.wav', bytes),
    ]);
    expect(sounds.map((s) => s.soundHash).toSet(), hasLength(4));
    for (final s in sounds) {
      expect(soloud.getLength(s).inMilliseconds, closeTo(250, 2));
    }
  });

  test('parallel loads mixed with a failing one', () async {
    final results = await Future.wait([
      soloud.loadMem('ok_1.wav', wavBytes()),
      soloud
          .loadMem('broken.wav', Uint8List.fromList(List.filled(256, 7)))
          .then<Object>((s) => s, onError: (Object e) => e),
      soloud.loadMem('ok_2.wav', wavBytes()),
    ]);
    expect(results[0], isA<AudioSource>());
    expect(results[1], isA<SoLoudException>());
    expect(results[2], isA<AudioSource>());
    expect(soloud.activeSounds, hasLength(2));
  });

  /// Starts six loads, deinitializes the engine while they run and returns
  /// what each load completed with.
  Future<List<Object?>> loadsInterruptedByDeinit() async {
    final loads = [
      for (var i = 0; i < 6; i++)
        soloud
            .loadMem(
              'during_deinit_$i.wav',
              wavBytes(duration: const Duration(seconds: 2)),
            )
            .then<Object?>((s) => s, onError: (Object e) => e),
    ];
    soloud.deinit();
    return Future.wait(loads).timeout(const Duration(seconds: 10));
  }

  test('deinit while loads are running fails them cleanly', () async {
    // Collect uncaught errors here, so that this test checks the results
    // even if the failures also escape to the zone.
    final uncaught = <Object>[];
    final results = await runZonedGuarded(
      loadsInterruptedByDeinit,
      (e, _) => uncaught.add(e),
    )!;
    for (final r in results) {
      // Each load either finished before the deinit or failed cleanly.
      expect(r, anyOf(isA<AudioSource>(), isA<SoLoudException>()));
    }
    expect(uncaught, isEmpty);
    expect(soloud.isInitialized, isFalse);

    await initTestEngine();
    expect(soloud.activeSounds, isEmpty);
    final sound = await loadTestSound();
    expect(soloud.getIsValidVoiceHandle(soloud.play(sound)), isTrue);
  });

  test('deinit while loads are running raises no uncaught error', () async {
    final results = await loadsInterruptedByDeinit();
    expect(results, hasLength(6));
  });

  group('joinTwoSources', () {
    test('lasts as long as the longer source', () async {
      final joined = await soloud.joinTwoSources(
        'joined.wav',
        wavBytes(duration: const Duration(milliseconds: 300)),
        wavBytes(duration: const Duration(milliseconds: 700), frequency: 880),
      );
      expect(soloud.getLength(joined).inMilliseconds, closeTo(700, 50));
      final h = soloud.play(joined, volume: 0.01);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      await soloud.stop(h);
      await soloud.disposeSource(joined);
      expect(soloud.activeSounds, isEmpty);
    });

    test('plays the first source left and the second right', () async {
      final joined = await soloud.joinTwoSources(
        'left_right.wav',
        wavBytes(duration: const Duration(seconds: 2), frequency: 300),
        wavBytes(duration: const Duration(seconds: 2), frequency: 1200),
      );
      soloud
        ..setGlobalVolume(1)
        ..play(joined, volume: 0.5);
      final out = await captureOutput();
      expect(out.frequency(0), closeTo(300, 15));
      expect(out.frequency(1), closeTo(1200, 30));
    });

    test('stereo sources are mixed to mono before joining', () async {
      final joined = await soloud.joinTwoSources(
        'stereo_in.wav',
        wavBytes(
          duration: const Duration(seconds: 2),
          frequency: 300,
          channels: 2,
        ),
        wavBytes(duration: const Duration(seconds: 2), frequency: 1200),
      );
      soloud
        ..setGlobalVolume(1)
        ..play(joined, volume: 0.5);
      final out = await captureOutput();
      expect(out.frequency(0), closeTo(300, 15));
      expect(out.frequency(1), closeTo(1200, 30));
    });

    test('in parallel with other loads', () async {
      final results = await Future.wait([
        soloud.joinTwoSources('j1', wavBytes(), wavBytes(frequency: 880)),
        soloud.loadMem('between.wav', wavBytes()),
        soloud.joinTwoSources(
          'j2',
          wavBytes(duration: const Duration(milliseconds: 500)),
          wavBytes(duration: const Duration(milliseconds: 200)),
        ),
      ]);
      expect(results.map((s) => s.soundHash).toSet(), hasLength(3));
      expect(soloud.getLength(results[0]).inMilliseconds, closeTo(1000, 50));
      expect(soloud.getLength(results[2]).inMilliseconds, closeTo(500, 50));
      expect(soloud.activeSounds, hasLength(3));
    });

    test('an invalid buffer fails the join', () async {
      await expectLater(
        soloud.joinTwoSources(
          'bad_join',
          wavBytes(),
          Uint8List.fromList(List.filled(128, 3)),
        ),
        throwsA(isA<SoLoudException>()),
      );
      expect(soloud.activeSounds, isEmpty);
    });
  });
}

// SoLoud engine lifecycle with the real engine: init, repeated init, invalid
// parameters, isInitialized, deinit while playing and the
// SoLoudNotInitializedException thrown by the API before init.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;

  tearDown(deinitTestEngine);

  group('before init', () {
    test('isInitialized is false', () {
      expect(soloud.isInitialized, isFalse);
    });

    test('the API throws SoLoudNotInitializedException', () async {
      const handle = SoundHandle(1);
      final notInitialized = throwsA(isA<SoLoudNotInitializedException>());

      expect(() => soloud.setGlobalVolume(0), notInitialized);
      expect(soloud.getGlobalVolume, notInitialized);
      expect(() => soloud.getVolume(handle), notInitialized);
      expect(() => soloud.setVolume(handle, 1), notInitialized);
      expect(() => soloud.getPan(handle), notInitialized);
      expect(() => soloud.setPan(handle, 0), notInitialized);
      expect(() => soloud.setPause(handle, true), notInitialized);
      expect(() => soloud.pauseSwitch(handle), notInitialized);
      expect(() => soloud.getPause(handle), notInitialized);
      expect(() => soloud.setRelativePlaySpeed(handle, 1), notInitialized);
      expect(() => soloud.getRelativePlaySpeed(handle), notInitialized);
      expect(() => soloud.getIsValidVoiceHandle(handle), notInitialized);
      expect(() => soloud.fadeVolume(handle, 0, Duration.zero), notInitialized);
      expect(
        () => soloud.oscillateVolume(handle, 0, 1, Duration.zero),
        notInitialized,
      );
      expect(() => soloud.schedulePause(handle, Duration.zero), notInitialized);
      expect(() => soloud.scheduleStop(handle, Duration.zero), notInitialized);
      expect(soloud.stopAll, notInitialized);
      expect(soloud.getActiveVoiceCount, notInitialized);
      expect(soloud.getVoiceCount, notInitialized);
      expect(soloud.getVisualizationEnabled, notInitialized);
      expect(soloud.getEngineTime, notInitialized);
      expect(
        () => soloud.fadeGlobalFilterParameter(
          FilterType.echoFilter,
          0,
          0,
          Duration.zero,
        ),
        notInitialized,
      );
      expect(() => soloud.speechText('hi'), notInitialized);
      await expectLater(
        soloud.loadWaveform(WaveForm.sin, false, 1, 0),
        notInitialized,
      );
      await expectLater(
        soloud.loadMem('x.wav', wavBytes(duration: Duration.zero)),
        notInitialized,
      );
      await expectLater(soloud.stop(handle), notInitialized);
      await expectLater(soloud.disposeAllSources(), notInitialized);
      await expectLater(soloud.changeDevice(), notInitialized);
    });

    test('a global filter cannot be activated', () {
      expect(
        soloud.filters.echoFilter.activate,
        throwsA(isA<SoLoudBackendNotInitedException>()),
      );
      expect(soloud.filters.echoFilter.isActive, isFalse);
    });

    test('createVoiceGroup throws instead of returning a handle', () {
      expect(
        soloud.createVoiceGroup,
        throwsA(isA<SoLoudCreateVoiceGroupDartException>()),
      );
    });

    test('deinit is a no-op', () {
      expect(soloud.deinit, returnsNormally);
      expect(soloud.isInitialized, isFalse);
    });

    test('deinitAsync is a no-op', () async {
      await soloud.deinitAsync();
      expect(soloud.isInitialized, isFalse);
    });
  });

  group('init', () {
    test('initializes the engine', () async {
      await soloud.init();
      expect(soloud.isInitialized, isTrue);
      expect(soloud.getActiveVoiceCount(), 0);
      expect(soloud.activeSounds, isEmpty);
    });

    test(
      'a second init re-initializes the engine and drops its sounds',
      () async {
        await initTestEngine();
        final sound = await loadTestWaveform();
        final handle = soloud.play(sound, looping: true);
        expect(soloud.getIsValidVoiceHandle(handle), isTrue);

        await soloud.init();
        expect(soloud.isInitialized, isTrue);
        expect(soloud.activeSounds, isEmpty);
        expect(soloud.getActiveVoiceCount(), 0);
        expect(soloud.getIsValidVoiceHandle(handle), isFalse);

        // And it works as a fresh engine.
        soloud.setGlobalVolume(0.1);
        final again = await loadTestWaveform();
        final h = soloud.play(again);
        expect(soloud.getIsValidVoiceHandle(h), isTrue);
      },
    );

    test('concurrent init calls all complete with the engine ready', () async {
      await Future.wait([soloud.init(), soloud.init(), soloud.init()]);
      expect(soloud.isInitialized, isTrue);
      soloud.setGlobalVolume(0);
      final sound = await loadTestWaveform();
      expect(soloud.getIsValidVoiceHandle(soloud.play(sound)), isTrue);
    });

    test('init after deinit works again', () async {
      for (var i = 0; i < 3; i++) {
        await initTestEngine();
        expect(soloud.isInitialized, isTrue);
        final sound = await loadTestWaveform();
        expect(soloud.getIsValidVoiceHandle(soloud.play(sound)), isTrue);
        soloud.deinit();
        expect(soloud.isInitialized, isFalse);
      }
    });

    test('honours the mono channel count', () async {
      await soloud.init(channels: Channels.mono, sampleRate: 22050);
      expect(soloud.isInitialized, isTrue);
      soloud.setGlobalVolume(0);
      final sound = await loadTestWaveform();
      expect(soloud.getIsValidVoiceHandle(soloud.play(sound)), isTrue);
    });

    test(
      'a device id that does not exist fails and leaves no engine',
      () async {
        await expectLater(
          soloud.init(
            // ignore: invalid_use_of_internal_member
            device: const PlaybackDevice(9999, false, 'missing'),
          ),
          throwsA(isA<SoLoudCppException>()),
        );
        expect(soloud.isInitialized, isFalse);
        // A failed init does not block the next one.
        await soloud.init();
        expect(soloud.isInitialized, isTrue);
      },
    );

    test('a deinit before init completes stops it', () async {
      final pending = soloud.init();
      soloud.deinit();
      await expectLater(
        pending,
        throwsA(isA<SoLoudInitializationStoppedByDeinitException>()),
      );
      expect(soloud.isInitialized, isFalse);
    });
  });

  group('deinit', () {
    test('while playing stops everything and invalidates the API', () async {
      await initTestEngine(globalVolume: 0);
      final sound = await loadTestWaveform();
      final looping = soloud.play(sound, looping: true);
      final paused = soloud.play(sound, paused: true);
      expect(soloud.getActiveVoiceCount(), greaterThan(0));

      soloud.deinit();
      expect(soloud.isInitialized, isFalse);
      expect(soloud.activeSounds, isEmpty);
      expect(
        () => soloud.getIsValidVoiceHandle(looping),
        throwsA(isA<SoLoudNotInitializedException>()),
      );
      expect(
        () => soloud.setPause(paused, false),
        throwsA(isA<SoLoudNotInitializedException>()),
      );
      expect(
        () => soloud.play(sound),
        throwsA(isA<SoLoudNotInitializedException>()),
      );
    });

    test('deinitAsync while playing', () async {
      await initTestEngine(globalVolume: 0);
      final sound = await loadTestSound();
      soloud.play(sound, looping: true);
      await soloud.deinitAsync();
      expect(soloud.isInitialized, isFalse);
      expect(soloud.activeSounds, isEmpty);
    });

    test('deinit twice is harmless', () async {
      await initTestEngine();
      soloud
        ..deinit()
        ..deinit();
      expect(soloud.isInitialized, isFalse);
    });
  });
}

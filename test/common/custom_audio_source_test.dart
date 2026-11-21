// Custom audio sources (`SoLoud.createCustomAudioSource`) with the real
// engine: creation and invalid parameters, play, pause, looping, volume, 3D,
// a bus, several voices and `singleInstance`, and disposing the source while
// it plays. The DSP callback is the test synth of support/custom_dsp.dart;
// levels are measured on the captured mixer output.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/custom_dsp.dart';
import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

void main() {
  setUpEngineTests(globalVolume: 0.2);
  final soloud = SoLoud.instance;

  AudioSource newSource({
    Channels channels = Channels.stereo,
    bool singleInstance = false,
  }) => soloud.createCustomAudioSource(
    sampleRate: 44100,
    channels: channels,
    webJsFunctionName: installTestDsp(),
    singleInstance: singleInstance,
  );

  /// Level of the louder output channel.
  double level(CapturedOutput out) =>
      out.rms(0) > out.rms(1) ? out.rms(0) : out.rms(1);

  final silentSkip = testDspIsAudible ? null : testDspSilentReason;

  group('creation', () {
    test('returns a registered source', () {
      final source = newSource();
      expect(soloud.activeSounds, contains(source));
      expect(source.soundHash.isValid, isTrue);
    });

    test('works for every channel count', () {
      for (final channels in Channels.values) {
        expect(
          () => newSource(channels: channels),
          returnsNormally,
          reason: '$channels',
        );
      }
    });

    test('without a callback or a JS function name throws', () {
      expect(
        soloud.createCustomAudioSource,
        throwsA(isA<SoLoudCppException>()),
      );
      expect(
        () => soloud.createCustomAudioSource(webJsFunctionName: ''),
        throwsA(isA<SoLoudCppException>()),
      );
    });

    test('a zero sample rate throws', () {
      expect(
        () => soloud.createCustomAudioSource(
          sampleRate: 0,
          webJsFunctionName: installTestDsp(),
        ),
        throwsA(isA<SoLoudCppException>()),
      );
    });

    test('different lifecycle callbacks throw', () {
      expect(
        () => soloud.createCustomAudioSource(
          webJsFunctionName: installTestDsp(),
          onInstanceCreatedAddress: 1,
          onInstanceDestroyedAddress: 2,
        ),
        throwsArgumentError,
      );
    });
  });

  group('playback', () {
    test('play gives a valid voice that keeps playing', () async {
      final h = soloud.play(newSource());
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(soloud.getPause(h), isFalse);
    });

    test('is heard', () async {
      soloud.play(newSource());
      final out = await captureOutput();
      expect(level(out), greaterThan(0.01));
    }, skip: silentSkip);

    test('pause silences it and resume brings it back', () async {
      // Something must play for the capture to run.
      final silence = await loadSilence(duration: const Duration(seconds: 5));
      final h = soloud.play(newSource());
      soloud
        ..setPause(h, true)
        ..play(silence);
      final paused = await captureOutput();
      expect(level(paused), lessThan(1e-4));
      soloud.setPause(h, false);
      final resumed = await captureOutput();
      expect(level(resumed), greaterThan(0.01));
    }, skip: silentSkip);

    test('looping round-trips', () {
      final h = soloud.play(newSource(), looping: true);
      expect(soloud.getLooping(h), isTrue);
      soloud.setLooping(h, false);
      expect(soloud.getLooping(h), isFalse);
    });

    test('volume scales the output', () async {
      final source = newSource();
      final h = soloud.play(source);
      final full = level(await captureOutput());
      soloud.setVolume(h, 0.25);
      final quarter = level(await captureOutput());
      expect(quarter / full, closeTo(0.25, 0.05));
    }, skip: silentSkip);

    test('3D position pans it', () async {
      soloud
        ..set3dListenerParameters(0, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0)
        ..play3d(newSource(), 5, 0, 0);
      final out = await captureOutput();
      expect(out.rms(1), greaterThan(out.rms(0) * 2), reason: 'on the right');
    }, skip: silentSkip);

    test('plays through a bus, and the bus volume applies', () async {
      final bus = soloud.createMixingBus()..playOnEngine();
      addTearDown(() {
        if (Buses().buses.contains(bus)) bus.dispose();
      });
      bus.play(newSource());
      expect(level(await captureOutput()), greaterThan(0.01));
      final silence = await loadSilence(duration: const Duration(seconds: 5));
      soloud
        ..setVolume(bus.soundHandle!, 0)
        ..play(silence);
      expect(level(await captureOutput()), lessThan(1e-4));
    }, skip: silentSkip);
  });

  group('voices', () {
    test('a source plays several voices at once', () {
      final source = newSource();
      final a = soloud.play(source);
      final b = soloud.play(source);
      expect(a, isNot(b));
      expect(soloud.getIsValidVoiceHandle(a), isTrue);
      expect(soloud.getIsValidVoiceHandle(b), isTrue);
      expect(source.handles, containsAll([a, b]));
    });

    test('singleInstance stops the voice already playing', () async {
      final source = newSource(singleInstance: true);
      final a = soloud.play(source);
      final b = soloud.play(source);
      expect(soloud.getIsValidVoiceHandle(b), isTrue);
      expect(await waitUntil(() => !soloud.getIsValidVoiceHandle(a)), isTrue);
    });
  });

  group('dispose', () {
    test('while playing stops its voices and silences it', () async {
      final source = newSource();
      final h = soloud.play(source);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      await soloud.disposeSource(source);
      expect(soloud.getIsValidVoiceHandle(h), isFalse);
      expect(soloud.activeSounds, isNot(contains(source)));
      soloud.play(await loadSilence(duration: const Duration(seconds: 5)));
      expect(level(await captureOutput()), lessThan(1e-4));
    });

    test('playing a disposed source throws', () async {
      final source = newSource();
      await soloud.disposeSource(source);
      expect(() => soloud.play(source), throwsA(isA<SoLoudException>()));
    });
  });
}

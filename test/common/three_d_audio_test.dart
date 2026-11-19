// 3D audio with the real engine. SoLoud has no getters for the 3D state of a
// voice, so the assertions are made on the captured mixer output: the level
// of each channel for positions, attenuation and listener orientation, and
// the frequency of a steady tone for the doppler shift.
@TestOn('vm')
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// SoLoud attenuation models (see SoLoud::AudioSource::ATTENUATION_MODELS).
const _noAttenuation = 0;
const _inverseDistance = 1;
const _linearDistance = 2;
const _exponentialDistance = 3;

void main() {
  // The output must be audible to be measured: keep it quiet but non-zero.
  setUpEngineTests(globalVolume: 0.05);
  final soloud = SoLoud.instance;

  late AudioSource tone;
  setUp(() async {
    tone = await loadTestWaveform();
    // A listener at the origin looking down -Z, as SoLoud defaults to.
    soloud.set3dListenerParameters(0, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0);
  });

  /// Plays [tone] in 3D at ([x], [y], [z]), lets [configure] change it, and
  /// returns the captured output.
  Future<CapturedOutput> render(
    double x,
    double y,
    double z, {
    void Function(SoundHandle handle)? configure,
  }) async {
    final h = soloud.play3d(tone, x, y, z, looping: true);
    configure?.call(h);
    final out = await captureOutput();
    await soloud.stop(h);
    return out;
  }

  /// Level of the louder channel.
  double level(CapturedOutput out) =>
      out.rms(0) > out.rms(1) ? out.rms(0) : out.rms(1);

  group('sound speed', () {
    test('round-trips', () {
      soloud.set3dSoundSpeed(100);
      expect(soloud.get3dSoundSpeed(), closeTo(100, 1e-3));
      soloud.set3dSoundSpeed(343);
      expect(soloud.get3dSoundSpeed(), closeTo(343, 1e-3));
    });

    test(
      'is kept when set before any sound is loaded',
      () async {
        await soloud.disposeAllSources();
        soloud.set3dSoundSpeed(100);
        await loadTestWaveform();
        expect(soloud.get3dSoundSpeed(), closeTo(100, 1e-3));
      },
      skip:
          'bug E5: every 3D setter/getter export returns early while no sound '
          'is loaded (getSoundsCount() == 0, src/bindings.cpp:4862 and the '
          'following 3D exports), so set3dSoundSpeed and '
          'set3dListener* called before the first load are silently lost '
          'and get3dSoundSpeed reads 0',
    );
  });

  group('panning', () {
    test('a source in front is centred', () async {
      final out = await render(0, 0, -1);
      expect(out.rms(0), greaterThan(0.001), reason: 'it is audible');
      expect(out.rms(0) / out.rms(1), closeTo(1, 0.05));
    });

    test('a source on the right is louder on the right', () async {
      final out = await render(5, 0, 0);
      expect(out.rms(1), greaterThan(out.rms(0) * 2));
    });

    test('a source on the left is louder on the left', () async {
      final out = await render(-5, 0, 0);
      expect(out.rms(0), greaterThan(out.rms(1) * 2));
    });

    test('set3dSourcePosition moves a playing source', () async {
      final out = await render(
        -5,
        0,
        0,
        configure: (h) => soloud.set3dSourcePosition(h, 5, 0, 0),
      );
      expect(out.rms(1), greaterThan(out.rms(0) * 2));
    });

    test('set3dSourceParameters moves a playing source', () async {
      final out = await render(
        -5,
        0,
        0,
        configure: (h) => soloud.set3dSourceParameters(h, 5, 0, 0, 0, 0, 0),
      );
      expect(out.rms(1), greaterThan(out.rms(0) * 2));
    });

    test('turning the listener around swaps the sides', () async {
      soloud.set3dListenerAt(0, 0, 1);
      final out = await render(5, 0, 0);
      expect(out.rms(0), greaterThan(out.rms(1) * 2));
    });

    test(
      'a listener upside down (set3dListenerParameters) swaps the sides',
      () async {
        soloud.set3dListenerParameters(0, 0, 0, 0, 0, -1, 0, -1, 0, 0, 0, 0);
        final out = await render(5, 0, 0);
        expect(out.rms(0), greaterThan(out.rms(1) * 2));
      },
    );

    test(
      'a listener upside down (set3dListenerUp) swaps the sides',
      () async {
        soloud.set3dListenerUp(0, -1, 0);
        final out = await render(5, 0, 0);
        expect(out.rms(0), greaterThan(out.rms(1) * 2));
      },
      skip:
          'bug E4: Player::set3dListenerUp calls soloud.set3dListenerAt '
          '(src/player.cpp:4194), so it overwrites the "at" vector and '
          'leaves "up" unchanged',
    );

    test('moving the listener past the source swaps the sides', () async {
      soloud.set3dListenerPosition(10, 0, 0);
      final out = await render(5, 0, 0);
      expect(out.rms(0), greaterThan(out.rms(1) * 2));
    });
  });

  group('attenuation', () {
    /// Level at [distance] in front of the listener with [model].
    Future<double> levelAt(
      double distance,
      int model, {
      double rolloff = 1,
      double min = 1,
      double max = 100,
    }) async {
      final out = await render(
        0,
        0,
        -distance,
        configure: (h) {
          soloud
            ..set3dSourceMinMaxDistance(h, min, max)
            ..set3dSourceAttenuation(h, model, rolloff);
        },
      );
      return level(out);
    }

    const attenuatedTwice =
        'bug E2: the 3D attenuation is applied twice, so the level follows the '
        'square of the model: update3dVoices_internal puts it both in '
        'mChannelVolume (src/soloud/src/core/soloud_core_3d.cpp:296) and in '
        'm3dVolume (:301), which updateVoiceVolume_internal multiplies into '
        'mOverallVolume (soloud_core_voiceops.cpp:201); the mixer uses '
        'mChannelVolume * mOverallVolume (soloud.cpp:2082)';

    test('by default distance does not attenuate', () async {
      final near = level(await render(0, 0, -1));
      final far = level(await render(0, 0, -50));
      expect(far / near, closeTo(1, 0.05));
    });

    test('no attenuation keeps the level', () async {
      final near = await levelAt(1, _noAttenuation);
      final far = await levelAt(8, _noAttenuation);
      expect(far / near, closeTo(1, 0.05));
    });

    for (final model in [
      _inverseDistance,
      _linearDistance,
      _exponentialDistance,
    ]) {
      test('model $model: a farther source is quieter', () async {
        final near = await levelAt(1, model, max: 11);
        final mid = await levelAt(3, model, max: 11);
        final far = await levelAt(6, model, max: 11);
        expect(mid, lessThan(near * 0.95));
        expect(far, lessThan(mid * 0.95));
      });
    }

    test('inverse distance: min / (min + rolloff * (d - min))', () async {
      final near = await levelAt(1, _inverseDistance);
      final far = await levelAt(4, _inverseDistance);
      expect(far / near, closeTo(1 / 4, 0.03));
    }, skip: attenuatedTwice);

    test('linear distance: 1 - rolloff * (d - min) / (max - min)', () async {
      final near = await levelAt(1, _linearDistance, max: 11);
      final mid = await levelAt(6, _linearDistance, max: 11);
      expect(mid / near, closeTo(0.5, 0.03));
    }, skip: attenuatedTwice);

    test('exponential distance: (d / min) ^ -rolloff', () async {
      final near = await levelAt(1, _exponentialDistance, rolloff: 2);
      final far = await levelAt(2, _exponentialDistance, rolloff: 2);
      expect(far / near, closeTo(1 / 4, 0.03));
    }, skip: attenuatedTwice);

    test('beyond the max distance the level stops falling', () async {
      final atMax = await levelAt(10, _inverseDistance, max: 10);
      final beyond = await levelAt(40, _inverseDistance, max: 10);
      expect(beyond / atMax, closeTo(1, 0.05));
    });

    test('a larger rolloff attenuates more', () async {
      final soft = await levelAt(4, _inverseDistance, rolloff: 0.5);
      final hard = await levelAt(4, _inverseDistance, rolloff: 2);
      expect(hard, lessThan(soft * 0.6));
    });

    test('inside the min distance there is no attenuation', () async {
      final near = await levelAt(1, _inverseDistance, min: 5);
      final inside = await levelAt(4, _inverseDistance, min: 5);
      expect(inside / near, closeTo(1, 0.05));
    });

    test('moving the listener away is like moving the source away', () async {
      final sourceMoved = await levelAt(8, _inverseDistance);
      soloud.set3dListenerPosition(0, 0, 6);
      final listenerMoved = await levelAt(2, _inverseDistance);
      expect(listenerMoved / sourceMoved, closeTo(1, 0.05));
    });
  });

  group('doppler', () {
    const c = 343.0;
    const v = 100.0;
    const inverted =
        'bug E3: the doppler shift has the wrong sign (an approaching source '
        'or listener lowers the pitch): doppler() in '
        'src/soloud/src/core/soloud_core_3d.cpp:144 projects the velocities '
        'on the listener-to-source vector and then uses '
        '(c - vls) / (c - vss), which is the formula for the '
        'source-to-listener vector';

    // A WAV tone: a waveform keeps its frequency whatever the voice speed
    // (BasicwaveInstance steps its phase by the voice sample rate), so it
    // shows no doppler shift.
    late AudioSource sine;
    setUp(() async => sine = await loadSine());

    /// Frequency heard of a 440 Hz source 10 m in front of the listener,
    /// with velocities along Z towards the other one.
    Future<double> frequency({
      double sourceTowards = 0,
      double listenerTowards = 0,
      double dopplerFactor = 1,
      double soundSpeed = c,
    }) async {
      soloud
        ..set3dSoundSpeed(soundSpeed)
        ..set3dListenerVelocity(0, 0, -listenerTowards);
      final h = soloud.play3d(sine, 0, 0, -10, looping: true);
      soloud
        ..set3dSourceVelocity(h, 0, 0, sourceTowards)
        ..set3dSourceDopplerFactor(h, dopplerFactor);
      final out = await captureOutput();
      await soloud.stop(h);
      return out.frequency(0);
    }

    test('a still source keeps its pitch', () async {
      expect(await frequency(), closeTo(440, 3));
    });

    test('a moving source is shifted by c / (c -+ v)', () async {
      final shifted = [
        await frequency(sourceTowards: v),
        await frequency(sourceTowards: -v),
      ]..sort();
      expect(shifted[0], closeTo(440 * c / (c + v), 4));
      expect(shifted[1], closeTo(440 * c / (c - v), 4));
    });

    test('a moving listener is shifted by (c +- v) / c', () async {
      final shifted = [
        await frequency(listenerTowards: v),
        await frequency(listenerTowards: -v),
      ]..sort();
      expect(shifted[0], closeTo(440 * (c - v) / c, 4));
      expect(shifted[1], closeTo(440 * (c + v) / c, 4));
    });

    test('an approaching source is higher', () async {
      expect(await frequency(sourceTowards: v), closeTo(440 * c / (c - v), 4));
    }, skip: inverted);

    test('a receding source is lower', () async {
      expect(await frequency(sourceTowards: -v), closeTo(440 * c / (c + v), 4));
    }, skip: inverted);

    test('an approaching listener hears it higher', () async {
      expect(
        await frequency(listenerTowards: v),
        closeTo(440 * (c + v) / c, 4),
      );
    }, skip: inverted);

    test('a doppler factor of 0 disables the shift', () async {
      expect(
        await frequency(sourceTowards: v, dopplerFactor: 0),
        closeTo(440, 3),
      );
    });

    test('a slower speed of sound shifts more', () async {
      final fast = await frequency(sourceTowards: -v);
      final slow = await frequency(sourceTowards: -v, soundSpeed: 200);
      expect((slow - 440).abs(), greaterThan((fast - 440).abs() * 1.5));
    });

    test('a velocity across the line of sight does not shift', () async {
      soloud
        ..set3dSoundSpeed(c)
        ..set3dListenerVelocity(0, 0, 0);
      final h = soloud.play3d(sine, 0, 0, -10, velX: v, looping: true);
      final out = await captureOutput();
      await soloud.stop(h);
      expect(out.frequency(0), closeTo(440, 3));
    });
  });

  test('play3d honours its volume and position', () async {
    final h = soloud.play3d(tone, 5, 0, 0, volume: 0.5, looping: true);
    expect(soloud.getVolume(h), closeTo(0.5, 1e-6));
    final out = await captureOutput();
    await soloud.stop(h);
    expect(out.rms(1), greaterThan(out.rms(0) * 2));
  });

  test('a 3D voice on a stopped handle ignores the setters', () async {
    final h = soloud.play3d(tone, 0, 0, -1);
    await soloud.stop(h);
    expect(
      () => soloud
        ..set3dSourcePosition(h, 1, 2, 3)
        ..set3dSourceVelocity(h, 1, 2, 3)
        ..set3dSourceParameters(h, 1, 2, 3, 4, 5, 6)
        ..set3dSourceMinMaxDistance(h, 1, 10)
        ..set3dSourceAttenuation(h, _inverseDistance, 1)
        ..set3dSourceDopplerFactor(h, 1),
      returnsNormally,
    );
  });
}

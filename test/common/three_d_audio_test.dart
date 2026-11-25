// 3D audio with the real engine. SoLoud has no getters for the 3D state of a
// voice, so the assertions are made on the captured mixer output: the level
// of each channel for positions, attenuation and listener orientation, and
// the frequency of a steady tone for the doppler shift.
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

    test('is kept when set before any sound is loaded', () async {
      await soloud.disposeAllSources();
      soloud.set3dSoundSpeed(100);
      await loadTestWaveform();
      expect(soloud.get3dSoundSpeed(), closeTo(100, 1e-3));
    });
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

    test('a listener upside down (set3dListenerUp) swaps the sides', () async {
      soloud.set3dListenerUp(0, -1, 0);
      final out = await render(5, 0, 0);
      expect(out.rms(0), greaterThan(out.rms(1) * 2));
    });

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
    });

    test('linear distance: 1 - rolloff * (d - min) / (max - min)', () async {
      final near = await levelAt(1, _linearDistance, max: 11);
      final mid = await levelAt(6, _linearDistance, max: 11);
      expect(mid / near, closeTo(0.5, 0.03));
    });

    test('exponential distance: (d / min) ^ -rolloff', () async {
      final near = await levelAt(1, _exponentialDistance, rolloff: 2);
      final far = await levelAt(2, _exponentialDistance, rolloff: 2);
      expect(far / near, closeTo(1 / 4, 0.03));
    });

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
    });

    test('a receding source is lower', () async {
      expect(await frequency(sourceTowards: -v), closeTo(440 * c / (c + v), 4));
    });

    test('an approaching listener hears it higher', () async {
      expect(
        await frequency(listenerTowards: v),
        closeTo(440 * (c + v) / c, 4),
      );
    });

    test('a doppler factor of 0 disables the shift', () async {
      expect(
        await frequency(sourceTowards: v, dopplerFactor: 0),
        closeTo(440, 3),
      );
    });

    test('a slower speed of sound shifts more', () async {
      final fast = await frequency(sourceTowards: -v);
      final slow = await frequency(sourceTowards: -v, soundSpeed: 200);
      // Receding at v: 440 * c / (c + v), so the shift is 440 * v / (c + v),
      // about 1.48 times larger at 200 m/s than at 343 m/s.
      expect(slow, closeTo(440 * 200 / (200 + v), 4));
      expect((slow - 440).abs(), greaterThan((fast - 440).abs() * 1.3));
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

  group('deferred update (set3dAutoUpdate)', () {
    test('is automatic by default, and init() restores the default', () async {
      expect(soloud.get3dAutoUpdate(), isTrue);
      soloud.set3dAutoUpdate(false);
      expect(soloud.get3dAutoUpdate(), isFalse);
      soloud.deinit();
      await initTestEngine(globalVolume: 0.05);
      expect(soloud.get3dAutoUpdate(), isTrue);
    });

    test('a setter waits for update3dAudio', () async {
      soloud.set3dAutoUpdate(false);
      final h = soloud.play3d(tone, -5, 0, 0, looping: true);
      soloud.set3dSourcePosition(h, 5, 0, 0);
      final before = await captureOutput();
      soloud.update3dAudio();
      final after = await captureOutput();
      await soloud.stop(h);
      // Still heard where play3d put it, on the left.
      expect(before.rms(0), greaterThan(before.rms(1) * 2));
      // After the update it is on the right.
      expect(after.rms(1), greaterThan(after.rms(0) * 2));
    });

    test(
      'one update3dAudio applies the listener and several sources',
      () async {
        soloud.set3dAutoUpdate(false);
        final left = soloud.play3d(tone, -5, 0, 0, looping: true);
        final right = soloud.play3d(tone, 5, 0, 0, looping: true);
        // Mute the right one with distance, and turn the listener around.
        soloud
          ..set3dSourceAttenuation(right, _linearDistance, 1)
          ..set3dSourceMinMaxDistance(right, 1, 2)
          ..set3dListenerAt(0, 0, 1)
          ..update3dAudio();
        final out = await captureOutput();
        await soloud.stop(left);
        await soloud.stop(right);
        // Only the source at x = -5 is heard, and with the listener turned
        // around it is on the right.
        expect(out.rms(1), greaterThan(out.rms(0) * 2));
      },
    );

    test('a listener change waits for update3dAudio', () async {
      soloud.set3dAutoUpdate(false);
      final h = soloud.play3d(tone, 5, 0, 0, looping: true);
      soloud.set3dListenerAt(0, 0, 1);
      final before = await captureOutput();
      soloud.update3dAudio();
      final after = await captureOutput();
      await soloud.stop(h);
      expect(before.rms(1), greaterThan(before.rms(0) * 2));
      expect(after.rms(0), greaterThan(after.rms(1) * 2));
    });

    test('play3d places a new voice without an update', () async {
      soloud.set3dAutoUpdate(false);
      final h = soloud.play3d(tone, 5, 0, 0, looping: true);
      final out = await captureOutput();
      await soloud.stop(h);
      expect(out.rms(1), greaterThan(out.rms(0) * 2));
    });

    test('enabling the automatic update applies the stored values', () async {
      soloud.set3dAutoUpdate(false);
      final h = soloud.play3d(tone, -5, 0, 0, looping: true);
      soloud
        ..set3dSourcePosition(h, 5, 0, 0)
        ..set3dAutoUpdate(true);
      final out = await captureOutput();
      await soloud.stop(h);
      expect(out.rms(1), greaterThan(out.rms(0) * 2));
    });

    test(
      'update3dAudio with the automatic update enabled is harmless',
      () async {
        final h = soloud.play3d(tone, 5, 0, 0, looping: true);
        soloud.update3dAudio();
        final out = await captureOutput();
        await soloud.stop(h);
        expect(out.rms(1), greaterThan(out.rms(0) * 2));
      },
    );
  });

  group('defaults of a sound (set3dDefault*)', () {
    /// Level of a new voice of [sound] at [distance] in front of the
    /// listener, with nothing set on its handle.
    Future<double> levelAt(double distance, [AudioSource? sound]) async {
      final h = soloud.play3d(sound ?? tone, 0, 0, -distance, looping: true);
      final out = await captureOutput();
      await soloud.stop(h);
      return level(out);
    }

    test('a new voice starts with the attenuation and distances', () async {
      soloud
        ..set3dDefaultAttenuation(tone, _linearDistance, 1)
        ..set3dDefaultMinMaxDistance(tone, 1, 11);
      final near = await levelAt(1);
      final mid = await levelAt(6);
      expect(mid / near, closeTo(0.5, 0.03));
    });

    test('the rolloff factor is part of the defaults', () async {
      soloud
        ..set3dDefaultAttenuation(tone, _exponentialDistance, 2)
        ..set3dDefaultMinMaxDistance(tone, 1, 100);
      final near = await levelAt(1);
      final far = await levelAt(2);
      expect(far / near, closeTo(1 / 4, 0.03));
    });

    test('they belong to one sound', () async {
      final other = await loadTestWaveform();
      soloud
        ..set3dDefaultAttenuation(tone, _linearDistance, 1)
        ..set3dDefaultMinMaxDistance(tone, 1, 11);
      final near = await levelAt(1, other);
      final mid = await levelAt(6, other);
      expect(mid / near, closeTo(1, 0.05));
    });

    test('a voice already playing keeps its values', () async {
      final h = soloud.play3d(tone, 0, 0, -6, looping: true);
      final before = level(await captureOutput());
      soloud
        ..set3dDefaultAttenuation(tone, _linearDistance, 1)
        ..set3dDefaultMinMaxDistance(tone, 1, 11)
        // Makes the engine compute the 3D state of the voice again.
        ..set3dSourcePosition(h, 0, 0, -6);
      final after = level(await captureOutput());
      await soloud.stop(h);
      expect(after / before, closeTo(1, 0.05));
    });

    test('a handle setter still changes one voice', () async {
      soloud
        ..set3dDefaultAttenuation(tone, _linearDistance, 1)
        ..set3dDefaultMinMaxDistance(tone, 1, 11);
      final near = await levelAt(1);
      final out = await render(
        0,
        0,
        -6,
        configure: (h) => soloud.set3dSourceAttenuation(h, _noAttenuation, 1),
      );
      expect(level(out) / near, closeTo(1, 0.05));
    });

    test('they hold with the automatic update disabled', () async {
      soloud
        ..set3dAutoUpdate(false)
        ..set3dDefaultAttenuation(tone, _linearDistance, 1)
        ..set3dDefaultMinMaxDistance(tone, 1, 11);
      final near = await levelAt(1);
      final mid = await levelAt(6);
      expect(mid / near, closeTo(0.5, 0.03));
    });

    test('the doppler factor is part of the defaults', () async {
      // A WAV tone: a waveform shows no doppler shift (see the doppler group).
      final sine = await loadSine();
      Future<double> frequency() async {
        final h = soloud.play3d(sine, 0, 0, -10, velZ: 100, looping: true);
        final out = await captureOutput();
        await soloud.stop(h);
        return out.frequency(0);
      }

      expect(await frequency(), greaterThan(500));
      soloud.set3dDefaultDopplerFactor(sine, 0);
      expect(await frequency(), closeTo(440, 3));
    });

    test('a sound that is not loaded is ignored', () async {
      await soloud.disposeSource(tone);
      expect(
        () => soloud
          ..set3dDefaultAttenuation(tone, _linearDistance, 1)
          ..set3dDefaultMinMaxDistance(tone, 1, 11)
          ..set3dDefaultDopplerFactor(tone, 0),
        returnsNormally,
      );
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

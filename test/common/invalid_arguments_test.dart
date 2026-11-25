// Invalid numeric arguments with the real engine: NaN, infinities, negative
// and out-of-range values for every method that takes numbers. Each call
// must either throw a Dart error about the argument (ArgumentError,
// AssertionError in debug builds, a SoLoudException) or accept the value and
// keep it within range. Never a crash, never a NaN read back, and never a
// NaN or infinity in the mixed output.
@Tags(['device'])
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// ignore_for_file: experimental_member_use, avoid_catching_errors

const _nan = double.nan;
const _inf = double.infinity;

/// Bad values for a parameter of type double.
const _badDoubles = [_nan, _inf, -_inf, -1.0, 1e30, -1e30];

/// Calls [call]: an error about the argument is fine, anything else fails.
Future<void> _rejectOrAccept(
  String what,
  FutureOr<void> Function() call,
) async {
  try {
    await call();
  } on ArgumentError {
    // Rejected (RangeError is an ArgumentError).
  } on AssertionError {
    // Rejected by a debug assertion.
  } on SoLoudException {
    // Rejected by the plugin.
  } catch (e) {
    fail('$what threw ${e.runtimeType}: $e');
  }
}

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  late AudioSource tone;
  late SoundHandle handle;
  setUp(() async {
    tone = await loadTestWaveform();
    handle = soloud.play(tone, looping: true, volume: 0.3);
    soloud.setGlobalVolume(0.5);
  });

  /// The engine still works and its output has no NaN or infinity.
  Future<void> expectSaneOutput() async {
    expect(soloud.isInitialized, isTrue);
    // Something must be playing for the capture.
    final probe = soloud.play(await loadTestWaveform(), volume: 0.1);
    expect(soloud.getIsValidVoiceHandle(probe), isTrue);
    final out = await captureOutput(frames: 4096);
    final bad = out.samples.where((s) => !s.isFinite).length;
    expect(bad, 0, reason: '$bad non-finite samples in the mix');
  }

  void expectFinite(double value, String what) =>
      expect(value.isFinite, isTrue, reason: '$what read back $value');

  group('voice parameters', () {
    test('setVolume', () async {
      for (final v in _badDoubles) {
        await _rejectOrAccept(
          'setVolume($v)',
          () => soloud.setVolume(handle, v),
        );
        expectFinite(soloud.getVolume(handle), 'getVolume after $v');
      }
      await expectSaneOutput();
    });

    test('setGlobalVolume', () async {
      for (final v in _badDoubles) {
        await _rejectOrAccept(
          'setGlobalVolume($v)',
          () => soloud.setGlobalVolume(v),
        );
        expectFinite(soloud.getGlobalVolume(), 'getGlobalVolume after $v');
      }
      soloud.setGlobalVolume(0.5);
      await expectSaneOutput();
    });

    test('setPan and setPanAbsolute', () async {
      for (final v in [..._badDoubles, 5.0, -5.0]) {
        await _rejectOrAccept('setPan($v)', () => soloud.setPan(handle, v));
        final pan = soloud.getPan(handle);
        expectFinite(pan, 'getPan after $v');
        expect(pan, inInclusiveRange(-1, 1));
        await _rejectOrAccept(
          'setPanAbsolute($v)',
          () => soloud.setPanAbsolute(handle, v, v),
        );
      }
      await expectSaneOutput();
    });

    test('setRelativePlaySpeed', () async {
      for (final v in [..._badDoubles, 0.0]) {
        await _rejectOrAccept(
          'setRelativePlaySpeed($v)',
          () => soloud.setRelativePlaySpeed(handle, v),
        );
        final speed = soloud.getRelativePlaySpeed(handle);
        expectFinite(speed, 'getRelativePlaySpeed after $v');
        expect(speed, greaterThan(0));
      }
      await expectSaneOutput();
    });

    test('seek to negative and past-the-end times', () async {
      final sound = await loadTestSound(duration: const Duration(seconds: 2));
      final h = soloud.play(sound, looping: true, volume: 0.2);
      for (final t in [
        const Duration(seconds: -5),
        const Duration(microseconds: -1),
        const Duration(days: 365),
      ]) {
        await _rejectOrAccept('seek($t)', () => soloud.seek(h, t));
        final p = soloud.getPosition(h);
        expect(p, greaterThanOrEqualTo(Duration.zero), reason: 'after $t');
      }
      await expectSaneOutput();
    });

    test('loop points', () async {
      final sound = await loadTestSound(duration: const Duration(seconds: 2));
      final h = soloud.play(sound, looping: true, volume: 0.2);
      for (final t in [const Duration(seconds: -1), const Duration(days: 1)]) {
        await _rejectOrAccept(
          'setLoopPoint($t)',
          () => soloud.setLoopPoint(h, t),
        );
        expect(soloud.getLoopPoint(h), greaterThanOrEqualTo(Duration.zero));
        await _rejectOrAccept(
          'setLoopEndPoint($t)',
          () => soloud.setLoopEndPoint(h, t),
        );
      }
      await _rejectOrAccept(
        'play with a negative loop start',
        () => soloud.play(sound, loopingStartAt: const Duration(seconds: -1)),
      );
      await _rejectOrAccept(
        'play with a loop end before its start',
        () => soloud.play(
          sound,
          looping: true,
          loopingStartAt: const Duration(seconds: 1),
          loopingEndAt: const Duration(milliseconds: 500),
        ),
      );
      await expectSaneOutput();
    });

    test('setDelaySamples with negative and huge counts', () async {
      for (final n in [-1, -1 << 31, 1 << 40]) {
        final h = soloud.play(tone, volume: 0.1, paused: true);
        await _rejectOrAccept(
          'setDelaySamples($n)',
          () => soloud.setDelaySamples(h, n),
        );
        soloud.setPause(h, false);
      }
      await expectSaneOutput();
    });

    test('play() with bad volume, pan and scale', () async {
      for (final v in _badDoubles) {
        await _rejectOrAccept(
          'play(volume: $v)',
          () => soloud.play(tone, volume: v),
        );
        await _rejectOrAccept('play(pan: $v)', () => soloud.play(tone, pan: v));
        await _rejectOrAccept(
          'play(scale: $v)',
          () => soloud.play(tone, scale: v),
        );
      }
      for (final h in tone.handles) {
        expectFinite(soloud.getVolume(h), 'volume of $h');
        expectFinite(soloud.getRelativePlaySpeed(h), 'speed of $h');
      }
      await expectSaneOutput();
    });

    test('playClocked and playScheduled with negative times', () async {
      await _rejectOrAccept(
        'playClocked(-1 s)',
        () =>
            soloud.playClocked(tone, const Duration(seconds: -1), volume: 0.1),
      );
      await _rejectOrAccept(
        'playScheduled(-1 s)',
        () => soloud.playScheduled(
          tone,
          const Duration(seconds: -1),
          volume: 0.1,
        ),
      );
      await _rejectOrAccept(
        'stopScheduled(-1 s)',
        () => soloud.stopScheduled(handle, const Duration(seconds: -1)),
      );
      await _rejectOrAccept(
        'fadeScheduled(NaN)',
        () => soloud.fadeScheduled(handle, Duration.zero, _nan, Duration.zero),
      );
      expectFinite(soloud.getVolume(handle), 'volume after fadeScheduled(NaN)');
    });
  });

  group('faders and oscillators', () {
    test('fades to NaN or over a negative time', () async {
      const negative = Duration(milliseconds: -100);
      const short = Duration(milliseconds: 50);
      for (final v in [_nan, _inf]) {
        await _rejectOrAccept(
          'fadeVolume($v)',
          () => soloud.fadeVolume(handle, v, short),
        );
        await _rejectOrAccept(
          'fadePan($v)',
          () => soloud.fadePan(handle, v, short),
        );
        await _rejectOrAccept(
          'fadeRelativePlaySpeed($v)',
          () => soloud.fadeRelativePlaySpeed(handle, v, short),
        );
      }
      await _rejectOrAccept(
        'fadeVolume(-t)',
        () => soloud.fadeVolume(handle, 0.2, negative),
      );
      await Future<void>.delayed(const Duration(milliseconds: 150));
      expectFinite(soloud.getVolume(handle), 'volume');
      expectFinite(soloud.getPan(handle), 'pan');
      expectFinite(soloud.getRelativePlaySpeed(handle), 'speed');
      await expectSaneOutput();
    });

    test('fadeGlobalVolume to NaN', () async {
      await _rejectOrAccept(
        'fadeGlobalVolume(NaN)',
        () => soloud.fadeGlobalVolume(_nan, const Duration(milliseconds: 50)),
      );
      await Future<void>.delayed(const Duration(milliseconds: 150));
      expectFinite(soloud.getGlobalVolume(), 'global volume');
      soloud.setGlobalVolume(0.5);
      await expectSaneOutput();
    });

    test('oscillators with NaN bounds or a zero/negative period', () async {
      for (final t in [Duration.zero, const Duration(milliseconds: -10)]) {
        await _rejectOrAccept(
          'oscillateVolume($t)',
          () => soloud.oscillateVolume(handle, 0.1, 0.3, t),
        );
        await _rejectOrAccept(
          'oscillatePan($t)',
          () => soloud.oscillatePan(handle, -1, 1, t),
        );
        await _rejectOrAccept(
          'oscillateRelativePlaySpeed($t)',
          () => soloud.oscillateRelativePlaySpeed(handle, 0.5, 2, t),
        );
        await _rejectOrAccept(
          'oscillateGlobalVolume($t)',
          () => soloud.oscillateGlobalVolume(0.2, 0.5, t),
        );
      }
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expectFinite(soloud.getVolume(handle), 'volume');
      expectFinite(soloud.getPan(handle), 'pan');
      expectFinite(soloud.getRelativePlaySpeed(handle), 'speed');
      expectFinite(soloud.getGlobalVolume(), 'global volume');
      await expectSaneOutput();
    });

    test('schedulePause and scheduleStop with negative times', () async {
      final a = soloud.play(tone, volume: 0.1);
      final b = soloud.play(tone, volume: 0.1);
      await _rejectOrAccept(
        'schedulePause(-1 s)',
        () => soloud.schedulePause(a, const Duration(seconds: -1)),
      );
      await _rejectOrAccept(
        'scheduleStop(-1 s)',
        () => soloud.scheduleStop(b, const Duration(seconds: -1)),
      );
      await expectSaneOutput();
    });
  });

  group('waveforms', () {
    test('setWaveformFreq, setWaveformScale, setWaveformDetune', () async {
      for (final v in [..._badDoubles, 0.0]) {
        await _rejectOrAccept(
          'setWaveformFreq($v)',
          () => soloud.setWaveformFreq(tone, v),
        );
        await _rejectOrAccept(
          'setWaveformScale($v)',
          () => soloud.setWaveformScale(tone, v),
        );
        await _rejectOrAccept(
          'setWaveformDetune($v)',
          () => soloud.setWaveformDetune(tone, v),
        );
      }
      soloud.setWaveformScale(tone, 1);
      await expectSaneOutput();
    });

    test('loadWaveform with NaN scale and detune', () async {
      for (final v in [_nan, _inf, -1.0]) {
        await _rejectOrAccept('loadWaveform($v)', () async {
          final w = await soloud.loadWaveform(WaveForm.saw, true, v, v);
          soloud.play(w, volume: 0.1);
        });
      }
      await expectSaneOutput();
    });
  });

  group('limits and analysis', () {
    test('setFftSmoothing', () {
      for (final v in [..._badDoubles, 2.0]) {
        expect(
          () => _rejectOrAccept(
            'setFftSmoothing($v)',
            () => soloud.setFftSmoothing(v),
          ),
          returnsNormally,
        );
      }
    });

    test('setFftDecibelRange', () async {
      for (final (min, max) in [
        (0.0, -100.0),
        (-30.0, -30.0),
        (_nan, 0.0),
        (-100.0, _nan),
        (-_inf, _inf),
      ]) {
        await _rejectOrAccept(
          'setFftDecibelRange($min, $max)',
          () => soloud.setFftDecibelRange(min, max),
        );
        expectFinite(soloud.minDecibels, 'minDecibels after ($min, $max)');
        expectFinite(soloud.maxDecibels, 'maxDecibels after ($min, $max)');
        expect(soloud.minDecibels, lessThan(soloud.maxDecibels));
      }
    });

    test('getApproximateVolume of channels that do not exist', () {
      for (final c in [-1, 2, 8, 1 << 20]) {
        double? v;
        expect(
          () => _rejectOrAccept(
            'getApproximateVolume($c)',
            () => v = soloud.getApproximateVolume(c),
          ),
          returnsNormally,
        );
        if (v != null) expectFinite(v!, 'getApproximateVolume($c)');
      }
    });

    test('setVisualizationEnabled with bad window sizes', () async {
      for (final w in [0, -1, 3, 1000, 1 << 20]) {
        await _rejectOrAccept(
          'setVisualizationEnabled(windowSize: $w)',
          () => soloud.setVisualizationEnabled(true, windowSize: w),
        );
      }
      await _rejectOrAccept(
        'disable',
        () => soloud.setVisualizationEnabled(false),
      );
      await expectSaneOutput();
    });

    test('setMaxActiveVoiceCount with negative counts', () {
      soloud.setMaxActiveVoiceCount(32);
      for (final n in [-1, -1 << 31]) {
        soloud.setMaxActiveVoiceCount(n);
        expect(soloud.getMaxActiveVoiceCount(), 32, reason: '$n');
      }
    });
  });

  group('3D audio', () {
    test('NaN and infinite positions, velocities and parameters', () async {
      final h = soloud.play3d(tone, 1, 0, 0, volume: 0.3);
      for (final v in [_nan, _inf, -_inf]) {
        await _rejectOrAccept(
          'set3dSourcePosition($v)',
          () => soloud.set3dSourcePosition(h, v, v, v),
        );
        await _rejectOrAccept(
          'set3dSourceVelocity($v)',
          () => soloud.set3dSourceVelocity(h, v, 0, 0),
        );
        await _rejectOrAccept(
          'set3dListenerPosition($v)',
          () => soloud.set3dListenerPosition(v, 0, 0),
        );
        await _rejectOrAccept(
          'set3dSoundSpeed($v)',
          () => soloud.set3dSoundSpeed(v),
        );
        await _rejectOrAccept(
          'set3dSourceDopplerFactor($v)',
          () => soloud.set3dSourceDopplerFactor(h, v),
        );
        await _rejectOrAccept(
          'set3dDefaultMinMaxDistance($v)',
          () => soloud.set3dDefaultMinMaxDistance(tone, v, v),
        );
        await _rejectOrAccept(
          'set3dDefaultAttenuation(rolloff $v)',
          () => soloud.set3dDefaultAttenuation(tone, 1, v),
        );
        await _rejectOrAccept(
          'set3dDefaultDopplerFactor($v)',
          () => soloud.set3dDefaultDopplerFactor(tone, v),
        );
      }
      await expectSaneOutput();
    });

    for (final (name, call) in <(String, void Function(SoundHandle))>[
      ('set3dSoundSpeed(0)', (_) => soloud.set3dSoundSpeed(0)),
      ('set3dSoundSpeed(-343)', (_) => soloud.set3dSoundSpeed(-343)),
      ('set3dListenerAt(0, 0, 0)', (_) => soloud.set3dListenerAt(0, 0, 0)),
      ('set3dListenerUp(0, 0, 0)', (_) => soloud.set3dListenerUp(0, 0, 0)),
      (
        'set3dSourceMinMaxDistance(10, 1)',
        (h) => soloud.set3dSourceMinMaxDistance(h, 10, 1),
      ),
      (
        'set3dSourceMinMaxDistance(0, 0)',
        (h) => soloud.set3dSourceMinMaxDistance(h, 0, 0),
      ),
      (
        'set3dSourceAttenuation(99, 1)',
        (h) => soloud.set3dSourceAttenuation(h, 99, 1),
      ),
      (
        'set3dSourceAttenuation(-1, 1)',
        (h) => soloud.set3dSourceAttenuation(h, -1, 1),
      ),
      (
        'set3dSourceAttenuation(1, -1)',
        (h) => soloud.set3dSourceAttenuation(h, 1, -1),
      ),
    ]) {
      test(name, () async {
        final h = soloud.play3d(tone, 1, 0, 0, volume: 0.3);
        await _rejectOrAccept(name, () => call(h));
        await expectSaneOutput();
      });
    }

    test(
      'zero min and max distance with inverse-distance attenuation',
      () async {
        final h = soloud.play3d(tone, 1, 0, 0, volume: 0.3);
        await _rejectOrAccept('distances 0, 0', () {
          soloud
            ..set3dSourceMinMaxDistance(h, 0, 0)
            ..set3dSourceAttenuation(h, 1, 1);
        });
        await expectSaneOutput();
      },
    );
  });

  group('streams and capture', () {
    for (final (name, make) in <(String, AudioSource Function())>[
      (
        'maxBufferSizeBytes: 0',
        () => soloud.setBufferStream(maxBufferSizeBytes: 0),
      ),
      (
        'maxBufferSizeBytes: -1',
        () => soloud.setBufferStream(maxBufferSizeBytes: -1),
      ),
      (
        'bufferingTimeNeeds: -1',
        () => soloud.setBufferStream(bufferingTimeNeeds: -1),
      ),
      (
        'bufferingTimeNeeds: NaN',
        () => soloud.setBufferStream(bufferingTimeNeeds: _nan),
      ),
      ('sampleRate: 0', () => soloud.setBufferStream(sampleRate: 0)),
      ('sampleRate: -1', () => soloud.setBufferStream(sampleRate: -1)),
    ]) {
      test('setBufferStream($name)', () async {
        await _rejectOrAccept(name, () {
          final s = make();
          soloud
            ..addAudioDataStream(s, Uint8List(4096))
            ..play(s, volume: 0.1);
        });
        await expectSaneOutput();
      });
    }

    for (final (name, make) in <(String, AudioSource Function())>[
      (
        'bufferSizeBytes: 0',
        () => soloud.setPullBufferStream(
          bufferSizeBytes: 0,
          audioSizeBytes: 1000,
        ),
      ),
      (
        'bufferSizeBytes: -1',
        () => soloud.setPullBufferStream(
          bufferSizeBytes: -1,
          audioSizeBytes: 1000,
        ),
      ),
      (
        'bufferTriggerPosition: NaN',
        () => soloud.setPullBufferStream(
          bufferTriggerPosition: _nan,
          audioSizeBytes: 1000,
        ),
      ),
      (
        'bufferTriggerPosition: -1',
        () => soloud.setPullBufferStream(
          bufferTriggerPosition: -1,
          audioSizeBytes: 1000,
        ),
      ),
      (
        'bufferTriggerPosition: 2',
        () => soloud.setPullBufferStream(
          bufferTriggerPosition: 2,
          audioSizeBytes: 1000,
        ),
      ),
      (
        'audioSizeBytes: -1',
        () => soloud.setPullBufferStream(audioSizeBytes: -1),
      ),
      (
        'PCM sampleRate: 0',
        () => soloud.setPullBufferStream(
          format: BufferType.s16le,
          sampleRate: 0,
          audioSizeBytes: 1000,
        ),
      ),
    ]) {
      test('setPullBufferStream($name)', () async {
        await _rejectOrAccept(name, () {
          soloud.play(make(), volume: 0.1);
        });
        await expectSaneOutput();
      });
    }

    for (final (name, start) in <(String, Stream<Uint8List> Function())>[
      (
        'bufferSizeBytes: 0',
        () => soloud.startMixerOutputStream(bufferSizeBytes: 0),
      ),
      (
        'bufferSizeBytes: -1',
        () => soloud.startMixerOutputStream(bufferSizeBytes: -1),
      ),
      (
        'bufferSizeBytes: -4096',
        () => soloud.startMixerOutputStream(bufferSizeBytes: -4096),
      ),
      (
        'notificationThresholdBytes: -1',
        () => soloud.startMixerOutputStream(notificationThresholdBytes: -1),
      ),
      (
        'chunkPCMFrames: 100',
        () => soloud.startMixerOutputStream(chunkPCMFrames: 100),
      ),
      (
        'sampleRate: 12345',
        () => soloud.startMixerOutputStream(sampleRate: 12345),
      ),
      ('channels: 7', () => soloud.startMixerOutputStream(channels: 7)),
    ]) {
      test('startMixerOutputStream($name)', () async {
        await _rejectOrAccept(name, () async {
          final sub = start().listen((_) {});
          await Future<void>.delayed(const Duration(milliseconds: 50));
          soloud.stopMixerOutputStream();
          await sub.cancel();
        });
        expect(soloud.isMixerOutputStreamRunning, isFalse);
        await expectSaneOutput();
      });
    }

    test('readSamplesFromMem with bad counts and times', () async {
      final wav = wavBytes();
      for (final (n, start, end) in [
        (0, 0.0, -1.0),
        (-1, 0.0, -1.0),
        (64, -1.0, -1.0),
        (64, 0.5, 0.2),
        (64, 5.0, -1.0),
        (64, _nan, -1.0),
      ]) {
        await _rejectOrAccept('readSamplesFromMem($n, $start, $end)', () async {
          final samples = await soloud.readSamplesFromMem(
            wav,
            n,
            startTime: start,
            endTime: end,
          );
          expect(samples.every((s) => s.isFinite), isTrue);
        });
      }
    });

    for (final (name, make) in <(String, Object Function())>[
      ('sampleRate: 0', () => soloud.setRealtimeStream(sampleRate: 0)),
      ('sampleRate: -1', () => soloud.setRealtimeStream(sampleRate: -1)),
      ('bufferFrames: 0', () => soloud.setRealtimeStream(bufferFrames: 0)),
      ('bufferFrames: -1', () => soloud.setRealtimeStream(bufferFrames: -1)),
      ('historyFrames: -1', () => soloud.setRealtimeStream(historyFrames: -1)),
    ]) {
      test('setRealtimeStream($name)', () async {
        await _rejectOrAccept(name, make);
        await expectSaneOutput();
      });
    }
  });

  group('global filters', () {
    test('NaN and out-of-range parameter values are not applied', () async {
      final echo = soloud.filters.echoFilter..activate();
      addTearDown(() {
        if (soloud.isInitialized) echo.deactivate();
      });
      final wet = echo.wet;
      final before = wet.value;
      for (final v in [_nan, _inf, -_inf, wet.min - 1, wet.max + 1]) {
        await _rejectOrAccept('wet = $v', () => wet.value = v);
        double? now;
        await _rejectOrAccept('read wet', () => now = wet.value);
        expect(now, isNotNull, reason: 'wet unreadable after $v');
        expect(now, before, reason: 'wet changed by $v');
      }
      await expectSaneOutput();
    });

    test('fading a parameter to NaN', () async {
      final echo = soloud.filters.echoFilter..activate();
      addTearDown(() {
        if (soloud.isInitialized) echo.deactivate();
      });
      await _rejectOrAccept(
        'fade wet to NaN',
        () => echo.wet.fadeFilterParameter(
          to: _nan,
          time: const Duration(milliseconds: 50),
        ),
      );
      await _rejectOrAccept(
        'fadeGlobalFilterParameter(NaN)',
        () => soloud.fadeGlobalFilterParameter(
          FilterType.echoFilter,
          0,
          _nan,
          const Duration(milliseconds: 50),
        ),
      );
      await Future<void>.delayed(const Duration(milliseconds: 150));
      await expectSaneOutput();
    });
  });
}

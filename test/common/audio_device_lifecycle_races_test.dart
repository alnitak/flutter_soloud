// Races in the output-device lifecycle coordination, with the real engine:
// idle timeouts, explicit start/stop, (simulated) interruptions and teardown.
//
// A copy runs in the example app
// (example/tests/tests/audio_device_lifecycle_races.dart) for the mobile
// lifecycles.
// Not on the web: there the device is always kept running (the idle timeout
// has no effect), so there is no device lifecycle to race on.
@TestOn('vm')
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart' show PlayerStateNotification;
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

const _defaultTimeout = Duration(milliseconds: 500);
const _raceTimeout = Duration(milliseconds: 400);

AudioDeviceState get _state => SoLoud.instance.getAudioDeviceState();

Future<AudioDeviceState> _waitForState(
  AudioDeviceState expected, {
  Duration timeout = const Duration(seconds: 2),
}) async {
  await waitUntil(() => _state == expected, timeout: timeout);
  return _state;
}

Future<Object?> _captureError(Future<void> operation) =>
    operation.then<Object?>((_) => null, onError: (Object error) => error);

Future<void> _next(PlayerStateNotification event) => SoLoudController()
    .soLoudFFI
    .stateChangedEvents
    .firstWhere((e) => e == event)
    .timeout(const Duration(seconds: 2));

void _interruption({required bool began}) =>
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: began);

void main() {
  prepareEngineHost();

  late AudioSource waveform;

  setUp(() async {
    SoLoud.instance.setAudioDeviceIdleTimeout(Duration.zero);
    await initTestEngine();
    waveform = await loadTestWaveform();
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
      reason: 'initial idle stop',
    );
  });

  tearDown(() async {
    SoLoud.instance.setAudioDeviceIdleTimeout(_defaultTimeout);
    await SoLoud.instance.deinitAsync();
  });

  /// A looping voice, with the device started for it.
  Future<SoundHandle> playing() async {
    final handle = SoLoud.instance.play(waveform, looping: true, volume: 0.1);
    expect(
      await _waitForState(AudioDeviceState.started),
      AudioDeviceState.started,
    );
    return handle;
  }

  test('failed playback leaves the device stopped', () async {
    expect(
      () => SoLoud.instance.play(waveform, busId: 0x7fffffff),
      throwsA(isA<SoLoudException>()),
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(_state, AudioDeviceState.stopped);
  });

  test(
    'an idle-timeout update right after an unpause keeps playback',
    () async {
      // Repeated with a paused voice kept alive, so the final idle check cannot
      // legitimately stop the device.
      final handle = SoLoud.instance.play(
        waveform,
        paused: true,
        looping: true,
        volume: 0.1,
      );
      for (var i = 0; i < 10; i++) {
        SoLoud.instance.setPause(handle, false);
        SoLoud.instance.setAudioDeviceIdleTimeout(_raceTimeout);
        expect(
          await _waitForState(AudioDeviceState.started),
          AudioDeviceState.started,
          reason: 'iteration $i',
        );
        expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
        expect(SoLoud.instance.getPause(handle), isFalse);
        await Future<void>.delayed(
          _raceTimeout + const Duration(milliseconds: 50),
        );
        expect(
          _state,
          AudioDeviceState.started,
          reason: 'iteration $i: the idle policy stopped an active voice',
        );

        SoLoud.instance.setPause(handle, true);
        expect(
          await _waitForState(AudioDeviceState.stopped),
          AudioDeviceState.stopped,
          reason: 'iteration $i: a paused voice should let the device stop',
        );
      }
    },
    tags: ['slow'],
  );

  test('an explicit prewarm times out while idle', () async {
    SoLoud.instance.setAudioDeviceIdleTimeout(_raceTimeout);
    await SoLoud.instance.startAudioDevice();
    expect(_state, AudioDeviceState.started);
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );
  });

  test('a newer start replaces the stale idle deadline', () async {
    SoLoud.instance.setAudioDeviceIdleTimeout(_raceTimeout);
    await SoLoud.instance.startAudioDevice();
    await Future<void>.delayed(const Duration(milliseconds: 250));
    await SoLoud.instance.startAudioDevice();
    await Future<void>.delayed(const Duration(milliseconds: 250));
    expect(
      _state,
      AudioDeviceState.started,
      reason: 'the first deadline stopped a newer explicit start',
    );
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
      reason: 'the fresh deadline was not applied',
    );
  });

  test('a stop following a start wins', () async {
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    final start = SoLoud.instance.startAudioDevice();
    await waitUntil(
      () => _state != AudioDeviceState.stopped,
      interval: Duration.zero,
      timeout: const Duration(seconds: 1),
    );
    expect(_state, isNot(AudioDeviceState.stopped));
    final stop = SoLoud.instance.stopAudioDevice();
    await Future.wait<void>([start, stop]);
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );
  });

  test('concurrent start and stop serialize (5x)', () async {
    // A long idle timeout, so no idle stop is in flight when checking.
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    for (var i = 0; i < 5; i++) {
      final results = await Future.wait<Object?>([
        _captureError(SoLoud.instance.startAudioDevice()),
        _captureError(SoLoud.instance.stopAudioDevice()),
      ]).timeout(const Duration(seconds: 5));
      expect(results, everyElement(isNull), reason: 'iteration $i');
      expect(
        _state,
        anyOf(AudioDeviceState.started, AudioDeviceState.stopped),
        reason: 'iteration $i left a transitional state',
      );
      await SoLoud.instance.startAudioDevice();
      await SoLoud.instance.stopAudioDevice();
      expect(
        await _waitForState(AudioDeviceState.stopped),
        AudioDeviceState.stopped,
        reason: 'iteration $i: recovery failed',
      );
    }
  });

  test('an interruption stops the device and its end restarts it', () async {
    final handle = await playing();
    SoLoud.instance.setPause(handle, true);
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );

    // Queue a start right before the interruption: the interruption wins,
    // the voice stays as it is and recovery applies on its end.
    SoLoud.instance.setPause(handle, false);
    final began = _next(PlayerStateNotification.interruptionBegan);
    _interruption(began: true);
    await began;
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
    expect(SoLoud.instance.getPause(handle), isFalse);

    final ended = _next(PlayerStateNotification.interruptionEnded);
    _interruption(began: false);
    await ended;
    expect(
      await _waitForState(AudioDeviceState.started),
      AudioDeviceState.started,
    );
  });

  test('an interruption ended right after it began recovers (20x)', () async {
    final handle = await playing();
    for (var i = 0; i < 20; i++) {
      final began = _next(PlayerStateNotification.interruptionBegan);
      final stopped = _next(PlayerStateNotification.stopped);
      final started = _next(PlayerStateNotification.started);
      _interruption(began: true);
      await began;
      // The interruption stop may still be pending or in flight.
      _interruption(began: false);
      await stopped;
      await started;
      expect(
        await _waitForState(AudioDeviceState.started),
        AudioDeviceState.started,
        reason: 'cycle $i lost the recovery start',
      );
      expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
      expect(SoLoud.instance.getPause(handle), isFalse);
    }
  });

  test('interruption recovery follows the idle policy', () async {
    final handle = await playing();
    // Finite idle timeout with nothing playing: stays stopped.
    SoLoud.instance.setPause(handle, true);
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );
    _interruption(began: true);
    _interruption(began: false);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    expect(
      _state,
      AudioDeviceState.stopped,
      reason: 'the idle finite policy restarted after an interruption',
    );

    // Keep-alive: recovers.
    SoLoud.instance.setAudioDeviceIdleTimeout(null);
    expect(
      await _waitForState(AudioDeviceState.started),
      AudioDeviceState.started,
    );
    _interruption(began: true);
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );
    _interruption(began: false);
    expect(
      await _waitForState(AudioDeviceState.started),
      AudioDeviceState.started,
    );
  });

  test('an explicit start recovers a missed interruption end', () async {
    // iOS does not reliably deliver the interruption end (notably while the
    // app is in the background). An explicit start must not be swallowed by
    // the latched interruption.
    SoLoud.instance.setAudioDeviceIdleTimeout(null);
    expect(
      await _waitForState(AudioDeviceState.started),
      AudioDeviceState.started,
    );
    _interruption(began: true);
    expect(
      await _waitForState(AudioDeviceState.stopped),
      AudioDeviceState.stopped,
    );
    await SoLoud.instance.startAudioDevice();
    expect(
      await _waitForState(AudioDeviceState.started),
      AudioDeviceState.started,
    );
  });

  test('teardown cancels a pending idle deadline', () async {
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    await SoLoud.instance.deinitAsync().timeout(const Duration(seconds: 5));
    expect(_state, AudioDeviceState.uninitialized);
  });

  test('teardown during an active start operation', () async {
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    final operation = _captureError(SoLoud.instance.startAudioDevice());
    final teardown = SoLoud.instance.deinitAsync().timeout(
      const Duration(seconds: 5),
    );
    expect(await operation, anyOf(isNull, isA<SoLoudException>()));
    await teardown;
    expect(_state, AudioDeviceState.uninitialized);
  });

  test('repeated start/deinit races (100x)', () async {
    await SoLoud.instance.deinitAsync();
    for (var i = 0; i < 100; i++) {
      await SoLoud.instance.init();
      expect(SoLoud.instance.isInitialized, isTrue, reason: 'cycle $i');
      final start = _captureError(SoLoud.instance.startAudioDevice());
      await SoLoud.instance.deinitAsync().timeout(const Duration(seconds: 5));
      expect(
        await start,
        anyOf(isNull, isA<SoLoudException>()),
        reason: 'cycle $i',
      );
      expect(SoLoud.instance.isInitialized, isFalse, reason: 'cycle $i');
      expect(_state, AudioDeviceState.uninitialized, reason: 'cycle $i');
    }
  }, tags: ['slow']);
}

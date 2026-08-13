// ignore_for_file: experimental_member_use

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart' show PlayerStateNotification;

Future<AudioDeviceState> _waitForDeviceState(
  AudioDeviceState expected, {
  Duration timeout = const Duration(seconds: 2),
}) async {
  var state = SoLoud.instance.getAudioDeviceState();
  final deadline = DateTime.now().add(timeout);
  while (state != expected && DateTime.now().isBefore(deadline)) {
    await Future<void>.delayed(const Duration(milliseconds: 10));
    state = SoLoud.instance.getAudioDeviceState();
  }
  return state;
}

Future<Object?> _captureError(Future<void> operation) {
  return operation.then<Object?>((_) => null, onError: (Object error) => error);
}

/// Race-focused coverage for output-device lifecycle coordination.
Future<StringBuffer> testAudioDeviceLifecycleRaces() async {
  final output = StringBuffer();
  if (kIsWeb) {
    output.writeln('Skipping native device lifecycle races on Web.');
    return output;
  }

  const defaultTimeout = Duration(milliseconds: 500);
  const raceTimeout = Duration(milliseconds: 400);
  SoLoud.instance.setAudioDeviceIdleTimeout(Duration.zero);
  await SoLoud.instance.init();

  try {
    var state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Initial idle stop failed: $state',
    );

    final waveform = await SoLoud.instance.loadWaveform(
      WaveForm.sin,
      false,
      1,
      0,
    );

    // Failed playback, including invalid bus validation, must not start audio.
    var failedPlaybackRejected = false;
    try {
      SoLoud.instance.play(waveform, busId: 0x7fffffff);
    } on SoLoudException {
      failedPlaybackRejected = true;
    }
    assert(
      failedPlaybackRejected,
      'Invalid-bus playback unexpectedly succeeded.',
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));
    state = SoLoud.instance.getAudioDeviceState();
    assert(
      state == AudioDeviceState.stopped,
      'Failed playback started the device: $state',
    );
    output.writeln('Failed playback leaves device stopped: OK');

    // An idle-policy update arriving immediately after an unpause must not
    // replace the required device start. Repeat this while a paused voice is
    // kept alive so the final idle check cannot legitimately stop the device.
    final raceHandle = SoLoud.instance.play(
      waveform,
      paused: true,
      looping: true,
      volume: 0.1,
    );
    for (var i = 0; i < 10; i++) {
      SoLoud.instance.setPause(raceHandle, false);
      SoLoud.instance.setAudioDeviceIdleTimeout(raceTimeout);
      state = await _waitForDeviceState(AudioDeviceState.started);
      assert(
        state == AudioDeviceState.started &&
            SoLoud.instance.getIsValidVoiceHandle(raceHandle) &&
            !SoLoud.instance.getPause(raceHandle),
        'Start/idle race iteration $i lost active playback: $state',
      );
      await Future<void>.delayed(
        raceTimeout + const Duration(milliseconds: 50),
      );
      assert(
        SoLoud.instance.getAudioDeviceState() == AudioDeviceState.started,
        'Active voice was stopped by idle policy in iteration $i.',
      );

      SoLoud.instance.setPause(raceHandle, true);
      state = await _waitForDeviceState(AudioDeviceState.stopped);
      assert(
        state == AudioDeviceState.stopped,
        'Paused voice did not stop the device in iteration $i: $state',
      );
    }
    await SoLoud.instance.stop(raceHandle);
    output
        .writeln('Start followed by idle update preserves playback (10x): OK');

    // Explicit prewarming while idle must apply a fresh timeout afterward.
    SoLoud.instance.setAudioDeviceIdleTimeout(raceTimeout);
    await SoLoud.instance.startAudioDevice();
    assert(
      SoLoud.instance.getAudioDeviceState() == AudioDeviceState.started,
      'Explicit prewarm did not complete in started state.',
    );
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Prewarmed device did not time out.',
    );
    output.writeln('Explicit prewarm times out while idle: OK');

    // A newer start must invalidate the old idle deadline and begin a fresh
    // one.
    await SoLoud.instance.startAudioDevice();
    await Future<void>.delayed(const Duration(milliseconds: 250));
    await SoLoud.instance.startAudioDevice();
    await Future<void>.delayed(const Duration(milliseconds: 250));
    state = SoLoud.instance.getAudioDeviceState();
    assert(
      state == AudioDeviceState.started,
      'The stale first idle deadline stopped a newer explicit start: $state',
    );
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Fresh idle deadline was not applied.',
    );
    output.writeln('Start supersedes stale delayed stop: OK');

    // Observe a start transition, then issue a later stop before discarding the
    // start Future. The later stop must determine the final state.
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    final orderedStart = SoLoud.instance.startAudioDevice();
    final startObservationDeadline =
        DateTime.now().add(const Duration(seconds: 1));
    while (SoLoud.instance.getAudioDeviceState() == AudioDeviceState.stopped &&
        DateTime.now().isBefore(startObservationDeadline)) {
      await Future<void>.delayed(Duration.zero);
    }
    assert(
      SoLoud.instance.getAudioDeviceState() != AudioDeviceState.stopped,
      'Start never entered or reached a non-stopped state.',
    );
    final orderedStop = SoLoud.instance.stopAudioDevice();
    await Future.wait<void>([orderedStart, orderedStop]);
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Stop following start was lost: $state',
    );
    output.writeln('Stop following start wins: OK');

    // Concurrent explicit operations must serialize, complete, and leave an
    // actual stable backend state. Verify normal recovery after every race.
    for (var i = 0; i < 5; i++) {
      final results = await Future.wait<Object?>([
        _captureError(SoLoud.instance.startAudioDevice()),
        _captureError(SoLoud.instance.stopAudioDevice()),
      ]).timeout(const Duration(seconds: 5));
      assert(
        results.every((error) => error == null),
        'Concurrent start/stop iteration $i failed: $results',
      );
      final settledState = SoLoud.instance.getAudioDeviceState();
      assert(
        settledState == AudioDeviceState.started ||
            settledState == AudioDeviceState.stopped,
        'Concurrent start/stop left transitional state: $settledState',
      );
      await SoLoud.instance.startAudioDevice();
      await SoLoud.instance.stopAudioDevice();
      state = await _waitForDeviceState(AudioDeviceState.stopped);
      assert(
        state == AudioDeviceState.stopped,
        'Lifecycle recovery failed: $state',
      );
    }
    output.writeln('Concurrent start/stop serialization (5x): OK');

// Queue a start immediately before an interruption. The interruption stop
// must win over the pending start, while the active voice remains intact
// and recovery still applies on interruption end.
    SoLoud.instance.setAudioDeviceIdleTimeout(Duration.zero);

    final handle = SoLoud.instance.play(
      waveform,
      looping: true,
      volume: 0.1,
    );

    state = await _waitForDeviceState(AudioDeviceState.started);
    assert(
      state == AudioDeviceState.started,
      'Playback did not start: $state',
    );

    SoLoud.instance.setPause(handle, true);
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Could not establish a stopped device before interruption race: $state',
    );

    SoLoud.instance.setPause(handle, false);
    final beganEvent = SoLoudController()
        .soLoudFFI
        .stateChangedEvents
        .firstWhere(
          (event) => event == PlayerStateNotification.interruptionBegan,
        )
        .timeout(const Duration(seconds: 2));
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: true);
    await beganEvent;
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Interruption did not stop device.',
    );
    assert(
      SoLoud.instance.getIsValidVoiceHandle(handle) &&
          !SoLoud.instance.getPause(handle),
      'Interruption begin mutated or invalidated the active voice.',
    );

    final endedEvent = SoLoudController()
        .soLoudFFI
        .stateChangedEvents
        .firstWhere(
          (event) => event == PlayerStateNotification.interruptionEnded,
        )
        .timeout(const Duration(seconds: 2));
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: false);
    await endedEvent;
    state = await _waitForDeviceState(AudioDeviceState.started);
    assert(
      state == AudioDeviceState.started,
      'Active interruption recovery failed.',
    );
    output.writeln('Active interruption recovery preserves voice state: OK');

    // End an interruption immediately after its begin notification. The
    // recovery start may arrive while interruptionStop is still pending or in
    // flight and must not be discarded.
    for (var i = 0; i < 20; i++) {
      final rapidBeganEvent = SoLoudController()
          .soLoudFFI
          .stateChangedEvents
          .firstWhere(
            (event) => event == PlayerStateNotification.interruptionBegan,
          )
          .timeout(const Duration(seconds: 2));
      final stoppedEvent = SoLoudController()
          .soLoudFFI
          .stateChangedEvents
          .firstWhere(
            (event) => event == PlayerStateNotification.stopped,
          )
          .timeout(const Duration(seconds: 2));
      final restartedEvent = SoLoudController()
          .soLoudFFI
          .stateChangedEvents
          .firstWhere(
            (event) => event == PlayerStateNotification.started,
          )
          .timeout(const Duration(seconds: 2));

      SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: true);
      await rapidBeganEvent;

      // End immediately; interruptionStop may still be pending or in flight.
      SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: false);

      await stoppedEvent;
      await restartedEvent;
      final rapidState = await _waitForDeviceState(AudioDeviceState.started);
      assert(
        rapidState == AudioDeviceState.started,
        'Rapid interruption cycle $i lost the recovery start: $rapidState',
      );
      assert(
        SoLoud.instance.getIsValidVoiceHandle(handle) &&
            !SoLoud.instance.getPause(handle),
        'Rapid interruption cycle $i changed the active voice.',
      );
    }
    output.writeln('Rapid interruption recovery (20x): OK');

    // Idle finite policy remains stopped after interruption recovery.
    SoLoud.instance.setPause(handle, true);
    SoLoud.instance.setAudioDeviceIdleTimeout(Duration.zero);
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Paused voice did not become idle.',
    );
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: true);
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: false);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    assert(
      SoLoud.instance.getAudioDeviceState() == AudioDeviceState.stopped,
      'Idle finite policy restarted after interruption.',
    );

    // Indefinite timeout is the other allowed interruption recovery reason.
    SoLoud.instance.setAudioDeviceIdleTimeout(null);
    state = await _waitForDeviceState(AudioDeviceState.started);
    assert(
      state == AudioDeviceState.started,
      'Keep-alive did not start device.',
    );
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: true);
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Keep-alive interruption did not stop.',
    );
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: false);
    state = await _waitForDeviceState(AudioDeviceState.started);
    assert(
      state == AudioDeviceState.started,
      'Keep-alive recovery did not restart.',
    );
    output.writeln('Idle/keep-alive interruption policies: OK');

    // An interruption whose "ended" notification never arrives must not wedge
    // the device permanently. iOS does not reliably deliver
    // AVAudioSessionInterruptionTypeEnded -- notably when the interruption ends
    // while the app is backgrounded -- and the interruption flag is otherwise
    // only cleared by init()/deinit(). While it was latched, startAudioDevice()
    // returned success without touching the device, so the app saw a healthy
    // API and silence. An explicit start is authoritative and must recover.
    SoLoud.instance.setAudioDeviceIdleTimeout(null);
    state = await _waitForDeviceState(AudioDeviceState.started);
    assert(
      state == AudioDeviceState.started,
      'Keep-alive did not start device before the missed-end check.',
    );
    SoLoudController().soLoudFFI.debugTriggerAudioInterruption(began: true);
    state = await _waitForDeviceState(AudioDeviceState.stopped);
    assert(
      state == AudioDeviceState.stopped,
      'Interruption did not stop device before the missed-end check.',
    );
    // Deliberately no matching `began: false`: that is the dropped one.
    await SoLoud.instance.startAudioDevice();
    state = await _waitForDeviceState(AudioDeviceState.started);
    assert(
      state == AudioDeviceState.started,
      'Explicit start did not recover from a missed interruption end.',
    );
    output.writeln('Explicit start recovers a missed interruption end: OK');

    await SoLoud.instance.stop(handle);
    await SoLoud.instance.disposeSource(waveform);

    // Teardown must cancel a long pending idle deadline and join the scheduler.
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    await SoLoud.instance.deinitAsync().timeout(const Duration(seconds: 5));
    assert(
      SoLoud.instance.getAudioDeviceState() == AudioDeviceState.uninitialized,
      'Teardown during pending timeout left a device initialized.',
    );
    output.writeln('Teardown cancels pending lifecycle request: OK');

    // Race teardown against an active explicit start operation. The operation
    // may finish or be rejected depending on lock acquisition, but teardown
    // must complete and leave no initialized backend or scheduler.
    await SoLoud.instance.init();
    SoLoud.instance.setAudioDeviceIdleTimeout(Duration.zero);
    await _waitForDeviceState(AudioDeviceState.stopped);
    SoLoud.instance.setAudioDeviceIdleTimeout(const Duration(seconds: 5));
    final activeOperation = _captureError(SoLoud.instance.startAudioDevice());
    final activeTeardown =
        SoLoud.instance.deinitAsync().timeout(const Duration(seconds: 5));
    final operationError = await activeOperation;
    await activeTeardown;
    assert(
      operationError == null || operationError is SoLoudException,
      'Unexpected lifecycle-operation error during teardown: $operationError',
    );
    assert(
      SoLoud.instance.getAudioDeviceState() == AudioDeviceState.uninitialized,
      'Teardown race left the device initialized.',
    );
    output.writeln('Teardown during active lifecycle operation: OK');

    // Repeated start/deinit races must not deadlock callback teardown or leak
    // scheduler threads. Keep the timeout finite so each cycle also exercises
    // the idle lifecycle path before the immediate start request.
    for (var i = 0; i < 100; i++) {
      await SoLoud.instance.init();
      assert(
        SoLoud.instance.isInitialized,
        'Async cycle $i failed to initialize.',
      );

      final startOperation = _captureError(
        SoLoud.instance.startAudioDevice(),
      );
      await SoLoud.instance.deinitAsync().timeout(const Duration(seconds: 5));
      final startError = await startOperation;
      assert(
        startError == null || startError is SoLoudException,
        'Async cycle $i start failed unexpectedly: $startError',
      );
      assert(
        !SoLoud.instance.isInitialized,
        'Async cycle $i failed to deinit.',
      );
      assert(
        SoLoud.instance.getAudioDeviceState() == AudioDeviceState.uninitialized,
        'Async cycle $i left the backend initialized.',
      );
    }
    output.writeln('Repeated start/deinit races (100x): OK');
  } finally {
    SoLoud.instance.setAudioDeviceIdleTimeout(defaultTimeout);
    await SoLoud.instance.deinitAsync();
  }

  return output;
}

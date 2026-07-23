import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Validates idle-timeout behavior for the audio device:
/// 1) set timeout to 500 ms, 2) init, 3) state is started,
/// 4) wait timeout, 5) state becomes stopped.
Future<StringBuffer> testAudioDeviceIdleTimeout() async {
  final strBuf = StringBuffer();
  const idleTimeout = Duration(milliseconds: 500);

  SoLoud.instance.setAudioDeviceIdleTimeout(idleTimeout);
  await SoLoud.instance.init();

  try {
    final startedState = SoLoud.instance.getAudioDeviceState();
    assert(
      startedState == AudioDeviceState.started,
      'Immediately after init(), expected AudioDeviceState.started '
      'but got $startedState.',
    );
    strBuf.writeln('State immediately after init(): $startedState');

    await Future<void>.delayed(idleTimeout);

    var stoppedState = SoLoud.instance.getAudioDeviceState();

    if (kIsWeb) {
      strBuf.writeln(
        'Web keeps the device running; skipping stopped-state assertion.',
      );
      return strBuf;
    }

    // Allow a short grace period for async stop transitions.
    final deadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (stoppedState != AudioDeviceState.stopped && DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stoppedState = SoLoud.instance.getAudioDeviceState();
    }

    assert(
      stoppedState == AudioDeviceState.stopped,
      'After waiting ${idleTimeout.inMilliseconds} ms, expected '
      'AudioDeviceState.stopped but got $stoppedState.',
    );
    strBuf.writeln('State after idle timeout: $stoppedState');

    // 1) Create a basic waveform audio source.
    final waveform = await SoLoud.instance.loadWaveform(
      WaveForm.sin,
      false,
      1,
      0,
    );
    SoLoud.instance.setWaveformFreq(waveform, 440);

    // 2) Create a handle with playback initially disabled (paused).
    final handle = SoLoud.instance.play(
      waveform,
      paused: true,
      looping: true,
      volume: 0.2,
    );

    // 3) Validate device state remains stopped.
    final stateAfterPausedHandle = SoLoud.instance.getAudioDeviceState();
    assert(
      stateAfterPausedHandle == AudioDeviceState.stopped,
      'After creating a paused handle, expected AudioDeviceState.stopped '
      'but got $stateAfterPausedHandle.',
    );
    strBuf.writeln('State after paused handle creation: $stateAfterPausedHandle');

    // 4) Play the sound handle for a few seconds.
    SoLoud.instance.setPause(handle, false);
    await Future<void>.delayed(const Duration(seconds: 2));

    // 5) Validate device state is started.
    final stateWhilePlaying = SoLoud.instance.getAudioDeviceState();
    assert(
      stateWhilePlaying == AudioDeviceState.started,
      'While playing, expected AudioDeviceState.started '
      'but got $stateWhilePlaying.',
    );
    strBuf.writeln('State while playing: $stateWhilePlaying');

    // Default explicit stop is idle-only and must not interrupt playback.
    await SoLoud.instance.stopAudioDevice();
    final stateAfterConditionalStop = SoLoud.instance.getAudioDeviceState();
    assert(
      stateAfterConditionalStop == AudioDeviceState.started,
      'stopAudioDevice() while active should be a no-op, but got '
      '$stateAfterConditionalStop.',
    );
    assert(
      !SoLoud.instance.getPause(handle),
      'Conditional device stop must not pause the active voice.',
    );
    strBuf.writeln(
      'State after conditional stop while active: '
      '$stateAfterConditionalStop',
    );

    // Forced stop operates only the output device and preserves voice state.
    await SoLoud.instance.stopAudioDevice(force: true);
    final stateAfterForcedStop = SoLoud.instance.getAudioDeviceState();
    assert(
      stateAfterForcedStop == AudioDeviceState.stopped,
      'stopAudioDevice(force: true) should stop during active playback, but '
      'got $stateAfterForcedStop.',
    );
    assert(
      !SoLoud.instance.getPause(handle),
      'Forced device stop must not pause or mutate the active voice.',
    );
    strBuf.writeln('State after forced stop: $stateAfterForcedStop');

    await SoLoud.instance.startAudioDevice();
    final stateAfterExplicitStart = SoLoud.instance.getAudioDeviceState();
    assert(
      stateAfterExplicitStart == AudioDeviceState.started,
      'startAudioDevice() should complete after restart, but got '
      '$stateAfterExplicitStart.',
    );
    strBuf.writeln('State after explicit restart: $stateAfterExplicitStart');

    // 6) Pause the sound handle.
    SoLoud.instance.setPause(handle, true);

    // 7) Wait idleTimeout again, then validate stopped.
    await Future<void>.delayed(idleTimeout);
    var stateAfterPauseIdle = SoLoud.instance.getAudioDeviceState();

    final pauseIdleDeadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateAfterPauseIdle != AudioDeviceState.stopped && DateTime.now().isBefore(pauseIdleDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateAfterPauseIdle = SoLoud.instance.getAudioDeviceState();
    }

    assert(
      stateAfterPauseIdle == AudioDeviceState.stopped,
      'After pausing and waiting ${idleTimeout.inMilliseconds} ms, expected '
      'AudioDeviceState.stopped but got $stateAfterPauseIdle.',
    );
    strBuf.writeln('State after pause + idle timeout: $stateAfterPauseIdle');

    // 8) Resume and verify started before schedulePause.
    SoLoud.instance.setPause(handle, false);
    var stateBeforeSchedulePause = SoLoud.instance.getAudioDeviceState();
    final startedBeforePauseDeadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateBeforeSchedulePause != AudioDeviceState.started && DateTime.now().isBefore(startedBeforePauseDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateBeforeSchedulePause = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateBeforeSchedulePause == AudioDeviceState.started,
      'Before schedulePause, expected AudioDeviceState.started '
      'but got $stateBeforeSchedulePause.',
    );
    strBuf.writeln('State before schedulePause: $stateBeforeSchedulePause');

    // 9) Schedule pause in 1000 ms and verify handle paused.
    SoLoud.instance.schedulePause(handle, const Duration(milliseconds: 1000));
    await Future<void>.delayed(const Duration(milliseconds: 1200));
    final pausedAfterSchedulePause = SoLoud.instance.getPause(handle);
    assert(
      pausedAfterSchedulePause,
      'After schedulePause(1000ms), expected handle to be paused.',
    );
    strBuf.writeln('Handle paused after schedulePause: $pausedAfterSchedulePause');

    // 10) Wait idle timeout again and verify stopped.
    await Future<void>.delayed(idleTimeout);
    var stateAfterSchedulePauseIdle = SoLoud.instance.getAudioDeviceState();
    final stoppedAfterSchedulePauseDeadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateAfterSchedulePauseIdle != AudioDeviceState.stopped && DateTime.now().isBefore(stoppedAfterSchedulePauseDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateAfterSchedulePauseIdle = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateAfterSchedulePauseIdle == AudioDeviceState.stopped,
      'After schedulePause and idle timeout, expected '
      'AudioDeviceState.stopped but got $stateAfterSchedulePauseIdle.',
    );
    strBuf.writeln(
      'State after schedulePause + idle timeout: $stateAfterSchedulePauseIdle',
    );

    // 11) Resume and verify started before scheduleStop.
    SoLoud.instance.setPause(handle, false);
    var stateBeforeScheduleStop = SoLoud.instance.getAudioDeviceState();
    final startedBeforeStopDeadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateBeforeScheduleStop != AudioDeviceState.started && DateTime.now().isBefore(startedBeforeStopDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateBeforeScheduleStop = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateBeforeScheduleStop == AudioDeviceState.started,
      'Before scheduleStop, expected AudioDeviceState.started '
      'but got $stateBeforeScheduleStop.',
    );
    strBuf.writeln('State before scheduleStop: $stateBeforeScheduleStop');

    // 12) Schedule stop in 1000 ms and verify handle invalidated.
    SoLoud.instance.scheduleStop(handle, const Duration(milliseconds: 1000));
    await Future<void>.delayed(const Duration(milliseconds: 1100));
    var isHandleValidAfterScheduleStop = SoLoud.instance.getIsValidVoiceHandle(handle);
    final invalidHandleDeadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (isHandleValidAfterScheduleStop && DateTime.now().isBefore(invalidHandleDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      isHandleValidAfterScheduleStop = SoLoud.instance.getIsValidVoiceHandle(handle);
    }
    assert(
      !isHandleValidAfterScheduleStop,
      'After scheduleStop(1000ms), expected handle to be invalid.',
    );
    strBuf.writeln(
      'Handle valid after scheduleStop: $isHandleValidAfterScheduleStop',
    );

    // 13) Wait idle timeout again and verify stopped (scheduleStop last).
    await Future<void>.delayed(idleTimeout);
    var stateAfterScheduleStopIdle = SoLoud.instance.getAudioDeviceState();
    final stoppedAfterScheduleStopDeadline = DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateAfterScheduleStopIdle != AudioDeviceState.stopped && DateTime.now().isBefore(stoppedAfterScheduleStopDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateAfterScheduleStopIdle = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateAfterScheduleStopIdle == AudioDeviceState.stopped,
      'After scheduleStop and idle timeout, expected '
      'AudioDeviceState.stopped but got $stateAfterScheduleStopIdle.',
    );
    strBuf.writeln(
      'State after scheduleStop + idle timeout: $stateAfterScheduleStopIdle',
    );

    // 14) Load a real asset and get its playback duration.
    await SoLoud.instance.disposeSource(waveform);
    final explosion = await SoLoud.instance.loadAsset(
      'assets/audio/explosion.mp3',
    );
    final explosionDuration = SoLoud.instance.getLength(explosion);
    strBuf.writeln(
      'Explosion duration: ${explosionDuration.inMilliseconds}ms',
    );

    // 15) Play unpaused and non-looping, then wait for started (max 1000 ms).
    final explosionHandle = SoLoud.instance.play(
      explosion,
      paused: false,
      looping: false,
    );
    var stateDuringExplosionStart = SoLoud.instance.getAudioDeviceState();
    final startedExplosionDeadline =
        DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateDuringExplosionStart != AudioDeviceState.started &&
        DateTime.now().isBefore(startedExplosionDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateDuringExplosionStart = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateDuringExplosionStart == AudioDeviceState.started,
      'After starting explosion playback, expected AudioDeviceState.started '
      'within 1000 ms but got $stateDuringExplosionStart.',
    );
    strBuf.writeln('State during explosion playback: $stateDuringExplosionStart');

    // 16) Wait for full playback duration.
    await Future<void>.delayed(explosionDuration);

    // 17) Validate device stops following playback completion.
    var stateAfterExplosionPlayback = SoLoud.instance.getAudioDeviceState();
    final stoppedAfterExplosionDeadline =
        DateTime.now().add(idleTimeout + const Duration(milliseconds: 1000));
    while (stateAfterExplosionPlayback != AudioDeviceState.stopped &&
        DateTime.now().isBefore(stoppedAfterExplosionDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateAfterExplosionPlayback = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateAfterExplosionPlayback == AudioDeviceState.stopped,
      'After explosion playback completion, expected '
      'AudioDeviceState.stopped but got $stateAfterExplosionPlayback.',
    );
    strBuf.writeln(
      'State after explosion playback completion: '
      '$stateAfterExplosionPlayback',
    );
    assert(
      !SoLoud.instance.getIsValidVoiceHandle(explosionHandle),
      'Explosion handle should be invalid after playback completion.',
    );

    await SoLoud.instance.disposeSource(explosion);

    // This exceeds unsigned 32-bit milliseconds. The previous signed 32-bit
    // native ABI wrapped it to 100 ms.
    const largeTimeout = Duration(milliseconds: 0x100000000 + 100);
    SoLoud.instance.setAudioDeviceIdleTimeout(largeTimeout);
  } finally {
    if (SoLoud.instance.isInitialized) {
      SoLoud.instance.deinit();
    }
  }

  // Verify both persistence across Player recreation and the 64-bit native
  // representation: the restored timeout must not behave like wrapped 100 ms
  // or the default 500 ms timeout.
  await SoLoud.instance.init();
  try {
    await Future<void>.delayed(const Duration(milliseconds: 750));
    final stateAfterLargePersistentTimeout =
        SoLoud.instance.getAudioDeviceState();
    assert(
      stateAfterLargePersistentTimeout == AudioDeviceState.started,
      'A persisted timeout larger than 32-bit milliseconds wrapped or reset: '
      '$stateAfterLargePersistentTimeout.',
    );
    strBuf.writeln(
      'State after persisted 64-bit timeout: '
      '$stateAfterLargePersistentTimeout',
    );

    SoLoud.instance.setAudioDeviceIdleTimeout(Duration.zero);
    var stateAfterRuntimeZero = SoLoud.instance.getAudioDeviceState();
    final zeroTimeoutDeadline =
        DateTime.now().add(const Duration(milliseconds: 1000));
    while (stateAfterRuntimeZero != AudioDeviceState.stopped &&
        DateTime.now().isBefore(zeroTimeoutDeadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 25));
      stateAfterRuntimeZero = SoLoud.instance.getAudioDeviceState();
    }
    assert(
      stateAfterRuntimeZero == AudioDeviceState.stopped,
      'Changing a restored timeout to zero while idle should stop the device, '
      'but got $stateAfterRuntimeZero.',
    );
    strBuf.writeln('State after runtime zero timeout: $stateAfterRuntimeZero');
  } finally {
    SoLoud.instance.setAudioDeviceIdleTimeout(idleTimeout);
    if (SoLoud.instance.isInitialized) {
      SoLoud.instance.deinit();
    }
  }

  return strBuf;
}

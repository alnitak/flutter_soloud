#pragma once

// Named barriers for forcing the device-lifecycle interleavings that matter.
//
// The races these cover are between a direct device operation (an explicit
// start/stop, a device change, a teardown) and lifecycle intent posted
// concurrently by ordinary playback. Every one of them turns on a window of a
// few instructions between observing state and acting on it, so a timing-based
// test can only make the collision likely. A barrier makes it certain.
//
// The whole facility compiles to nothing unless SOLOUD_LIFECYCLE_TEST_HOOKS is
// defined, which no shipping build does. `SOLOUD_TEST_BARRIER()` expands to a
// void expression, so the production call sites cost nothing and cannot drift
// out of sync with the tests.

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)

namespace soloud_test
{
  /// Points a test can park production code at. Each names the instant
  /// *after* a decision has been made but *before* it has been acted on --
  /// that is the window every one of these races lives in.
  enum class DeviceBarrier
  {
    /// Inside the native changeDevice() export, after the global `player` has
    /// been read but before Player::changeDevice() runs. Parks a change worker
    /// against a concurrent teardown.
    changeDeviceEntered = 0,
    /// In Player::changeDevice(), after `shouldStartReplacement` has been
    /// decided from the active-voice count and device state.
    changeDeviceStartDecided,
    /// In Player::stopAudioDevice(), after the conditional form has observed
    /// the active-voice count as zero.
    stopAudioDeviceVoiceCountObserved,
    /// In Player::startAudioDevice(), after the stale-interruption latch has
    /// been cleared and superseded work cancelled.
    startAudioDeviceLatchCleared,
    /// In Player::performAudioDeviceStart(), before the backend start runs.
    performAudioDeviceStartEntered,
    /// In MixerOutput::onAudioData(), after the callback has been admitted to
    /// the active capture session but before it touches any capture state.
    mixerCaptureCallbackAdmitted,
    /// In the miniaudio notification callback, after it has been admitted and
    /// has pinned the current engine, but before it dereferences the Soloud or
    /// the interruption callback's Player* context.
    deviceNotificationAdmitted,
    /// At the top of Player::dispose(), before it stops accepting lifecycle
    /// requests. Parking here gives the scheduler an opportunity to act on
    /// anything teardown queued behind it, which is what makes "teardown
    /// performed no device start" a deterministic assertion instead of a race.
    playerDisposeEntered,
    /// In the deferred idle-timeout worker, after it has applied a published
    /// policy but before it decides whether to go idle. This is the window a
    /// naive "clear the queued flag after applying" scheme loses a write in.
    idleTimeoutWorkerApplied,
    barrierCount
  };

  /// Arm [barrier]. The *next* thread reaching it parks until released; any
  /// thread arriving after that passes straight through, so a barrier cannot
  /// accidentally catch work the test itself triggered.
  void armBarrier(DeviceBarrier barrier);

  /// Block until a thread has parked on [barrier].
  void waitBarrierReached(DeviceBarrier barrier);

  /// Release [barrier] and disarm it.
  void releaseBarrier(DeviceBarrier barrier);

  /// Called from production code. A no-op unless [barrier] is armed.
  void hitBarrier(DeviceBarrier barrier);

  /// Force the next [count] backend device starts to fail, so a test can drive
  /// the rebuild/retry path and the failure reporting behind it.
  void failNextDeviceStarts(int count);

  /// How many forced failures are still pending.
  int pendingForcedDeviceStartFailures();

  /// Counts real backend device starts -- the points where ma_device_start()
  /// actually runs, not merely where the engine considered starting. A start
  /// request that finds the device already running costs nothing and is
  /// deliberately not counted, which is what lets a test assert "teardown
  /// performed no device start" without tripping over a start that was already
  /// queued and harmlessly no-ops.
  void recordBackendDeviceStart();
  int backendDeviceStartCount();
  void resetBackendDeviceStartCount();

  /// Consume one forced failure. Called by the backend start path so a test
  /// can drive rebuild/retry deterministically rather than by unplugging
  /// hardware. Returns true when this start must be failed.
  bool consumeForcedDeviceStartFailure();
} // namespace soloud_test

#define SOLOUD_TEST_BARRIER(name)                                              \
  ::soloud_test::hitBarrier(::soloud_test::DeviceBarrier::name)

#else

#define SOLOUD_TEST_BARRIER(name) ((void)0)

#endif // SOLOUD_LIFECYCLE_TEST_HOOKS

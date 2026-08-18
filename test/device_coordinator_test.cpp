// Standalone native regression tests for the audio-device lifecycle
// coordinator.
//
// Every race covered here lives in a window of a few instructions between a
// direct device operation *observing* state and *acting* on it, while ordinary
// playback posts lifecycle intent from another thread without taking the
// device-operation mutex. Timing-based tests can only make those collisions
// likely, so each one here is forced with a named barrier
// (src/device_lifecycle_test_hooks.h) that parks production code exactly at the
// observation point.
//
// What these tests pin down:
//
//   * a device change and a teardown cannot overlap on the same Player -- the
//     change is pinned for its whole native operation, so a concurrent
//     dispose() cannot free the Player underneath it;
//   * a direct operation only cancels lifecycle work that predates its own
//     decision: a conditional stop, a device change and an explicit start each
//     leave newer intent queued instead of erasing it;
//   * an explicit start that a genuine OS interruption overtakes reports the
//     failure instead of falsely succeeding, and does not cancel the stop that
//     interruption queued;
//   * setAudioDeviceIdleTimeout() returns promptly while the engine lifecycle
//     mutex is held by an in-flight initialization;
//   * an automatic start that exhausts rebuild/retry is reported as an event,
//     so a background failure cannot leave a silent, apparently-playing engine;
//   * clocked/scheduled playback never performs a backend device start on the
//     calling thread.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_device_coordinator_test.sh

#include "device_lifecycle_test_hooks.h"
#include "mixeroutput/mixer_output.h"
#include "enums.h"
#include "soloud/include/soloud_internal.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

extern "C"
{
    void prepareEngineInit(int64_t owner_engine_id);
    enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate,
                                 unsigned int bufferSize, unsigned int channels,
                                 unsigned int lowLatency);
    void dispose();
    int isInited();

    enum PlayerErrors loadWaveform(int waveform, bool superWave, float scale,
                                   float detune, unsigned int *hash);
    enum PlayerErrors play(unsigned int soundHash, unsigned int busId,
                           float volume, float pan, bool paused, bool looping,
                           double loopingStartAt, unsigned int *handle);
    enum PlayerErrors playClocked(unsigned int soundHash, double soundTime,
                                  unsigned int busId, float volume, float pan,
                                  unsigned int *handle);
    enum PlayerErrors setPause(unsigned int handle, bool pause);
    enum PlayerErrors stop(unsigned int handle);

    enum PlayerErrors changeDevice(int deviceID);
    enum PlayerErrors startAudioDevice();
    enum PlayerErrors stopAudioDevice(unsigned int force);
    enum AudioDeviceState getAudioDeviceState();
    void setAudioDeviceIdleTimeout(int64_t timeoutMs);

    void setDartEventCallback(void (*voice_ended)(unsigned int *),
                              void (*file_loaded)(enum PlayerErrors *, char *,
                                                  unsigned int *, uint64_t *),
                              void (*state_changed)(enum PlayerStateEvents *),
                              int64_t owner_engine_id);

    enum PlayerErrors play3dClocked(unsigned int soundHash, double soundTime,
                                    unsigned int busId, float posX, float posY,
                                    float posZ, float velX, float velY,
                                    float velZ, float volume,
                                    unsigned int *handle);
    enum PlayerErrors playScheduled(unsigned int soundHash, double atTime,
                                    double duration, unsigned int busId,
                                    float volume, float pan,
                                    unsigned int *handle);

    enum PlayerErrors startMixerCapture(int format, int sampleRate,
                                        int channels, int bufferSizeBytes,
                                        int notificationThresholdBytes,
                                        int chunkPCMFrames);
    void stopMixerCapture();
    int isMixerCaptureRunning();

    // Test-only hooks (SOLOUD_LIFECYCLE_TEST_HOOKS).
    bool requestEngineTeardownForEngine(int64_t engine_id);
    void clearDartCallbackRegistrations();
    void soloudTestLockInitDeinit();
    void soloudTestUnlockInitDeinit();
    void soloudTestSetEngineStateCallback(unsigned int enable);
    int soloudTestIdleTimeoutWorkerPeak();
    void soloudTestResetIdleTimeoutWorkerPeak();
    int64_t soloudTestAppliedIdleTimeoutMs();
    int soloudTestEngineTeardownCompletedCount();
}

namespace
{

using soloud_test::DeviceBarrier;

constexpr int64_t kEngineId = 2001;

// Wall-clock budget for a call that must not wait on a device operation or on
// the engine lifecycle mutex. Generous on purpose: the number it excludes is
// the multi-second device open, not a scheduling hiccup.
constexpr long long kNonBlockingBudgetMs = 250;

// Long enough that the idle scheduler never fires during a test, but still a
// finite policy: the indefinite (-1) keep-alive is an input to several of the
// decisions under test and would make them trivially true.
constexpr int64_t kQuietIdleTimeoutMs = 600000;

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...)                                    \
    do                                                                    \
    {                                                                     \
        ++gAssertions;                                                    \
        if (!(condition))                                                 \
        {                                                                 \
            ++gFailures;                                                  \
            std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", __FILE__, \
                         __LINE__, ##__VA_ARGS__);                        \
        }                                                                 \
    } while (0)

std::atomic<int> gStartFailureEvents{0};

void onVoiceEnded(unsigned int *handle) { std::free(handle); }

void onFileLoaded(enum PlayerErrors *e, char *name, unsigned int *hash,
                  uint64_t *counter)
{
    std::free(e);
    std::free(name);
    std::free(hash);
    std::free(counter);
}

std::atomic<int> gStateEvents{0};

void onStateChanged(enum PlayerStateEvents *state)
{
    if (state == nullptr)
        return;
    if (*state == PlayerStateEvents::event_audio_device_start_failed)
        gStartFailureEvents.fetch_add(1, std::memory_order_acq_rel);
    gStateEvents.fetch_add(1, std::memory_order_acq_rel);
    // The native bridge malloc()s this per event and hands ownership over,
    // exactly as it does to Dart. Freeing it keeps leak-sanitizer runs
    // meaningful.
    std::free(state);
}

/// Bring an engine up. Returns false when the environment has no usable output
/// device, which every caller treats as "skip" rather than "pass".
bool bringUpEngine()
{
    prepareEngineInit(kEngineId);
    const PlayerErrors err = initEngine(-1, 44100, 2048, 2, 1);
    if (err != PlayerErrors::noError)
        return false;
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);
    return isInited() != 0;
}

void tearDownEngine()
{
    if (isInited())
        dispose();
}

/// A waveform needs no asset and no decoder, so it is the cheapest way to get a
/// real unpaused voice -- which is what makes resumeEngine() post a genuine
/// start request from another thread.
unsigned int loadTestWaveform()
{
    unsigned int hash = 0;
    const PlayerErrors err = loadWaveform(0, false, 1.0f, 1.0f, &hash);
    return err == PlayerErrors::noError ? hash : 0;
}

unsigned int playUnpaused(unsigned int hash)
{
    unsigned int handle = 0;
    play(hash, 0, 1.0f, 0.0f, /*paused*/ false, /*looping*/ true, 0.0, &handle);
    return handle;
}

long long millisSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

// ---------------------------------------------------------------------------

/// A device change holds the engine for its whole native operation, so a
/// teardown cannot dispose the Player it is running inside.
///
/// Without the pin the change worker resumes inside a freed Player: the window
/// is wide here because device enumeration deliberately runs before the
/// device-operation lock is taken.
void testChangeDeviceCannotOverlapTeardown()
{
    std::printf("change device vs teardown\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    soloud_test::armBarrier(DeviceBarrier::changeDeviceEntered);

    std::atomic<bool> changeDone{false};
    std::atomic<bool> disposeDone{false};

    std::thread changer([&] {
        changeDevice(-1);
        changeDone.store(true, std::memory_order_release);
    });

    soloud_test::waitBarrierReached(DeviceBarrier::changeDeviceEntered);

    std::thread disposer([&] {
        dispose();
        disposeDone.store(true, std::memory_order_release);
    });

    // The teardown must not be able to run while the change is parked inside
    // the engine. If it can, the change is about to touch freed memory.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT(!disposeDone.load(std::memory_order_acquire),
           "dispose() ran while a device change was inside the engine");
    EXPECT(!changeDone.load(std::memory_order_acquire),
           "the parked device change should not have completed yet");

    soloud_test::releaseBarrier(DeviceBarrier::changeDeviceEntered);
    changer.join();
    disposer.join();

    EXPECT(changeDone.load(std::memory_order_acquire), "changeDevice() hung");
    EXPECT(disposeDone.load(std::memory_order_acquire), "dispose() hung");
    EXPECT(isInited() == 0, "the engine should be torn down at the end");
    std::printf("  ok: teardown serialized behind the device change\n");
}

/// A device change arriving after a teardown finds no initialized engine and
/// reports it, rather than operating on whatever Player is installed.
void testStaleChangeDeviceAfterTeardown()
{
    std::printf("stale device change after teardown\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    dispose();
    EXPECT(isInited() == 0, "the engine should be torn down");

    const PlayerErrors err = changeDevice(-1);
    EXPECT(err == PlayerErrors::backendNotInited,
           "a device change after teardown should report backendNotInited, "
           "got %d",
           (int)err);
    std::printf("  ok: reported backendNotInited\n");
}

/// A conditional stop observes an idle engine, then playback starts. The stop
/// must not erase the newer start, and must not stop the device under it.
void testConditionalStopDoesNotSwallowNewPlayback()
{
    std::printf("conditional stop vs play\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    // A long finite timeout keeps the idle scheduler from stopping the device
    // for unrelated reasons. Deliberately not the indefinite (-1) policy: that
    // is itself one of the inputs to the decisions under test, and would make
    // them trivially true.
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");

    soloud_test::armBarrier(DeviceBarrier::stopAudioDeviceVoiceCountObserved);

    std::atomic<int> stopResult{-1};
    std::thread stopper([&] {
        stopResult.store((int)stopAudioDevice(/*force*/ 0),
                         std::memory_order_release);
    });

    soloud_test::waitBarrierReached(
        DeviceBarrier::stopAudioDeviceVoiceCountObserved);

    // The stop has already decided the engine is idle. Start playback now:
    // play() does not wait for the device-operation mutex, so its start request
    // lands while the stop is parked.
    const unsigned int handle = playUnpaused(hash);
    EXPECT(handle != 0, "playback should start");

    soloud_test::releaseBarrier(
        DeviceBarrier::stopAudioDeviceVoiceCountObserved);
    stopper.join();

    EXPECT(stopResult.load(std::memory_order_acquire) ==
               (int)PlayerErrors::noError,
           "the conditional stop should report success");

    // Give the scheduler a moment to act on whichever request survived.
    for (int i = 0; i < 100 && getAudioDeviceState() != audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(getAudioDeviceState() == audioDeviceStarted,
           "an unpaused voice must not be left with a stopped device "
           "(state %d)",
           (int)getAudioDeviceState());

    stop(handle);
    tearDownEngine();
    std::printf("  ok: the newer start survived the conditional stop\n");
}

/// The same guarantee for an unpause rather than a fresh play.
void testConditionalStopDoesNotSwallowUnpause()
{
    std::printf("conditional stop vs unpause\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");

    unsigned int handle = 0;
    play(hash, 0, 1.0f, 0.0f, /*paused*/ true, /*looping*/ true, 0.0, &handle);
    EXPECT(handle != 0, "a paused voice should be created");

    soloud_test::armBarrier(DeviceBarrier::stopAudioDeviceVoiceCountObserved);

    std::thread stopper([&] { stopAudioDevice(/*force*/ 0); });
    soloud_test::waitBarrierReached(
        DeviceBarrier::stopAudioDeviceVoiceCountObserved);

    setPause(handle, false);

    soloud_test::releaseBarrier(
        DeviceBarrier::stopAudioDeviceVoiceCountObserved);
    stopper.join();

    for (int i = 0; i < 100 && getAudioDeviceState() != audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(getAudioDeviceState() == audioDeviceStarted,
           "an unpaused voice must not be left with a stopped device "
           "(state %d)",
           (int)getAudioDeviceState());

    stop(handle);
    tearDownEngine();
    std::printf("  ok: the unpause survived the conditional stop\n");
}

/// A device change decides the replacement can stay stopped, then playback
/// starts. The replacement must come up running rather than silently stopped.
void testChangeDeviceDoesNotSwallowPlayback()
{
    std::printf("change device vs play\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");

    // Start from a stopped device so `shouldStartReplacement` is decided false.
    // Without this the swap sees a running device, decides to restart it
    // anyway, and the test would pass no matter what the cancellation does.
    stopAudioDevice(/*force*/ 1);
    EXPECT(getAudioDeviceState() != audioDeviceStarted,
           "the device must be stopped before the swap decides (state %d)",
           (int)getAudioDeviceState());

    soloud_test::armBarrier(DeviceBarrier::changeDeviceStartDecided);

    std::atomic<int> changeResult{-1};
    std::thread changer([&] {
        changeResult.store((int)changeDevice(-1), std::memory_order_release);
    });

    soloud_test::waitBarrierReached(DeviceBarrier::changeDeviceStartDecided);

    const unsigned int handle = playUnpaused(hash);
    EXPECT(handle != 0, "playback should start");

    soloud_test::releaseBarrier(DeviceBarrier::changeDeviceStartDecided);
    changer.join();

    EXPECT(changeResult.load(std::memory_order_acquire) ==
               (int)PlayerErrors::noError,
           "the device change should succeed");

    for (int i = 0; i < 100 && getAudioDeviceState() != audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(getAudioDeviceState() == audioDeviceStarted,
           "the replacement device must be running under an active voice "
           "(state %d)",
           (int)getAudioDeviceState());

    stop(handle);
    tearDownEngine();
    std::printf("  ok: the replacement came up running\n");
}

/// An explicit start clears the stale-interruption latch, then a genuine
/// interruption arrives. The start must report the failure rather than claim
/// success, and must not cancel the stop the interruption queued.
void testExplicitStartYieldsToGenuineInterruption()
{
    std::printf("explicit start vs genuine interruption\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    // Begin with the device running. If the interruption stop is erased the
    // device simply stays started, which is what makes the final assertion
    // discriminating rather than trivially true.
    startAudioDevice();
    for (int i = 0; i < 100 && getAudioDeviceState() != audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT(getAudioDeviceState() == audioDeviceStarted,
           "the device must be running before the explicit start under test "
           "(state %d)",
           (int)getAudioDeviceState());

    soloud_test::armBarrier(DeviceBarrier::startAudioDeviceLatchCleared);

    std::atomic<int> startResult{-1};
    std::thread starter([&] {
        startResult.store((int)startAudioDevice(), std::memory_order_release);
    });

    soloud_test::waitBarrierReached(DeviceBarrier::startAudioDeviceLatchCleared);

    // Delivered through the backend notification path the OS uses, which -- as
    // in production -- does not take the engine lifecycle mutex the parked
    // start is holding.
    SoLoud::miniaudio_debugTriggerAudioInterruption(true);

    soloud_test::releaseBarrier(DeviceBarrier::startAudioDeviceLatchCleared);
    starter.join();

    EXPECT(startResult.load(std::memory_order_acquire) ==
               (int)PlayerErrors::audioDeviceFailedToStart,
           "an explicit start overtaken by a real interruption must report "
           "failure, got %d",
           startResult.load(std::memory_order_acquire));

    // Let the interruption stop run; it must still be queued.
    for (int i = 0; i < 100 && getAudioDeviceState() == audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(getAudioDeviceState() != audioDeviceStarted,
           "the interruption stop was cancelled by the older explicit start: "
           "the device is still running (state %d)",
           (int)getAudioDeviceState());

    SoLoud::miniaudio_debugTriggerAudioInterruption(false);
    tearDownEngine();
    std::printf("  ok: the interruption stop survived the explicit start\n");
}

/// The idle-timeout setter is synchronous and documented as callable at any
/// time, so it must not wait for the engine lifecycle mutex -- which an
/// in-flight initialization holds across the whole native device open.
void testIdleTimeoutSetterDoesNotBlockOnInit()
{
    std::printf("idle timeout setter vs held lifecycle mutex\n");

    // Run the setter on its own thread and time it from here. If it blocks on
    // the lifecycle mutex it blocks *forever* -- this test holds that mutex --
    // so calling it inline would hang the suite instead of failing it.
    soloudTestLockInitDeinit();

    std::atomic<bool> setterReturned{false};
    const auto started = std::chrono::steady_clock::now();
    std::thread setter([&] {
        setAudioDeviceIdleTimeout(1234);
        setterReturned.store(true, std::memory_order_release);
    });

    while (!setterReturned.load(std::memory_order_acquire) &&
           millisSince(started) < kNonBlockingBudgetMs)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    const long long elapsed = millisSince(started);
    const bool returnedInTime = setterReturned.load(std::memory_order_acquire);

    // Release the mutex either way, so a failing setter can finish and be
    // joined rather than stranding the thread.
    soloudTestUnlockInitDeinit();
    setter.join();

    EXPECT(returnedInTime,
           "setAudioDeviceIdleTimeout() was still blocked after %lldms while "
           "the lifecycle mutex was held (budget %lldms)",
           elapsed, kNonBlockingBudgetMs);

    // The published policy must survive to the next engine.
    if (!bringUpEngine())
    {
        std::printf("  ok: returned in %lldms (engine bring-up skipped)\n",
                    elapsed);
        return;
    }
    tearDownEngine();
    std::printf("  ok: returned in %lldms\n", elapsed);
}

/// An automatic start that exhausts rebuild/retry must be observable. Nothing
/// can return it to the caller -- play() completed long before -- so the engine
/// publishes it as an event.
void testAutomaticStartFailureIsReported()
{
    std::printf("automatic start failure is reported\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");

    stopAudioDevice(/*force*/ 1);
    gStartFailureEvents.store(0, std::memory_order_release);

    // Two forced failures: the initial start and the retry after the device has
    // been rebuilt. That is the whole automatic recovery path.
    soloud_test::failNextDeviceStarts(2);

    const unsigned int handle = playUnpaused(hash);
    EXPECT(handle != 0, "playback should start");

    for (int i = 0;
         i < 200 && gStartFailureEvents.load(std::memory_order_acquire) == 0;
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(gStartFailureEvents.load(std::memory_order_acquire) > 0,
           "a background start failure must be published as an event");
    EXPECT(soloud_test::pendingForcedDeviceStartFailures() == 0,
           "both the start and its retry should have been attempted");

    soloud_test::failNextDeviceStarts(0);
    stop(handle);
    tearDownEngine();
    std::printf("  ok: the failure reached the event bridge\n");
}

/// Clocked and scheduled playback must not run a backend device start on the
/// calling thread. Proven structurally rather than by timing: the barrier sits
/// inside performAudioDeviceStart(), so if a call parked there it never returns
/// while the barrier is armed.
void testClockedPlaybackDoesNotStartDeviceInline()
{
    std::printf("clocked/scheduled playback does not start the device inline\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");

    stopAudioDevice(/*force*/ 1);
    EXPECT(getAudioDeviceState() != audioDeviceStarted,
           "the device should be stopped before the clocked play");

    // Each of the three was changed independently, so each is asserted
    // independently rather than by resemblance to playClocked().
    struct ScheduledCall
    {
        const char *name;
        PlayerErrors (*invoke)(unsigned int hash, unsigned int *handle);
    };
    static const ScheduledCall calls[] = {
        {"playClocked",
         [](unsigned int h, unsigned int *out) {
             return playClocked(h, 0.0, 0, 1.0f, 0.0f, out);
         }},
        {"play3dClocked",
         [](unsigned int h, unsigned int *out) {
             return play3dClocked(h, 0.0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  1.0f, out);
         }},
        {"playScheduled",
         [](unsigned int h, unsigned int *out) {
             return playScheduled(h, 0.0, 0.0, 0, 1.0f, 0.0f, out);
         }},
    };

    for (const ScheduledCall &call : calls)
    {
        stopAudioDevice(/*force*/ 1);
        soloud_test::armBarrier(DeviceBarrier::performAudioDeviceStartEntered);

        std::atomic<bool> returned{false};
        unsigned int handle = 0;
        std::thread caller([&] {
            call.invoke(hash, &handle);
            returned.store(true, std::memory_order_release);
        });

        // If the call still started the device inline it would be parked on the
        // barrier right now and this would time out.
        const auto started = std::chrono::steady_clock::now();
        while (!returned.load(std::memory_order_acquire) &&
               millisSince(started) < kNonBlockingBudgetMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        EXPECT(returned.load(std::memory_order_acquire),
               "%s() performed a backend device start on the calling thread",
               call.name);

        soloud_test::releaseBarrier(
            DeviceBarrier::performAudioDeviceStartEntered);
        caller.join();

        EXPECT(handle != 0, "%s() should have created a voice", call.name);
        if (handle != 0)
            stop(handle);
    }

    tearDownEngine();
    std::printf("  ok: all three queued the device start\n");
}


/// Engine teardown must not start the audio device, even when the configured
/// idle policy is the indefinite keep-alive.
///
/// disposeAllSound() is a *runtime* operation: after stopping the device it
/// honours the policy and queues a start again, which is correct while the
/// engine lives. Running it as part of teardown had deinit() ask the scheduler
/// to start the device moments before joining that same scheduler -- a wholly
/// pointless ma_device_start() that teardown then waits out, on exactly the
/// backends where starting is slow.
///
/// Asserted by counting entries into performAudioDeviceStart() rather than by
/// racing the scheduler, so the result does not depend on whether the scheduler
/// happened to dequeue the request before Player::dispose() stopped it.
void testTeardownDoesNotRestartDeviceUnderKeepAlive()
{
    std::printf("teardown does not restart the device under keep-alive\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    // The indefinite keep-alive: this is the policy that makes teardown want to
    // restart the device.
    setAudioDeviceIdleTimeout(-1);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");
    const unsigned int handle = playUnpaused(hash);
    EXPECT(handle != 0, "playback should start");

    // Let any start the keep-alive policy legitimately wanted settle first, so
    // only teardown's own attempts are counted.
    for (int i = 0; i < 100 && getAudioDeviceState() != audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    soloud_test::resetBackendDeviceStartCount();

    // Park teardown at the top of Player::dispose(), i.e. after anything it
    // does *before* stopping the scheduler, and hold it there long enough for
    // the scheduler to act on whatever that queued. Without this the assertion
    // is a race: teardown normally reaches dispose() and stops the scheduler
    // before it can perform the start, so the bug hides.
    soloud_test::armBarrier(DeviceBarrier::playerDisposeEntered);
    std::thread teardown([] { dispose(); });
    soloud_test::waitBarrierReached(DeviceBarrier::playerDisposeEntered);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    soloud_test::releaseBarrier(DeviceBarrier::playerDisposeEntered);
    teardown.join();

    EXPECT(soloud_test::backendDeviceStartCount() == 0,
           "teardown performed %d backend device start(s)",
           soloud_test::backendDeviceStartCount());
    EXPECT(isInited() == 0, "the engine should be torn down");
    std::printf("  ok: no device start during teardown\n");
}

/// The same guarantee for the FlutterEngine-owned teardown path, which reaches
/// disposeLocked() through requestEngineTeardownForEngine() rather than
/// through Dart's deinit().
void testEngineOwnedTeardownDoesNotRestartDevice()
{
    std::printf("FlutterEngine teardown does not restart the device\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(-1);

    const unsigned int hash = loadTestWaveform();
    EXPECT(hash != 0, "the test waveform should load");
    const unsigned int handle = playUnpaused(hash);
    EXPECT(handle != 0, "playback should start");
    for (int i = 0; i < 100 && getAudioDeviceState() != audioDeviceStarted; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    soloud_test::resetBackendDeviceStartCount();
    soloud_test::armBarrier(DeviceBarrier::playerDisposeEntered);

    // isInited() goes false at the *start* of teardown, so waiting on it would
    // let this test read the counter while the detached worker is still inside
    // Player::dispose() and the backend shutdown. Synchronize on the worker's
    // own completion instead.
    const int teardownsBefore = soloudTestEngineTeardownCompletedCount();

    EXPECT(requestEngineTeardownForEngine(kEngineId),
           "the owning engine should be allowed to tear down");

    soloud_test::waitBarrierReached(DeviceBarrier::playerDisposeEntered);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    soloud_test::releaseBarrier(DeviceBarrier::playerDisposeEntered);

    for (int i = 0; i < 500 &&
                    soloudTestEngineTeardownCompletedCount() == teardownsBefore;
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT(soloudTestEngineTeardownCompletedCount() == teardownsBefore + 1,
           "the detached teardown worker did not complete");

    EXPECT(isInited() == 0, "the engine should be torn down");
    EXPECT(soloud_test::backendDeviceStartCount() == 0,
           "engine teardown performed %d backend device start(s)",
           soloud_test::backendDeviceStartCount());
    tearDownEngine();
    std::printf("  ok: no device start during engine teardown\n");
}

/// Repeated timeout updates while the lifecycle mutex is held must collapse
/// onto one deferred worker, and the last published value must win.
void testIdleTimeoutApplyIsCoalesced()
{
    std::printf("idle timeout apply is coalesced\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    soloudTestResetIdleTimeoutWorkerPeak();
    soloudTestLockInitDeinit();

    constexpr int kUpdates = 32;
    long long slowest = 0;
    for (int i = 1; i <= kUpdates; ++i)
    {
        const auto started = std::chrono::steady_clock::now();
        setAudioDeviceIdleTimeout(1000 + i);
        const long long elapsed = millisSince(started);
        if (elapsed > slowest)
            slowest = elapsed;
    }

    EXPECT(slowest < kNonBlockingBudgetMs,
           "the slowest of %d setter calls took %lldms while the lifecycle "
           "mutex was held (budget %lldms)",
           kUpdates, slowest, kNonBlockingBudgetMs);

    soloudTestUnlockInitDeinit();

    const int64_t expected = 1000 + kUpdates;
    for (int i = 0; i < 200 && soloudTestAppliedIdleTimeoutMs() != expected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(soloudTestAppliedIdleTimeoutMs() == expected,
           "the last published policy should win: expected %lld, applied %lld",
           (long long)expected, (long long)soloudTestAppliedIdleTimeoutMs());
    EXPECT(soloudTestIdleTimeoutWorkerPeak() <= 1,
           "%d deferred workers existed at once; %d setter calls must collapse "
           "onto one",
           soloudTestIdleTimeoutWorkerPeak(), kUpdates);

    tearDownEngine();
    std::printf("  ok: %d updates, one worker, last value applied\n", kUpdates);
}

/// A publication landing in the worker's own apply/idle window must not be
/// lost. This is the window a "clear the queued flag after applying" scheme
/// drops a write in, so it is forced with a barrier rather than raced.
void testIdleTimeoutPublicationRacingWorkerExitIsApplied()
{
    std::printf("timeout published as the worker exits is still applied\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    // Get a worker running and parked just after it applied the first value.
    soloud_test::armBarrier(DeviceBarrier::idleTimeoutWorkerApplied);
    soloudTestLockInitDeinit();
    setAudioDeviceIdleTimeout(4321);
    soloudTestUnlockInitDeinit();

    soloud_test::waitBarrierReached(DeviceBarrier::idleTimeoutWorkerApplied);

    // The worker has applied 4321, released the lifecycle mutex, and is about
    // to decide whether to go idle. Publish a newer value into exactly that
    // window -- with the lifecycle mutex held, so the setter's opportunistic
    // try_lock fails and it must rely on the worker noticing. Without that the
    // setter simply applies the value itself and the window is never tested.
    soloudTestLockInitDeinit();
    setAudioDeviceIdleTimeout(8765);
    soloudTestUnlockInitDeinit();

    soloud_test::releaseBarrier(DeviceBarrier::idleTimeoutWorkerApplied);

    for (int i = 0; i < 200 && soloudTestAppliedIdleTimeoutMs() != 8765; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT(soloudTestAppliedIdleTimeoutMs() == 8765,
           "a policy published in the worker's exit window was lost: applied "
           "%lld",
           (long long)soloudTestAppliedIdleTimeoutMs());

    tearDownEngine();
    std::printf("  ok: the racing publication was applied\n");
}


/// Retiring the engine-level state callback must never race its dispatch, and
/// must never dispatch through a null pointer.
///
/// This targets SoLoud::_stateChangedCallback specifically -- the pointer the
/// miniaudio notification threads read and that teardown clears -- not the
/// Dart-side bridge, which the callback-generation gate already serializes.
/// The read side goes through the real backend notification path; the write
/// side mirrors what Player::dispose() does.
///
/// A bare `if (cb != nullptr) cb(...)` is two loads: a clear landing between
/// them calls null. Run under ThreadSanitizer -- an unsynchronized pointer is a
/// data race whether or not it happens to crash on a given run.
void testStateCallbackRetirementRacesDispatch()
{
    std::printf("engine state callback retirement vs dispatch\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    gStateEvents.store(0, std::memory_order_release);

    constexpr int kRounds = 4000;
    std::atomic<int> dispatched{0};
    std::atomic<bool> go{false};

    // The read side: the backend notification path an OS interruption uses.
    std::thread dispatcher([&] {
        while (!go.load(std::memory_order_acquire))
        {
        }
        bool began = true;
        for (int i = 0; i < kRounds; ++i)
        {
            SoLoud::miniaudio_debugTriggerAudioInterruption(began);
            began = !began;
            dispatched.fetch_add(1, std::memory_order_acq_rel);
        }
    });

    // The write side: retire and republish, as teardown and init do. Both
    // threads run a fixed number of rounds and are released together, so the
    // window actually overlaps rather than one finishing first.
    std::thread writer([&] {
        while (!go.load(std::memory_order_acquire))
        {
        }
        for (int i = 0; i < kRounds; ++i)
        {
            soloudTestSetEngineStateCallback(0);
            soloudTestSetEngineStateCallback(1);
        }
    });

    go.store(true, std::memory_order_release);
    writer.join();
    dispatcher.join();

    EXPECT(dispatched.load(std::memory_order_acquire) > 0,
           "the dispatcher should have run");
    // Reaching here without a crash, and without TSan reporting a race on the
    // callback pointer, is the assertion. How many dispatches land while a
    // registration is live is legitimately nondeterministic.
    std::printf("  ok: %d dispatches raced 2000 retirements\n",
                dispatched.load(std::memory_order_acquire));

    soloudTestSetEngineStateCallback(1);
    // The interruption latch may be left set by the last toggle.
    SoLoud::miniaudio_debugTriggerAudioInterruption(false);
    tearDownEngine();
}


/// Feeds the mixer capture the way the real-time audio callback does.
void feedCapture(int frames)
{
    std::vector<float> block(static_cast<size_t>(frames) * 2, 0.25f);
    MixerOutput::instance().onAudioData(block.data(),
                                        static_cast<unsigned int>(frames));
}

/// stop() must not destroy capture state while an audio callback is inside it.
///
/// onAudioData() used to test `m_running` once and then keep using
/// non-atomic capture state, so a callback preempted right after that check
/// resumed inside buffers stop() had already released.
void testMixerStopWaitsForInFlightPcmCallback()
{
    std::printf("mixer PCM stop vs in-flight audio callback\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    EXPECT(startMixerCapture(MIXER_OUTPUT_PCM_F32LE, 44100, 2, 65536, 4096,
                             -1) == PlayerErrors::noError,
           "the PCM capture should start");

    soloud_test::armBarrier(DeviceBarrier::mixerCaptureCallbackAdmitted);

    std::atomic<bool> callbackReturned{false};
    std::thread audio([&] {
        feedCapture(256);
        callbackReturned.store(true, std::memory_order_release);
    });

    soloud_test::waitBarrierReached(DeviceBarrier::mixerCaptureCallbackAdmitted);

    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        stopMixerCapture();
        stopReturned.store(true, std::memory_order_release);
    });

    // The capture is still being written to. stop() must not get as far as
    // releasing the buffers, the encoder or the queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT(!stopReturned.load(std::memory_order_acquire),
           "stop() tore the capture down while an audio callback was inside it");

    soloud_test::releaseBarrier(DeviceBarrier::mixerCaptureCallbackAdmitted);
    audio.join();
    stopper.join();

    EXPECT(callbackReturned.load(std::memory_order_acquire),
           "the audio callback should have completed");
    EXPECT(isMixerCaptureRunning() == 0, "the capture should be stopped");

    tearDownEngine();
    std::printf("  ok: stop waited for the callback to leave\n");
}

/// The same guarantee for a compressed capture, which is the path that pushes
/// into m_pcmQueue -- the object stop() resets.
void testMixerStopWaitsForInFlightCompressedCallback()
{
    std::printf("mixer compressed stop vs in-flight audio callback\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    // WAV needs no Xiph libraries, so this path is available in every build.
    const PlayerErrors started =
        startMixerCapture(MIXER_OUTPUT_WAV, 44100, 2, 262144, 8192, -1);
    if (started != PlayerErrors::noError)
    {
        std::printf("  skipped: compressed capture unavailable (%d)\n",
                    (int)started);
        tearDownEngine();
        return;
    }

    soloud_test::armBarrier(DeviceBarrier::mixerCaptureCallbackAdmitted);

    std::thread audio([&] { feedCapture(256); });
    soloud_test::waitBarrierReached(DeviceBarrier::mixerCaptureCallbackAdmitted);

    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        stopMixerCapture();
        stopReturned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT(!stopReturned.load(std::memory_order_acquire),
           "stop() released the encoder/queue while a callback was pushing "
           "into it");

    soloud_test::releaseBarrier(DeviceBarrier::mixerCaptureCallbackAdmitted);
    audio.join();
    stopper.join();

    EXPECT(isMixerCaptureRunning() == 0, "the capture should be stopped");
    tearDownEngine();
    std::printf("  ok: stop waited for the queue push to finish\n");
}

/// A callback admitted to capture A must never write into capture B.
///
/// The session id is monotonic precisely so an "inactive -> active"
/// transition cannot be mistaken for the session the callback first observed.
void testMixerStopThenStartDoesNotAdmitOldCallback()
{
    std::printf("mixer stop then immediate start vs a parked callback\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    EXPECT(startMixerCapture(MIXER_OUTPUT_PCM_F32LE, 44100, 2, 65536, 4096,
                             -1) == PlayerErrors::noError,
           "capture A should start");

    soloud_test::armBarrier(DeviceBarrier::mixerCaptureCallbackAdmitted);
    std::thread audio([&] { feedCapture(256); });
    soloud_test::waitBarrierReached(DeviceBarrier::mixerCaptureCallbackAdmitted);

    // Stop A and start B while the old callback is parked. stop() cannot
    // complete until it leaves, so release it first and only then swap --
    // which is itself the guarantee: the swap is serialized behind the
    // callback rather than racing it.
    std::atomic<bool> swapDone{false};
    std::thread swapper([&] {
        stopMixerCapture();
        startMixerCapture(MIXER_OUTPUT_PCM_F32LE, 22050, 2, 65536, 4096, -1);
        swapDone.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT(!swapDone.load(std::memory_order_acquire),
           "the capture was replaced while a callback was still inside the "
           "old one");

    soloud_test::releaseBarrier(DeviceBarrier::mixerCaptureCallbackAdmitted);
    audio.join();
    swapper.join();

    EXPECT(isMixerCaptureRunning() == 1, "capture B should be running");
    stopMixerCapture();
    tearDownEngine();
    std::printf("  ok: the swap was serialized behind the old callback\n");
}

/// Teardown must not free the engine while a device notification is inside it.
///
/// Storing nullptr into the backend's engine pointer only stops notifications
/// that have not started; one that already loaded it goes on to dereference a
/// destroyed Soloud -- and, on the interruption branch, a freed Player*.
void testTeardownWaitsForInFlightNotification(bool interruption)
{
    std::printf("teardown vs in-flight %s notification\n",
                interruption ? "interruption" : "state-change");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    soloud_test::armBarrier(DeviceBarrier::deviceNotificationAdmitted);

    std::atomic<bool> notificationReturned{false};
    std::thread notifier([&] {
        // Both go through the real backend notification path. The
        // interruption branch is the one carrying the raw Player* context.
        SoLoud::miniaudio_debugTriggerAudioInterruption(interruption);
        notificationReturned.store(true, std::memory_order_release);
    });

    soloud_test::waitBarrierReached(DeviceBarrier::deviceNotificationAdmitted);

    std::atomic<bool> teardownReturned{false};
    std::thread teardown([&] {
        dispose();
        teardownReturned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT(!teardownReturned.load(std::memory_order_acquire),
           "teardown ran to completion while a notification was inside the "
           "engine");

    soloud_test::releaseBarrier(DeviceBarrier::deviceNotificationAdmitted);
    notifier.join();
    teardown.join();

    EXPECT(notificationReturned.load(std::memory_order_acquire),
           "the notification should have completed");
    EXPECT(isInited() == 0, "the engine should be torn down");
    std::printf("  ok: teardown waited the notification out\n");
}

/// A notification from a retired session must not act on the engine that
/// replaced it.
void testRetiredNotificationCannotReachReplacementEngine()
{
    std::printf("retired notification vs replacement engine\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }

    dispose();
    EXPECT(isInited() == 0, "the first engine should be torn down");

    // Delivered while nothing is published: admission is closed, so this must
    // be a no-op rather than a dispatch into whatever comes next.
    gStateEvents.store(0, std::memory_order_release);
    SoLoud::miniaudio_debugTriggerAudioInterruption(true);
    SoLoud::miniaudio_debugTriggerAudioInterruption(false);
    EXPECT(gStateEvents.load(std::memory_order_acquire) == 0,
           "a notification with no engine published dispatched %d event(s)",
           gStateEvents.load(std::memory_order_acquire));

    if (!bringUpEngine())
    {
        std::printf("  skipped: replacement engine unavailable\n");
        return;
    }
    gStateEvents.store(0, std::memory_order_release);

    // The replacement engine is live, so its own notifications work normally.
    SoLoud::miniaudio_debugTriggerAudioInterruption(true);
    SoLoud::miniaudio_debugTriggerAudioInterruption(false);
    EXPECT(gStateEvents.load(std::memory_order_acquire) > 0,
           "the replacement engine should receive its own notifications");

    tearDownEngine();
    std::printf("  ok: retired session dispatched nothing\n");
}


/// A device replacement must not become current while a notification admitted
/// to the previous device is still running.
///
/// Retiring only around engine teardown is not enough: changeDevice() destroys
/// and rebuilds the same global ma_device while the engine object stays alive,
/// so an interruption admitted against device A could otherwise resume after B
/// had taken its place and stop B because of an event belonging to a device
/// that no longer exists.
void testDeviceSwapWaitsForNotificationAdmittedToOldDevice()
{
    std::printf("device swap vs notification admitted to the old device\n");
    if (!bringUpEngine())
    {
        std::printf("  skipped: no usable output device\n");
        return;
    }
    setAudioDeviceIdleTimeout(kQuietIdleTimeoutMs);

    soloud_test::armBarrier(DeviceBarrier::deviceNotificationAdmitted);

    std::atomic<bool> notificationReturned{false};
    std::thread notifier([&] {
        // The interruption branch is the one that reaches back into Player and
        // can stop a device.
        SoLoud::miniaudio_debugTriggerAudioInterruption(true);
        notificationReturned.store(true, std::memory_order_release);
    });

    soloud_test::waitBarrierReached(DeviceBarrier::deviceNotificationAdmitted);

    std::atomic<bool> swapReturned{false};
    std::thread swapper([&] {
        changeDevice(-1);
        swapReturned.store(true, std::memory_order_release);
    });

    // The replacement must not become current while the old device's
    // notification is still inside the engine.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT(!swapReturned.load(std::memory_order_acquire),
           "the device was replaced while a notification admitted to the "
           "previous device was still running");

    soloud_test::releaseBarrier(DeviceBarrier::deviceNotificationAdmitted);
    notifier.join();
    swapper.join();

    EXPECT(notificationReturned.load(std::memory_order_acquire),
           "the notification should have completed");

    // Clear the latch the interruption set, so the engine is left usable.
    SoLoud::miniaudio_debugTriggerAudioInterruption(false);
    tearDownEngine();
    std::printf("  ok: the swap waited out the old device's notification\n");
}

} // namespace

int main()
{
    testChangeDeviceCannotOverlapTeardown();
    testStaleChangeDeviceAfterTeardown();
    testConditionalStopDoesNotSwallowNewPlayback();
    testConditionalStopDoesNotSwallowUnpause();
    testChangeDeviceDoesNotSwallowPlayback();
    testExplicitStartYieldsToGenuineInterruption();
    testIdleTimeoutSetterDoesNotBlockOnInit();
    testAutomaticStartFailureIsReported();
    testClockedPlaybackDoesNotStartDeviceInline();
    testTeardownDoesNotRestartDeviceUnderKeepAlive();
    testEngineOwnedTeardownDoesNotRestartDevice();
    testIdleTimeoutApplyIsCoalesced();
    testIdleTimeoutPublicationRacingWorkerExitIsApplied();
    testStateCallbackRetirementRacesDispatch();
    testMixerStopWaitsForInFlightPcmCallback();
    testMixerStopWaitsForInFlightCompressedCallback();
    testMixerStopThenStartDoesNotAdmitOldCallback();
    testTeardownWaitsForInFlightNotification(/*interruption*/ false);
    testTeardownWaitsForInFlightNotification(/*interruption*/ true);
    testRetiredNotificationCannotReachReplacementEngine();
    testDeviceSwapWaitsForNotificationAdmittedToOldDevice();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

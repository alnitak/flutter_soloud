// Standalone native regression tests for FlutterEngine lifecycle ownership.
//
// The native engine is process-global: one Player, one output device, one
// lifecycle scheduler, and Dart callable pointers held both globally and inside
// every buffer-backed sound. The Dart isolate that drives it belongs to a
// single FlutterEngine, which can go away while the process keeps running -- a
// cached engine behind audio_service, an add-to-app host destroying an engine,
// or a hot restart swapping the isolate underneath a live engine. The Android
// plugin bridges those transitions into the two entry points exercised here:
//
//   clearDartCallbackRegistrationsForEngine()  (hot restart, and detach)
//   requestEngineTeardownForEngine()           (engine destroy)
//
// What these tests pin down:
//
//   * retirement makes *every* callable inert synchronously -- the four global
//     ones and the per-BufferStream/PullBufferStream ones, which no retirement
//     can afford to walk to. The per-source ones are driven through the real
//     dispatchers the audio thread uses, not a stand-in;
//   * a replacement engine's registration does not resurrect a retired
//     engine's sources;
//   * callback ownership and lifecycle ownership are separate, so an engine
//     that owns the callables but not the claim retires its own callables and
//     still cannot dispose the engine that replaced it;
//   * a teardown queued by a destroyed engine cannot dispose or disturb the
//     engine that claimed after it;
//   * an engine destroyed while its initialization is parked inside the device
//     open still ends up with nothing left running;
//   * both hooks return promptly while init_deinit_mutex is held by an
//     unrelated operation -- they run on Android's platform thread, where
//     waiting for a device operation is an ANR;
//   * duplicate destroy/detach notifications tear down exactly once.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_engine_lifecycle_test.sh

#include "audiobuffer/metadata_ffi.h"
#include "enums.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

extern "C"
{
    void prepareEngineInit(int64_t owner_engine_id);
    enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate,
                                 unsigned int bufferSize, unsigned int channels,
                                 unsigned int lowLatency);
    void dispose();
    int isInited();
    void setDartEventCallback(void (*voice_ended)(unsigned int *),
                              void (*file_loaded)(enum PlayerErrors *, char *,
                                                  unsigned int *, uint64_t *),
                              void (*state_changed)(enum PlayerStateEvents *),
                              int64_t owner_engine_id);
    bool setMixerOutputCallbackForEngine(void (*callback)(unsigned char *,
                                                          uint64_t),
                                         int64_t owner_engine_id);
    enum PlayerErrors startMixerCapture(int format, int sampleRate, int channels,
                                        int bufferSizeBytes,
                                        int notificationThresholdBytes,
                                        int chunkPCMFrames);
    void stopMixerCapture();
    int isMixerCaptureRunning();
    bool clearDartCallbackRegistrationsForEngine(int64_t engine_id);
    bool requestEngineTeardownForEngine(int64_t engine_id);
    void requestEngineShutdown();
    uint64_t currentEngineShutdownEpoch();
    bool prepareEngineInitForRequest(int64_t owner_engine_id,
                                     uint64_t shutdown_epoch);
    enum PlayerErrors setPullBufferStream(
        unsigned int *hash, unsigned int bufferSizeBytes,
        double bufferTriggerPosition, unsigned int sampleRate,
        unsigned int channels, int format, uint64_t audioSizeBytes,
        dartOnBufferingCallback_t onBufferingCallback,
        dartOnMetadataCallback_t onMetadataCallback,
        dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
        dartOnAudioDurationCallback_t onAudioDurationCallback);

    // Test-only hooks (SOLOUD_LIFECYCLE_TEST_HOOKS).
    void soloudTestLockInitDeinit();
    void soloudTestUnlockInitDeinit();
    void soloudTestInvokeStateChanged(unsigned int state);
    bool soloudTestInvokeStreamCallbacks(unsigned int hash);
    int soloudTestPlayerIsInited();
    int soloudTestCallbacksAreLive();
    void soloudTestInvokeMixerOutput();
    void soloudTestArmInitBarrier();
    void soloudTestWaitInitBarrierReached();
    void soloudTestReleaseInitBarrier();
}

namespace
{

constexpr int64_t kEngineA = 1001;
constexpr int64_t kEngineB = 1002;
constexpr int64_t kNoEngineId = -1;

// A lifecycle hook takes one uncontended lock and does a handful of stores. The
// number this excludes is the one that matters: the seconds a device stop, a
// scheduler join or a decode can take while init_deinit_mutex is held.
constexpr long long kPlatformThreadBudgetMs = 100;

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

std::atomic<int> gVoiceEndedCalls{0};
std::atomic<int> gFileLoadedCalls{0};
std::atomic<int> gStateChangedCalls{0};
std::atomic<int> gStreamCallbackCalls{0};
std::atomic<int> gMixerOutputCalls{0};

// Stand-ins for the Dart trampolines. Native code owns the pointers it hands
// over, exactly as the real callables do.
void onVoiceEnded(unsigned int *handle)
{
    std::free(handle);
    ++gVoiceEndedCalls;
}

void onFileLoaded(enum PlayerErrors *error, char *name, unsigned int *hash,
                  uint64_t *counter)
{
    std::free(error);
    std::free(name);
    std::free(hash);
    std::free(counter);
    ++gFileLoadedCalls;
}

void onStateChanged(enum PlayerStateEvents *state)
{
    std::free(state);
    ++gStateChangedCalls;
}

void onMixerOutput(unsigned char *, uint64_t) { ++gMixerOutputCalls; }

void onBuffering(bool, unsigned int, double) { ++gStreamCallbackCalls; }
void onMetadata(struct AudioMetadataFFI) { ++gStreamCallbackCalls; }
void onMoreDataIsNeeded(uint64_t) { ++gStreamCallbackCalls; }
void onAudioDuration(double) { ++gStreamCallbackCalls; }

void registerCallbacksFor(int64_t engineId)
{
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, engineId);
}

/// How many times one dispatch through the global state-changed bridge reached
/// a callable: 1 while it is live, 0 once it has been retired.
int stateChangedDelta()
{
    const int before = gStateChangedCalls.load();
    soloudTestInvokeStateChanged(0);
    return gStateChangedCalls.load() - before;
}

/// Whether one dispatch through the function MixerOutput holds reached the
/// published mixer callable.
int mixerOutputDelta()
{
    const int before = gMixerOutputCalls.load();
    soloudTestInvokeMixerOutput();
    return gMixerOutputCalls.load() - before;
}

/// How many of a real PullBufferStream's four callables one dispatch reached.
/// Goes through the same dispatchers the audio thread calls, and is measured as
/// a delta because creating the stream already fires some of them.
int streamCallDelta(unsigned int hash)
{
    const int before = gStreamCallbackCalls.load();
    if (!soloudTestInvokeStreamCallbacks(hash))
        return -1;
    return gStreamCallbackCalls.load() - before;
}

/// Fails the run rather than letting it hang forever.
///
/// The failure mode these lifecycle tests guard against is not always a wrong
/// answer: unserialized capture stops join the same thread twice and wedge, and
/// a wedged suite in CI is worse than a failing one. Nothing here should take
/// anywhere near this long.
void startWatchdog(int limitSeconds)
{
    std::thread(
        [limitSeconds]
        {
            std::this_thread::sleep_for(std::chrono::seconds(limitSeconds));
            std::fprintf(stderr,
                         "\n  FAIL: watchdog fired after %ds -- the suite is "
                         "wedged, which is itself the bug.\n",
                         limitSeconds);
            std::fflush(stderr);
            std::fflush(stdout);
            std::_Exit(1);
        })
        .detach();
}

template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 5000)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

long long millisSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

/// Every test starts from a process with no engine and no lifecycle claim.
void resetGlobalState()
{
    dispose();
    gVoiceEndedCalls = 0;
    gFileLoadedCalls = 0;
    gStateChangedCalls = 0;
    gStreamCallbackCalls = 0;
    gMixerOutputCalls = 0;
}

bool initEngineAs(int64_t engineId)
{
    prepareEngineInit(engineId);
    return initEngine(-1, 44100, 2048, 2, 0) == noError;
}

/// Create a PullBufferStream holding all four Dart callables. Returns 0 on
/// failure.
unsigned int createPullStream()
{
    unsigned int hash = 0;
    const PlayerErrors error = setPullBufferStream(
        &hash, 1024 * 64, 0.5, 44100, 2, static_cast<int>(BufferType::PCM_S16LE),
        1024 * 1024, onBuffering, onMetadata, onMoreDataIsNeeded,
        onAudioDuration);
    return error == noError ? hash : 0;
}

// ---------------------------------------------------------------------------

/// Hot restart keeps the same FlutterEngine -- same engine id, same lifecycle
/// claim -- and replaces only the isolate. Every callable must go inert
/// immediately, including the ones living inside sounds, which is the part a
/// retirement cannot afford to walk to. The engine stays claimed, because the
/// new isolate's init() is what disposes the stale engine.
void testHotRestartRetiresEveryCallable()
{
    std::printf("hot restart retires every callable, sources included\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);

    const unsigned int hash = createPullStream();
    EXPECT(hash != 0, "a pull buffer stream should be created");

    EXPECT(stateChangedDelta() == 1,
           "a registered global callable should be invoked");
    EXPECT(streamCallDelta(hash) == 4,
           "all four registered stream callables should be invoked");

    EXPECT(clearDartCallbackRegistrationsForEngine(kEngineA),
           "the owning engine should be allowed to retire its callables");

    EXPECT(soloudTestCallbacksAreLive() == 0,
           "nothing may be live once the owner has retired");
    EXPECT(stateChangedDelta() == 0,
           "a retired global callable must never be invoked again");
    EXPECT(streamCallDelta(hash) == 0,
           "retired stream callables must never be invoked again -- these are "
           "the ones the audio thread calls, and the isolate is gone");

    // The claim survived the restart, so a later destroy of the same
    // FlutterEngine is still accepted.
    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "hot restart must not release the lifecycle claim");

    resetGlobalState();
}

/// A replacement engine publishes its own registration while a retired engine's
/// sources are still in the Player. Those sources must stay dead: their
/// callables belong to an isolate that no longer exists.
void testReplacementDoesNotResurrectRetiredSources()
{
    std::printf("a new registration does not resurrect retired sources\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);

    const unsigned int hash = createPullStream();
    EXPECT(hash != 0, "a pull buffer stream should be created");
    EXPECT(streamCallDelta(hash) == 4, "A's stream should be live");

    EXPECT(clearDartCallbackRegistrationsForEngine(kEngineA),
           "A should retire its own callables");

    // B claims and publishes. A's sound object is still in the Player, still
    // holding A's now-dangling callable pointers.
    prepareEngineInit(kEngineB);
    registerCallbacksFor(kEngineB);

    EXPECT(soloudTestCallbacksAreLive() == 1,
           "B's own registration should be live");
    EXPECT(streamCallDelta(hash) == 0,
           "A's stream callables must stay inert under B's registration");
    EXPECT(stateChangedDelta() == 1, "B's global callables should work");

    resetGlobalState();
}

/// A detaching engine must never retire somebody else's callables.
void testCallbackRetirementIsScopedToTheOwner()
{
    std::printf("callback retirement is scoped to the owning engine\n");
    resetGlobalState();

    prepareEngineInit(kEngineA);
    registerCallbacksFor(kEngineA);

    EXPECT(!clearDartCallbackRegistrationsForEngine(kEngineB),
           "a non-owner must not retire the current registration");
    EXPECT(stateChangedDelta() == 1,
           "the owner's callables must still be live");

    EXPECT(!clearDartCallbackRegistrationsForEngine(kNoEngineId),
           "the no-engine sentinel must never match an owner");

    resetGlobalState();
}

/// The regression behind the "retire callables even when teardown is refused"
/// rule. Engine A's initialization worker can win init_deinit_mutex after B has
/// already claimed, leaving A owning the callables and B owning the engine.
/// Destroying A must retire A's callables and leave B's engine alone.
void testCallbackOwnerDiffersFromLifecycleOwner()
{
    std::printf("callback owner may differ from lifecycle owner\n");
    resetGlobalState();

    prepareEngineInit(kEngineA);
    registerCallbacksFor(kEngineA);
    EXPECT(stateChangedDelta() == 1, "A's callables should start out live");

    // B claims the native engine while A's callables are still published.
    prepareEngineInit(kEngineB);

    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "A must not tear down the engine B now owns");
    EXPECT(stateChangedDelta() == 0,
           "A's callables must be retired even though its teardown was refused");
    EXPECT(requestEngineTeardownForEngine(kEngineB),
           "B's lifecycle claim must be intact");

    resetGlobalState();
}

/// Nothing is claimed, so there is nothing for a destroyed engine to tear down.
void testTeardownRefusedWithoutAClaim()
{
    std::printf("teardown is refused when nothing is claimed\n");
    resetGlobalState();

    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "an unclaimed engine has nothing to tear down");
    EXPECT(!requestEngineTeardownForEngine(kNoEngineId),
           "the no-engine sentinel must never be accepted");

    // An ordinary Dart deinit releases the claim, so a detach arriving
    // afterwards is a no-op rather than a second teardown.
    prepareEngineInit(kEngineA);
    dispose();
    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "dispose() must release the lifecycle claim");

    resetGlobalState();
}

/// The mixer callable is published on its own and re-published whenever capture
/// starts, so it needs its own ownership check and its own generation. It is
/// also the one callable a *worker* isolate publishes -- mixer capture is
/// documented as runnable from one via `SoLoudIsolate` -- and a worker cannot
/// read `PlatformDispatcher.engineId`, so it arrives with the no-engine
/// sentinel and no engine to check against.
void testMixerCallbackPublicationIsOwnerScoped()
{
    std::printf("mixer callback publication is owner scoped\n");
    resetGlobalState();

    prepareEngineInit(kEngineA);
    registerCallbacksFor(kEngineA);

    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kEngineA),
           "the owner should be allowed to publish the mixer callable");
    EXPECT(mixerOutputDelta() == 1, "the published callable should be invoked");

    EXPECT(!setMixerOutputCallbackForEngine(onMixerOutput, kEngineB),
           "a non-owner must not publish over the live registration");

    // A worker isolate: it cannot name its engine, but the registration it
    // belongs to is live, so it joins it.
    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kNoEngineId),
           "a worker isolate should join the live registration");
    EXPECT(mixerOutputDelta() == 1,
           "a worker's callable should be invoked while its engine is live");

    EXPECT(clearDartCallbackRegistrationsForEngine(kEngineA),
           "A should retire its own callables");
    EXPECT(mixerOutputDelta() == 0,
           "the mixer callable must be inert once its registration is retired");
    EXPECT(!setMixerOutputCallbackForEngine(onMixerOutput, kEngineA),
           "publication must be refused after the registration is retired");

    resetGlobalState();
}

/// The regression this ownership model exists for, on the one callable a worker
/// isolate publishes. `onEngineWillDestroy()` fires while the engine is still
/// valid, so a capture isolate can still be running native calls across the
/// retirement boundary -- and it publishes with the no-engine sentinel, which
/// used to be an unconditional bypass. A callable it arms there must not come
/// back to life when a replacement engine claims the next generation.
void testStaleWorkerMixerCallbackCannotBeRevived()
{
    std::printf("a retired worker's mixer callable is never revived\n");
    resetGlobalState();

    // Engine A is live and a worker isolate publishes its capture callable.
    prepareEngineInit(kEngineA);
    registerCallbacksFor(kEngineA);
    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kNoEngineId),
           "the worker should join A's live registration");
    EXPECT(mixerOutputDelta() == 1, "A's worker callable should be live");

    // A's FlutterEngine is being destroyed. The worker is still running.
    EXPECT(clearDartCallbackRegistrationsForEngine(kEngineA),
           "A's registration should be retired");

    // The worker races the destruction and re-arms its callable.
    EXPECT(!setMixerOutputCallbackForEngine(onMixerOutput, kNoEngineId),
           "a worker must not publish into a retired registration");
    EXPECT(mixerOutputDelta() == 0,
           "nothing should be reachable while no registration is live");

    // A replacement engine claims the next generation. The stale worker
    // callable must not inherit it.
    prepareEngineInit(kEngineB);
    registerCallbacksFor(kEngineB);
    EXPECT(mixerOutputDelta() == 0,
           "a stale worker callable must not be revived by B's generation");

    // B's own mixer callable works, so this is a targeted refusal and not a
    // wedged mixer path.
    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kEngineB),
           "B should be able to publish its own mixer callable");
    EXPECT(mixerOutputDelta() == 1, "B's callable should be invoked");

    resetGlobalState();
}

/// The mixer notification thread invokes the callback MixerOutput holds while
/// two lifecycle paths replace it: engine teardown -- nothing stops a
/// FlutterEngine being destroyed with a capture still running, because there is
/// no Dart left to stop it first -- and a capture starting on another isolate,
/// which republishes independently. Both write what that thread is reading, so
/// the pointer has to be safe to publish concurrently, not merely ordered
/// around. Run under ThreadSanitizer this is the scenario that proves it.
void testTeardownDuringActiveMixerCapture()
{
    std::printf("teardown races an active mixer capture safely\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);
    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kEngineA),
           "the owner should publish its mixer callable");

    // Chunk mode on purpose: it advances the read offset itself, so the
    // notification thread keeps dispatching. Threshold mode latches after one
    // notification until a consumer advances the buffer, which would give the
    // reader a single read to race against.
    EXPECT(startMixerCapture(MIXER_OUTPUT_PCM_S16LE, 44100, 2,
                             /*bufferSizeBytes=*/64 * 1024,
                             /*notificationThresholdBytes=*/256,
                             /*chunkPCMFrames=*/2048) == noError,
           "the capture should start");
    EXPECT(isMixerCaptureRunning() == 1, "the capture should be running");

    // Wait for the notification thread to actually invoke the callback, so the
    // writes below land while it is reading rather than before it starts.
    const bool dispatched = waitFor([] { return gMixerOutputCalls.load() > 0; },
                                    3000);

    // A worker isolate republishing its capture callable, in a tight loop, for
    // long enough that writes interleave with many reads. A burst that happens
    // to land between two reads would prove nothing.
    const int dispatchesBefore = gMixerOutputCalls.load();
    const auto raceDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < raceDeadline &&
           gMixerOutputCalls.load() - dispatchesBefore < 100)
    {
        setMixerOutputCallbackForEngine(onMixerOutput, kNoEngineId);
    }
    const int dispatchesDuringRace = gMixerOutputCalls.load() - dispatchesBefore;

    // The FlutterEngine is destroyed with the capture still running.
    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "the owning engine's teardown should be accepted");
    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "the native engine should be disposed");
    EXPECT(isMixerCaptureRunning() == 0,
           "teardown should stop the capture it inherited");
    EXPECT(mixerOutputDelta() == 0,
           "the mixer callable must be inert after teardown");

    // Say what was actually exercised. A green run with no overlapping
    // dispatches would prove nothing about the concurrency, and this test only
    // earns its name under ThreadSanitizer.
    std::printf("  (%d dispatches raced the republish loop)\n",
                dispatchesDuringRace);
    EXPECT(dispatched && dispatchesDuringRace > 0,
           "the notification thread must dispatch while the callback is being "
           "republished, or this scenario tests nothing");

    resetGlobalState();
}

/// A capture must not be able to start once teardown has been decided. The
/// window is wide and reachable: requestEngineTeardownForEngine() returns
/// immediately, its worker may not run for a while, and a worker isolate's
/// `isInitialized` still reads true in the meantime -- so `SoLoudIsolate` can
/// legitimately ask to start a capture right inside it. Forced here by parking
/// the teardown worker on the lifecycle lock, which is exactly where the gap
/// lives.
void testCaptureCannotStartAfterTeardownIsDecided()
{
    std::printf("a capture cannot start once teardown has been decided\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);
    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kEngineA),
           "the owner should publish its mixer callable");

    // Park the teardown worker before it reaches the mixer, so the test sits
    // inside the window rather than hoping to land in it.
    soloudTestLockInitDeinit();

    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "the owning engine's teardown should be accepted");

    // This is the call a still-running capture isolate would make here.
    EXPECT(startMixerCapture(MIXER_OUTPUT_PCM_S16LE, 44100, 2,
                             /*bufferSizeBytes=*/64 * 1024,
                             /*notificationThresholdBytes=*/256,
                             /*chunkPCMFrames=*/2048) == backendNotInited,
           "a capture must be refused once teardown has been decided");
    EXPECT(isMixerCaptureRunning() == 0, "no capture should be running");

    soloudTestUnlockInitDeinit();

    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "the native engine should be disposed");
    EXPECT(isMixerCaptureRunning() == 0,
           "no capture may outlive the engine it belonged to");

    resetGlobalState();
}

/// Two stops arriving at once. `MixerOutput::stop()` guards itself with a
/// check-then-act on `m_running`, so unserialized both callers can pass it and
/// then join the same two std::threads and reset the same unique_ptrs -- a
/// double join is undefined behaviour, not a lost update.
///
/// This is reachable without two engines: a capture isolate calling
/// `SoLoudIsolate.stopMixerOutputStream()` while engine teardown stops the same
/// capture from its own worker. Released from a spin so the two land together;
/// a stopper that simply wins never enters the window at all.
///
/// Note for anyone reading a ThreadSanitizer run of this test: restarting a
/// capture while the engine is playing also surfaces a *different*, older race
/// that this branch does not touch. MixerOutput::start() rewrites m_format,
/// m_channels, m_bufferSize and m_buffer, while the audio thread is inside
/// onAudioData() reading them -- it checks m_running on entry, but stop() never
/// waits for a callback already past that check, so the next start() can
/// rewrite the state underneath it. It predates this work (the branch only
/// changed how m_callback is stored) and belongs to the audio path rather than
/// to engine lifecycle, so it is reported rather than fixed here.
void testConcurrentCaptureStopsAreSerialized()
{
    std::printf("concurrent capture stops are serialized\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);
    EXPECT(setMixerOutputCallbackForEngine(onMixerOutput, kEngineA),
           "the owner should publish its mixer callable");

    constexpr int kRounds = 25;
    int started = 0;
    for (int round = 0; round < kRounds; ++round)
    {
        if (startMixerCapture(MIXER_OUTPUT_PCM_S16LE, 44100, 2,
                              /*bufferSizeBytes=*/64 * 1024,
                              /*notificationThresholdBytes=*/256,
                              /*chunkPCMFrames=*/2048) != noError)
            break;
        ++started;

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        auto stopper = [&ready, &go]
        {
            ready.fetch_add(1);
            while (!go.load())
            {
            }
            stopMixerCapture();
        };

        std::thread a(stopper);
        std::thread b(stopper);
        while (ready.load() < 2)
        {
        }
        go.store(true);
        a.join();
        b.join();

        if (isMixerCaptureRunning() != 0)
            break;
    }

    EXPECT(started == kRounds,
           "every round should have started a capture (started %d of %d)",
           started, kRounds);
    EXPECT(isMixerCaptureRunning() == 0,
           "simultaneous stops must leave the capture stopped, exactly once");

    // And the same collision against the stop engine teardown performs.
    EXPECT(startMixerCapture(MIXER_OUTPUT_PCM_S16LE, 44100, 2,
                             /*bufferSizeBytes=*/64 * 1024,
                             /*notificationThresholdBytes=*/256,
                             /*chunkPCMFrames=*/2048) == noError,
           "a final capture should start");

    std::atomic<bool> keepStopping{true};
    std::thread stopper([&keepStopping]
                        {
        while (keepStopping.load())
            stopMixerCapture(); });

    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "the owning engine's teardown should be accepted");
    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "the native engine should be disposed");

    keepStopping.store(false);
    stopper.join();

    EXPECT(isMixerCaptureRunning() == 0, "the capture should be stopped");

    resetGlobalState();
}

/// The sequence the iOS plugin runs in its lifecycle handshake, which is the
/// only supported point at which iOS can retire a previous isolate's callables.
///
/// iOS has no equivalent of Android's onPreEngineRestart(), so after a hot
/// restart the old isolate's callables are still registered under the *same*
/// engine id when the new isolate starts initializing. The handler retires them
/// and then takes a fresh claim, in that order, on the platform thread.
void testIosLifecycleHandshake()
{
    std::printf("the iOS handshake retires stale callables and reclaims\n");
    resetGlobalState();

    // The old isolate: initialized, callables registered under engine A.
    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);
    const unsigned int hash = createPullStream();
    EXPECT(hash != 0, "a pull buffer stream should be created");
    EXPECT(stateChangedDelta() == 1, "the old isolate's callables are live");

    // Hot restart: the isolate is gone, the FlutterEngine and its id are not.
    // The replacement isolate's handshake arrives, for the same engine id.
    EXPECT(clearDartCallbackRegistrationsForEngine(kEngineA),
           "the handshake should retire the previous isolate's callables");
    EXPECT(stateChangedDelta() == 0,
           "the old isolate's global callables must be inert");
    EXPECT(streamCallDelta(hash) == 0,
           "the old isolate's stream callables must be inert too");

    prepareEngineInit(kEngineA);

    // The claim is established before the reply, so an engine deallocated while
    // the replacement is opening the device still has something to tear down.
    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "the handshake should leave a claim the detach hook can tear down");
    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "that teardown should dispose the engine");

    resetGlobalState();
}

/// A handshake names its own engine, so it can never retire another engine's
/// callables -- the case that matters when two engines overlap and the
/// replacement is the one that owns the registration.
void testIosHandshakeIsScopedToItsEngine()
{
    std::printf("the iOS handshake cannot retire another engine's callables\n");
    resetGlobalState();

    prepareEngineInit(kEngineB);
    registerCallbacksFor(kEngineB);
    EXPECT(stateChangedDelta() == 1, "B's callables should be live");

    // A late handshake from engine A, whose isolate is starting while B holds
    // the registration.
    EXPECT(!clearDartCallbackRegistrationsForEngine(kEngineA),
           "A's handshake must not retire B's callables");
    EXPECT(stateChangedDelta() == 1, "B's callables must still be live");

    resetGlobalState();
}

/// The iOS prepare handshake cannot claim the engine synchronously: Dart is
/// suspended while the request crosses to the platform thread, and `deinit()`
/// can run in that gap. A claim landing on the far side of that teardown would
/// lower the shutdown flag and leave ownership recorded for an engine that no
/// longer exists -- the initialization lost, but its claim would win.
void testSupersededPrepareCannotClaim()
{
    std::printf("a prepare superseded by deinit cannot claim afterwards\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);

    // A new initialization begins: Dart reads the epoch and sends the request.
    const uint64_t epoch = currentEngineShutdownEpoch();

    // While it is in flight, deinit() runs to completion.
    requestEngineShutdown();
    dispose();

    // The request arrives late.
    EXPECT(!prepareEngineInitForRequest(kEngineA, epoch),
           "a prepare superseded by a shutdown must be refused");
    EXPECT(soloudTestPlayerIsInited() == 0, "the engine must stay disposed");
    EXPECT(isInited() == 0, "readiness must stay false");
    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "the refused prepare must not have left a lifecycle claim");

    // A later, legitimate initialization still claims normally.
    const uint64_t freshEpoch = currentEngineShutdownEpoch();
    EXPECT(prepareEngineInitForRequest(kEngineA, freshEpoch),
           "a fresh request should claim");
    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "and that claim should be tearable down");

    resetGlobalState();
}

/// Every route that requests a shutdown has to invalidate requests in flight,
/// not just the one Dart calls: an engine destroyed through the detach hook
/// supersedes a pending initialization just as much as deinit() does.
void testEveryShutdownRouteInvalidatesPendingPrepare()
{
    std::printf("every shutdown route invalidates a pending prepare\n");
    resetGlobalState();

    // Route 1: the explicit shutdown request Dart's deinit path uses.
    uint64_t epoch = currentEngineShutdownEpoch();
    requestEngineShutdown();
    EXPECT(!prepareEngineInitForRequest(kEngineA, epoch),
           "requestEngineShutdown() must invalidate a pending prepare");

    // Route 2: dispose().
    epoch = currentEngineShutdownEpoch();
    dispose();
    EXPECT(!prepareEngineInitForRequest(kEngineA, epoch),
           "dispose() must invalidate a pending prepare");

    // Route 3: a FlutterEngine being destroyed.
    prepareEngineInit(kEngineA);
    epoch = currentEngineShutdownEpoch();
    EXPECT(requestEngineTeardownForEngine(kEngineA),
           "the teardown should be accepted");
    EXPECT(!prepareEngineInitForRequest(kEngineA, epoch),
           "an accepted teardown must invalidate a pending prepare");

    resetGlobalState();
}

/// The replacement-ordering case: a request that was cancelled, a newer
/// initialization that succeeded, and then the stale request arriving. It must
/// not disturb the newer engine in any way.
void testStalePrepareCannotDisturbNewerInit()
{
    std::printf("a stale prepare cannot disturb a newer initialization\n");
    resetGlobalState();

    // Request A goes out.
    const uint64_t staleEpoch = currentEngineShutdownEpoch();

    // A is cancelled by a deinit.
    requestEngineShutdown();
    dispose();

    // A newer initialization claims and registers, for a different engine.
    const uint64_t freshEpoch = currentEngineShutdownEpoch();
    EXPECT(prepareEngineInitForRequest(kEngineB, freshEpoch),
           "the newer initialization should claim");
    registerCallbacksFor(kEngineB);
    EXPECT(stateChangedDelta() == 1, "B's callables should be live");

    // Now A arrives.
    EXPECT(!prepareEngineInitForRequest(kEngineA, staleEpoch),
           "the stale request must be refused");
    EXPECT(stateChangedDelta() == 1,
           "the stale request must not retire B's callables");
    EXPECT(requestEngineTeardownForEngine(kEngineB),
           "B must still own its lifecycle claim");
    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "the stale request must not have claimed anything for A");

    resetGlobalState();
}

/// The ordinary destroy path: callables inert at once, native engine gone
/// shortly after, and the duplicate notification (onEngineWillDestroy() and
/// onDetachedFromEngine() both fire) tears down exactly once.
void testEngineDestroyDisposesTheNativeEngine()
{
    std::printf("engine destroy disposes the native engine exactly once\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);
    EXPECT(soloudTestPlayerIsInited() == 1, "the player should be initialized");
    EXPECT(isInited() == 1, "readiness should be published");

    const auto start = std::chrono::steady_clock::now();
    const bool accepted = requestEngineTeardownForEngine(kEngineA);
    const long long elapsed = millisSince(start);

    EXPECT(accepted, "the owning engine's teardown should be accepted");
    EXPECT(elapsed < kPlatformThreadBudgetMs,
           "teardown request took %lldms; it must not block the platform thread",
           elapsed);
    EXPECT(stateChangedDelta() == 0,
           "the callables must be inert as soon as the request returns");

    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "the native engine should be disposed by the worker");
    EXPECT(isInited() == 0, "readiness must not survive the teardown");

    // onDetachedFromEngine() arriving after onEngineWillDestroy().
    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "a duplicate destroy/detach must not tear down again");

    resetGlobalState();
}

/// A FlutterEngine destroyed while its initialization is parked inside the
/// device open. On Android that window is seconds long, and it is the one where
/// no callables are registered yet -- so only the lifecycle claim can authorize
/// the teardown.
void testEngineDestroyedDuringInitialization()
{
    std::printf("an engine destroyed mid-initialization leaves nothing\n");
    resetGlobalState();

    soloudTestArmInitBarrier();

    std::atomic<int> initResult{-1};
    std::thread initWorker([&initResult]
                           { initResult = initEngineAs(kEngineA) ? 1 : 0; });

    // Park inside initEngine(), just past the device open.
    soloudTestWaitInitBarrierReached();

    const auto start = std::chrono::steady_clock::now();
    const bool accepted = requestEngineTeardownForEngine(kEngineA);
    const long long elapsed = millisSince(start);

    EXPECT(accepted,
           "an engine still holding the claim must be able to tear down what "
           "its initialization is building");
    EXPECT(elapsed < kPlatformThreadBudgetMs,
           "teardown request took %lldms while an init held the lifecycle lock; "
           "it must not wait for it",
           elapsed);

    soloudTestReleaseInitBarrier();
    initWorker.join();

    EXPECT(initResult.load() == 0,
           "an initialization cannot report ready after its engine was "
           "destroyed");
    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "whatever the initialization built must be disposed");
    EXPECT(isInited() == 0, "readiness must not be published");
    EXPECT(!requestEngineTeardownForEngine(kEngineA),
           "the claim must have been released by the teardown");

    resetGlobalState();
}

/// A teardown worker that reaches init_deinit_mutex after a replacement engine
/// has claimed must leave that engine completely alone. This is the scenario
/// the generation exists for, forced rather than raced: the mutex is held for
/// the whole window in which the replacement claims and registers.
void testStaleTeardownCannotDisposeReplacement()
{
    std::printf("a stale teardown cannot dispose the replacement engine\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);

    // Stand in for an unrelated operation holding the lifecycle lock -- a
    // loadFile(), a device change -- so the teardown worker has to queue.
    soloudTestLockInitDeinit();

    const auto start = std::chrono::steady_clock::now();
    const bool accepted = requestEngineTeardownForEngine(kEngineA);
    const long long elapsed = millisSince(start);

    EXPECT(accepted, "A's teardown should be accepted while A still owns");
    EXPECT(elapsed < kPlatformThreadBudgetMs,
           "teardown request took %lldms with the lifecycle lock held; it must "
           "not wait for it",
           elapsed);
    EXPECT(stateChangedDelta() == 0,
           "A's callables must be inert immediately, not once the lock frees");

    // The replacement claims and publishes its own callables while A's worker
    // is still queued behind the mutex.
    prepareEngineInit(kEngineB);
    registerCallbacksFor(kEngineB);

    soloudTestUnlockInitDeinit();

    // Give the queued worker every chance to run and do damage.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT(soloudTestPlayerIsInited() == 1,
           "A's stale worker must not dispose the engine B claimed");
    EXPECT(stateChangedDelta() == 1,
           "B's callables must survive A's teardown");
    EXPECT(requestEngineTeardownForEngine(kEngineB),
           "B's lifecycle claim must still be current");
    EXPECT(waitFor([] { return soloudTestPlayerIsInited() == 0; }),
           "B's own teardown should dispose the engine");

    resetGlobalState();
}

/// Retirement must not wait for the locks that guard Player and its sounds.
/// `sounds_mutex` in particular is held across decoding by addAudioDataStream(),
/// and init_deinit_mutex for the whole of a dispose().
void testRetirementNeverWaitsForNativeWork()
{
    std::printf("retirement never waits for the native lifecycle locks\n");
    resetGlobalState();

    if (!initEngineAs(kEngineA))
    {
        EXPECT(false, "the engine should initialize");
        return;
    }
    registerCallbacksFor(kEngineA);
    const unsigned int hash = createPullStream();
    EXPECT(hash != 0, "a pull buffer stream should be created");

    soloudTestLockInitDeinit();

    const auto start = std::chrono::steady_clock::now();
    const bool cleared = clearDartCallbackRegistrationsForEngine(kEngineA);
    const long long elapsed = millisSince(start);

    EXPECT(cleared, "the owner should be allowed to retire its callables");
    EXPECT(elapsed < kPlatformThreadBudgetMs,
           "callback clear took %lldms with the lifecycle lock held; it must "
           "not wait for it",
           elapsed);
    EXPECT(stateChangedDelta() == 0,
           "global callables must be inert before the lock is released");

    soloudTestUnlockInitDeinit();

    // The source-owned callables were inert from the moment the hook returned,
    // with the Player untouched -- nothing was deferred to make that true.
    EXPECT(streamCallDelta(hash) == 0,
           "stream callables must be inert without any deferred cleanup");

    resetGlobalState();
}

} // namespace

int main()
{
    startWatchdog(180);

    // Every scenario below needs an engine that can actually initialize. A
    // deliberate failure beats a green run that skipped the substance: on a
    // machine with no output device at all, miniaudio still opens its null
    // backend, so this failing means something is wrong with the build or the
    // environment, not merely that the box is headless.
    const bool canInit = initEngineAs(kEngineA);
    dispose();
    if (!canInit)
    {
        std::fprintf(stderr,
                     "FATAL: no output device could be opened, not even a null "
                     "backend; the lifecycle scenarios cannot run.\n"
                     "Set SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE=1 to downgrade "
                     "this to a skip.\n");
        return std::getenv("SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE") ? 0 : 1;
    }

    testHotRestartRetiresEveryCallable();
    testIosLifecycleHandshake();
    testSupersededPrepareCannotClaim();
    testEveryShutdownRouteInvalidatesPendingPrepare();
    testStalePrepareCannotDisturbNewerInit();
    testIosHandshakeIsScopedToItsEngine();
    testReplacementDoesNotResurrectRetiredSources();
    testCallbackRetirementIsScopedToTheOwner();
    testCallbackOwnerDiffersFromLifecycleOwner();
    testTeardownRefusedWithoutAClaim();
    testMixerCallbackPublicationIsOwnerScoped();
    testStaleWorkerMixerCallbackCannotBeRevived();
    testTeardownDuringActiveMixerCapture();
    testCaptureCannotStartAfterTeardownIsDecided();
    testConcurrentCaptureStopsAreSerialized();
    testEngineDestroyDisposesTheNativeEngine();
    testEngineDestroyedDuringInitialization();
    testStaleTeardownCannotDisposeReplacement();
    testRetirementNeverWaitsForNativeWork();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

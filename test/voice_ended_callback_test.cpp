// Standalone native regression tests for voice-ended callback dispatch.
//
// stopVoice_internal() runs with SoLoud's audio mutex held. It used to invoke
// the embedder's voice-ended callback from there, which in flutter_soloud
// reaches into Player state guarded by sounds_mutex:
//
//     audio mutex -> voice-ended callback -> sounds_mutex
//
// while Player::disposeSound() holds the opposite order:
//
//     sounds_mutex -> soloud.stop() -> audio mutex
//
// One voice ending while another sound is disposed wedges both threads, and
// with the audio mutex stranded every later SoLoud call (deinit() included)
// blocks forever. Ended voices are now queued and dispatched after the mutex
// is released; these tests pin that behaviour down.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_voice_ended_callback_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 1024;
constexpr unsigned int kBufferSize = 512;

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...) do { \
    ++gAssertions; \
    if (!(condition)) { \
        ++gFailures; \
        std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", \
                     __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

// ---- Watchdog -------------------------------------------------------------
// The pre-fix engine deadlocks rather than misbehaves, so a plain assertion
// would hang the suite forever. Any scenario that can wedge runs under a
// watchdog: if it does not finish in time the process reports the failure and
// exits immediately, because the wedged threads can never be joined.

class Watchdog {
public:
    Watchdog(const char* what, std::chrono::milliseconds limit)
        : what_(what) {
        thread_ = std::thread([this, limit] {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!done_.wait_for(lock, limit, [this] { return finished_; })) {
                std::fprintf(stderr,
                             "  FAIL timed out after %lld ms: %s\n"
                             "       (the voice-ended callback deadlocked "
                             "against the audio mutex)\n",
                             static_cast<long long>(limit.count()), what_);
                std::fflush(stderr);
                std::fflush(stdout);
                std::_Exit(1);
            }
        });
    }

    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        done_.notify_all();
        thread_.join();
    }

private:
    const char* what_;
    std::mutex mutex_;
    std::condition_variable done_;
    bool finished_ = false;
    std::thread thread_;
};

// Blocks until `predicate` holds or `limit` elapses. Returns false on timeout
// so callers can keep making progress instead of hanging.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// ---- Test audio sources ---------------------------------------------------

class SilenceSourceInstance final : public SoLoud::AudioSourceInstance {
public:
    // `frameCount == 0` means "never ends"; the voice only goes away when the
    // test stops it explicitly.
    explicit SilenceSourceInstance(unsigned int frameCount)
        : frameCount_(frameCount) {
    }

    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        unsigned int count = samplesToRead;
        if (frameCount_ != 0) {
            const unsigned int available =
                frameCount_ - std::min(offset_, frameCount_);
            count = std::min(samplesToRead, available);
        }
        for (unsigned int channel = 0; channel < mChannels; ++channel) {
            std::fill(buffer + channel * bufferSize,
                      buffer + channel * bufferSize + count, 0.0f);
        }
        offset_ += count;
        return count;
    }

    bool hasEnded() override {
        return frameCount_ != 0 && offset_ >= frameCount_;
    }

    SoLoud::result rewind() override {
        offset_ = 0;
        mStreamPosition = 0;
        return SoLoud::SO_NO_ERROR;
    }

private:
    unsigned int frameCount_;
    unsigned int offset_ = 0;
};

class SilenceSource final : public SoLoud::AudioSource {
public:
    explicit SilenceSource(unsigned int frameCount = 0)
        : frameCount_(frameCount) {
        mBaseSamplerate = static_cast<float>(kSampleRate);
        mChannels = 1;
    }

    SoLoud::AudioSourceInstance* createInstance() override {
        return new SilenceSourceInstance(frameCount_);
    }

private:
    unsigned int frameCount_;
};

// ---- Rig ------------------------------------------------------------------

struct Rig {
    Rig() {
        const SoLoud::result result = engine.init(
            SoLoud::Soloud::CLIP_ROUNDOFF,
            SoLoud::Soloud::NULLDRIVER,
            kSampleRate,
            kBufferSize,
            1);
        EXPECT(result == SoLoud::SO_NO_ERROR,
               "null backend initialization failed: %d", result);
        engine.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
    }

    ~Rig() {
        engine.setVoiceEndedCallback(nullptr);
        engine.deinit();
    }

    // A voice only owns resample data (and therefore only reports as ended)
    // once it has been through a mix cycle, so play + mix is the minimum setup
    // for the callback to fire at all.
    SoLoud::handle startVoice(SoLoud::AudioSource& source) {
        const SoLoud::handle handle = engine.play(source);
        EXPECT(handle != 0, "play returned an invalid handle");
        return handle;
    }

    void mix(unsigned int frameCount = kBufferSize) {
        std::vector<float> output(frameCount, 0.0f);
        engine.mix(output.data(), frameCount);
    }

    SilenceSource source;
    SoLoud::Soloud engine;
};

// The engine takes a plain C function pointer, so the callback body has to
// reach the active test through file-scope state.
struct CallbackState {
    SoLoud::Soloud* engine = nullptr;
    std::vector<unsigned int> handles;
    std::mutex handlesMutex;
    // Set if the callback ever ran while the engine believed it was inside the
    // audio mutex. This is exactly the condition the fix rules out.
    std::atomic<bool> sawAudioMutexHeld{false};
    // Optional embedder lock, standing in for flutter_soloud's sounds_mutex,
    // which is a recursive_mutex there too.
    std::recursive_mutex* embedderMutex = nullptr;
    std::atomic<bool> entered{false};
    // Handle the callback re-enters SoLoud with, if any.
    std::atomic<SoLoud::handle> reentryHandle{0};
    std::atomic<int> reentryVoiceCount{-1};
    // Handle the *first* callback stops, to exercise re-entrant voice
    // termination. Consumed once.
    std::atomic<SoLoud::handle> stopOnFirstCallback{0};
    // If set, the first callback unregisters the callback. Consumed once.
    std::atomic<bool> unregisterOnFirstCallback{false};
    // Callback nesting: >1 means a dispatch ran from inside another dispatch.
    std::atomic<int> depth{0};
    std::atomic<int> maxDepth{0};
};

CallbackState* gState = nullptr;

void recordVoiceEnded(unsigned int* handle) {
    CallbackState* state = gState;
    if (state == nullptr)
        return;

    if (state->engine != nullptr && state->engine->mInsideAudioThreadMutex)
        state->sawAudioMutexHeld.store(true);

    const int depth = state->depth.fetch_add(1) + 1;
    int seenMax = state->maxDepth.load();
    while (depth > seenMax &&
           !state->maxDepth.compare_exchange_weak(seenMax, depth)) {
    }

    state->entered.store(true);

    // Record on entry so `handles` reflects invocation order, nesting included.
    {
        std::lock_guard<std::mutex> lock(state->handlesMutex);
        state->handles.push_back(*handle);
    }

    // Take the embedder lock the way flutter_soloud's callback takes
    // sounds_mutex. Under the old code this is where the audio mutex got
    // stranded.
    if (state->embedderMutex != nullptr) {
        std::lock_guard<std::recursive_mutex> lock(*state->embedderMutex);
        (void)lock;
    }

    // Re-enter SoLoud from inside the callback: every one of these calls takes
    // the audio mutex itself.
    if (state->engine != nullptr) {
        if (state->unregisterOnFirstCallback.exchange(false))
            state->engine->setVoiceEndedCallback(nullptr);

        const SoLoud::handle stopTarget = state->stopOnFirstCallback.exchange(0);
        if (stopTarget != 0)
            state->engine->stop(stopTarget);

        const SoLoud::handle reentry = state->reentryHandle.load();
        if (reentry != 0) {
            state->engine->setVolume(reentry, 0.5f);
            state->reentryVoiceCount.store(
                static_cast<int>(state->engine->getActiveVoiceCount()));
        }
    }

    state->depth.fetch_sub(1);
}

// ---- Tests ----------------------------------------------------------------

void testCallbackRunsWithAudioMutexReleased() {
    std::printf("Test: voice-ended callback runs with the audio mutex "
                "released\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    const SoLoud::handle handle = rig.startVoice(rig.source);
    rig.mix();
    rig.engine.stop(handle);

    EXPECT(state.handles.size() == 1,
           "expected one ended handle, got %zu", state.handles.size());
    EXPECT(!state.handles.empty() && state.handles[0] == handle,
           "callback reported handle %u, expected %u",
           state.handles.empty() ? 0u : state.handles[0], handle);
    EXPECT(!state.sawAudioMutexHeld.load(),
           "callback ran while the audio mutex was held");
    EXPECT(rig.engine.mEndedVoiceCount == 0,
           "pending queue not drained, %u entries left",
           rig.engine.mEndedVoiceCount);

    gState = nullptr;
}

void testNaturalEndOfStreamDispatchesOutsideMutex() {
    std::printf("Test: a voice ending during mix() dispatches outside the "
                "mutex\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    SilenceSource finiteSource(kBufferSize / 2);
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    const SoLoud::handle handle = rig.startVoice(finiteSource);
    // First cycle assigns resample data, second one runs the source past EOF.
    rig.mix();
    rig.mix();

    EXPECT(state.handles.size() == 1,
           "expected one ended handle from EOF, got %zu", state.handles.size());
    EXPECT(!state.handles.empty() && state.handles[0] == handle,
           "callback reported handle %u, expected %u",
           state.handles.empty() ? 0u : state.handles[0], handle);
    EXPECT(!state.sawAudioMutexHeld.load(),
           "callback ran while the audio mutex was held");

    gState = nullptr;
}

void testMultipleEndedVoicesArriveInQueueOrder() {
    std::printf("Test: voices ended in one batch arrive in order\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    constexpr unsigned int kVoices = 4;
    std::vector<SoLoud::handle> handles;
    for (unsigned int i = 0; i < kVoices; ++i)
        handles.push_back(rig.startVoice(rig.source));
    rig.mix();

    // stopAll() walks voices in ascending index under a single lock, so the
    // whole batch is queued before the unlock drains it.
    rig.engine.stopAll();

    std::vector<SoLoud::handle> expected = handles;
    std::sort(expected.begin(), expected.end(),
              [](SoLoud::handle a, SoLoud::handle b) {
                  return (a & 0xfff) < (b & 0xfff);
              });

    EXPECT(state.handles.size() == kVoices,
           "expected %u ended handles, got %zu", kVoices,
           state.handles.size());
    if (state.handles.size() == kVoices) {
        for (unsigned int i = 0; i < kVoices; ++i) {
            EXPECT(state.handles[i] == expected[i],
                   "ended handle %u was %u, expected %u",
                   i, state.handles[i], expected[i]);
        }
    }
    EXPECT(!state.sawAudioMutexHeld.load(),
           "callback ran while the audio mutex was held");

    gState = nullptr;
}

void testCallbackCanReenterSoloud() {
    std::printf("Test: the callback can call back into SoLoud\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    const SoLoud::handle survivor = rig.startVoice(rig.source);
    const SoLoud::handle ending = rig.startVoice(rig.source);
    rig.mix();

    // Re-entrant SoLoud calls from the callback take the audio mutex, which
    // self-deadlocks if the callback still runs underneath it.
    state.reentryHandle.store(survivor);
    rig.engine.stop(ending);

    EXPECT(state.handles.size() == 1,
           "expected one ended handle, got %zu", state.handles.size());
    EXPECT(state.reentryVoiceCount.load() >= 1,
           "re-entrant getActiveVoiceCount() returned %d",
           state.reentryVoiceCount.load());
    EXPECT(rig.engine.getVolume(survivor) == 0.5f,
           "re-entrant setVolume() did not take effect, volume is %f",
           rig.engine.getVolume(survivor));
    EXPECT(!state.sawAudioMutexHeld.load(),
           "callback ran while the audio mutex was held");

    gState = nullptr;
}

// A callback that terminates another voice queues a handle from inside the
// dispatch loop. Without the dispatch-in-progress guard the nested unlock
// starts its own dispatch, so the new handle jumps the rest of the batch
// (A -> C -> B for a queued [A, B]) and every level stacks another
// VOICE_COUNT-sized snapshot on the stack.
void testReentrantStopDoesNotRecurseOrReorder() {
    std::printf("Test: a callback stopping another voice keeps FIFO order and "
                "does not recurse\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    // A second source so stopAudioSource() below queues exactly the first two
    // voices and leaves the third for the callback to stop.
    SilenceSource otherSource;
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    const SoLoud::handle first = rig.startVoice(rig.source);
    const SoLoud::handle second = rig.startVoice(rig.source);
    const SoLoud::handle stoppedByCallback = rig.startVoice(otherSource);
    rig.mix();

    std::vector<SoLoud::handle> batch = {first, second};
    std::sort(batch.begin(), batch.end(),
              [](SoLoud::handle a, SoLoud::handle b) {
                  return (a & 0xfff) < (b & 0xfff);
              });

    state.stopOnFirstCallback.store(stoppedByCallback);
    rig.engine.stopAudioSource(rig.source);

    const std::vector<SoLoud::handle> expected = {
        batch[0], batch[1], stoppedByCallback};

    EXPECT(state.handles.size() == expected.size(),
           "expected %zu ended handles, got %zu", expected.size(),
           state.handles.size());
    if (state.handles.size() == expected.size()) {
        for (unsigned int i = 0; i < expected.size(); ++i) {
            EXPECT(state.handles[i] == expected[i],
                   "ended handle %u was %u, expected %u",
                   i, state.handles[i], expected[i]);
        }
    }
    EXPECT(state.maxDepth.load() == 1,
           "dispatch recursed %d levels deep, expected a flat drain",
           state.maxDepth.load());
    EXPECT(rig.engine.mEndedVoiceCount == 0,
           "pending queue not drained, %u entries left",
           rig.engine.mEndedVoiceCount);
    EXPECT(!state.sawAudioMutexHeld.load(),
           "callback ran while the audio mutex was held");

    gState = nullptr;
}

// Unregistering mid-batch must suppress the handles that have not been
// delivered yet, otherwise the atomic setter buys nothing during teardown: the
// dispatcher would keep calling a pointer the embedder has already retired.
void testUnregisteringMidBatchSuppressesRemainingHandles() {
    std::printf("Test: clearing the callback mid-batch stops the rest of the "
                "batch\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    constexpr unsigned int kVoices = 4;
    for (unsigned int i = 0; i < kVoices; ++i)
        rig.startVoice(rig.source);
    rig.mix();

    state.unregisterOnFirstCallback.store(true);
    rig.engine.stopAll();

    EXPECT(state.handles.size() == 1,
           "expected dispatch to stop after the callback unregistered itself, "
           "got %zu handles", state.handles.size());
    EXPECT(rig.engine.mEndedVoiceCount == 0,
           "pending queue not drained after unregistering, %u entries left",
           rig.engine.mEndedVoiceCount);

    gState = nullptr;
}

void testNullCallbackDrainsPendingHandles() {
    std::printf("Test: a null callback drains the queue without dispatching\n");
    Watchdog watchdog(__func__, std::chrono::seconds(10));

    Rig rig;
    CallbackState state;
    state.engine = &rig.engine;
    gState = &state;
    rig.engine.setVoiceEndedCallback(nullptr);

    const SoLoud::handle first = rig.startVoice(rig.source);
    rig.mix();
    rig.engine.stop(first);

    EXPECT(state.handles.empty(),
           "null callback still dispatched %zu handles",
           state.handles.size());
    EXPECT(rig.engine.mEndedVoiceCount == 0,
           "pending queue not drained with a null callback, %u entries left",
           rig.engine.mEndedVoiceCount);

    // Registering a callback afterwards must not replay the discarded batch.
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);
    const SoLoud::handle second = rig.startVoice(rig.source);
    rig.mix();
    rig.engine.stop(second);

    EXPECT(state.handles.size() == 1,
           "expected only the new handle, got %zu", state.handles.size());
    EXPECT(!state.handles.empty() && state.handles[0] == second,
           "callback reported handle %u, expected %u",
           state.handles.empty() ? 0u : state.handles[0], second);

    gState = nullptr;
}

// The original bug, reproduced with the two lock orders running concurrently.
void testLockOrderInversionDoesNotDeadlock() {
    std::printf("Test: voice end vs. dispose lock-order inversion completes\n");
    Watchdog watchdog(__func__, std::chrono::seconds(15));

    Rig rig;
    std::recursive_mutex soundsMutex;  // stands in for Player::sounds_mutex
    CallbackState state;
    state.engine = &rig.engine;
    state.embedderMutex = &soundsMutex;
    gState = &state;
    rig.engine.setVoiceEndedCallback(recordVoiceEnded);

    const SoLoud::handle ending = rig.startVoice(rig.source);
    const SoLoud::handle disposed = rig.startVoice(rig.source);
    rig.mix();

    std::atomic<bool> soundsMutexHeld{false};
    std::atomic<bool> disposeDone{false};
    // Both handshakes are hard requirements, not best-effort delays: if either
    // times out the two lock orders never overlapped and even the pre-fix engine
    // would sail through, so the test has to fail rather than pass vacuously.
    // (EXPECT touches non-atomic counters, so the worker records its result and
    // the assertion happens on the main thread after the join.)
    std::atomic<bool> sawCallbackEntry{false};

    // The "disposer": sounds_mutex -> soloud.stop() -> audio mutex.
    std::thread disposer([&] {
        std::lock_guard<std::recursive_mutex> lock(soundsMutex);
        soundsMutexHeld.store(true);
        // Hold the embedder lock until the other thread is inside the callback;
        // that overlap is the window the old code deadlocked in.
        sawCallbackEntry.store(waitFor([&] { return state.entered.load(); },
                                       std::chrono::seconds(2)));
        rig.engine.stop(disposed);
        disposeDone.store(true);
    });

    // The "audio side": audio mutex -> voice-ended callback -> sounds_mutex.
    const bool disposerReady = waitFor([&] { return soundsMutexHeld.load(); },
                                       std::chrono::seconds(2));
    rig.engine.stop(ending);

    disposer.join();

    EXPECT(disposerReady,
           "the disposing thread never took the embedder lock, so the two lock "
           "orders never overlapped");
    EXPECT(sawCallbackEntry.load(),
           "the voice-ended callback never ran while the embedder lock was "
           "held, so the two lock orders never overlapped");
    EXPECT(disposeDone.load(), "the disposing thread never completed");
    EXPECT(state.handles.size() == 2,
           "expected both voices to report ended, got %zu",
           state.handles.size());
    EXPECT(!state.sawAudioMutexHeld.load(),
           "callback ran while the audio mutex was held");

    // The engine must still be usable rather than wedged.
    const SoLoud::handle after = rig.startVoice(rig.source);
    rig.mix();
    rig.engine.stop(after);
    EXPECT(state.handles.size() == 3,
           "engine stopped reporting ended voices after the race, got %zu",
           state.handles.size());

    gState = nullptr;
}

}  // namespace

int main() {
    testCallbackRunsWithAudioMutexReleased();
    testNaturalEndOfStreamDispatchesOutsideMutex();
    testMultipleEndedVoicesArriveInQueueOrder();
    testCallbackCanReenterSoloud();
    testReentrantStopDoesNotRecurseOrReorder();
    testUnregisteringMidBatchSuppressesRemainingHandles();
    testNullCallbackDrainsPendingHandles();
    testLockOrderInversionDoesNotDeadlock();

    if (gFailures != 0) {
        std::fprintf(stderr, "\n%d of %d assertions failed.\n",
                     gFailures, gAssertions);
        return 1;
    }

    std::printf("\nAll %d assertions passed.\n", gAssertions);
    return 0;
}

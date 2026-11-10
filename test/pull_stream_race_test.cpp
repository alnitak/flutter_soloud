// The threads that share a PullBufferStream:
//
//   * a feeder delivering data (addPullBufferDataStream()), which queues the
//     bytes, decodes them and then checks the buffering: it pauses the voice
//     when the decoded audio runs low and resumes it when enough is back;
//   * the mixer (getAudio(), with SoLoud's audio mutex held), which drains the
//     decoded audio, decodes queued bytes itself when no delivery holds the
//     decode mutex, and asks for more data;
//   * control threads that seek (with SoLoud's audio mutex held, as SoLoud
//     applies seeks), reset and read the stream.
//
// Scenarios (run one by naming it):
//
//   mixer-seek    The mixer decodes the data that completes a seek out of the
//                 buffered range, while the feeder that delivered it is
//                 parked after queuing it. The mixer must go on mixing: the
//                 buffering check reads the voice's pause state, which takes
//                 the audio mutex the mixer holds.
//   deliver-seek  A delivery completes a seek and is parked in its buffering
//                 check while another thread seeks the voice. Both must
//                 finish once the delivery is released: the seek holds the
//                 audio mutex and waits for the decode mutex, so the check
//                 must not run while the delivery holds the decode mutex.
//
// A scenario that deadlocks cannot be cleaned up after: the run then prints
// its counts and exits with failure.
//
// The stream is raw 16-bit stereo PCM at 44.1 kHz, driven through the exports
// Dart uses, on a real engine (the null backend). Barriers
// (device_lifecycle_test_hooks.h) park the threads at the points that matter.
//
//   ./test/run_pull_stream_race_test.sh [--asan|--tsan] [scenario]
#include "bindings.h"
#include "device_lifecycle_test_hooks.h"
#include "enums.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using soloud_test::DeviceBarrier;

constexpr int64_t kEngineId = 4021;
constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kChannels = 2;
constexpr unsigned int kBytesPerFrame = kChannels * 2;
/// 20 minutes of audio, so seeks to 300 s and 600 s are inside the file.
constexpr uint64_t kTotalBytes = uint64_t(kSampleRate) * kBytesPerFrame * 1200;
/// 32768 frames, 0.74 s: more than the 0.5 s a paused stream needs to resume.
constexpr unsigned int kChunkFrames = 32768;

int gAssertions = 0;
int gFailures = 0;
#define EXPECT(cond, ...)                                                      \
    do {                                                                       \
        ++gAssertions;                                                         \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL: ");                                           \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

std::vector<unsigned char> gChunk;
std::mutex gRequestMutex;
std::vector<uint64_t> gRequests;

void onMoreData(uint64_t offset) {
    std::lock_guard<std::mutex> lock(gRequestMutex);
    gRequests.push_back(offset);
}

void onVoiceEnded(unsigned int *h) { std::free(h); }
void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                  unsigned long long *t) {
    (void)e; (void)n; (void)h; (void)t;
}
void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

uint64_t offsetOf(double seconds) {
    return static_cast<uint64_t>(seconds * kSampleRate) * kBytesPerFrame;
}

/// Runs [fn] on its own thread and waits up to [ms] for it to return. A
/// thread that does not return is left running: the caller then reports a
/// deadlock and ends the run.
bool returnsWithin(int ms, std::function<void()> fn) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread([fn, done] {
        fn();
        done->store(true);
    }).detach();
    for (int i = 0; i < ms / 5 && !done->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return done->load();
}

bool barrierReached(DeviceBarrier barrier, int ms) {
    return returnsWithin(ms, [barrier] { soloud_test::waitBarrierReached(barrier); });
}

[[noreturn]] void deadlocked(const char *what) {
    EXPECT(false, "%s", what);
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    std::fflush(stdout);
    _exit(1);
}

struct Stream {
    unsigned int hash = 0;
    unsigned int voice = 0;
};

/// A playing stream holding its first chunk.
Stream startStream() {
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        gRequests.clear();
    }
    Stream s;
    PlayerErrors err = setPullBufferStream(
        &s.hash, 1u << 20, 0.5, kSampleRate, kChannels,
        static_cast<int>(BufferType::PCM_S16LE), kTotalBytes, nullptr, nullptr,
        onMoreData, nullptr);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    err = addPullBufferDataStream(s.hash, gChunk.data(),
                                  static_cast<unsigned int>(gChunk.size()), 0);
    EXPECT(err == PlayerErrors::noError, "first delivery: %d", (int)err);
    err = play(s.hash, 0, 0.05f, 0.0f, false, false, 0.0, 0.0, -1, -1, 1.0f,
               &s.voice);
    EXPECT(err == PlayerErrors::noError && s.voice != 0, "play: %d", (int)err);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT(getPause(s.voice) == 0, "the voice should be playing");
    return s;
}

void endStream(const Stream &s) {
    stop(s.voice);
    disposeSound(s.hash);
}

void deliverAt(const Stream &s, double seconds) {
    addPullBufferDataStream(s.hash, gChunk.data(),
                            static_cast<unsigned int>(gChunk.size()),
                            offsetOf(seconds));
}

void mixerCompletesSeek() {
    std::printf("mixer-seek: the mixer decodes the data that completes a seek\n");
    const Stream s = startStream();
    EXPECT(seek(s.voice, 300.0f) == PlayerErrors::noError, "seek to 300 s");

    // The feeder queues the data at 300 s and parks before decoding it, so
    // only the mixer can decode it.
    soloud_test::armBarrier(DeviceBarrier::pullDataQueued);
    soloud_test::armBarrier(DeviceBarrier::pullSeekCompleted);
    std::thread feeder([&s] { deliverAt(s, 300.0); });
    EXPECT(barrierReached(DeviceBarrier::pullDataQueued, 3000),
           "the delivery did not reach the decode");
    const bool completed = barrierReached(DeviceBarrier::pullSeekCompleted, 3000);
    EXPECT(completed, "the mixer did not complete the seek");
    soloud_test::releaseBarrier(DeviceBarrier::pullSeekCompleted);

    // The audio mutex is free again between blocks unless the mixer is stuck.
    const unsigned int voice = s.voice;
    if (!returnsWithin(3000, [voice] { (void)getPause(voice); }))
        deadlocked("the mixer holds the audio mutex forever after completing "
                   "the seek (getPause() did not return within 3 s)");

    soloud_test::releaseBarrier(DeviceBarrier::pullDataQueued);
    feeder.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double start = 0, end = 0;
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start >= 299.9 && end > 300.0,
           "the stream should hold the data from 300 s, buffered [%.2f, %.2f]",
           start, end);
    EXPECT(getPause(s.voice) == 0, "the voice should play on after the seek");
    endStream(s);
}

void deliveryAndSeek() {
    std::printf("deliver-seek: a delivery in its buffering check, and a seek\n");
    const Stream s = startStream();
    EXPECT(seek(s.voice, 300.0f) == PlayerErrors::noError, "seek to 300 s");

    soloud_test::armBarrier(DeviceBarrier::pullBufferingCheckEntered);
    auto delivered = std::make_shared<std::atomic<bool>>(false);
    std::thread([s, delivered] {
        deliverAt(s, 300.0);
        delivered->store(true);
    }).detach();
    EXPECT(barrierReached(DeviceBarrier::pullBufferingCheckEntered, 3000),
           "the delivery did not reach its buffering check");

    // SoLoud seeks with its audio mutex held; the stream's seek then takes
    // the decode mutex.
    auto seeked = std::make_shared<std::atomic<bool>>(false);
    const unsigned int voice = s.voice;
    std::thread([voice, seeked] {
        seek(voice, 600.0f);
        seeked->store(true);
    }).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    soloud_test::releaseBarrier(DeviceBarrier::pullBufferingCheckEntered);

    for (int i = 0; i < 600 && !(delivered->load() && seeked->load()); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!delivered->load() || !seeked->load())
        deadlocked("the delivery and the seek wait for each other (not done "
                   "within 3 s of releasing the delivery)");
    EXPECT(returnsWithin(3000, [voice] { (void)getPause(voice); }),
           "the audio mutex should be free after the seek");
    endStream(s);
}
} // namespace

int main(int argc, char **argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const auto want = [&](const char *name) { return only.empty() || only == name; };

    gChunk.resize(size_t(kChunkFrames) * kBytesPerFrame);
    for (unsigned int i = 0; i < kChunkFrames; ++i) {
        const int16_t v = static_cast<int16_t>(
            8000.0 * std::sin(2.0 * M_PI * 440.0 * i / kSampleRate));
        for (unsigned int c = 0; c < kChannels; ++c)
            std::memcpy(&gChunk[(size_t(i) * kChannels + c) * 2], &v, 2);
    }

    setAudioDeviceIdleTimeout(600000);
    prepareEngineInit(kEngineId);
    if (initEngine(-1, kSampleRate, 2048, kChannels, 1, 0, 0) !=
        PlayerErrors::noError) {
        std::printf("engine did not start\n");
        return 2;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);

    if (want("mixer-seek")) mixerCompletesSeek();
    if (want("deliver-seek")) deliveryAndSeek();

    dispose();
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

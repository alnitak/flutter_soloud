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
//   seek          A seek out of the buffered range parks right before it
//                 discards the buffer, and a feeder delivers the data at its
//                 target. The delivery must not decode until the seek is
//                 done; its data then lands at the target.
//   rewind        The same with a seek to 0, a rewind.
//   seek-during-decode
//                 A delivery parks while it decodes, holding the decode
//                 mutex, and the (paused) voice is rewound and then seeked.
//                 Neither may wait for the delivery: SoLoud seeks with its
//                 audio mutex held, so the mixer would wait too. Once the
//                 voice plays again the last seek is applied.
//   loop-during-decode
//                 A looping voice waits for its first data while a delivery
//                 parks in its decode. The mixer rewinds such a voice on
//                 every block, and must not wait for the delivery; the
//                 delivered data is then kept.
//   reset         The mixer parks while it decodes a queued chunk, and the
//                 stream is reset. The reset must wait for the mixer; the
//                 stream then asks for its data from 0.
//   reset-during-decode
//                 A delivery parks while it decodes, and the stream is
//                 reset. The reset waits for the delivery, but the audio
//                 mutex, which the reset also takes, must stay free
//                 meanwhile: the mixer would wait too.
//   read-reset    The mixer parks between taking samples from the buffer and
//                 counting them as read, and the stream is reset. The reset
//                 must wait for the mixer, and leave an empty range, [0, 0].
//   range         The mixer parks at the same point. The buffered range read
//                 meanwhile must still end where the delivered data ends.
//   dispose       A delivery parks right after it has looked the playing
//                 stream up, and disposeSound() runs meanwhile. The disposal
//                 must wait for the delivery, which then completes; a later
//                 delivery finds no stream.
//   dispose-all   The same with disposeAllSound().
//   lock-during-decode
//                 A delivery parks while it decodes. Other users of the
//                 Player's sounds_mutex must not wait for it, and a disposal
//                 meanwhile waits for the delivery without holding that
//                 mutex.
//   replay        A short stream whose whole file has been delivered is sought
//                 to 0, and a looping one wraps. Each must ask for its data
//                 from 0 again, accept it and play it: through to its end, or
//                 round to its next wrap.
//   stress        A PCM stream and an MP3 stream
//                 (example/assets/audio/8_bit_mentality.mp3) are fed, seeked
//                 (to 0 too), reset and read by four threads at once while
//                 they play. Under ThreadSanitizer this reports any access to
//                 the decode state that the decode mutex does not order, and
//                 under AddressSanitizer a decoder or buffer used after it
//                 was replaced.
//
// A scenario that deadlocks, or whose parked threads would use discarded or
// freed state once released, cannot be cleaned up after: the run then prints
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
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

/// A seek out of the buffered range (or, for 0, a rewind) parks right before
/// it discards the buffer. A delivery made meanwhile must not decode until
/// the seek is done, and its data then lands at the target.
void deliveryDuringDiscard(const char *name, float target) {
    std::printf("%s: a delivery while a seek to %.0f s discards the buffer\n",
                name, target);
    const Stream s = startStream();
    soloud_test::armBarrier(DeviceBarrier::pullSeekDiscarding);
    const unsigned int voice = s.voice;
    std::thread seeker([voice, target] { seek(voice, target); });
    EXPECT(barrierReached(DeviceBarrier::pullSeekDiscarding, 3000),
           "the seek did not reach the discard");

    soloud_test::armBarrier(DeviceBarrier::pullDecoding);
    std::thread feeder([&s, target] { deliverAt(s, target); });
    if (barrierReached(DeviceBarrier::pullDecoding, 300))
        deadlocked("the delivery decoded while the seek was discarding the "
                   "buffer (releasing them would let the seek clear what the "
                   "delivery decodes)");

    soloud_test::releaseBarrier(DeviceBarrier::pullSeekDiscarding);
    seeker.join();
    EXPECT(barrierReached(DeviceBarrier::pullDecoding, 3000),
           "the delivery did not decode after the seek");
    soloud_test::releaseBarrier(DeviceBarrier::pullDecoding);
    feeder.join();
    double start = 0, end = 0;
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start >= target - 0.01 && end > target + 0.5,
           "the delivered data should be at %.0f s, buffered [%.2f, %.2f]",
           target, start, end);
    endStream(s);
}

/// A delivery parks in its decode, holding the decode mutex, while the voice
/// is seeked to 0 (a rewind) and then to 300 s. The voice is paused, so that
/// only the delivery can reach the barrier. With SoLoud's audio mutex held,
/// neither seek may wait for the delivery.
void seekDuringDecode() {
    std::printf("seek-during-decode: seeks while a delivery decodes\n");
    const Stream s = startStream();
    const unsigned int voice = s.voice;
    setPause(voice, true);
    soloud_test::armBarrier(DeviceBarrier::pullDecoding);
    std::thread feeder([&s] {
        addPullBufferDataStream(s.hash, gChunk.data(),
                                static_cast<unsigned int>(gChunk.size()),
                                gChunk.size());
    });
    EXPECT(barrierReached(DeviceBarrier::pullDecoding, 3000),
           "the delivery did not reach its decode");
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        gRequests.clear();
    }
    EXPECT(returnsWithin(300, [voice] { seek(voice, 0.0f); }),
           "a rewind waited for the delivery's decode");
    EXPECT(returnsWithin(300, [voice] { seek(voice, 300.0f); }),
           "a seek waited for the delivery's decode");
    // Shared: a getPosition() that does not return in time outlives this.
    auto reported = std::make_shared<std::atomic<double>>(-1.0);
    EXPECT(returnsWithin(300, [voice, reported] {
               reported->store(getPosition(voice));
           }),
           "the audio mutex is held while the delivery decodes");
    double position = reported->load();
    EXPECT(position > 299.9 && position < 300.1,
           "the seeked voice should report 300 s, got %.2f", position);

    soloud_test::releaseBarrier(DeviceBarrier::pullDecoding);
    feeder.join();
    setPause(voice, false);
    bool askedFor300 = false;
    for (int i = 0; i < 100 && !askedFor300; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(gRequestMutex);
        for (uint64_t offset : gRequests) askedFor300 |= offset == offsetOf(300.0);
    }
    EXPECT(askedFor300, "the stream should ask for the data at 300 s");
    deliverAt(s, 300.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    position = getPosition(voice);
    EXPECT(position >= 300.0 && position < 301.0,
           "the voice should play from 300 s, at %.2f", position);
    endStream(s);
}

/// A looping voice waits for its first data, so the mixer rewinds it on every
/// block, while a delivery parks in its decode. Played paused until the
/// delivery has parked, so that only the delivery can reach the barrier.
void loopDuringDecode() {
    std::printf("loop-during-decode: a looping voice waits for a decode\n");
    unsigned int hash = 0;
    PlayerErrors err = setPullBufferStream(
        &hash, 1u << 20, 0.5, kSampleRate, kChannels,
        static_cast<int>(BufferType::PCM_S16LE), kTotalBytes, nullptr, nullptr,
        onMoreData, nullptr);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    unsigned int voice = 0;
    err = play(hash, 0, 0.05f, 0.0f, true, true, 0.0, 0.0, -1, -1, 1.0f,
               &voice);
    EXPECT(err == PlayerErrors::noError && voice != 0, "play: %d", (int)err);
    soloud_test::armBarrier(DeviceBarrier::pullDecoding);
    std::thread feeder([hash] {
        addPullBufferDataStream(hash, gChunk.data(),
                                static_cast<unsigned int>(gChunk.size()), 0);
    });
    EXPECT(barrierReached(DeviceBarrier::pullDecoding, 3000),
           "the delivery did not reach its decode");
    setPause(voice, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT(returnsWithin(300, [voice] { (void)getPosition(voice); }),
           "the mixer waits for the delivery's decode to rewind the voice");

    soloud_test::releaseBarrier(DeviceBarrier::pullDecoding);
    feeder.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double start = 0, end = 0;
    getPullBufferTimeRange(hash, &start, &end);
    EXPECT(start < 0.01 && end > 0.5,
           "the delivered data should be kept, buffered [%.2f, %.2f]", start,
           end);
    const double position = getPosition(voice);
    EXPECT(position > 0.0 && position < 0.5,
           "the voice should play the delivered data, at %.2f", position);
    stop(voice);
    disposeSound(hash);
}

/// The mixer parks while it decodes a queued chunk. A reset meanwhile must
/// wait for it, and the stream then asks for its data from the start.
void resetDuringMixerDecode() {
    std::printf("reset: a reset while the mixer decodes\n");
    const Stream s = startStream();
    soloud_test::armBarrier(DeviceBarrier::pullDataQueued);
    soloud_test::armBarrier(DeviceBarrier::pullDecoding);
    std::thread feeder([&s] {
        addPullBufferDataStream(s.hash, gChunk.data(),
                                static_cast<unsigned int>(gChunk.size()), 0);
    });
    EXPECT(barrierReached(DeviceBarrier::pullDataQueued, 3000),
           "the delivery did not queue its data");
    EXPECT(barrierReached(DeviceBarrier::pullDecoding, 3000),
           "the mixer did not decode the queued data");
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        gRequests.clear();
    }

    auto reset = std::make_shared<std::atomic<bool>>(false);
    const unsigned int hash = s.hash;
    std::thread([hash, reset] {
        resetPullBufferStream(hash);
        reset->store(true);
    }).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (reset->load())
        deadlocked("the reset finished while the mixer was decoding "
                   "(releasing the mixer would let it write into the reset "
                   "stream)");

    soloud_test::releaseBarrier(DeviceBarrier::pullDecoding);
    soloud_test::releaseBarrier(DeviceBarrier::pullDataQueued);
    feeder.join();
    for (int i = 0; i < 600 && !reset->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT(reset->load(), "the reset did not finish after the mixer");
    bool askedForStart = false;
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        for (uint64_t offset : gRequests) askedForStart |= offset == 0;
    }
    EXPECT(askedForStart, "the reset stream should ask for data from 0");
    deliverAt(s, 0.0);
    double start = 0, end = 0;
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start < 0.01 && end > 0.5,
           "the reset stream should hold the new data from 0, buffered "
           "[%.2f, %.2f]", start, end);
    endStream(s);
}

/// The mixer parks between taking samples from the buffer and counting them
/// as read. A reset meanwhile must wait for it: counted after the reset, that
/// read would leave the empty stream claiming a block it does not have.
/// A delivery parks in its decode while the stream is reset. The voice is
/// paused, so that only the delivery can reach the barrier.
void resetDuringDecode() {
    std::printf("reset-during-decode: a reset while a delivery decodes\n");
    const Stream s = startStream();
    const unsigned int voice = s.voice;
    setPause(voice, true);
    soloud_test::armBarrier(DeviceBarrier::pullDecoding);
    std::thread feeder([&s] {
        addPullBufferDataStream(s.hash, gChunk.data(),
                                static_cast<unsigned int>(gChunk.size()),
                                gChunk.size());
    });
    EXPECT(barrierReached(DeviceBarrier::pullDecoding, 3000),
           "the delivery did not reach its decode");
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        gRequests.clear();
    }
    auto reset = std::make_shared<std::atomic<bool>>(false);
    const unsigned int hash = s.hash;
    std::thread([hash, reset] {
        resetPullBufferStream(hash);
        reset->store(true);
    }).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(!reset->load(), "the reset should wait for the delivery");
    EXPECT(returnsWithin(300, [voice] { (void)getPosition(voice); }),
           "the audio mutex is held while a reset waits for a delivery");

    soloud_test::releaseBarrier(DeviceBarrier::pullDecoding);
    feeder.join();
    for (int i = 0; i < 600 && !reset->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!reset->load())
        deadlocked("the reset did not finish after the delivery");
    bool askedForStart = false;
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        for (uint64_t offset : gRequests) askedForStart |= offset == 0;
    }
    EXPECT(askedForStart, "the reset stream should ask for data from 0");
    double start = -1, end = -1;
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start == 0.0 && end == 0.0,
           "the reset stream should be empty, buffered [%.2f, %.2f]", start,
           end);
    endStream(s);
}

void resetDuringMixerRead() {
    std::printf("read-reset: a reset while the mixer counts what it read\n");
    const Stream s = startStream();
    soloud_test::armBarrier(DeviceBarrier::pullSamplesRead);
    EXPECT(barrierReached(DeviceBarrier::pullSamplesRead, 3000),
           "the mixer did not read the buffer");

    auto reset = std::make_shared<std::atomic<bool>>(false);
    const unsigned int hash = s.hash;
    std::thread([hash, reset] {
        resetPullBufferStream(hash);
        reset->store(true);
    }).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const bool finishedWhileReading = reset->load();
    EXPECT(!finishedWhileReading,
           "the reset finished between the mixer's read and its count");

    soloud_test::releaseBarrier(DeviceBarrier::pullSamplesRead);
    for (int i = 0; i < 600 && !reset->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT(reset->load(), "the reset did not finish after the mixer");
    // The mixer holds the audio mutex until it has counted its read.
    const unsigned int voice = s.voice;
    if (!returnsWithin(3000, [voice] { (void)getPause(voice); }))
        deadlocked("the mixer did not finish its block");
    double start = -1, end = -1;
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start == 0.0 && end == 0.0,
           "the reset stream should hold nothing, buffered [%.6f, %.6f]",
           start, end);
    endStream(s);
}

/// The mixer parks between taking samples from the buffer and counting them
/// as read. The buffered range read meanwhile must still end where the
/// delivered data ends: nothing was delivered or discarded.
void rangeDuringMixerRead() {
    std::printf("range: the buffered range while the mixer counts what it read\n");
    const Stream s = startStream();
    const double delivered = static_cast<double>(kChunkFrames) / kSampleRate;
    double start = -1, end = -1;
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start == 0.0 && std::fabs(end - delivered) < 1e-9,
           "the range should be [0, %.9f] between blocks, got [%.9f, %.9f]",
           delivered, start, end);

    soloud_test::armBarrier(DeviceBarrier::pullSamplesRead);
    EXPECT(barrierReached(DeviceBarrier::pullSamplesRead, 3000),
           "the mixer did not read the buffer");
    getPullBufferTimeRange(s.hash, &start, &end);
    EXPECT(start == 0.0 && std::fabs(end - delivered) < 1e-9,
           "the range should stay [0, %.9f] while the mixer reads, got "
           "[%.9f, %.9f]", delivered, start, end);
    soloud_test::releaseBarrier(DeviceBarrier::pullSamplesRead);
    endStream(s);
}

/// A delivery parks right after it has looked the playing stream up, and the
/// stream is disposed meanwhile (all sounds when [disposeAll]). The disposal
/// must wait for the delivery: released after it, the delivery would decode
/// into the destroyed stream.
void deliveryDuringDisposal(const char *name, bool disposeAll) {
    std::printf("%s: a delivery parked after its lookup while %s runs\n", name,
                disposeAll ? "disposeAllSound()" : "disposeSound()");
    const Stream s = startStream();
    const unsigned int hash = s.hash;
    soloud_test::armBarrier(DeviceBarrier::pullDataSoundLookedUp);
    auto delivered = std::make_shared<std::atomic<int>>(-1);
    std::thread feeder([hash, delivered] {
        delivered->store(static_cast<int>(addPullBufferDataStream(
            hash, gChunk.data(), static_cast<unsigned int>(gChunk.size()),
            gChunk.size())));
    });
    EXPECT(barrierReached(DeviceBarrier::pullDataSoundLookedUp, 3000),
           "the delivery did not look the stream up");

    auto disposed = std::make_shared<std::atomic<bool>>(false);
    std::thread disposer([hash, disposeAll, disposed] {
        if (disposeAll)
            disposeAllSound();
        else
            disposeSound(hash);
        disposed->store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (disposed->load())
        deadlocked("the disposal finished while a delivery was parked between "
                   "its lookup and its use of the stream (releasing it would "
                   "decode into the destroyed stream)");

    soloud_test::releaseBarrier(DeviceBarrier::pullDataSoundLookedUp);
    feeder.join();
    disposer.join();
    EXPECT(delivered->load() == static_cast<int>(PlayerErrors::noError),
           "the parked delivery should complete before the disposal, got %d",
           delivered->load());
    EXPECT(getIsValidVoiceHandle(s.voice) == 0,
           "the disposal should have stopped the stream's voice");
    const PlayerErrors after = addPullBufferDataStream(
        hash, gChunk.data(), static_cast<unsigned int>(gChunk.size()), 0);
    EXPECT(after == PlayerErrors::soundHashNotFound,
           "a delivery after the disposal should find no stream, got %d",
           static_cast<int>(after));
}

/// A delivery to a stream nothing plays parks in its decode, so only the
/// delivery can reach the barrier. sounds_mutex must be free meanwhile, also
/// while a disposal of the stream waits for the delivery.
void lockDuringDecode() {
    std::printf("lock-during-decode: sounds_mutex while a delivery decodes\n");
    unsigned int hash = 0;
    PlayerErrors err = setPullBufferStream(
        &hash, 1u << 20, 0.5, kSampleRate, kChannels,
        static_cast<int>(BufferType::PCM_S16LE), kTotalBytes, nullptr, nullptr,
        onMoreData, nullptr);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    soloud_test::armBarrier(DeviceBarrier::pullDecoding);
    auto delivered = std::make_shared<std::atomic<int>>(-1);
    std::thread feeder([hash, delivered] {
        delivered->store(static_cast<int>(addPullBufferDataStream(
            hash, gChunk.data(), static_cast<unsigned int>(gChunk.size()), 0)));
    });
    EXPECT(barrierReached(DeviceBarrier::pullDecoding, 3000),
           "the delivery did not reach its decode");
    // countAudioSource() only looks the sound up under sounds_mutex.
    EXPECT(returnsWithin(300, [hash] { (void)countAudioSource(hash); }),
           "sounds_mutex is held while the delivery decodes");

    auto disposed = std::make_shared<std::atomic<bool>>(false);
    std::thread disposer([hash, disposed] {
        disposeSound(hash);
        disposed->store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (disposed->load())
        deadlocked("the disposal finished while a delivery was decoding into "
                   "the stream");
    EXPECT(returnsWithin(300, [hash] { (void)countAudioSource(hash); }),
           "sounds_mutex is held while a disposal waits for the delivery");

    soloud_test::releaseBarrier(DeviceBarrier::pullDecoding);
    feeder.join();
    disposer.join();
    EXPECT(delivered->load() == static_cast<int>(PlayerErrors::noError),
           "the parked delivery should complete, got %d", delivered->load());
    EXPECT(countAudioSource(hash) == 0, "the stream should be disposed");
}

/// The replayed file: 0.5 s, delivered in two halves.
constexpr double kShortSeconds = 0.5;

bool requestedFromStart() {
    std::lock_guard<std::mutex> lock(gRequestMutex);
    return std::find(gRequests.begin(), gRequests.end(), 0u) != gRequests.end();
}

bool waitForRequestFromStart(int ms) {
    for (int i = 0; i < ms / 5 && !requestedFromStart(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return requestedFromStart();
}

/// Delivers the whole short file, as two halves, and reports each result.
void deliverShortFile(const Stream &s, PlayerErrors results[2]) {
    const uint64_t half = offsetOf(kShortSeconds / 2);
    for (int i = 0; i < 2; ++i)
        results[i] = addPullBufferDataStream(s.hash, gChunk.data(),
                                             static_cast<unsigned int>(half),
                                             i * half);
}

/// A playing short stream holding its whole file. Requests made until then
/// (the first, for offset 0, comes with the stream) are dropped.
Stream startShortStream(bool looping) {
    Stream s;
    PlayerErrors err = setPullBufferStream(
        &s.hash, 1u << 20, 0.5, kSampleRate, kChannels,
        static_cast<int>(BufferType::PCM_S16LE), offsetOf(kShortSeconds),
        nullptr, nullptr, onMoreData, nullptr);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    PlayerErrors delivered[2];
    deliverShortFile(s, delivered);
    EXPECT(delivered[0] == PlayerErrors::noError &&
               delivered[1] == PlayerErrors::noError,
           "the file should be delivered: %d, %d", (int)delivered[0],
           (int)delivered[1]);
    {
        std::lock_guard<std::mutex> lock(gRequestMutex);
        gRequests.clear();
    }
    err = play(s.hash, 0, 0.05f, 0.0f, false, looping, 0.0, 0.0, -1, -1, 1.0f,
               &s.voice);
    EXPECT(err == PlayerErrors::noError && s.voice != 0, "play: %d", (int)err);
    return s;
}

/// A stream whose whole file has been delivered is sought to 0, and then a
/// looping one wraps. Each must ask for the file from 0 again and play it.
void replayFullyDelivered() {
    std::printf("replay: a fully delivered stream sought to 0, and looping\n");
    {
        const Stream s = startShortStream(/*looping*/ false);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        EXPECT(!requestedFromStart(),
               "the stream should not ask for data from 0 before the seek");
        EXPECT(seek(s.voice, 0.0f) == PlayerErrors::noError, "seek to 0");
        EXPECT(waitForRequestFromStart(500),
               "the stream sought to 0 should ask for its data from 0");
        EXPECT(getIsValidVoiceHandle(s.voice) == 1,
               "the voice sought to 0 should wait for its data, not end");
        PlayerErrors again[2];
        deliverShortFile(s, again);
        EXPECT(again[0] == PlayerErrors::noError &&
                   again[1] == PlayerErrors::noError,
               "the file should be accepted again from 0: %d, %d",
               (int)again[0], (int)again[1]);
        // It plays the 0.5 s through and ends, as it did the first time.
        bool ended = false;
        for (int i = 0; i < 400 && !ended; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ended = getIsValidVoiceHandle(s.voice) == 0;
        }
        EXPECT(ended, "the replayed voice should play to its end and stop");
        disposeSound(s.hash);
    }
    {
        const Stream s = startShortStream(/*looping*/ true);
        EXPECT(waitForRequestFromStart(1500),
               "the looping stream should ask for its data from 0 when it "
               "wraps");
        PlayerErrors again[2];
        deliverShortFile(s, again);
        EXPECT(again[0] == PlayerErrors::noError &&
                   again[1] == PlayerErrors::noError,
               "the looping stream should accept its file again: %d, %d",
               (int)again[0], (int)again[1]);
        // It plays the file round to its next wrap. Requests made before the
        // refill was decoded are dropped first.
        {
            std::lock_guard<std::mutex> lock(gRequestMutex);
            gRequests.clear();
        }
        EXPECT(waitForRequestFromStart(1500),
               "the looping stream should wrap again after its refill");
        EXPECT(getIsValidVoiceHandle(s.voice) == 1,
               "the looping voice should keep playing");
        endStream(s);
    }
}

// ------------------------------------------------------------------ stress
std::vector<unsigned char> gMp3;
std::mutex gStressMutex;
std::condition_variable gStressCv;
std::deque<uint64_t> gStressRequests;
std::atomic<bool> gStressStop{false};

void onStressData(uint64_t offset) {
    std::lock_guard<std::mutex> lock(gStressMutex);
    gStressRequests.push_back(offset);
    gStressCv.notify_all();
}

/// Plays one stream while threads feed it, seek it (to 0 too, a rewind),
/// reset it and read its buffered range for [ms]. [source] holds the encoded
/// file; empty means raw PCM, any bytes at any offset.
void stressStream(const char *what, BufferType format, unsigned int sampleRate,
                  uint64_t totalBytes, const std::vector<unsigned char> &source,
                  float maxSeek, int ms) {
    std::printf("stress: %s fed, seeked, reset and read at once\n", what);
    {
        std::lock_guard<std::mutex> lock(gStressMutex);
        gStressRequests.clear();
    }
    gStressStop.store(false);
    unsigned int hash = 0;
    PlayerErrors err = setPullBufferStream(
        &hash, 1u << 20, 0.5, sampleRate, kChannels, static_cast<int>(format),
        totalBytes, nullptr, nullptr, onStressData, nullptr);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);

    std::thread feeder([&] {
        while (!gStressStop.load()) {
            uint64_t offset = 0;
            {
                std::unique_lock<std::mutex> lock(gStressMutex);
                gStressCv.wait_for(lock, std::chrono::milliseconds(5), [] {
                    return !gStressRequests.empty() || gStressStop.load();
                });
                if (gStressRequests.empty()) continue;
                offset = gStressRequests.front();
                gStressRequests.pop_front();
            }
            if (offset >= totalBytes) continue;
            const unsigned char *data = gChunk.data();
            size_t len = gChunk.size();
            if (!source.empty()) {
                data = source.data() + offset;
                len = std::min<size_t>(32768, source.size() - offset);
            }
            len = std::min<size_t>(len, totalBytes - offset);
            addPullBufferDataStream(hash, data, static_cast<unsigned int>(len),
                                    offset);
        }
    });
    // A first chunk, as Dart sends once the stream exists.
    onStressData(0);
    unsigned int voice = 0;
    err = play(hash, 0, 0.05f, 0.0f, false, false, 0.0, 0.0, -1, -1, 1.0f,
               &voice);
    EXPECT(err == PlayerErrors::noError && voice != 0, "play: %d", (int)err);

    std::thread seeker([&] {
        unsigned int n = 0;
        while (!gStressStop.load()) {
            const float t = (n++ % 5 == 0) ? 0.0f : maxSeek * ((n * 37) % 100) / 100.0f;
            seek(voice, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(7));
        }
    });
    std::thread resetter([&] {
        while (!gStressStop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            resetPullBufferStream(hash);
        }
    });
    std::thread reader([&] {
        while (!gStressStop.load()) {
            double start = 0, end = 0;
            getPullBufferTimeRange(hash, &start, &end);
            (void)getPosition(voice);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    gStressStop.store(true);
    seeker.join();
    resetter.join();
    reader.join();
    feeder.join();
    EXPECT(returnsWithin(3000, [voice] { (void)getPause(voice); }),
           "the engine should still respond after the stress");
    stop(voice);
    disposeSound(hash);
}

void stress() {
    stressStream("a PCM stream", BufferType::PCM_S16LE, kSampleRate,
                 kTotalBytes, {}, 900.0f, 1500);
    if (gMp3.empty()) {
        std::printf("  (no MP3 file, the MP3 stream is skipped)\n");
        return;
    }
    stressStream("an MP3 stream", BufferType::AUTO, kSampleRate, gMp3.size(),
                 gMp3, 60.0f, 1500);
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

    {
        std::ifstream in("example/assets/audio/8_bit_mentality.mp3",
                         std::ios::binary);
        gMp3.assign(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
    }

    if (want("mixer-seek")) mixerCompletesSeek();
    if (want("deliver-seek")) deliveryAndSeek();
    if (want("seek")) deliveryDuringDiscard("seek", 300.0f);
    if (want("rewind")) deliveryDuringDiscard("rewind", 0.0f);
    if (want("seek-during-decode")) seekDuringDecode();
    if (want("loop-during-decode")) loopDuringDecode();
    if (want("reset")) resetDuringMixerDecode();
    if (want("reset-during-decode")) resetDuringDecode();
    if (want("read-reset")) resetDuringMixerRead();
    if (want("range")) rangeDuringMixerRead();
    if (want("dispose")) deliveryDuringDisposal("dispose", false);
    if (want("dispose-all")) deliveryDuringDisposal("dispose-all", true);
    if (want("lock-during-decode")) lockDuringDecode();
    if (want("replay")) replayFullyDelivered();
    if (want("stress")) stress();

    dispose();
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

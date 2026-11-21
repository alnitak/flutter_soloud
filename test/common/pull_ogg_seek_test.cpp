// Seeks in an Ogg (Opus or Vorbis) pull stream.
//
// A pull stream maps a seek out of its buffered range to a byte offset in the
// encoded file and asks for the data there. For Ogg the decoder keeps an index
// of page granule positions against the byte offsets it has consumed. The
// index has to keep giving offsets into the file after a seek, when the
// decoder is fed from the middle of the file:
//
//   * a seek back to before the data fed since an earlier seek must ask for
//     data inside the file, and the stream must play on from there;
//   * a seek to a time the decoder has indexed since an earlier seek must ask
//     for the data at that time in the file.
//
// Until the stream knows its duration (for Ogg it reads it from the end of
// the file, with requests of its own) it cannot map a time it has not decoded
// yet to an offset:
//
//   * a seek made meanwhile must be applied once the duration has arrived,
//     without asking for the same data over and over.
//
// The stream is driven through the exports Dart uses, on a real engine (the
// null backend), with a buffer of about one second so that both seeks leave
// it. A feeder answers each request for data from the file, as the Dart side
// does.
//
// Nothing here depends on how fast the machine is: every step waits for the
// state it needs (with a timeout that only a failing run reaches), and the
// request a seek made is told apart from one the stream was about to make
// anyway.
//
//   ./test/common/pull_ogg_seek_test.sh [--asan|--tsan] [file]
#include "bindings.h"
#include "audiobuffer/stream_decoder.h"
#include "enums.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {
constexpr int64_t kEngineId = 4017;
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

std::vector<unsigned char> gFile;
std::atomic<unsigned int> gHash{0};
/// Held by the feeder while it delivers, so a stream is disposed of only
/// between deliveries: this test is not about that race.
std::mutex gDeliveryMutex;
std::mutex gMutex;
std::condition_variable gCv;
/// A request for data, with the generation of the stream that made it.
struct Request {
    uint64_t offset;
    int generation;
};
std::deque<Request> gRequests;
std::vector<uint64_t> gLog;
std::atomic<bool> gStop{false};
/// The offset after the last chunk delivered: what the stream asks for next
/// when it only wants the data that follows. Guarded by gMutex.
uint64_t gNextSequential = 0;
/// How many more chunks the feeder may deliver, or -1 for no limit. At 0 it
/// delivers nothing and the requests wait, like a network that has stalled.
std::atomic<int> gDeliveriesLeft{-1};
/// Counts the streams, so that a request the feeder took from one stream is
/// not answered to the next. Changed with gDeliveryMutex held.
std::atomic<int> gStreamGeneration{0};

// How long to wait for something the stream is due to do. Only a failing run
// waits this long; a sanitizer build on a loaded CI runner needs seconds for
// what takes milliseconds elsewhere.
constexpr int kWaitMs = 30000;

/// The duration the stream has reported, or 0 until it has found it.
std::atomic<double> gDuration{0.0};
void onDuration(double seconds) { gDuration.store(seconds); }

void onMoreData(uint64_t offset) {
    std::lock_guard<std::mutex> lock(gMutex);
    gRequests.push_back({offset, gStreamGeneration.load()});
    gLog.push_back(offset);
    gCv.notify_all();
}

void feeder() {
    while (!gStop.load()) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(gMutex);
            // Like Dart, which gets the callback only after
            // setPullBufferStream() has returned the hash.
            gCv.wait_for(lock, std::chrono::milliseconds(20), [] {
                return (!gRequests.empty() && gHash.load() != 0 &&
                        gDeliveriesLeft.load() != 0) ||
                       gStop.load();
            });
            if (gRequests.empty() || gHash.load() == 0 ||
                gDeliveriesLeft.load() == 0)
                continue;
            request = gRequests.front();
            gRequests.pop_front();
        }
        std::lock_guard<std::mutex> delivering(gDeliveryMutex);
        const unsigned int hash = gHash.load();
        // The stream the request came from may have been replaced since.
        if (hash == 0 || request.generation != gStreamGeneration.load())
            continue;
        const uint64_t offset = request.offset;
        if (offset >= gFile.size()) continue; // nothing there to give
        const size_t len =
            std::min<size_t>(32768, gFile.size() - static_cast<size_t>(offset));
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gNextSequential = offset + len;
        }
        if (gDeliveriesLeft.load() > 0) --gDeliveriesLeft;
        addPullBufferDataStream(hash, gFile.data() + offset,
                                static_cast<unsigned int>(len), offset);
    }
}

void onVoiceEnded(unsigned int *h) { std::free(h); }
void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                  uint64_t *t) {
    (void)e; (void)n; (void)h; (void)t;
}
void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

bool waitFor(const std::function<bool()> &pred, int ms = kWaitMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

size_t logSize() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gLog.size();
}

/// The file offset of [seconds], from a decoder fed the whole file from byte 0.
uint64_t fileOffsetOf(double seconds) {
    StreamDecoder ref;
    int sr = 0, ch = 0;
    for (size_t pos = 0; pos < gFile.size(); pos += 16384) {
        std::vector<unsigned char> chunk(
            gFile.begin() + pos,
            gFile.begin() + std::min(gFile.size(), pos + 16384));
        ref.decode(chunk, &sr, &ch, nullptr, 0);
    }
    return ref.timeToByteOffset(seconds);
}

/// Starts a stream of the file and plays it. With [settled] it returns once
/// the stream knows its duration and has its buffer full: from then on it can
/// map any time to an offset, and it asks for nothing until the voice has
/// played most of the buffer.
unsigned int startStream(const char *path, bool settled = true) {
    {
        std::lock_guard<std::mutex> delivering(gDeliveryMutex);
        ++gStreamGeneration;
    }
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gRequests.clear();
        gLog.clear();
        gNextSequential = 0;
    }
    gDuration.store(0.0);
    // About one second of decoded audio, so seeks leave the buffer.
    unsigned int hash = 0;
    PlayerErrors err = setPullBufferStream(
        &hash, 44100 * 2 * 4, 0.8, 44100, 2, static_cast<int>(BufferType::AUTO),
        gFile.size(), nullptr, nullptr, onMoreData, onDuration);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    gHash.store(hash);
    unsigned int voice = 0;
    err = play(gHash, 0, 0.05f, 0.0f, false, false, 0.0, 0.0, -1, -1, 1.0f,
               &voice);
    EXPECT(err == PlayerErrors::noError && voice != 0, "play: %d", (int)err);
    double start = 0, end = 0;
    EXPECT(waitFor([&] {
               getPullBufferTimeRange(gHash, &start, &end);
               return end > 0.5;
           }),
           "%s: the stream should buffer its start", path);
    if (settled) {
        EXPECT(waitFor([&] { return gDuration.load() > 0.0; }),
               "%s: the stream should find its duration", path);
        getPullBufferTimeRange(gHash, &start, &end);
    }
    std::printf("  %s: buffered [%.2f, %.2f], duration %.2f s, before the "
                "first seek\n",
                path, start, end, gDuration.load());
    return voice;
}

/// Seeks [voice] to [target] and returns the offset the seek asked for, or
/// UINT64_MAX if it asked for nothing.
///
/// The voice is playing, so the stream may ask for the data that follows what
/// it already has just before the seek is applied (a seek that finds a
/// delivery decoding is applied by the mixer's next block). That request is
/// for the offset after the last chunk delivered, and is not the seek's.
uint64_t seekAndGetRequest(unsigned int voice, float target) {
    size_t before = 0;
    uint64_t following = 0;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        before = gLog.size();
        following = gNextSequential;
    }
    seek(voice, target);
    uint64_t requested = UINT64_MAX;
    waitFor([&] {
        std::lock_guard<std::mutex> lock(gMutex);
        for (size_t i = before; i < gLog.size(); ++i) {
            if (gLog[i] == following) continue;
            requested = gLog[i];
            return true;
        }
        return false;
    });
    return requested;
}

void endStream(unsigned int voice) {
    stop(voice);
    unsigned int hash = 0;
    {
        std::lock_guard<std::mutex> delivering(gDeliveryMutex);
        hash = gHash.exchange(0);
    }
    disposeSound(hash);
}

/// Out of the buffer to 5 s, then back to 2 s: before the data the decoder
/// has seen since the first seek.
void seekBackBeforeTheFirstSeek(const char *path) {
    std::printf("seek back before the first seek (%s)\n", path);
    const unsigned int voice = startStream(path);
    const uint64_t first = seekAndGetRequest(voice, 5.0f);
    double start = 0, end = 0;
    EXPECT(waitFor([&] {
               getPullBufferTimeRange(gHash, &start, &end);
               return start >= 4.9 && end > 5.5;
           }),
           "%s: the stream should play on from 5 s, buffered [%.2f, %.2f]",
           path, start, end);
    std::printf("  seek(5): requested from %llu, buffered [%.2f, %.2f]\n",
                (unsigned long long)first, start, end);

    const uint64_t second = seekAndGetRequest(voice, 2.0f);
    const bool backAt2 = waitFor([&] {
        getPullBufferTimeRange(gHash, &start, &end);
        return start < 3.0 && end > 2.5;
    });
    std::printf("  seek(2): requested from %llu (2 s is at %llu), buffered "
                "[%.2f, %.2f]\n",
                (unsigned long long)second,
                (unsigned long long)fileOffsetOf(2.0), start, end);
    EXPECT(second < first,
           "%s: the seek back to 2 s should request data before the 5 s seek's "
           "offset %llu, got %llu (UINT64_MAX: none requested)",
           path, (unsigned long long)first, (unsigned long long)second);
    EXPECT(backAt2, "%s: the stream should play on from 2 s, buffered "
                    "[%.2f, %.2f]", path, start, end);
    endStream(voice);
}

/// Out of the buffer to 5 s, play on, then seek back out of the buffer to a
/// time the decoder has seen since the first seek: the decoder knows where it
/// is in the file.
void seekBackIntoTheIndexedRange(const char *path) {
    std::printf("seek back into the indexed range (%s)\n", path);
    const unsigned int voice = startStream(path);
    const uint64_t first = seekAndGetRequest(voice, 5.0f);
    // The first seek's offset may be an estimate, so the data it brings may
    // be later than 5 s in the file; what the decoder indexes from there on
    // starts at that point.
    // Then play on until 6.6 s has been decoded and has left the buffer.
    const double target = 6.6;
    double start = 0, end = 0;
    EXPECT(waitFor([&] {
               getPullBufferTimeRange(gHash, &start, &end);
               return getPosition(voice) >= 7.6 && start > target + 0.1;
           }),
           "%s: the voice should play on from 5 s past 7.6 s (at %.2f, "
           "buffered [%.2f, %.2f])",
           path, getPosition(voice), start, end);
    std::printf("  seek(5): requested from %llu; now at %.2f, buffered "
                "[%.2f, %.2f]\n",
                (unsigned long long)first, getPosition(voice), start, end);
    const uint64_t expected = fileOffsetOf(target);
    const uint64_t got = seekAndGetRequest(voice, (float)target);
    std::printf("  seek(%.1f): requested from %llu (%.1f s is at %llu, the "
                "5 s seek started at %llu)\n",
                target, (unsigned long long)got, target,
                (unsigned long long)expected, (unsigned long long)first);
    EXPECT(got != UINT64_MAX && got + 4096 >= expected && got <= expected + 4096,
           "%s: seeking to %.1f s requested byte %llu; it is at byte %llu",
           path, target, (unsigned long long)got, (unsigned long long)expected);
    endStream(voice);
}

/// A seek made while the stream is still finding its duration. The first
/// chunk has been delivered and the network stalls on the next request, so
/// the stream can only map the first second to an offset; 5 s has to wait
/// for the duration. Once the network answers again the seek must be applied,
/// and the stream must not ask for one offset over and over meanwhile.
void seekBeforeTheDurationIsKnown(const char *path) {
    std::printf("seek before the duration is known (%s)\n", path);
    gDeliveriesLeft.store(1);
    const unsigned int voice = startStream(path, /*settled*/ false);
    const bool stalled = waitFor([] {
        std::lock_guard<std::mutex> lock(gMutex);
        return !gRequests.empty();
    });
    EXPECT(stalled && gDuration.load() <= 0.0,
           "%s: the stream should be waiting for more data, its duration "
           "unknown (%f)",
           path, gDuration.load());

    const size_t before = logSize();
    seek(voice, 5.0f);
    gDeliveriesLeft.store(-1);
    gCv.notify_all();

    // How often the offset asked for most since the seek was asked for.
    uint64_t repeated = 0;
    const auto mostRepeats = [&] {
        std::lock_guard<std::mutex> lock(gMutex);
        size_t most = 0;
        for (size_t i = before; i < gLog.size(); ++i) {
            size_t count = 0;
            for (size_t j = before; j < gLog.size(); ++j)
                count += gLog[j] == gLog[i];
            if (count > most) {
                most = count;
                repeated = gLog[i];
            }
        }
        return most;
    };
    double start = 0, end = 0;
    // A stream asking for the same data again and again will not get
    // anywhere: no need to wait for the timeout then.
    waitFor([&] {
        getPullBufferTimeRange(gHash, &start, &end);
        return (start >= 4.9 && end > 5.5) || mostRepeats() > 8;
    });
    const size_t repeats = mostRepeats();
    std::printf("  seek(5): buffered [%.2f, %.2f], duration %.2f s, %zu "
                "requests since the seek\n",
                start, end, gDuration.load(), logSize() - before);
    EXPECT(repeats <= 2, "%s: offset %llu was requested %zu times after the "
                         "seek", path, (unsigned long long)repeated, repeats);
    EXPECT(start >= 4.9 && end > 5.5,
           "%s: the stream should play on from 5 s once its duration is "
           "known, buffered [%.2f, %.2f]",
           path, start, end);
    endStream(voice);
}

bool loadFile(const char *path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    ++gAssertions;
    if (!in) {
        std::printf("cannot open %s\n", path);
        ++gFailures;
        return false;
    }
    gFile.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(gFile.data()),
            static_cast<std::streamsize>(gFile.size()));
    return true;
}
} // namespace

int main(int argc, char **argv) {
    std::vector<const char *> files;
    if (argc > 1) {
        files.push_back(argv[1]);
    } else {
        files.push_back("example/assets/audio/sample-OPUS.opus");
        files.push_back("example/assets/audio/sample-vorbis.ogg");
    }
    setAudioDeviceIdleTimeout(600000);
    prepareEngineInit(kEngineId);
    if (initEngine(-1, 44100, 2048, 2, 1, 0, 0) != PlayerErrors::noError) {
        std::printf("engine did not start\n");
        return 2;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);
    std::thread feed(feeder);

    for (const char *path : files) {
        if (!loadFile(path)) continue;
        seekBackBeforeTheFirstSeek(path);
        seekBackIntoTheIndexedRange(path);
        seekBeforeTheDurationIsKnown(path);
    }

    gStop.store(true);
    feed.join();
    dispose();
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

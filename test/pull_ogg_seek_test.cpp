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
// The stream is driven through the exports Dart uses, on a real engine (the
// null backend), with a buffer of about one second so that both seeks leave
// it. A feeder answers each request for data from the file, as the Dart side
// does.
//
//   ./test/run_pull_ogg_seek_test.sh [--asan] [file]
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
std::deque<uint64_t> gRequests;
std::vector<uint64_t> gLog;
std::atomic<bool> gStop{false};

void onMoreData(uint64_t offset) {
    std::lock_guard<std::mutex> lock(gMutex);
    gRequests.push_back(offset);
    gLog.push_back(offset);
    gCv.notify_all();
}

void feeder() {
    while (!gStop.load()) {
        uint64_t offset;
        {
            std::unique_lock<std::mutex> lock(gMutex);
            // Like Dart, which gets the callback only after
            // setPullBufferStream() has returned the hash.
            gCv.wait_for(lock, std::chrono::milliseconds(20), [] {
                return (!gRequests.empty() && gHash.load() != 0) ||
                       gStop.load();
            });
            if (gRequests.empty() || gHash.load() == 0) continue;
            offset = gRequests.front();
            gRequests.pop_front();
        }
        if (offset >= gFile.size()) continue; // nothing there to give
        const size_t len =
            std::min<size_t>(32768, gFile.size() - static_cast<size_t>(offset));
        std::lock_guard<std::mutex> delivering(gDeliveryMutex);
        const unsigned int hash = gHash.load();
        if (hash != 0)
            addPullBufferDataStream(hash, gFile.data() + offset,
                                    static_cast<unsigned int>(len), offset);
    }
}

void onVoiceEnded(unsigned int *h) { std::free(h); }
void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                  unsigned long long *t) {
    (void)e; (void)n; (void)h; (void)t;
}
void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

bool waitFor(const std::function<bool()> &pred, int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

unsigned int startStream(const char *path) {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gRequests.clear();
        gLog.clear();
    }
    // About one second of decoded audio, so seeks leave the buffer.
    unsigned int hash = 0;
    PlayerErrors err = setPullBufferStream(
        &hash, 44100 * 2 * 4, 0.8, 44100, 2, static_cast<int>(BufferType::AUTO),
        gFile.size(), nullptr, nullptr, onMoreData, nullptr);
    EXPECT(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    gHash.store(hash);
    unsigned int voice = 0;
    err = play(gHash, 0, 0.05f, 0.0f, false, false, 0.0, 0.0, -1, -1, 1.0f,
               &voice);
    EXPECT(err == PlayerErrors::noError && voice != 0, "play: %d", (int)err);
    double start = 0, end = 0;
    waitFor([&] {
        getPullBufferTimeRange(gHash, &start, &end);
        return end > 0.5;
    }, 5000);
    std::printf("  %s: buffered [%.2f, %.2f] before the first seek\n", path,
                start, end);
    return voice;
}

/// Seeks [voice] to [target] and returns the first offset requested after it,
/// or UINT64_MAX if none comes.
uint64_t seekAndGetRequest(unsigned int voice, float target) {
    const size_t before = logSize();
    seek(voice, target);
    waitFor([&] { return logSize() > before; }, 3000);
    std::lock_guard<std::mutex> lock(gMutex);
    return gLog.size() > before ? gLog[before] : UINT64_MAX;
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
    waitFor([&] {
        getPullBufferTimeRange(gHash, &start, &end);
        return start >= 4.9 && end > 5.5;
    }, 5000);
    std::printf("  seek(5): requested from %llu, buffered [%.2f, %.2f]\n",
                (unsigned long long)first, start, end);

    const uint64_t second = seekAndGetRequest(voice, 2.0f);
    waitFor([&] {
        getPullBufferTimeRange(gHash, &start, &end);
        return start < 3.0 && end > 2.5;
    }, 3000);
    std::printf("  seek(2): requested from %llu (2 s is at %llu), buffered "
                "[%.2f, %.2f]\n",
                (unsigned long long)second,
                (unsigned long long)fileOffsetOf(2.0), start, end);
    EXPECT(second < first,
           "%s: the seek back to 2 s should request data before the 5 s seek's "
           "offset %llu, got %llu (UINT64_MAX: none requested)",
           path, (unsigned long long)first, (unsigned long long)second);
    EXPECT(end > 2.5, "%s: the stream should play on from 2 s, buffered "
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
    waitFor([&] { return getPosition(voice) >= 7.6; }, 6000);
    double start = 0, end = 0;
    getPullBufferTimeRange(gHash, &start, &end);
    std::printf("  seek(5): requested from %llu; now at %.2f, buffered "
                "[%.2f, %.2f]\n",
                (unsigned long long)first, getPosition(voice), start, end);
    const double target = 6.6;
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
    }

    gStop.store(true);
    feed.join();
    dispose();
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

// Seeks in an MP3 pull stream.
//
// A pull stream maps a seek out of its buffered range to a byte offset in the
// encoded file and asks for the data there. For MP3 the decoder builds a seek
// table (drmp3_calculate_seek_points) from the first data it gets. MP3 frames
// carry no timestamp, so that table only maps times to file offsets when it
// was built from the start of the file:
//
//   * after a seek, a seek back must still request the offset the whole
//     file's seek map gives, not one from a table built from the data that
//     started at the earlier seek (on a variable bit rate file the two
//     differ).
//
// The stream is driven through the exports Dart uses, on a real engine (the
// null backend), with a buffer of about one second so that the seeks leave
// it. A feeder answers each request for data from the file, as the Dart side
// does, and the test mixes the engine itself as well, so playback runs
// faster than real time.
//
//   ./test/run_pull_mp3_seek_test.sh [--asan] [vbr.mp3]
//
// The runner encodes the fixture with ffmpeg: five seconds of silence, then
// ten of independent noise per channel, at VBR quality 0.
#include "bindings.h"
#include "player.h"
#include "audiobuffer/mp3_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"
#include "enums.h"
#include "soloud/src/audiosource/wav/dr_mp3.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// The engine's Player, defined in bindings.cpp.
extern "C" std::unique_ptr<Player> &player;

namespace {
constexpr int64_t kEngineId = 4019;
// The largest MPEG-1 Layer III frame at 44.1 kHz (320 kbps, padded).
constexpr uint64_t kMaxFrameBytes = 1045;
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

/// Waits for [pred], mixing a block every millisecond on top of the null
/// device's own mixing.
bool waitFor(const std::function<bool()> &pred, int ms) {
    std::vector<float> output(512 * 2);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        player->soloud.mix(output.data(), 512);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

size_t logSize() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gLog.size();
}

/// The file offset of [seconds] in a seek map of the whole file.
uint64_t fileOffsetOf(double seconds) {
    drmp3 decoder{};
    const bool opened =
        drmp3_init_memory(&decoder, gFile.data(), gFile.size(), nullptr);
    EXPECT(opened, "the whole-file reference decoder should open the file");
    if (!opened) return 0;
    drmp3_uint32 count = 4096;
    std::vector<drmp3_seek_point> points(count);
    const bool ok =
        drmp3_calculate_seek_points(&decoder, &count, points.data()) != 0;
    EXPECT(ok && count > 0, "the whole-file reference should have a seek map");
    const uint64_t frame = static_cast<uint64_t>(seconds * decoder.sampleRate);
    uint64_t offset = 0;
    for (drmp3_uint32 i = 0; ok && i < count; ++i) {
        if (points[i].pcmFrameIndex > frame) break;
        offset = points[i].seekPosInBytes;
    }
    drmp3_uninit(&decoder);
    return offset;
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

/// Seeks [voice] to [target] and returns the first offset requested after it
/// that is below [below] (the stream may still ask for data after the
/// current window first), or UINT64_MAX if none comes.
uint64_t seekAndGetRequestBelow(unsigned int voice, float target,
                                uint64_t below) {
    const size_t before = logSize();
    seek(voice, target);
    uint64_t requested = UINT64_MAX;
    waitFor([&] {
        std::lock_guard<std::mutex> lock(gMutex);
        for (size_t i = before; i < gLog.size(); ++i) {
            if (gLog[i] < below) {
                requested = gLog[i];
                return true;
            }
        }
        return false;
    }, 3000);
    return requested;
}

uint64_t distance(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

void endStream(unsigned int voice) {
    stop(voice);
    unsigned int hash = 0;
    {
        std::lock_guard<std::mutex> delivering(gDeliveryMutex);
        hash = gHash.exchange(0);
    }
    disposeSound(hash);
}

/// The decoder keeps the file's seek map across a seek, and only a full
/// cleanup discards it.
void seekMapSurvivesASeek() {
    std::printf("the seek map survives a seek\n");
    MP3DecoderWrapper indexed;
    indexed.detectedType = DetectedType::BUFFER_MP3_WITH_ID3;
    EXPECT(indexed.initializeDecoder(44100, 2),
           "the decoder should initialize");
    std::vector<unsigned char> whole = gFile;
    int rate = 44100, channels = 2;
    indexed.decode(whole, &rate, &channels, 8192);
    const uint64_t original = indexed.timeToByteOffset(0.5);
    EXPECT(original != 0, "the seek map should map 0.5 s");
    indexed.prepareForSeek(6 * 44100 * 2, fileOffsetOf(6.0));
    EXPECT(indexed.canSeekToTime(0.5) &&
               indexed.timeToByteOffset(0.5) == original,
           "after a seek, before any data, 0.5 s should still map to %llu, "
           "got %llu", (unsigned long long)original,
           (unsigned long long)indexed.timeToByteOffset(0.5));
    indexed.cleanup();
    EXPECT(!indexed.canSeekToTime(0.5), "a cleanup should discard the map");
}

/// Out of the buffer to 6 s, then back to 0.5 s, twice: the seek back must
/// request the whole file's offset for 0.5 s and play on from there.
void seekBackUsesTheFileMap(const char *path) {
    std::printf("seek back after a seek (%s)\n", path);
    const unsigned int voice = startStream(path);
    const uint64_t expected = fileOffsetOf(0.5);
    for (int pass = 0; pass < 2; ++pass) {
        const uint64_t first = seekAndGetRequest(voice, 6.0f);
        double start = 0, end = 0;
        const bool ready = waitFor([&] {
            getPullBufferTimeRange(gHash, &start, &end);
            return end > 6.8;
        }, 5000);
        EXPECT(ready, "pass %d: the stream should buffer past 6.8 s after the "
                      "seek to 6 s, buffered [%.2f, %.2f]", pass, start, end);
        const uint64_t requested = seekAndGetRequestBelow(
            voice, 0.5f, std::max<uint64_t>(first, 32768));
        std::printf("  pass %d: seek(6) requested %llu; seek(0.5) requested "
                    "%llu, the file map says %llu\n",
                    pass, (unsigned long long)first,
                    (unsigned long long)requested,
                    (unsigned long long)expected);
        // Adjacent seek points and the decoder's preroll can move the
        // offset by a few frames.
        EXPECT(expected != 0 && requested != UINT64_MAX &&
                   distance(requested, expected) <= 4 * kMaxFrameBytes,
               "pass %d: the seek back to 0.5 s requested byte %llu; the file "
               "map has it at %llu", pass, (unsigned long long)requested,
               (unsigned long long)expected);
        const bool back = waitFor([&] {
            getPullBufferTimeRange(gHash, &start, &end);
            return start < 2.0 && end > 1.0;
        }, 5000);
        EXPECT(back, "pass %d: the stream should play on from 0.5 s, "
                     "buffered [%.2f, %.2f]", pass, start, end);
    }
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
    if (argc < 2) {
        std::printf("usage: %s vbr.mp3\n", argv[0]);
        return 2;
    }
    setAudioDeviceIdleTimeout(600000);
    prepareEngineInit(kEngineId);
    if (initEngine(-1, 44100, 2048, 2, 1, 0, 0) != PlayerErrors::noError) {
        std::printf("engine did not start\n");
        return 2;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);
    std::thread feed(feeder);

    if (loadFile(argv[1])) {
        seekMapSurvivesASeek();
        seekBackUsesTheFileMap(argv[1]);
    }

    gStop.store(true);
    feed.join();
    dispose();
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

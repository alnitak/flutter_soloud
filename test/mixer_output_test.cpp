// Standalone native tests for the mixer-output capture ring (MixerOutput):
// what it keeps when the reader falls behind, what a single large write does
// and what it emits.
//
// The audio thread's side is driven by hand: each test calls onAudioData()
// with frames whose samples count up (0, 1, 2, ...), so the bytes the ring
// holds and the chunks it emits can be checked for order and for which ones
// were kept. The notification thread is the real one.
//
//   * in fixed-chunk mode every complete chunk is delivered, in order, also
//     when several are buffered before the notification thread looks;
//   * an exact fill keeps every byte, and an overrun keeps the newest bytes,
//     in order, rather than discarding the ring;
//   * a single write larger than the whole ring keeps its newest bytes and
//     writes nothing outside the ring (AddressSanitizer reports it otherwise);
//   * a surround ring keeps whole frames through a fill and an overrun.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_mixer_output_test.sh            (plain)
//   ./test/run_mixer_output_test.sh --asan     (AddressSanitizer + UBSan)
//   ./test/run_mixer_output_test.sh --tsan     (ThreadSanitizer)
//
// A scenario name after the flag runs only that scenario.

#include "mixeroutput/mixer_output.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

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

constexpr int kChannels = 2;
constexpr int kSampleRate = 48000;

MixerOutput &capture() { return MixerOutput::instance(); }

/// Feeds [frames] frames of [channels] whose samples continue the count from
/// [next].
void feed(unsigned int frames, float &next, int channels = kChannels)
{
    std::vector<float> block(static_cast<size_t>(frames) * channels);
    for (auto &sample : block)
        sample = next++;
    capture().onAudioData(block.data(), frames);
}

/// The unread bytes of the ring, in order, as floats.
std::vector<float> unread()
{
    const size_t available = capture().getAvailableBytes();
    const size_t size = capture().getBufferSize();
    const size_t read = capture().getReadOffset();
    const uint8_t *ring = capture().getBufferPointer();
    std::vector<uint8_t> bytes(available);
    for (size_t i = 0; i < available; ++i)
        bytes[i] = ring[(read + i) % size];
    std::vector<float> floats(available / sizeof(float));
    std::memcpy(floats.data(), bytes.data(), floats.size() * sizeof(float));
    return floats;
}

/// True when [samples] are exactly the last samples.size() values counted
/// before [next].
bool isNewestRun(const std::vector<float> &samples, float next)
{
    for (size_t i = 0; i < samples.size(); ++i)
        if (samples[i] != next - static_cast<float>(samples.size() - i))
            return false;
    return true;
}

template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

PlayerErrors startPcm(size_t ringBytes, size_t thresholdBytes, int chunkFrames)
{
    return capture().start(MIXER_OUTPUT_PCM_F32LE, kSampleRate, kChannels,
                           ringBytes, thresholdBytes, chunkFrames);
}

std::mutex gChunkMutex;
std::vector<std::vector<float>> gChunks; // copied when the callback runs

void copyChunk(uint8_t *data, size_t length)
{
    std::vector<float> chunk(length / sizeof(float));
    std::memcpy(chunk.data(), data, chunk.size() * sizeof(float));
    std::lock_guard<std::mutex> lock(gChunkMutex);
    gChunks.push_back(std::move(chunk));
}

size_t chunkCount()
{
    std::lock_guard<std::mutex> lock(gChunkMutex);
    return gChunks.size();
}

void everyBufferedChunkIsDelivered()
{
    std::printf("fixed chunks: every buffered chunk is delivered, in order\n");
    constexpr int kChunkFrames = 2048;
    constexpr size_t kChunkFloats = kChunkFrames * kChannels;
    {
        std::lock_guard<std::mutex> lock(gChunkMutex);
        gChunks.clear();
    }
    capture().setDataCallback(copyChunk);
    EXPECT(startPcm(1 << 20, 4096, kChunkFrames) == noError, "chunks: start");
    float next = 0;
    // Three chunks in one callback: a device period of 6144 frames, or a
    // notification thread that ran late. All three are buffered at once.
    feed(3 * kChunkFrames, next);
    EXPECT(waitFor([] { return chunkCount() >= 3; }, 2000),
           "chunks: all 3 buffered chunks must be delivered, got %zu",
           chunkCount());
    // And the stream keeps going afterwards, at the usual device block size.
    for (int block = 0; block < 4 * kChunkFrames / 512; ++block)
        feed(512, next);
    EXPECT(waitFor([] { return chunkCount() >= 7; }, 2000),
           "chunks: 4 more chunks must follow, got %zu in all", chunkCount());
    capture().setDataCallback(nullptr);
    capture().stop();
    std::lock_guard<std::mutex> lock(gChunkMutex);
    EXPECT(gChunks.size() == 7, "chunks: exactly 7 chunks, got %zu",
           gChunks.size());
    for (size_t c = 0; c < gChunks.size(); ++c)
    {
        bool inOrder = gChunks[c].size() == kChunkFloats;
        for (size_t i = 0; inOrder && i < kChunkFloats; ++i)
            inOrder = gChunks[c][i] == static_cast<float>(c * kChunkFloats + i);
        EXPECT(inOrder, "chunks: chunk %zu must hold samples %zu.. in order", c,
               c * kChunkFloats);
    }
}

// No consumer: nothing advances the read position.
void exactFillKeepsEveryByte()
{
    std::printf("an exact fill keeps the ring's bytes\n");
    capture().setDataCallback(nullptr);
    EXPECT(startPcm(4096, 4096, -1) == noError, "fill: start");
    float next = 0;
    feed(256, next); // 2048 bytes
    EXPECT(capture().getAvailableBytes() == 2048,
           "fill: half full holds 2048 bytes, got %zu",
           capture().getAvailableBytes());
    feed(256, next); // 4096 bytes: exactly the ring
    EXPECT(capture().getAvailableBytes() == 4096,
           "fill: an exactly full ring must hold its data, %zu of 4096 bytes "
           "readable",
           capture().getAvailableBytes());
    EXPECT(isNewestRun(unread(), next),
           "fill: the kept bytes must be the newest, in order");
    capture().stop();
}

void overrunKeepsTheNewestBytes()
{
    std::printf("an overrun drops only the oldest bytes\n");
    capture().setDataCallback(nullptr);
    EXPECT(startPcm(4096, 4096, -1) == noError, "overrun: start");
    float next = 0;
    feed(128, next); // 1024 bytes
    feed(448, next); // 3584 more: 512 past the ring
    EXPECT(capture().getAvailableBytes() == 4096,
           "overrun: a ring overrun by 512 bytes must stay full, %zu of 4096 "
           "bytes readable",
           capture().getAvailableBytes());
    EXPECT(isNewestRun(unread(), next),
           "overrun: the kept bytes must be the newest, in order");
    for (int block = 0; block < 5; ++block)
        feed(300, next); // keeps overrunning by uneven amounts
    EXPECT(capture().getAvailableBytes() == 4096,
           "overrun: repeated overruns must leave the ring full, %zu of 4096 "
           "bytes readable",
           capture().getAvailableBytes());
    EXPECT(isNewestRun(unread(), next),
           "overrun: repeated overruns must keep the newest bytes");
    capture().stop();
}

// Under AddressSanitizer a write outside the ring aborts the run here.
void oversizedWriteStaysInsideTheRing()
{
    std::printf("a write larger than the ring keeps its newest bytes\n");
    capture().setDataCallback(nullptr);
    EXPECT(startPcm(4096, 4096, -1) == noError, "oversized: start");
    float next = 0;
    feed(16, next);   // move the write position off 0
    feed(2048, next); // 16384 bytes into a 4096-byte ring
    EXPECT(capture().getAvailableBytes() == 4096,
           "oversized: the ring must be full after the write, %zu of 4096 "
           "bytes readable",
           capture().getAvailableBytes());
    EXPECT(isNewestRun(unread(), next),
           "oversized: the kept bytes must be the write's last ones");
    capture().stop();
}

// A 1 MiB default is not a whole number of 6-channel F32 frames (24 bytes).
// The ring rounds down to whole frames, and every fill, wrap and overrun has
// to keep frame boundaries where they were.
void surroundRingKeepsWholeFrames()
{
    std::printf("a surround ring keeps whole frames\n");
    constexpr int channels = 6;
    constexpr size_t requestedBytes = 1024 * 1024;
    constexpr size_t frameBytes = channels * sizeof(float);
    capture().setDataCallback(nullptr);
    const PlayerErrors result = capture().start(
        MIXER_OUTPUT_PCM_F32LE, kSampleRate, channels, requestedBytes, 4096, -1);
    EXPECT(result == noError, "surround: the 1 MiB default must start");
    if (result != noError)
        return;
    const size_t capacity = capture().getBufferSize();
    EXPECT(capacity == requestedBytes - requestedBytes % frameBytes,
           "surround: the ring must round down to whole frames, got %zu",
           capacity);
    float next = 0;
    feed(static_cast<unsigned int>(capacity / frameBytes), next, channels);
    EXPECT(capture().getAvailableBytes() == capacity,
           "surround: an exact fill must keep the whole ring, %zu of %zu "
           "bytes readable",
           capture().getAvailableBytes(), capacity);
    EXPECT(isNewestRun(unread(), next),
           "surround: a fill must keep the frames in order");
    feed(13, next, channels);
    EXPECT(capture().getAvailableBytes() == capacity,
           "surround: an overrun must keep a full ring, %zu of %zu bytes "
           "readable",
           capture().getAvailableBytes(), capacity);
    EXPECT(capture().getReadOffset() % frameBytes == 0,
           "surround: an overrun must leave the read position on a frame "
           "boundary");
    EXPECT(isNewestRun(unread(), next),
           "surround: an overrun must keep the newest frames in order");
    capture().stop();

    EXPECT(capture().start(MIXER_OUTPUT_PCM_F32LE, kSampleRate, channels,
                           frameBytes - 1, 1, -1) == invalidParameter,
           "surround: a ring smaller than one frame must be refused");
    EXPECT(capture().start(MIXER_OUTPUT_PCM_F32LE, kSampleRate, channels,
                           2048 * frameBytes - 1, 4096, 2048) ==
               invalidParameter,
           "surround: a chunk must fit the rounded ring");
    for (int count : {3, 6})
    {
        for (auto format : {MIXER_OUTPUT_PCM_S8, MIXER_OUTPUT_PCM_S16LE})
        {
            const size_t bytesPerFrame =
                count * (format == MIXER_OUTPUT_PCM_S8 ? 1 : 2);
            EXPECT(capture().start(format, kSampleRate, count, requestedBytes,
                                   4096, -1) == noError,
                   "surround: an integer PCM default must start (%d channels)",
                   count);
            EXPECT(capture().getBufferSize() ==
                       requestedBytes - requestedBytes % bytesPerFrame,
                   "surround: an integer PCM ring must round to whole frames");
            capture().stop();
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const char *const kScenarios[] = {"chunks", "fill", "overrun", "oversized",
                                      "surround"};
    if (only != nullptr)
    {
        bool known = false;
        for (const char *name : kScenarios)
            known = known || std::strcmp(only, name) == 0;
        if (!known)
        {
            std::fprintf(stderr, "Unknown scenario '%s'.\n", only);
            return 2;
        }
    }
    auto wanted = [&](const char *name)
    { return only == nullptr || std::strcmp(only, name) == 0; };

    if (wanted("chunks"))
        everyBufferedChunkIsDelivered();
    if (wanted("fill"))
        exactFillKeepsEveryByte();
    if (wanted("overrun"))
        overrunKeepsTheNewestBytes();
    if (wanted("oversized"))
        oversizedWriteStaysInsideTheRing();
    if (wanted("surround"))
        surroundRingKeepsWholeFrames();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

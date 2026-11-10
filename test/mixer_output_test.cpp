// Standalone native tests for the mixer-output capture ring (MixerOutput).
//
// The audio thread's side is driven by hand: each test calls onAudioData()
// with frames whose samples count up (0, 1, 2, ...), so the bytes the ring
// holds and the chunks it emits can be checked for order and for which ones
// were kept. The notification thread is the real one.
//
//   * in fixed-chunk mode every complete chunk is delivered, in order, also
//     when several are buffered before the notification thread looks.
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

} // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const char *const kScenarios[] = {"chunks"};
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

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

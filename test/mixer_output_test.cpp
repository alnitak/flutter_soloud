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
//   * a surround ring keeps whole frames through a fill and an overrun;
//   * a notification handled late -- after the next chunk, or after a restart
//     with a larger ring -- still gets the bytes it was sent for; a packet is
//     claimed once, a discarded one not at all, unclaimed packets stay
//     bounded, and claiming a threshold packet acknowledges its bytes in its
//     own capture's ring only.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_mixer_output_test.sh            (plain)
//   ./test/run_mixer_output_test.sh --asan     (AddressSanitizer + UBSan)
//   ./test/run_mixer_output_test.sh --tsan     (ThreadSanitizer)
//
// --web-model after the checker flag builds the web path instead (inline
// notifications, the packet pool) on the host. A scenario name after the
// flags runs only that scenario.

#include "mixeroutput/mixer_output.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
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
constexpr size_t kBytesPerFrame = kChannels * sizeof(float);

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

/// The bytes a consumer gets for a notification when it handles it. With
/// owned packets a notification carries a token, claimed here; before them it
/// carried an address into capture storage, read here as a consumer did.
template <typename Capture>
auto claimNotice(uint8_t *data, size_t, int)
    -> decltype(Capture::takePacket(data, std::declval<uint32_t &>(), nullptr),
                std::vector<uint8_t>())
{
    uint32_t captureId = 0;
    size_t length = 0;
    uint8_t *bytes = Capture::takePacket(data, captureId, &length);
    std::vector<uint8_t> copy;
    if (bytes != nullptr)
        copy.assign(bytes, bytes + length);
    std::free(bytes);
    return copy;
}

template <typename Capture>
std::vector<uint8_t> claimNotice(uint8_t *data, size_t length, long)
{
    return std::vector<uint8_t>(data, data + length);
}

std::vector<uint8_t> claim(uint8_t *data, size_t length)
{
    return claimNotice<MixerOutput>(data, length, 0);
}

std::vector<float> asFloats(const std::vector<uint8_t> &bytes)
{
    std::vector<float> floats(bytes.size() / sizeof(float));
    std::memcpy(floats.data(), bytes.data(), floats.size() * sizeof(float));
    return floats;
}

std::mutex gChunkMutex;
std::vector<std::vector<float>> gChunks; // claimed when the callback runs

void copyChunk(uint8_t *data, size_t length)
{
    std::vector<float> chunk = asFloats(claim(data, length));
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

std::mutex gNoticeMutex;
std::vector<std::pair<uint8_t *, size_t>> gNotices; // handled later

void keepNotice(uint8_t *data, size_t length)
{
    std::lock_guard<std::mutex> lock(gNoticeMutex);
    gNotices.emplace_back(data, length);
}

size_t noticeCount()
{
    std::lock_guard<std::mutex> lock(gNoticeMutex);
    return gNotices.size();
}

std::pair<uint8_t *, size_t> notice(size_t index)
{
    std::lock_guard<std::mutex> lock(gNoticeMutex);
    return gNotices[index];
}

void clearNotices()
{
    std::lock_guard<std::mutex> lock(gNoticeMutex);
    gNotices.clear();
}

/// True when [samples] count up from [first].
bool countsFrom(const std::vector<float> &samples, float first)
{
    for (size_t i = 0; i < samples.size(); ++i)
        if (samples[i] != first + static_cast<float>(i))
            return false;
    return !samples.empty();
}

// A Dart listener runs on its isolate's event loop, after the notification:
// here, after the next chunk has been emitted.
void lateClaimGetsItsOwnChunk()
{
    std::printf("deferred: a chunk handled after the next one is emitted is "
                "still its own\n");
    constexpr int kChunkFrames = 2048;
    constexpr size_t kChunkFloats = kChunkFrames * kChannels;
    clearNotices();
    capture().setDataCallback(keepNotice);
    EXPECT(startPcm(1 << 20, 4096, kChunkFrames) == noError,
           "deferred: start");
    float next = 0;
    feed(2 * kChunkFrames, next);
    EXPECT(waitFor([] { return noticeCount() >= 2; }, 2000),
           "deferred: two chunks must be notified, got %zu", noticeCount());
    if (noticeCount() >= 2)
    {
        const std::vector<float> first =
            asFloats(claim(notice(0).first, notice(0).second));
        const std::vector<float> second =
            asFloats(claim(notice(1).first, notice(1).second));
        EXPECT(first.size() == kChunkFloats && countsFrom(first, 0),
               "deferred: the first chunk must hold samples 0.., it starts "
               "at %.0f",
               first.empty() ? -1.0 : static_cast<double>(first[0]));
        EXPECT(second.size() == kChunkFloats &&
                   countsFrom(second, static_cast<float>(kChunkFloats)),
               "deferred: the second chunk must hold samples %zu..",
               kChunkFloats);
    }
    capture().setDataCallback(nullptr);
    capture().stop();
}

// The listener's turn comes after the capture was stopped and restarted with
// a larger ring, which frees the storage the first capture notified from.
// AddressSanitizer reports reading it as a heap-use-after-free.
void lateClaimSurvivesARestart()
{
    std::printf("restart: a packet handled after a restart with a larger ring "
                "is still its own\n");
    clearNotices();
    capture().setDataCallback(keepNotice);
    EXPECT(startPcm(64 * 1024, 4096, -1) == noError, "restart: start");
    float next = 0;
    feed(1024, next); // 8192 bytes, over the threshold
    EXPECT(waitFor([] { return noticeCount() >= 1; }, 2000),
           "restart: the threshold must be notified");
    capture().setDataCallback(nullptr);
    capture().stop();
    EXPECT(startPcm(4 << 20, 4096, -1) == noError,
           "restart: a larger capture starts");
    float later = 0;
    feed(512, later); // 4096 bytes of the new capture
    if (noticeCount() >= 1)
    {
        const std::vector<float> packet =
            asFloats(claim(notice(0).first, notice(0).second));
        EXPECT(packet.size() == 1024 * kChannels && countsFrom(packet, 0),
               "restart: the packet must hold the first capture's %d "
               "samples, got %zu",
               1024 * kChannels, packet.size());
        EXPECT(capture().getAvailableBytes() == 4096,
               "restart: a stale packet must not acknowledge the new "
               "capture's bytes, %zu of 4096 left",
               capture().getAvailableBytes());
    }
    capture().stop();
}

/// Token rules, which only owned packets have.
template <typename Capture>
auto checkTokens(int) -> decltype(Capture::discardPackets(), void())
{
    // A packet is claimed once, and a discarded one not at all.
    clearNotices();
    capture().setDataCallback(keepNotice);
    EXPECT(startPcm(1 << 20, 4096, 2048) == noError, "tokens: start");
    float next = 0;
    feed(2 * 2048, next);
    EXPECT(waitFor([] { return noticeCount() >= 2; }, 2000),
           "tokens: two chunks must be notified");
    capture().setDataCallback(nullptr);
    capture().stop();
    if (noticeCount() >= 2)
    {
        uint32_t id = 0;
        size_t length = 0;
        uint8_t *bytes = Capture::takePacket(notice(0).first, id, &length);
        EXPECT(bytes != nullptr && length == 2048 * kBytesPerFrame &&
                   id == capture().captureId(),
               "tokens: a claim returns the packet, its size and its capture");
        std::free(bytes);
        EXPECT(Capture::takePacket(notice(0).first, id, &length) == nullptr &&
                   length == 0,
               "tokens: a packet can be claimed only once");
        Capture::discardPackets();
        EXPECT(Capture::takePacket(notice(1).first, id, &length) == nullptr,
               "tokens: a discarded packet cannot be claimed");
    }

    // A consumer that stopped claiming gets a bounded number of packets; the
    // rest of the data waits in the ring, where an overrun drops the oldest.
    clearNotices();
    capture().setDataCallback(keepNotice);
    EXPECT(startPcm(1 << 20, 4096, 2048) == noError, "tokens: restart");
    for (int chunk = 0; chunk < 100; ++chunk)
        feed(2048, next);
    waitFor([] { return noticeCount() >= 100; }, 300);
    const size_t pending = noticeCount();
    EXPECT(pending >= 8 && pending <= 64,
           "tokens: unclaimed packets must stay bounded, got %zu", pending);
    capture().setDataCallback(nullptr);
    capture().stop();
    Capture::discardPackets();

    // Claiming a threshold packet -- whole surround frames, at most 64 KiB --
    // acknowledges its bytes, which re-arms the notification.
    clearNotices();
    capture().setDataCallback(keepNotice);
    constexpr int channels = 6;
    EXPECT(capture().start(MIXER_OUTPUT_PCM_F32LE, kSampleRate, channels,
                           1 << 20, 4096, -1) == noError,
           "tokens: a surround threshold capture starts");
    next = 0;
    feed(4096, next, channels); // 98304 bytes
    EXPECT(waitFor([] { return noticeCount() >= 1; }, 2000),
           "tokens: the threshold must be notified");
    if (noticeCount() >= 1)
    {
        const size_t length = notice(0).second;
        EXPECT(length % (channels * sizeof(float)) == 0 && length > 0 &&
                   length <= 64 * 1024,
               "tokens: a threshold packet is whole frames, at most 64 KiB, "
               "got %zu bytes",
               length);
        EXPECT(capture().getAvailableBytes() == 98304,
               "tokens: an unclaimed packet's bytes stay in the ring");
        uint32_t id = 0;
        size_t claimed = 0;
        std::free(Capture::takePacket(notice(0).first, id, &claimed));
        EXPECT(claimed == length &&
                   capture().getAvailableBytes() == 98304 - length,
               "tokens: claiming acknowledges the packet's bytes, %zu left",
               capture().getAvailableBytes());
        // The web path notifies from the audio callback: give it one.
        feed(16, next, channels);
        EXPECT(waitFor([] { return noticeCount() >= 2; }, 2000),
               "tokens: the acknowledgement re-arms the notification");
    }
    capture().setDataCallback(nullptr);
    capture().stop();
    Capture::discardPackets();
}

template <typename Capture>
void checkTokens(long)
{
    EXPECT(false, "tokens: notifications carry no owned packets");
}

void packetsAreClaimedOnce()
{
    std::printf("tokens: packets are claimed once, bounded and acknowledged\n");
    checkTokens<MixerOutput>(0);
}

} // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const char *const kScenarios[] = {"chunks",   "fill",     "overrun",
                                      "oversized", "surround", "deferred",
                                      "restart",  "tokens"};
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
    if (wanted("deferred"))
        lateClaimGetsItsOwnChunk();
    if (wanted("restart"))
        lateClaimSurvivesARestart();
    if (wanted("tokens"))
        packetsAreClaimedOnce();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

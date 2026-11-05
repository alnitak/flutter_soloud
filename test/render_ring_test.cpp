// Standalone native tests for the render-ahead ring (Phase 1 of the
// retroactive re-mixing feature, see OPTION_B_RETROACTIVE_REMIX_PLAN.md).
//
// Part A unit-tests the RenderRing SPSC buffer itself: roundtrip, wraparound,
// underrun, write clamping and the rewind/rewrite primitives the retroactive
// re-mix (Phase 3) is built on.
//
// Part B drives a real Soloud engine on the null backend (no audio hardware
// needed) and verifies the engine-side integration: configuration plumbing,
// ring allocation in postinit_internal, top-up mixing, and the playhead/latency
// time model. The null backend has no device callback, so the test performs
// the callback's duties (renderRingTopUp_internal + ring read) by hand -- the
// exact same calls the miniaudio callback makes.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_render_ring_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_render_ring.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kQuantum = 1024;      // engine mix quantum (bufferSize)
constexpr unsigned int kRenderAhead = 4096;  // render-ahead depth in frames
constexpr unsigned int kDevicePeriod = 512;  // small device period in frames

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

// ---- Part A: RenderRing unit tests ---------------------------------------

void testRingRoundtrip()
{
    SoLoud::RenderRing ring;
    ring.init(64, 2);
    EXPECT(ring.isInited(), "ring should be inited");
    EXPECT(ring.getCapacity() == 64, "capacity 64, got %u", ring.getCapacity());
    EXPECT(ring.availableToRead() == 0, "empty ring has nothing to read");
    EXPECT(ring.availableToWrite() == 64, "empty ring fully writable");

    std::vector<float> in(64 * 2);
    for (unsigned int i = 0; i < 64 * 2; ++i)
        in[i] = static_cast<float>(i);
    EXPECT(ring.write(in.data(), 64) == 64, "write of 64 frames short");
    EXPECT(ring.availableToRead() == 64, "64 frames available");
    EXPECT(ring.availableToWrite() == 0, "ring full");

    std::vector<float> out(64 * 2, 0.0f);
    EXPECT(ring.read(out.data(), 64) == 64, "read of 64 frames short");
    EXPECT(out == in, "roundtrip data mismatch");
    EXPECT(ring.availableToRead() == 0, "ring drained");
}

void testRingWraparound()
{
    SoLoud::RenderRing ring;
    ring.init(64, 1);

    // Push 40, pop 32, push 40 again: the second write wraps the ring.
    std::vector<float> chunk(40);
    std::vector<float> out(40);
    for (unsigned int i = 0; i < 40; ++i)
        chunk[i] = static_cast<float>(i);
    ring.write(chunk.data(), 40);

    std::vector<float> sink(32);
    EXPECT(ring.read(sink.data(), 32) == 32, "first partial read short");

    for (unsigned int i = 0; i < 40; ++i)
        chunk[i] = static_cast<float>(100 + i);
    EXPECT(ring.write(chunk.data(), 40) == 40, "wrapping write short");
    EXPECT(ring.availableToRead() == 48, "48 frames after wrap, got %u",
           ring.availableToRead());

    EXPECT(ring.read(out.data(), 40) == 40, "wrapped read short");
    // First 8 frames are the tail of the first chunk, then the second chunk.
    bool ok = true;
    for (unsigned int i = 0; i < 8; ++i)
        ok = ok && out[i] == static_cast<float>(32 + i);
    for (unsigned int i = 0; i < 32; ++i)
        ok = ok && out[8 + i] == static_cast<float>(100 + i);
    EXPECT(ok, "wrapped read data mismatch");
}

void testRingUnderrunAndClamp()
{
    SoLoud::RenderRing ring;
    ring.init(16, 1);

    std::vector<float> in(16, 1.0f);
    // Over-capacity writes are clamped, never corrupting.
    EXPECT(ring.write(in.data(), 32) == 16, "over-capacity write not clamped");
    std::vector<float> out(16, 0.0f);
    // Under-available reads return a short count for silence-filling.
    EXPECT(ring.read(out.data(), 16) == 16, "drain short");
    EXPECT(ring.read(out.data(), 16) == 0, "empty read should return 0");
}

void testRingRewriteAndRewind()
{
    SoLoud::RenderRing ring;
    ring.init(64, 1);

    std::vector<float> in(64);
    for (unsigned int i = 0; i < 64; ++i)
        in[i] = 1.0f;
    ring.write(in.data(), 64);

    // The device consumes 10 frames; everything from the read head on is
    // still rewritable.
    std::vector<float> sink(10);
    ring.read(sink.data(), 10);
    const unsigned long long readHead = ring.getReadPosition();

    // Rewrite frames [readHead + 4, readHead + 8) as seen by future reads.
    std::vector<float> patch(4, 9.0f);
    ring.rewrite(readHead + 4, patch.data(), 4);

    // Rewind the write head and overwrite the tail.
    ring.rewindWrite(readHead + 16);
    std::vector<float> tail(8, 7.0f);
    ring.write(tail.data(), 8);

    std::vector<float> out(24);
    EXPECT(ring.read(out.data(), 24) == 24, "rewrite read short");
    bool ok = true;
    for (unsigned int i = 0; i < 4; ++i)
        ok = ok && out[i] == 1.0f;                  // untouched
    for (unsigned int i = 4; i < 8; ++i)
        ok = ok && out[i] == 9.0f;                  // rewritten patch
    for (unsigned int i = 8; i < 16; ++i)
        ok = ok && out[i] == 1.0f;                  // untouched
    for (unsigned int i = 16; i < 24; ++i)
        ok = ok && out[i] == 7.0f;                  // rewound + rewritten tail
    EXPECT(ok, "rewrite/rewind data mismatch");
}

// A retroactive rewrite runs on an API thread while the device callback
// reads the ring with no lock. Every frame carries its position and the
// generation that wrote it in both channels; a frame copied while it was being
// overwritten would mix two generations. Run under -fsanitize=thread to have
// the data race itself reported.
void testRingConcurrentRewrite()
{
    constexpr unsigned int kCap = 2048;
    constexpr unsigned int kPeriod = 64;
    SoLoud::RenderRing ring;
    ring.init(kCap, 2);

    auto encode = [](unsigned long long pos, unsigned int gen, float *frame) {
        const float v = static_cast<float>((pos % 4096) * 1024 + (gen % 1024));
        frame[0] = v;
        frame[1] = v;
    };

    std::atomic<bool> stop{false};
    std::atomic<unsigned long long> framesRead{0};
    std::atomic<unsigned long long> badFrames{0};
    std::atomic<unsigned long long> shortReads{0};

    std::thread reader([&] {
        std::vector<float> out(kPeriod * 2);
        unsigned long long pos = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            const unsigned int got = ring.read(out.data(), kPeriod);
            if (got < kPeriod)
                shortReads.fetch_add(1, std::memory_order_relaxed);
            for (unsigned int f = 0; f < got; ++f, ++pos) {
                const float a = out[f * 2];
                const float b = out[f * 2 + 1];
                const unsigned long long v = static_cast<unsigned long long>(a);
                if (a != b || v / 1024 != pos % 4096)
                    badFrames.fetch_add(1, std::memory_order_relaxed);
            }
            framesRead.fetch_add(got, std::memory_order_relaxed);
        }
    });

    std::vector<float> block(kPeriod * 4 * 2);
    unsigned int gen = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    unsigned long long rewrites = 0;
    while (std::chrono::steady_clock::now() < until) {
        // Append whole blocks of generation 0 while there is room.
        const unsigned int blockFrames = kPeriod * 4;
        if (ring.availableToWrite() >= blockFrames) {
            const unsigned long long w = ring.getWritePosition();
            for (unsigned int f = 0; f < blockFrames; ++f)
                encode(w + f, 0, &block[f * 2]);
            ring.write(block.data(), blockFrames);
        }
        // Rewrite everything not yet consumed with a new generation, one
        // block at a time, exactly as retroactiveEnd_internal does.
        ++gen;
        unsigned long long pos = ring.getReadPosition();
        const unsigned long long w = ring.getWritePosition();
        while (pos < w) {
            const unsigned long long end = pos + blockFrames < w ? pos + blockFrames : w;
            const unsigned long long from = ring.beginRewrite(pos);
            if (from < end) {
                const unsigned int n = static_cast<unsigned int>(end - from);
                for (unsigned int f = 0; f < n; ++f)
                    encode(from + f, gen, &block[f * 2]);
                ring.rewrite(from, block.data(), n);
            }
            ring.endRewrite();
            pos = end;
        }
        ++rewrites;
    }
    stop.store(true);
    reader.join();

    EXPECT(framesRead.load() > kCap * 4, "reader made too little progress (%llu frames)",
           static_cast<unsigned long long>(framesRead.load()));
    EXPECT(rewrites > 100, "writer made too few rewrite passes (%llu)", rewrites);
    EXPECT(badFrames.load() == 0, "%llu frames were torn or misplaced",
           static_cast<unsigned long long>(badFrames.load()));
}

// The reader stops at a rewrite's fence instead of copying frames that are
// being overwritten, and resumes when the rewrite ends.
void testRingReadStopsAtFence()
{
    SoLoud::RenderRing ring;
    ring.init(64, 1);
    std::vector<float> in(32, 1.0f);
    ring.write(in.data(), 32);

    // A rewrite from frame 8 on is in progress: the reader gets frames 0-7
    // only (after its short wait) and keeps the rest for the next read.
    EXPECT(ring.beginRewrite(8) == 8, "idle reader must not move the rewrite start");
    std::vector<float> out(32, 0.0f);
    const unsigned int got = ring.read(out.data(), 16, 50);
    EXPECT(got == 8, "read crossed the rewrite fence (%u frames)", got);
    std::vector<float> patch(24, 5.0f);
    ring.rewrite(8, patch.data(), 24);
    ring.endRewrite();
    EXPECT(ring.read(out.data(), 24) == 24, "read after the rewrite came up short");
    bool ok = true;
    for (unsigned int i = 0; i < 24; ++i)
        ok = ok && out[i] == 5.0f;
    EXPECT(ok, "frames after the fence must come from the rewrite");

    // A rewrite that starts behind what the reader already consumed is moved
    // up to the read head.
    ring.write(in.data(), 16);
    std::vector<float> sink(4);
    ring.read(sink.data(), 4);
    const unsigned long long head = ring.getReadPosition();
    EXPECT(ring.beginRewrite(head - 2) == head, "rewrite start must not precede the read head");
    ring.endRewrite();
}

// ---- Part B: engine integration on the null backend -----------------------

// Emits a constant 0.5 on every frame. (An impulse at frame 0 would be
// squashed by the engine's click-removal volume ramp, which starts every
// voice at volume 0.)
class DcSourceInstance final : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        for (unsigned int channel = 0; channel < mChannels; ++channel) {
            std::fill(buffer + channel * bufferSize,
                      buffer + channel * bufferSize + samplesToRead, 0.5f);
        }
        return samplesToRead;
    }

    bool hasEnded() override { return false; }

    SoLoud::result rewind() override {
        mStreamPosition = 0;
        return SoLoud::SO_NO_ERROR;
    }
};

class DcSource final : public SoLoud::AudioSource {
public:
    DcSource() {
        mBaseSamplerate = static_cast<float>(kSampleRate);
        mChannels = 1;
    }

    SoLoud::AudioSourceInstance* createInstance() override {
        return new DcSourceInstance();
    }
};

void testEngineFeatureOff()
{
    SoLoud::Soloud engine;
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);

    EXPECT(!engine.isRenderAheadEnabled(), "ring must default to disabled");
    EXPECT(!engine.mRenderRing.isInited(), "ring must not be allocated");

    DcSource source;
    engine.play(source);
    std::vector<float> out(kQuantum * 2);
    engine.mix(out.data(), kQuantum);

    // Without the ring the playhead is the mix clock.
    EXPECT(engine.getPlayheadTime() == engine.getEngineTime(),
           "playhead must equal engine time when disabled");
    EXPECT(engine.getOutputLatency() == 0, "latency must be 0 when disabled");
    EXPECT(engine.getEngineTime() > 0, "engine time must advance");

    engine.deinit();
}

void testEngineFeatureOn()
{
    SoLoud::Soloud engine;
    engine.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);

    EXPECT(engine.isRenderAheadEnabled(), "ring must be enabled");
    EXPECT(engine.mRenderRing.isInited(), "ring must be allocated");
    // The engine quantum stays the configured buffer size, decoupled from
    // the device period.
    EXPECT(engine.getBackendBufferSize() == kQuantum,
           "engine quantum must be the configured buffer size, got %u",
           engine.getBackendBufferSize());
    EXPECT(engine.mRenderRing.getCapacity() ==
               kRenderAhead + kQuantum + 4 * kDevicePeriod,
           "unexpected ring capacity %u", engine.mRenderRing.getCapacity());

    DcSource source;
    engine.play(source);

    // Drive the device callback's duties by hand: top up, then consume one
    // device period.
    engine.renderRingTopUp_internal();
    const unsigned int available = engine.mRenderRing.availableToRead();
    EXPECT(available >= kRenderAhead,
           "top-up must reach the render-ahead depth, got %u", available);
    EXPECT(available % kQuantum == 0,
           "top-up must mix whole quanta, got %u", available);

    // The mix clock advanced by exactly the mixed frames (float accumulation
    // in mStreamTime, hence the 1e-6 tolerance).
    const double engineTime = engine.getEngineTime();
    const double expectedTime =
        static_cast<double>(available) / kSampleRate;
    EXPECT(std::fabs(engineTime - expectedTime) < 1e-6,
           "engine time %f != mixed frames %f", engineTime, expectedTime);

    // Playhead = mix clock - ring depth; nothing consumed yet, so the
    // playhead is still at 0 (the whole mix is ahead of the device).
    EXPECT(engine.getPlayheadTime() == 0,
           "playhead must be 0 before any consumption, got %f",
           engine.getPlayheadTime());
    EXPECT(engine.getOutputLatency() > 0, "latency must be positive");

    // Consume one device period, like the miniaudio callback would.
    std::vector<float> period(kDevicePeriod * 2, 0.0f);
    const unsigned int got = engine.mRenderRing.read(period.data(), kDevicePeriod);
    EXPECT(got == kDevicePeriod, "device period read short: %u", got);

    // The voice must be audible inside the first device period the device
    // reads (after the click-removal ramp).
    bool audible = false;
    for (unsigned int i = 0; i < kDevicePeriod * 2; ++i)
        audible = audible || period[i] != 0.0f;
    EXPECT(audible, "voice must be audible in the first ring period");

    // The playhead advanced by exactly one device period.
    const double playhead = engine.getPlayheadTime();
    const double expectedPlayhead =
        static_cast<double>(kDevicePeriod) / kSampleRate;
    EXPECT(std::fabs(playhead - expectedPlayhead) < 1e-6,
           "playhead %f != one device period %f", playhead, expectedPlayhead);

    // Steady state: consume until the top-up refills the ring.
    engine.renderRingTopUp_internal();
    EXPECT(engine.mRenderRing.availableToRead() >= kRenderAhead,
           "steady-state top-up must maintain the render-ahead depth");

    engine.deinit();
}

} // namespace

// The device callback must keep playing what the ring holds while an API
// thread holds the audio mutex (a long re-mix, a slow call): with the render
// worker it only copies frames out. A paced fake device thread calls the same
// entry point the miniaudio callback uses. Without a worker (the null backend
// default, and the fallback) the callback mixes itself and waits for the
// mutex, which is the control case.
unsigned long long framesServedWhileMutexHeld(bool withWorker, bool &workerRan)
{
    SoLoud::Soloud engine;
    engine.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
    engine.mRenderWorkerWanted = withWorker; // what a device backend sets
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);
    workerRan = engine.isRenderWorkerRunning_internal();

    DcSource source;
    engine.play(source);
    engine.renderRingTopUp_internal();

    std::atomic<bool> stop{false};
    std::atomic<unsigned long long> fromRing{0};
    const auto period = std::chrono::microseconds(
        1000000ull * kDevicePeriod / kSampleRate);
    std::thread device([&] {
        std::vector<float> buf(kDevicePeriod * 2);
        auto next = std::chrono::steady_clock::now();
        while (!stop.load()) {
            fromRing += engine.readRenderRingForDevice_internal(buf.data(), kDevicePeriod);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });
    // Let the device and the worker settle, then hold the mutex for 60 ms
    // (less than the 93 ms the ring holds).
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.lockAudioMutex_internal();
    const unsigned long long before = fromRing.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const unsigned long long during = fromRing.load() - before;
    engine.unlockAudioMutex_internal();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop = true;
    device.join();
    engine.stopAll();
    engine.deinit();
    EXPECT(!engine.isRenderWorkerRunning_internal(), "deinit() must stop the worker");
    return during;
}

void testRenderWorkerKeepsDeviceFed()
{
    bool workerRan = false;
    const unsigned long long withWorker = framesServedWhileMutexHeld(true, workerRan);
    EXPECT(workerRan, "the render worker did not start");
    // 60 ms is ~5 device periods; allow for scheduling jitter.
    EXPECT(withWorker >= 3 * kDevicePeriod,
           "device got %llu frames while the audio mutex was held (want >= %u)",
           withWorker, 3 * kDevicePeriod);
    const unsigned long long without = framesServedWhileMutexHeld(false, workerRan);
    EXPECT(!workerRan, "no worker was requested");
    // Without a worker each top-up mixes whole quanta, so the ring can sit up
    // to kQuantum - 1 frames above kRenderAhead; reads that keep it at or
    // above kRenderAhead need no lock. That allows at most kQuantum frames
    // (two 512-frame reads) before the callback blocks on the mutex.
    EXPECT(without <= kQuantum,
           "control: without a worker the callback should wait for the mutex (%llu frames)",
           without);
    std::printf("  device frames served during a 60 ms audio-mutex hold: %llu with the "
                "render worker, %llu without\n", withWorker, without);
}

int main()
{
    testRingRoundtrip();
    testRingWraparound();
    testRingUnderrunAndClamp();
    testRingRewriteAndRewind();
    testRingConcurrentRewrite();
    testRingReadStopsAtFence();
    testEngineFeatureOff();
    testEngineFeatureOn();
    testRenderWorkerKeepsDeviceFed();

    std::fprintf(stderr, "\n%d assertions, %d failures\n", gAssertions,
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}

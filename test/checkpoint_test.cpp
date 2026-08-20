// Standalone native test for mix checkpoints (Phase 2 of the retroactive
// re-mixing feature, see OPTION_B_RETROACTIVE_REMIX_PLAN.md).
//
// The cornerstone bit-exactness test: play several voices with different
// samplerates and active faders, mix N quanta, roll the engine back to the
// checkpoint at a mid-window quantum boundary, re-mix, and require
// BIT-IDENTICAL output. Any mutable state missing from the snapshot
// (faders, resampler ping-pong blocks, click-ramp volumes, source
// consumption state, ...) shows up as a memcmp divergence.
//
// Deliberately out of scope (Phase 3 concerns): no voice ends inside the
// rolled-back window (ended-voice reconciliation), and no voice is created
// or stopped between capture and restore.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_checkpoint_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_checkpoint.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kQuantum = 1024;      // engine mix quantum (bufferSize)
constexpr unsigned int kRenderAhead = 4096;  // render-ahead depth in frames
constexpr unsigned int kDevicePeriod = 512;  // small device period in frames
constexpr unsigned int kQuanta = 8;          // quanta mixed before the rollback
constexpr unsigned int kRollbackQuantum = 3; // re-mix starts at this quantum

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

// ---- Deterministic test source --------------------------------------------

// A decaying sine whose only mutable state is its oscillator phase and
// amplitude. getAudio() is a pure function of that state, so a correct
// capture/restore reproduces the exact same samples.
class SineSourceInstance final : public SoLoud::AudioSourceInstance {
public:
    struct SineSnapshot final : SoLoud::SourceStateSnapshot {
        double mPhase;
        float mAmp;
    };

    double mPhase = 0.0;
    double mPhaseStep = 0.0;
    float mAmp = 0.5f;
    float mDecay = 0.9995f;

    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        for (unsigned int ch = 0; ch < mChannels; ++ch) {
            double phase = mPhase;
            float amp = mAmp;
            for (unsigned int i = 0; i < samplesToRead; ++i) {
                buffer[i + ch * bufferSize] =
                    static_cast<float>(std::sin(phase)) * amp;
                phase += mPhaseStep;
                amp *= mDecay;
            }
            // Keep the per-channel evolution identical for multi-channel
            // sources; only the first channel's state is authoritative.
            if (ch == 0) {
                mPhase = phase;
                mAmp = amp;
            }
        }
        return samplesToRead;
    }

    // Never ends: ended-voice reconciliation across a rollback is Phase 3.
    bool hasEnded() override { return false; }

    SoLoud::result rewind() override {
        mPhase = 0.0;
        mAmp = 0.5f;
        mStreamPosition = 0;
        return SoLoud::SO_NO_ERROR;
    }

    SoLoud::SourceStateSnapshot* captureSourceState() override {
        auto* snap = new SineSnapshot;
        snap->mPhase = mPhase;
        snap->mAmp = mAmp;
        return snap;
    }

    void restoreSourceState(SoLoud::SourceStateSnapshot* aSnapshot) override {
        auto* snap = static_cast<SineSnapshot*>(aSnapshot);
        mPhase = snap->mPhase;
        mAmp = snap->mAmp;
    }
};

class SineSource final : public SoLoud::AudioSource {
public:
    explicit SineSource(float aSamplerate, float aFrequency) {
        mBaseSamplerate = aSamplerate;
        mChannels = 1;
        mFrequency = aFrequency;
    }

    SoLoud::AudioSourceInstance* createInstance() override {
        auto* instance = new SineSourceInstance();
        instance->mPhaseStep =
            2.0 * M_PI * mFrequency / static_cast<double>(mBaseSamplerate);
        return instance;
    }

    float mFrequency;
};

// ---- Tests -----------------------------------------------------------------

// Without the render-ahead ring no checkpoints may be captured at all.
void testNoCaptureWhenDisabled()
{
    SoLoud::Soloud engine;
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);

    SineSource source(static_cast<float>(kSampleRate), 440.0f);
    engine.play(source);
    std::vector<float> out(kQuantum * 2);
    engine.mix(out.data(), kQuantum);
    engine.mix(out.data(), kQuantum);

    EXPECT(engine.mCheckpointPool.empty(),
           "checkpoint pool must stay empty when the ring is disabled");
    EXPECT(engine.findCheckpointAtOrBefore_internal(engine.getEngineTime()) ==
               -1,
           "find must return -1 when the ring is disabled");
    EXPECT(!engine.restoreMixCheckpoint_internal(0),
           "restore must fail when the ring is disabled");

    engine.deinit();
}

void testBitExactRemix()
{
    SoLoud::Soloud engine;
    engine.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);
    EXPECT(engine.isRenderAheadEnabled(), "ring must be enabled");

    const unsigned int expectedPool =
        (kRenderAhead + kQuantum - 1) / kQuantum + 2;
    EXPECT(engine.mCheckpointPool.size() == expectedPool,
           "pool size %zu != expected %u", engine.mCheckpointPool.size(),
           expectedPool);

    // Four voices: plain at base samplerate, one below the engine rate to
    // engage the resampler ping-pong/leftover state, one with a volume fader
    // and one with a pan fader crossing the rollback window.
    SineSource baseSource(static_cast<float>(kSampleRate), 440.0f);
    SineSource slowSource(22050.0f, 330.0f);
    SineSource volSource(static_cast<float>(kSampleRate), 550.0f);
    SineSource panSource(48000.0f, 220.0f);

    engine.play(baseSource);
    engine.play(slowSource);
    SoLoud::handle volHandle = engine.play(volSource);
    SoLoud::handle panHandle = engine.play(panSource);

    // Faders start at engine time 0 and run past the rollback point.
    const SoLoud::time fadeLen =
        6.0 * kQuantum / static_cast<double>(kSampleRate);
    engine.fadeVolume(volHandle, 0.1f, fadeLen);
    engine.fadePan(panHandle, 0.9f, fadeLen);

    // Mix kQuanta quanta, saving each quantum's interleaved output.
    std::vector<std::vector<float>> original(kQuanta,
                                             std::vector<float>(kQuantum * 2));
    SoLoud::time rollbackTime = 0;
    for (unsigned int q = 0; q < kQuanta; ++q) {
        engine.mix(original[q].data(), kQuantum);
        if (q + 1 == kRollbackQuantum)
            rollbackTime = engine.getEngineTime();
    }
    const SoLoud::time endTime = engine.getEngineTime();

    // Roll back to the checkpoint at the start of quantum kRollbackQuantum.
    const int poolIndex =
        engine.findCheckpointAtOrBefore_internal(rollbackTime);
    EXPECT(poolIndex >= 0, "no checkpoint found at rollback time %f",
           static_cast<double>(rollbackTime));
    if (poolIndex >= 0) {
        EXPECT(engine.mCheckpointPool[poolIndex].mTime == rollbackTime,
               "checkpoint time %f != rollback time %f",
               static_cast<double>(engine.mCheckpointPool[poolIndex].mTime),
               static_cast<double>(rollbackTime));
        EXPECT(engine.restoreMixCheckpoint_internal(poolIndex),
               "restore failed");
    }

    // Re-mix the remaining quanta into fresh buffers and compare bit-exactly.
    std::vector<std::vector<float>> remixed(
        kQuanta - kRollbackQuantum, std::vector<float>(kQuantum * 2));
    for (unsigned int q = kRollbackQuantum; q < kQuanta; ++q) {
        engine.mix(remixed[q - kRollbackQuantum].data(), kQuantum);
        const std::vector<float>& expected = original[q];
        const std::vector<float>& actual = remixed[q - kRollbackQuantum];
        if (std::memcmp(expected.data(), actual.data(),
                        kQuantum * 2 * sizeof(float)) != 0) {
            unsigned int first = 0;
            while (first < kQuantum * 2 && expected[first] == actual[first])
                ++first;
            EXPECT(false,
                   "quantum %u diverges at sample %u: %a != %a", q, first,
                   static_cast<double>(expected[first]),
                   static_cast<double>(actual[first]));
        }
    }

    // The re-mix must reproduce the mix clock exactly, too.
    EXPECT(engine.getEngineTime() == endTime,
           "engine time %f != original %f after re-mix",
           static_cast<double>(engine.getEngineTime()),
           static_cast<double>(endTime));

    engine.deinit();
}

void testFindAndRestoreEdgeCases()
{
    SoLoud::Soloud engine;
    engine.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);

    SineSource source(static_cast<float>(kSampleRate), 440.0f);
    engine.play(source);

    // Out-of-range and garbage indices must fail gracefully.
    EXPECT(!engine.restoreMixCheckpoint_internal(-1), "restore(-1) must fail");
    EXPECT(!engine.restoreMixCheckpoint_internal(99999),
           "restore(huge) must fail");
    EXPECT(engine.findCheckpointAtOrBefore_internal(0) == -1,
           "find on an empty pool must return -1");

    // Mix more quanta than the pool holds, so the oldest checkpoint is
    // overwritten.
    std::vector<float> out(kQuantum * 2);
    SoLoud::time firstTime = 0;
    for (unsigned int q = 0; q < kQuanta; ++q) {
        engine.mix(out.data(), kQuantum);
        if (q == 0)
            firstTime = engine.getEngineTime();
    }

    // A time before the oldest retained checkpoint yields -1 (the pool holds
    // 6 of the 8 captured checkpoints, so the first one is long gone).
    EXPECT(engine.findCheckpointAtOrBefore_internal(firstTime) == -1,
           "find at an overwritten checkpoint time must return -1");

    // The oldest retained checkpoint is findable and restorable.
    const int oldest =
        engine.findCheckpointAtOrBefore_internal(engine.getEngineTime());
    EXPECT(oldest >= 0, "oldest retained checkpoint must be found");
    if (oldest >= 0)
        EXPECT(engine.restoreMixCheckpoint_internal(oldest),
               "restore of the oldest retained checkpoint must succeed");

    engine.deinit();
}

} // namespace

int main()
{
    testNoCaptureWhenDisabled();
    testBitExactRemix();
    testFindAndRestoreEdgeCases();

    std::fprintf(stderr, "\n%d assertions, %d failures\n", gAssertions,
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}

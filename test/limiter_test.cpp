// Standalone correctness tests for the look-ahead brick-wall limiter.
//
// Build & run from the flutter_soloud repo root:
//
//   c++ -std=c++17 -O2 -Wall \
//       -I src/soloud/include \
//       -o /tmp/limiter_test \
//       test/limiter_test.cpp src/filters/limiter.cpp \
//   && /tmp/limiter_test
//
// Or use the convenience wrapper:
//
//   ./test/run_limiter_test.sh
//
// Tests live in this file. SoLoud base classes are stubbed below so the
// limiter can be exercised without linking the full SoLoud engine.

#include "../src/filters/limiter.h"
#include "../src/soloud/include/soloud_filter.h"
#include "../src/soloud/include/soloud_fader.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ---- SoLoud stubs ---------------------------------------------------------
// Minimal symbol implementations so the linker is happy without pulling in
// the rest of the SoLoud engine. None of these stubs are exercised by the
// limiter beyond what's noted.

namespace SoLoud {

Fader::Fader() {
    mFrom = 0; mTo = 0; mDelta = 0;
    mTime = 0; mStartTime = 0; mEndTime = 0;
    mCurrent = 0; mActive = 0;
}
void Fader::setLFO(float, float, time, time) {}
void Fader::set(float, float, time, time) {}
float Fader::get(time) { return mCurrent; }

FilterInstance::FilterInstance() {
    mNumParams = 0;
    mParamChanged = 0;
    mParam = nullptr;
    mParamFader = nullptr;
}
FilterInstance::~FilterInstance() {
    delete[] mParam;
    delete[] mParamFader;
}
result FilterInstance::initParams(int aNumParams) {
    mNumParams = (unsigned int)aNumParams;
    mParam = new float[aNumParams];
    mParamFader = new Fader[aNumParams];
    for (int i = 0; i < aNumParams; ++i) mParam[i] = 0.0f;
    return 0;
}
void FilterInstance::updateParams(time) {}
void FilterInstance::filter(float*, unsigned int, unsigned int, unsigned int, float, time) {}
void FilterInstance::filterChannel(float*, unsigned int, float, time, unsigned int, unsigned int) {}
float FilterInstance::getFilterParameter(unsigned int aId) { return mParam[aId]; }
void FilterInstance::setFilterParameter(unsigned int aId, float aValue) { mParam[aId] = aValue; }
void FilterInstance::fadeFilterParameter(unsigned int, float, time, time) {}
void FilterInstance::oscillateFilterParameter(unsigned int, float, float, time, time) {}

Filter::Filter() {}
Filter::~Filter() {}
int Filter::getParamCount() { return 0; }
const char* Filter::getParamName(unsigned int) { return ""; }
unsigned int Filter::getParamType(unsigned int) { return FLOAT_PARAM; }
float Filter::getParamMax(unsigned int) { return 0; }
float Filter::getParamMin(unsigned int) { return 0; }

} // namespace SoLoud

// ---- Test harness ---------------------------------------------------------

static int g_failures = 0;
static int g_assertions = 0;

#define EXPECT(cond, fmt, ...) do { \
    g_assertions++; \
    if (!(cond)) { \
        g_failures++; \
        std::fprintf(stderr, "  FAIL [%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

constexpr float SAMPLERATE = 48000.0f;

// Build a fresh limiter with maximizer-style defaults.
struct LimiterRig {
    Limiter parent;
    LimiterInstance* inst;
    float ceilingDb;
    float ceilingLin;

    explicit LimiterRig(
        float wet = 1.0f,
        float thresholdDb = -3.0f,
        float ceilingDb = -1.0f,
        float kneeDb = 6.0f,
        float releaseMs = 50.0f,
        float lookaheadMs = 1.0f
    ) : parent((unsigned int)SAMPLERATE) {
        parent.mWet = wet;
        parent.mThreshold = thresholdDb;
        parent.mOutputCeiling = ceilingDb;
        parent.mKneeWidth = kneeDb;
        parent.mReleaseTime = releaseMs;
        parent.mAttackTime = lookaheadMs;
        inst = static_cast<LimiterInstance*>(parent.createInstance());
        this->ceilingDb = ceilingDb;
        this->ceilingLin = std::pow(10.0f, ceilingDb / 20.0f);
    }
    ~LimiterRig() { delete inst; }

    // Buffers are planar: channel ch lives at [ch*frames, ch*frames + frames).
    void run(float* buf, unsigned int frames, unsigned int channels) {
        inst->filter(buf, frames, frames, channels, SAMPLERATE, 0.0);
    }
};

// Helpers to read/write planar stereo buffers in the test.
static inline float& planar(std::vector<float>& buf, unsigned int frames,
                            unsigned int frameIdx, unsigned int ch) {
    return buf[(size_t)ch * frames + frameIdx];
}
static inline float planarRead(const std::vector<float>& buf, unsigned int frames,
                               unsigned int frameIdx, unsigned int ch) {
    return buf[(size_t)ch * frames + frameIdx];
}

// Allow a tiny float tolerance above the ceiling for cumulative FP error.
constexpr float CEILING_EPS = 1e-5f;

static float maxAbs(const float* buf, size_t n) {
    float m = 0.f;
    for (size_t i = 0; i < n; ++i) {
        float a = std::fabs(buf[i]);
        if (a > m) m = a;
    }
    return m;
}

// ---- Tests ----------------------------------------------------------------

// 1. Ceiling MUST hold for a brutal one-sample full-scale impulse (the case
//    the original SoLoud limiter leaks badly because of attack smoothing).
static void testCeilingHonoredOnImpulse() {
    std::printf("Test: ceiling honored on full-scale impulse\n");
    LimiterRig rig;
    constexpr unsigned int CHANNELS = 2;
    constexpr unsigned int FRAMES = 4096;
    std::vector<float> buf(FRAMES * CHANNELS, 0.0f);
    // Full-scale spike on both channels at frame 100 (planar).
    planar(buf, FRAMES, 100, 0) = 1.0f;
    planar(buf, FRAMES, 100, 1) = 1.0f;

    rig.run(buf.data(), FRAMES, CHANNELS);

    float peak = maxAbs(buf.data(), buf.size());
    EXPECT(peak <= rig.ceilingLin + CEILING_EPS,
           "peak %.6f > ceiling %.6f", peak, rig.ceilingLin);
    std::printf("    peak after limiter: %.6f (ceiling %.6f)\n", peak, rig.ceilingLin);
}

// 2. Sustained over-ceiling sine — output peak must stay at or below ceiling
//    once the lookahead window has filled.
static void testCeilingHonoredOnLoudSine() {
    std::printf("Test: ceiling honored on +6 dB sustained sine\n");
    LimiterRig rig;
    constexpr unsigned int CHANNELS = 2;
    constexpr unsigned int FRAMES = (unsigned int)(SAMPLERATE * 0.2f); // 200 ms
    std::vector<float> buf(FRAMES * CHANNELS);
    const float freq = 1000.0f;
    const float amp = 2.0f; // +6 dB above 0 dBFS
    for (unsigned int i = 0; i < FRAMES; ++i) {
        float v = amp * std::sin(2.0f * (float)M_PI * freq * (float)i / SAMPLERATE);
        planar(buf, FRAMES, i, 0) = v;
        planar(buf, FRAMES, i, 1) = v;
    }

    rig.run(buf.data(), FRAMES, CHANNELS);

    // Skip the lookahead priming region when measuring.
    const size_t startFrame = 200;
    float peakL = 0.f, peakR = 0.f;
    for (size_t i = startFrame; i < FRAMES; ++i) {
        peakL = std::max(peakL, std::fabs(planarRead(buf, FRAMES, i, 0)));
        peakR = std::max(peakR, std::fabs(planarRead(buf, FRAMES, i, 1)));
    }
    float peak = std::max(peakL, peakR);
    EXPECT(peak <= rig.ceilingLin + CEILING_EPS,
           "peak %.6f > ceiling %.6f", peak, rig.ceilingLin);
    std::printf("    steady-state peak: %.6f (ceiling %.6f)\n", peak, rig.ceilingLin);
}

// 3. Quiet input below the output ceiling should pass through with maximizer
//    drive (after the lookahead delay), without extra gain reduction.
static void testQuietGetsMaximizerDrive() {
    std::printf("Test: -20 dB sine gets threshold autogain\n");
    LimiterRig rig;
    constexpr unsigned int CHANNELS = 2;
    constexpr unsigned int FRAMES = (unsigned int)(SAMPLERATE * 0.05f); // 50 ms
    std::vector<float> in(FRAMES * CHANNELS);
    const float freq = 1000.0f;
    const float amp = 0.1f; // -20 dB, well below -6 dB threshold knee
    for (unsigned int i = 0; i < FRAMES; ++i) {
        float v = amp * std::sin(2.0f * (float)M_PI * freq * (float)i / SAMPLERATE);
        planar(in, FRAMES, i, 0) = v;
        planar(in, FRAMES, i, 1) = v;
    }
    std::vector<float> buf = in;
    rig.run(buf.data(), FRAMES, CHANNELS);

    const float driveLin = std::pow(10.0f, -rig.parent.mThreshold / 20.0f);

    // Internal delay = ringSize - 1 = lookaheadSamples - 1 samples. After
    // priming, output[i] should equal driven input[i - delay] sample-accurately.
    const unsigned int lookahead = (unsigned int)(1.0f * 0.001f * SAMPLERATE + 0.5f);
    const unsigned int delay = lookahead - 1;
    float maxErr = 0.f;
    for (unsigned int i = delay; i < FRAMES; ++i) {
        for (unsigned int c = 0; c < CHANNELS; ++c) {
            float err = std::fabs(planarRead(buf, FRAMES, i, c) -
                                  planarRead(in, FRAMES, i - delay, c) * driveLin);
            if (err > maxErr) maxErr = err;
        }
    }
    EXPECT(maxErr < 1e-6f, "quiet signal not driven cleanly, max sample error = %.3e", maxErr);
    std::printf("    max sample error vs delayed driven input: %.3e\n", maxErr);
}

// 4. Stereo-linked gain — asymmetric L/R input should preserve the L:R
//    amplitude ratio. The original limiter tracked L and R independently
//    and broke the stereo image on transients.
static void testStereoLinked() {
    std::printf("Test: stereo image preserved under limiting\n");
    LimiterRig rig;
    constexpr unsigned int CHANNELS = 2;
    constexpr unsigned int FRAMES = (unsigned int)(SAMPLERATE * 0.1f);
    std::vector<float> buf(FRAMES * CHANNELS);
    const float ampL = 1.5f; // over ceiling
    const float ampR = 0.5f; // under ceiling
    const float freq = 1000.0f;
    for (unsigned int i = 0; i < FRAMES; ++i) {
        float t = 2.0f * (float)M_PI * freq * (float)i / SAMPLERATE;
        planar(buf, FRAMES, i, 0) = ampL * std::sin(t);
        planar(buf, FRAMES, i, 1) = ampR * std::sin(t);
    }
    rig.run(buf.data(), FRAMES, CHANNELS);

    // After settling, the two channels should still maintain their input
    // amplitude ratio (3:1) within float tolerance.
    const size_t startFrame = 200;
    float peakL = 0.f, peakR = 0.f;
    for (size_t i = startFrame; i < FRAMES; ++i) {
        peakL = std::max(peakL, std::fabs(planarRead(buf, FRAMES, i, 0)));
        peakR = std::max(peakR, std::fabs(planarRead(buf, FRAMES, i, 1)));
    }
    float ratio = peakL / std::max(peakR, 1e-9f);
    float expectedRatio = ampL / ampR; // 3.0
    float ratioErr = std::fabs(ratio - expectedRatio) / expectedRatio;
    EXPECT(ratioErr < 0.02f,
           "stereo ratio drifted: got %.4f, expected %.4f (%.2f%% error)",
           ratio, expectedRatio, ratioErr * 100.f);
    EXPECT(peakL <= rig.ceilingLin + CEILING_EPS,
           "L peak %.6f > ceiling %.6f", peakL, rig.ceilingLin);
    std::printf("    L peak %.4f, R peak %.4f, ratio %.4f (expected %.4f)\n",
                peakL, peakR, ratio, expectedRatio);
}

// 5. Random over-ceiling noise across many parameter combinations — fuzz
//    the limiter to verify the ceiling guarantee under arbitrary settings.
static void testCeilingHonoredUnderFuzz() {
    std::printf("Test: ceiling honored across fuzzed parameter combos\n");
    constexpr unsigned int CHANNELS = 2;
    constexpr unsigned int FRAMES = 8192;
    std::srand(0xC0FFEE);

    struct Params { float threshold; float ceiling; float knee; float release; float attack; };
    Params combos[] = {
        {-12.0f, -1.0f,  6.0f,  50.0f,   1.0f},
        { -6.0f, -0.1f,  2.0f, 200.0f,   5.0f},
        { -3.0f, -0.5f,  0.0f,   1.0f,   0.5f},
        {-24.0f, -3.0f, 12.0f, 500.0f,  10.0f},
        {-30.0f, -6.0f, 20.0f, 100.0f,   2.0f},
        { -1.0f, -0.05f, 0.5f,  20.0f, 100.0f},
    };

    for (const auto& p : combos) {
        LimiterRig rig(1.0f, p.threshold, p.ceiling, p.knee, p.release, p.attack);
        std::vector<float> buf(FRAMES * CHANNELS);
        for (unsigned int i = 0; i < FRAMES * CHANNELS; ++i) {
            buf[i] = 4.0f * ((float)std::rand() / (float)RAND_MAX - 0.5f); // [-2, 2]
        }
        rig.run(buf.data(), FRAMES, CHANNELS);

        // Skip the lookahead window for the peak measurement (planar: skip
        // the first `look` frames in each channel slab).
        const unsigned int look = std::max(2u,
            (unsigned int)(p.attack * 0.001f * SAMPLERATE + 0.5f));
        float peak = 0.f;
        for (unsigned int c = 0; c < CHANNELS; ++c) {
            for (unsigned int i = look; i < FRAMES; ++i) {
                float a = std::fabs(planarRead(buf, FRAMES, i, c));
                if (a > peak) peak = a;
            }
        }
        EXPECT(peak <= rig.ceilingLin + CEILING_EPS,
               "thr=%.1f ceil=%.2f knee=%.1f rel=%.0f atk=%.1f -> peak %.6f > ceil %.6f",
               p.threshold, p.ceiling, p.knee, p.release, p.attack, peak, rig.ceilingLin);
    }
    std::printf("    %zu combos passed\n", sizeof(combos)/sizeof(combos[0]));
}

// 6. Gain ramps DOWN before a peak instead of catching up after.
//    Plant a single over-ceiling spike on a steady sub-ceiling tone and
//    verify (a) the spike emerges at exactly the ceiling and (b) the
//    samples emitted in the look-ahead window before it show clear
//    pre-emptive gain reduction — i.e. the limiter started ramping the
//    gain down BEFORE the peak arrived, not after.
static void testLookaheadRampPrecedesPeak() {
    std::printf("Test: gain ramps DOWN before the peak (true look-ahead)\n");
    // With -1 dB threshold, the maximizer applies +1 dB drive before limiting.
    LimiterRig rig(1.0f, -1.0f, -1.0f, 0.0f, 50.0f, 2.0f /*2 ms look-ahead*/);
    constexpr unsigned int CHANNELS = 1;
    constexpr unsigned int FRAMES = 4096;
    const unsigned int lookahead = (unsigned int)(2.0f * 0.001f * SAMPLERATE + 0.5f);
    const unsigned int delay = lookahead - 1;
    const unsigned int spikeIdx = 1000;
    std::vector<float> buf(FRAMES * CHANNELS, 0.5f); // sustained tone (under ceiling)
    buf[spikeIdx] = 4.0f;

    rig.run(buf.data(), FRAMES, CHANNELS);

    const unsigned int outSpikeIdx = spikeIdx + delay;
    EXPECT(outSpikeIdx < FRAMES, "test setup error: out-of-range spike");

    // The spike at the output should land at (or just below) ceiling.
    float spikeOut = std::fabs(buf[outSpikeIdx]);
    EXPECT(spikeOut <= rig.ceilingLin + CEILING_EPS,
           "spike at output exceeded ceiling: %.6f > %.6f", spikeOut, rig.ceilingLin);
    EXPECT(spikeOut > 0.85f * rig.ceilingLin,
           "spike fell well below ceiling — limiter overreacting? %.6f vs %.6f",
           spikeOut, rig.ceilingLin);

    // Sample emitted RIGHT before the spike: driven tone attenuated by the
    // ramp. Required gain at the spike is ceiling/(4 dB driven by +1 dB).
    float preSpike = std::fabs(buf[outSpikeIdx - 1]);
    EXPECT(preSpike < 0.45f,
           "no pre-emptive gain reduction before peak: pre-spike sample = %.4f", preSpike);

    // And a sample well before the look-ahead window started attenuating:
    // should still be the tone with maximizer drive applied.
    const unsigned int farBefore = outSpikeIdx - lookahead - 200;
    float farBeforeSample = std::fabs(buf[farBefore]);
    const float drivenTone = 0.5f * std::pow(10.0f, 1.0f / 20.0f);
    EXPECT(std::fabs(farBeforeSample - drivenTone) < 1e-5f,
           "tone was attenuated outside the lookahead window: %.4f", farBeforeSample);

    std::printf("    far-before %.4f, pre-spike %.4f, spike %.4f (ceiling %.4f)\n",
                farBeforeSample, preSpike, spikeOut, rig.ceilingLin);
}

// 7. Regression test for the planar-vs-interleaved buffer layout bug.
//    SoLoud passes audio in PLANAR layout: channel ch occupies the contiguous
//    range [ch*aBufferSize, ch*aBufferSize + aSamples). If the limiter
//    indexes the buffer as interleaved, samples get scrambled across
//    channels and the output is heavy distortion.
//
//    To catch this: feed two completely different signals into L and R
//    (1 kHz sine into L, 7 kHz sine into R, both below the output ceiling
//    after drive) and verify each output channel matches its OWN delayed,
//    driven input — no cross-channel bleed.
static void testPlanarLayoutNoCrossChannelBleed() {
    std::printf("Test: planar buffer layout — no cross-channel bleed\n");
    LimiterRig rig;
    constexpr unsigned int CHANNELS = 2;
    constexpr unsigned int FRAMES = 2048;
    std::vector<float> in(FRAMES * CHANNELS, 0.0f);
    const float ampL = 0.1f, freqL = 1000.0f; // L: 1 kHz quiet
    const float ampR = 0.1f, freqR = 7000.0f; // R: 7 kHz quiet (well below thr)
    for (unsigned int i = 0; i < FRAMES; ++i) {
        planar(in, FRAMES, i, 0) = ampL * std::sin(2.0f * (float)M_PI * freqL * (float)i / SAMPLERATE);
        planar(in, FRAMES, i, 1) = ampR * std::sin(2.0f * (float)M_PI * freqR * (float)i / SAMPLERATE);
    }
    std::vector<float> buf = in;
    rig.run(buf.data(), FRAMES, CHANNELS);

    const unsigned int delay = (unsigned int)(1.0f * 0.001f * SAMPLERATE + 0.5f) - 1;
    const float driveLin = std::pow(10.0f, -rig.parent.mThreshold / 20.0f);
    float maxErrL = 0.f, maxErrR = 0.f;
    for (unsigned int i = delay; i < FRAMES; ++i) {
        maxErrL = std::max(maxErrL, std::fabs(planarRead(buf, FRAMES, i, 0) -
                                              planarRead(in, FRAMES, i - delay, 0) * driveLin));
        maxErrR = std::max(maxErrR, std::fabs(planarRead(buf, FRAMES, i, 1) -
                                              planarRead(in, FRAMES, i - delay, 1) * driveLin));
    }
    EXPECT(maxErrL < 1e-5f, "L channel corrupted, err = %.3e", maxErrL);
    EXPECT(maxErrR < 1e-5f, "R channel corrupted, err = %.3e", maxErrR);
    std::printf("    max err L=%.3e, R=%.3e (both below ceiling, expect driven identity)\n",
                maxErrL, maxErrR);
}

// ---- main -----------------------------------------------------------------

int main() {
    std::printf("=== flutter_soloud limiter correctness tests ===\n\n");

    testCeilingHonoredOnImpulse();
    testCeilingHonoredOnLoudSine();
    testQuietGetsMaximizerDrive();
    testStereoLinked();
    testCeilingHonoredUnderFuzz();
    testLookaheadRampPrecedesPeak();
    testPlanarLayoutNoCrossChannelBleed();

    std::printf("\n%d/%d assertions passed, %d failures\n",
                g_assertions - g_failures, g_assertions, g_failures);
    return g_failures == 0 ? 0 : 1;
}

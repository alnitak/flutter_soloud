// Standalone DSP correctness tests for the sine ring / amplitude modulator.
//
// Build & run from the flutter_soloud repo root:
//
//   ./test/run_amplitude_modulator_test.sh
//
// Unlike limiter_test.cpp, SoLoud's real FilterInstance/Fader implementations
// (soloud_filter.cpp, soloud_fader.cpp) are linked in, so updateParams(),
// fadeFilterParameter() and oscillateFilterParameter() are exercised exactly
// as they run inside the engine.

#include "../src/filters/amplitude_modulator_filter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

constexpr double PI = 3.141592653589793238462643383279502884;

struct Rig {
    AmplitudeModulator parent;
    AmplitudeModulatorInstance *inst;

    explicit Rig(float wet = 1.0f, float freq = 440.0f) {
        parent.mWet = wet;
        parent.mFrequency = freq;
        inst = static_cast<AmplitudeModulatorInstance *>(parent.createInstance());
    }
    ~Rig() { delete inst; }

    // Buffers are planar: channel ch lives at [ch*stride, ch*stride + frames).
    void run(float *buf, unsigned int frames, unsigned int channels,
             float sr, double time = 0.0, unsigned int stride = 0) {
        inst->filter(buf, frames, stride ? stride : frames, channels, sr, time);
    }
};

// Run a mono signal through a fresh filter in fixed-size blocks.
static std::vector<float> processMono(const std::vector<float> &in, float sr,
                                      float wet, float freq,
                                      unsigned int block = 512) {
    Rig rig(wet, freq);
    std::vector<float> out = in;
    for (size_t pos = 0; pos < out.size(); pos += block) {
        unsigned int n = (unsigned int)std::min<size_t>(block, out.size() - pos);
        rig.run(out.data() + pos, n, 1, sr);
    }
    return out;
}

// Exact reference carrier for integer frequency/sample rate: the phase is
// reduced with integer arithmetic, so it has no accumulated error at all.
static double referenceCarrier(long long n, long long freq, long long sr) {
    long long k = (freq * n) % sr;
    return std::sin(2.0 * PI * (double)k / (double)sr);
}

// Hann-windowed single-bin DFT magnitude, normalized so that a sine of
// amplitude A at exactly `freq` reads as A.
static double toneAmplitude(const std::vector<float> &x, double freq, double sr) {
    const size_t n = x.size();
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double w = 0.5 - 0.5 * std::cos(2.0 * PI * (double)i / (double)n);
        double ph = 2.0 * PI * freq * (double)i / sr;
        re += w * x[i] * std::cos(ph);
        im -= w * x[i] * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

static double toDb(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-30)); }

static bool allFinite(const std::vector<float> &x) {
    for (float v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

static float maxAbs(const std::vector<float> &x) {
    float m = 0.f;
    for (float v : x) m = std::max(m, std::fabs(v));
    return m;
}

// Frequency estimate from the positive-going zero crossings of a signal,
// interpolated to sub-sample accuracy.
static double zeroCrossingFrequency(const std::vector<float> &x, double sr) {
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            double t = (double)(i - 1) + x[i - 1] / (double)(x[i - 1] - x[i]);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    }
    if (count < 2) return 0.0;
    return (double)(count - 1) * sr / (last - first);
}

// ---- Tests ----------------------------------------------------------------

// 1. wet = 0 must be bit-transparent, multichannel, with arbitrary input.
static void testDryTransparency() {
    std::printf("Test: wet = 0 is transparent\n");
    constexpr unsigned int CH = 2, FRAMES = 4096;
    std::vector<float> in(CH * FRAMES);
    std::srand(1234);
    for (float &v : in) v = (float)std::rand() / (float)RAND_MAX * 2.0f - 1.0f;

    Rig rig(0.0f, 16000.0f);
    std::vector<float> buf = in;
    rig.run(buf.data(), FRAMES, CH, 44100.0f);
    EXPECT(std::memcmp(buf.data(), in.data(), in.size() * sizeof(float)) == 0,
           "wet=0 altered the signal");
}

// 2. A constant input at full wet must come out as the carrier sine itself.
static void testFullWetCarrier() {
    std::printf("Test: full wet on DC input yields the carrier sine\n");
    constexpr float SR = 48000.0f;
    constexpr float AMP = 0.5f;
    std::vector<float> in(48000, AMP);
    std::vector<float> out = processMono(in, SR, 1.0f, 440.0f);

    double maxErr = 0.0;
    for (size_t n = 0; n < out.size(); ++n) {
        double ref = AMP * referenceCarrier((long long)n, 440, 48000);
        maxErr = std::max(maxErr, std::fabs(out[n] - ref));
    }
    EXPECT(maxErr < 1e-6, "max sample error vs 0.5*sin: %g", maxErr);

    double a440 = toneAmplitude(out, 440.0, SR);
    double est = zeroCrossingFrequency(out, SR);
    EXPECT(std::fabs(a440 - AMP) < 1e-3, "440 Hz amplitude %.6f != %.3f", a440, AMP);
    EXPECT(std::fabs(est - 440.0) < 0.01, "estimated frequency %.4f Hz", est);
    // Suppressed carrier: no DC offset left in the output.
    double mean = 0.0;
    for (float v : out) mean += v;
    mean /= (double)out.size();
    EXPECT(std::fabs(mean) < 1e-5, "output has DC offset %g", mean);
    std::printf("    max err %.3g, amplitude %.6f, freq %.4f Hz, DC %.3g\n",
                maxErr, a440, est, mean);
}

// 3. 16 kHz must be exact at 44.1 and 48 kHz, i.e. not quantized to an
//    integer samples-per-cycle period (which at 44.1 kHz would give 22050 Hz
//    for a 2-sample period or 14700 Hz for a 3-sample period). Also run for
//    10 minutes of audio to check the phase accumulator doesn't drift.
static void testHighFrequencyAccuracy() {
    std::printf("Test: 16 kHz carrier accuracy at 44.1 kHz and 48 kHz\n");
    for (long long sr : {44100LL, 48000LL}) {
        const long long total = sr * 60 * 10; // 10 minutes
        Rig rig(1.0f, 16000.0f);
        constexpr unsigned int BLOCK = 512;
        std::vector<float> buf(BLOCK);
        double maxErr = 0.0, tailErr = 0.0;
        std::vector<float> firstSecond;
        firstSecond.reserve((size_t)sr);
        for (long long pos = 0; pos < total; pos += BLOCK) {
            unsigned int n = (unsigned int)std::min<long long>(BLOCK, total - pos);
            std::fill(buf.begin(), buf.begin() + n, 1.0f);
            rig.run(buf.data(), n, 1, (float)sr);
            for (unsigned int i = 0; i < n; ++i) {
                long long idx = pos + i;
                double err = std::fabs(buf[i] - referenceCarrier(idx, 16000, sr));
                maxErr = std::max(maxErr, err);
                if (idx >= total - sr) tailErr = std::max(tailErr, err);
                if (idx < sr) firstSecond.push_back(buf[i]);
            }
        }
        // Matching the exact reference sample by sample over 10 minutes pins
        // the frequency far tighter than any estimator could; the spectral
        // checks below make the absence of period quantization explicit.
        double a16k = toneAmplitude(firstSecond, 16000.0, (double)sr);
        double worstAlias = 0.0;
        for (long long period = 2; period <= 4; ++period) {
            double f = (double)sr / (double)period;
            if (std::fabs(f - 16000.0) > 1.0)
                worstAlias = std::max(worstAlias, toneAmplitude(firstSecond, f, (double)sr));
        }
        EXPECT(maxErr < 1e-5, "sr=%lld: max sample error %g over 10 min", sr, maxErr);
        EXPECT(tailErr < 1e-5, "sr=%lld: phase drift, last-second error %g", sr, tailErr);
        EXPECT(std::fabs(a16k - 1.0) < 1e-3, "sr=%lld: 16 kHz amplitude %.6f", sr, a16k);
        EXPECT(toDb(worstAlias) < -90.0, "sr=%lld: integer-period tone at %.1f dB",
               sr, toDb(worstAlias));
        std::printf("    sr=%lld: max err %.3g (last second %.3g), 16 kHz amplitude "
                    "%.6f, integer-period tones %.1f dB\n",
                    sr, maxErr, tailErr, a16k, toDb(worstAlias));
    }
}

// 4. 1 kHz sine x 16 kHz carrier -> 15 kHz + 17 kHz, no 1 kHz, no 16 kHz.
static void testSpectralTranslation() {
    std::printf("Test: 1 kHz input x 16 kHz carrier -> 15 kHz and 17 kHz\n");
    for (double sr : {44100.0, 48000.0}) {
        const size_t n = (size_t)sr; // 1 s: 1 Hz bin spacing
        std::vector<float> in(n);
        for (size_t i = 0; i < n; ++i)
            in[i] = (float)std::sin(2.0 * PI * 1000.0 * (double)i / sr);
        std::vector<float> out = processMono(in, (float)sr, 1.0f, 16000.0f, 441);

        double a1k = toneAmplitude(out, 1000.0, sr);
        double a15k = toneAmplitude(out, 15000.0, sr);
        double a16k = toneAmplitude(out, 16000.0, sr);
        double a17k = toneAmplitude(out, 17000.0, sr);
        // sin(a)sin(b) = 0.5 cos(a-b) - 0.5 cos(a+b)
        EXPECT(std::fabs(a15k - 0.5) < 1e-3, "sr=%.0f: 15 kHz amplitude %.6f", sr, a15k);
        EXPECT(std::fabs(a17k - 0.5) < 1e-3, "sr=%.0f: 17 kHz amplitude %.6f", sr, a17k);
        EXPECT(toDb(a1k / 0.5) < -90.0, "sr=%.0f: 1 kHz residue %.1f dB", sr, toDb(a1k / 0.5));
        EXPECT(toDb(a16k / 0.5) < -90.0, "sr=%.0f: carrier leak %.1f dB", sr, toDb(a16k / 0.5));
        std::printf("    sr=%.0f: 15k %.4f, 17k %.4f, 1k %.1f dB, 16k %.1f dB\n",
                    sr, a15k, a17k, toDb(a1k / 0.5), toDb(a16k / 0.5));
    }
}

// 5. One big buffer vs many small (irregular) buffers must be identical, and
//    a buffer stride larger than the frame count must be honored.
static void testCallbackContinuity() {
    std::printf("Test: output independent of callback buffer sizes\n");
    constexpr unsigned int CH = 2, FRAMES = 20000;
    constexpr float SR = 44100.0f;
    std::vector<float> in(CH * FRAMES);
    for (unsigned int i = 0; i < FRAMES; ++i) {
        in[i] = (float)std::sin(2.0 * PI * 1000.0 * i / SR);
        in[FRAMES + i] = (float)std::cos(2.0 * PI * 313.0 * i / SR);
    }

    Rig big(0.7f, 16000.0f);
    std::vector<float> ref = in;
    big.run(ref.data(), FRAMES, CH, SR);

    // Feed the same audio in irregular chunks, each copied into a scratch
    // buffer whose per-channel stride is bigger than the chunk (like
    // SoLoud's mix buffers) to catch aSamples/aBufferSize mix-ups.
    Rig chunked(0.7f, 16000.0f);
    const unsigned int sizes[] = {1, 7, 64, 333, 512, 2, 1023, 4096, 5};
    constexpr unsigned int STRIDE = 4200;
    std::vector<float> scratch(CH * STRIDE);
    std::vector<float> out(CH * FRAMES);
    unsigned int pos = 0, k = 0;
    while (pos < FRAMES) {
        unsigned int n = std::min(sizes[k++ % 9], FRAMES - pos);
        std::fill(scratch.begin(), scratch.end(), 123.0f); // sentinel
        for (unsigned int ch = 0; ch < CH; ++ch)
            std::memcpy(&scratch[ch * STRIDE], &in[ch * FRAMES + pos], n * sizeof(float));
        chunked.run(scratch.data(), n, CH, SR, 0.0, STRIDE);
        bool sentinelOk = true;
        for (unsigned int ch = 0; ch < CH; ++ch) {
            std::memcpy(&out[ch * FRAMES + pos], &scratch[ch * STRIDE], n * sizeof(float));
            for (unsigned int i = n; i < STRIDE; ++i)
                if (scratch[ch * STRIDE + i] != 123.0f) sentinelOk = false;
        }
        EXPECT(sentinelOk, "filter wrote outside [0, aSamples) of a channel");
        pos += n;
    }
    EXPECT(std::memcmp(ref.data(), out.data(), ref.size() * sizeof(float)) == 0,
           "chunked output differs from single-buffer output");
}

// 6. Identical channels must get identical output, and each channel must
//    match what a mono stream gets: the oscillator advances once per frame.
static void testStereoCoherence() {
    std::printf("Test: carrier phase shared across channels\n");
    constexpr float SR = 48000.0f;
    constexpr unsigned int FRAMES = 4800;
    std::vector<float> mono(FRAMES);
    for (unsigned int i = 0; i < FRAMES; ++i)
        mono[i] = (float)std::sin(2.0 * PI * 1000.0 * i / SR);
    std::vector<float> monoOut = processMono(mono, SR, 1.0f, 16000.0f, 480);

    for (unsigned int ch : {2u, 6u, 8u}) {
        Rig rig(1.0f, 16000.0f);
        std::vector<float> buf(ch * FRAMES);
        for (unsigned int c = 0; c < ch; ++c)
            std::memcpy(&buf[c * FRAMES], mono.data(), FRAMES * sizeof(float));
        for (unsigned int pos = 0; pos < FRAMES; pos += 480) {
            // Process each block in place inside a planar copy.
            std::vector<float> block(ch * 480);
            for (unsigned int c = 0; c < ch; ++c)
                std::memcpy(&block[c * 480], &buf[c * FRAMES + pos], 480 * sizeof(float));
            rig.run(block.data(), 480, ch, SR);
            for (unsigned int c = 0; c < ch; ++c)
                std::memcpy(&buf[c * FRAMES + pos], &block[c * 480], 480 * sizeof(float));
        }
        bool same = true;
        for (unsigned int c = 0; c < ch; ++c)
            if (std::memcmp(&buf[c * FRAMES], monoOut.data(), FRAMES * sizeof(float)) != 0)
                same = false;
        EXPECT(same, "%u channels: some channel differs from the mono result", ch);
    }
}

// 7. Parameter validation and SoLoud's fade / oscillate machinery.
static void testLiveParameters() {
    std::printf("Test: parameter set/get, validation, fade and oscillate\n");
    Rig rig;
    auto *inst = rig.inst;
    EXPECT(rig.parent.getParamCount() == 2, "param count");
    EXPECT(std::strcmp(rig.parent.getParamName(0), "Wet") == 0, "name 0");
    EXPECT(std::strcmp(rig.parent.getParamName(1), "Frequency") == 0, "name 1");
    EXPECT(rig.parent.getParamMin(1) == 0.1f && rig.parent.getParamMax(1) == 20000.0f,
           "frequency range");
    EXPECT(inst->getFilterParameter(AmplitudeModulator::WET) == 1.0f, "default wet");
    EXPECT(inst->getFilterParameter(AmplitudeModulator::FREQUENCY) == 440.0f, "default freq");

    inst->setFilterParameter(AmplitudeModulator::WET, 0.25f);
    inst->setFilterParameter(AmplitudeModulator::FREQUENCY, 16000.0f);
    EXPECT(inst->getFilterParameter(0) == 0.25f, "wet set");
    EXPECT(inst->getFilterParameter(1) == 16000.0f, "freq set");

    // Invalid values are ignored.
    for (float bad : {-0.1f, 1.5f, NAN, INFINITY}) inst->setFilterParameter(0, bad);
    for (float bad : {0.0f, 0.05f, 20000.5f, -440.0f, NAN, INFINITY})
        inst->setFilterParameter(1, bad);
    EXPECT(inst->getFilterParameter(0) == 0.25f, "invalid wet accepted");
    EXPECT(inst->getFilterParameter(1) == 16000.0f, "invalid freq accepted");
    EXPECT(rig.parent.setParam(1, 30000.0f) == SoLoud::INVALID_PARAMETER, "parent setParam");
    EXPECT(rig.parent.setParam(1, 1000.0f) == SoLoud::SO_NO_ERROR, "parent setParam ok");

    // Fade the frequency 440 -> 880 Hz over 1 s, processing in 10 ms blocks.
    // The parameter must follow the fade and the carrier must stay
    // continuous (no phase reset when the increment changes).
    constexpr float SR = 48000.0f;
    constexpr unsigned int BLOCK = 480;
    inst->setFilterParameter(0, 1.0f);
    inst->setFilterParameter(1, 440.0f);
    inst->fadeFilterParameter(1, 880.0f, 1.0, 0.0);
    std::vector<float> buf(BLOCK);
    float prev = 0.0f, maxStep = 0.0f;
    bool monotonic = true;
    float lastParam = 440.0f;
    for (int b = 0; b <= 110; ++b) {
        double t = b * (double)BLOCK / SR;
        std::fill(buf.begin(), buf.end(), 1.0f);
        rig.run(buf.data(), BLOCK, 1, SR, t);
        float p = inst->getFilterParameter(1);
        if (p < lastParam) monotonic = false;
        lastParam = p;
        for (float v : buf) {
            maxStep = std::max(maxStep, std::fabs(v - prev));
            prev = v;
        }
        if (b == 50)
            EXPECT(std::fabs(p - 660.0f) < 5.0f, "mid-fade frequency %.2f", p);
    }
    EXPECT(monotonic, "frequency fade not monotonic");
    EXPECT(inst->getFilterParameter(1) == 880.0f, "fade end %.2f",
           inst->getFilterParameter(1));
    // Largest possible step of a continuous 880 Hz unit sine at 48 kHz.
    float bound = (float)(2.0 * PI * 880.0 / SR) * 1.01f;
    EXPECT(maxStep <= bound, "carrier discontinuity during fade: step %.5f > %.5f",
           maxStep, bound);

    // Oscillate wet between 0 and 1 with a 0.5 s period.
    inst->oscillateFilterParameter(0, 0.0f, 1.0f, 0.5, 0.0);
    float lo = 1.0f, hi = 0.0f;
    for (int b = 0; b < 100; ++b) {
        std::fill(buf.begin(), buf.end(), 1.0f);
        rig.run(buf.data(), BLOCK, 1, SR, b * (double)BLOCK / SR);
        float w = inst->getFilterParameter(0);
        lo = std::min(lo, w);
        hi = std::max(hi, w);
    }
    EXPECT(lo < 0.05f && hi > 0.95f, "wet oscillation range [%.3f, %.3f]", lo, hi);
    // A direct set cancels the oscillation.
    inst->setFilterParameter(0, 0.5f);
    rig.run(buf.data(), BLOCK, 1, SR, 10.123);
    EXPECT(inst->getFilterParameter(0) == 0.5f, "set did not cancel oscillation");
}

// 8. Out-of-range requests for the active sample rate must stay stable and
//    never put the carrier at or above Nyquist.
static void testNyquistSafety() {
    std::printf("Test: Nyquist safety\n");
    // 20 kHz is a valid public value but above Nyquist at these rates.
    for (float sr : {8000.0f, 22050.0f, 32000.0f}) {
        // 2 s of DC input: 0.5 Hz bins, so the clamped carrier sits on a bin.
        std::vector<float> out = processMono(
            std::vector<float>((size_t)sr * 2, 1.0f), sr, 1.0f, 20000.0f);
        double expected = sr * AmplitudeModulator::MAX_NYQUIST_RATIO;
        double aClamp = toneAmplitude(out, expected, sr);
        EXPECT(allFinite(out) && maxAbs(out) <= 1.0f, "sr=%.0f: unstable", sr);
        EXPECT(expected < sr * 0.5, "sr=%.0f: clamp at/above Nyquist", sr);
        EXPECT(std::fabs(aClamp - 1.0) < 1e-3, "sr=%.0f: carrier not at clamp %.2f Hz "
               "(amplitude %.6f)", sr, expected, aClamp);
        std::printf("    sr=%.0f: 20 kHz request -> %.2f Hz carrier (amplitude %.6f)\n",
                    sr, expected, aClamp);
    }

    // Faders bypass setFilterParameter validation; feed hostile targets.
    const float hostile[] = {1e9f, -5.0f, 0.0f, NAN, INFINITY, -INFINITY};
    for (float target : hostile) {
        Rig rig(1.0f, 1000.0f);
        rig.inst->fadeFilterParameter(1, target, 0.01, 0.0);
        rig.inst->fadeFilterParameter(0, 1.0f, 0.0, 0.0);
        std::vector<float> buf(4800);
        bool ok = true;
        for (int b = 0; b < 20; ++b) {
            std::fill(buf.begin(), buf.end(), 1.0f);
            rig.run(buf.data(), (unsigned int)buf.size(), 1, 48000.0f, b * 0.1);
            if (!allFinite(buf) || maxAbs(buf) > 1.0f) ok = false;
        }
        EXPECT(ok, "fade to %g produced non-finite or unbounded output", target);
    }
    // Hostile wet values via a fader, and zero / NaN sample rates.
    {
        Rig rig(1.0f, 1000.0f);
        rig.inst->fadeFilterParameter(0, NAN, 0.01, 0.0);
        std::vector<float> buf(4800, 1.0f);
        rig.run(buf.data(), 4800, 1, 48000.0f, 1.0);
        EXPECT(allFinite(buf) && maxAbs(buf) <= 1.0f, "NaN wet unstable");
        std::vector<float> in(1024, 0.3f), b2 = in;
        rig.run(b2.data(), 1024, 1, 0.0f);
        rig.run(b2.data(), 1024, 1, NAN);
        rig.run(b2.data(), 1024, 1, -44100.0f);
        EXPECT(b2 == in, "invalid sample rate should leave audio untouched");
    }
}

int main() {
    testDryTransparency();
    testFullWetCarrier();
    testHighFrequencyAccuracy();
    testSpectralTranslation();
    testCallbackContinuity();
    testStereoCoherence();
    testLiveParameters();
    testNyquistSafety();

    std::printf("\n%d assertions, %d failures\n", g_assertions, g_failures);
    return g_failures == 0 ? 0 : 1;
}

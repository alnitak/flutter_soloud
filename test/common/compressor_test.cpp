// Standalone native tests for the plugin's Compressor filter
// (src/filters/compressor.cpp), driving CompressorInstance::filter() directly
// on synthetic planar buffers.
//
// Cases (pass one name to run only that one):
//
//   unity     below threshold (hard knee) the signal passes unchanged, and
//             MAKEUP_GAIN scales it by 10^(dB/20);
//   static    a constant level above threshold settles at
//             T + (L - T) / RATIO dB, for several thresholds and ratios;
//   knee      soft knee: at the threshold the reduction is knee*slope/8, and
//             the static curve is continuous and monotonic across the knee;
//   attack    after a step from silence, the level detector (one-pole in dB)
//             reaches its final reduction with the configured time constant,
//             and a longer attack is slower;
//   release   after a step down, the gain returns to unity after
//             RELEASE * ln(2) (for a step from 0 dB to -40 dB, T = -20 dB);
//   wet       WET = 0 is an exact bypass; WET = 0.5 is the dry/wet mix;
//   channels  each channel has its own envelope;
//   params    out-of-range values are rejected and leave the parameter
//             alone; parent setParam() values reach new instances;
//             getParamCount() is the 7 real parameters;
//   samplerate attack follows the sample rate passed to filter(), not the
//             one the filter was constructed with;
//   sweep     random in-range parameters on noise never produce NaN/Inf.
//
// No audio device and no assets.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/compressor_test.sh [--asan] [case]

#include "filters/compressor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        ++gChecks;                                                          \
        if (!(cond)) {                                                      \
            ++gFailures;                                                    \
            std::fprintf(stderr, "    FAIL [%s:%d] %s: ", __FILE__,         \
                         __LINE__, #cond);                                  \
            std::fprintf(stderr, __VA_ARGS__);                              \
            std::fprintf(stderr, "\n");                                     \
        }                                                                   \
    } while (0)

constexpr float kSr = 44100.f;
constexpr unsigned kBlock = SAMPLE_GRANULARITY;

using Params = std::vector<std::pair<unsigned, float>>;

double toDb(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

/// Planar: channel c at [c * frames]. Processes [in] through [inst] in blocks.
std::vector<float> run(SoLoud::FilterInstance &inst, const std::vector<float> &in,
                       unsigned channels, float sr = kSr, unsigned block = kBlock,
                       double startTime = 0) {
    const unsigned frames = (unsigned)(in.size() / channels);
    std::vector<float> out = in;
    std::vector<float> buf((size_t)block * channels);
    for (unsigned pos = 0; pos < frames; pos += block) {
        const unsigned n = std::min(block, frames - pos);
        for (unsigned c = 0; c < channels; c++)
            std::memcpy(&buf[(size_t)c * block], &in[(size_t)c * frames + pos], n * sizeof(float));
        inst.filter(buf.data(), n, block, channels, sr, startTime + pos / sr);
        for (unsigned c = 0; c < channels; c++)
            std::memcpy(&out[(size_t)c * frames + pos], &buf[(size_t)c * block], n * sizeof(float));
    }
    return out;
}

std::unique_ptr<SoLoud::FilterInstance> make(Compressor &c, const Params &p) {
    std::unique_ptr<SoLoud::FilterInstance> inst(c.createInstance());
    for (auto &kv : p) inst->setFilterParameter(kv.first, kv.second);
    return inst;
}

std::vector<float> constant(unsigned frames, float v) { return std::vector<float>(frames, v); }

/// Hard-knee static curve: output level in dB for a constant input level.
double expectedStatic(double inDb, double T, double R, double makeup = 0) {
    return (inDb > T ? T + (inDb - T) / R : inDb) + makeup;
}

void caseUnity() {
    Compressor c(44100);
    // Sine peaking at -20 dBFS, threshold -6, hard knee.
    std::vector<float> in(44100);
    for (unsigned i = 0; i < in.size(); i++) in[i] = 0.1f * sinf(2 * (float)M_PI * 440 * i / kSr);
    auto inst = make(c, {{Compressor::THRESHOLD, -6.f}, {Compressor::KNEE_WIDTH, 0.f},
                         {Compressor::RATIO, 4.f}});
    auto out = run(*inst, in, 1);
    float d = 0;
    for (size_t i = 0; i < in.size(); i++) d = std::max(d, std::fabs(out[i] - in[i]));
    std::printf("    below threshold: max |out-in| = %g\n", d);
    CHECK(d < 1e-5f, "below threshold the compressor changed the signal by %g", d);

    for (float makeup : {6.f, -12.f, 20.f}) {
        auto m = make(c, {{Compressor::THRESHOLD, -6.f}, {Compressor::KNEE_WIDTH, 0.f},
                          {Compressor::MAKEUP_GAIN, makeup}});
        auto o = run(*m, in, 1);
        const double expect = std::pow(10.0, makeup / 20.0);
        double worst = 0;
        for (size_t i = 0; i < in.size(); i++)
            if (std::fabs(in[i]) > 0.01f) worst = std::max(worst, std::fabs(o[i] / in[i] - expect));
        std::printf("    makeup %+.0f dB: gain %.4f expected %.4f (worst dev %.2e)\n", makeup,
                    o[100] / in[100], expect, worst);
        CHECK(worst < expect * 1e-3, "makeup %f dB: gain deviates by %g", makeup, worst);
    }
}

void caseStatic() {
    Compressor c(44100);
    const struct { float T, R, level; } cases[] = {
        {-20, 4, 1.0f}, {-20, 2, 0.5f}, {-10, 10, 1.0f}, {-30, 3, 0.2f},
        {-6, 1, 1.0f}, {-40, 8, 0.05f}, {0, 4, 1.0f},
    };
    for (auto &k : cases) {
        auto inst = make(c, {{Compressor::THRESHOLD, k.T}, {Compressor::RATIO, k.R},
                             {Compressor::KNEE_WIDTH, 0.f}, {Compressor::ATTACK_TIME, 1.f},
                             {Compressor::RELEASE_TIME, 50.f}});
        auto out = run(*inst, constant(44100, k.level), 1);
        const double outDb = toDb(std::fabs(out.back()));
        const double exp = expectedStatic(toDb(k.level), k.T, k.R);
        std::printf("    T %+5.0f R %4.1f in %+6.1f dB -> out %+7.2f dB (expected %+7.2f)\n",
                    k.T, k.R, toDb(k.level), outDb, exp);
        CHECK(std::fabs(outDb - exp) < 0.05, "static curve T=%f R=%f in=%f: got %f expected %f",
              k.T, k.R, k.level, outDb, exp);
    }
}

void caseKnee() {
    Compressor c(44100);
    const float T = -20, R = 4, W = 10;
    const double slope = 1.0 / R - 1.0;
    // Exactly at threshold.
    {
        auto inst = make(c, {{Compressor::THRESHOLD, T}, {Compressor::RATIO, R},
                             {Compressor::KNEE_WIDTH, W}, {Compressor::ATTACK_TIME, 1.f}});
        const float level = (float)std::pow(10.0, T / 20.0);
        auto out = run(*inst, constant(44100, level), 1);
        const double gr = toDb(out.back() / level);
        std::printf("    knee %g at threshold: reduction %.3f dB (expected %.3f)\n", W, gr,
                    W * slope / 8);
        CHECK(std::fabs(gr - W * slope / 8) < 0.05, "knee at threshold: %f dB", gr);
    }
    // Continuous, monotonic static curve across the knee.
    double prevOut = -1e9, prevGr = 1;
    bool monotonic = true, grMonotonic = true;
    for (double inDb = T - 15; inDb <= T + 15; inDb += 0.5) {
        auto inst = make(c, {{Compressor::THRESHOLD, T}, {Compressor::RATIO, R},
                             {Compressor::KNEE_WIDTH, W}, {Compressor::ATTACK_TIME, 1.f}});
        const float level = (float)std::pow(10.0, inDb / 20.0);
        auto out = run(*inst, constant(22050, level), 1);
        const double o = toDb(out.back());
        const double gr = o - inDb;
        if (o < prevOut - 1e-3) monotonic = false;
        if (gr > prevGr + 1e-3) grMonotonic = false;
        // Below the knee: unity. Above: hard-knee curve.
        if (inDb <= T - W / 2 - 0.01)
            CHECK(std::fabs(gr) < 0.01, "below knee %f dB: reduction %f", inDb, gr);
        if (inDb >= T + W / 2 + 0.01)
            CHECK(std::fabs(o - expectedStatic(inDb, T, R)) < 0.05, "above knee %f: out %f", inDb, o);
        prevOut = o;
        prevGr = gr;
    }
    CHECK(monotonic, "soft-knee static curve is not monotonic");
    CHECK(grMonotonic, "soft-knee gain reduction does not grow with level");
}

/// Frames until the gain reduction in dB reaches 90% of its final value.
unsigned attackFrames(float attackMs, float sr = kSr, unsigned ctorSr = 44100) {
    Compressor c(ctorSr);
    auto inst = make(c, {{Compressor::THRESHOLD, -20.f}, {Compressor::RATIO, 4.f},
                         {Compressor::KNEE_WIDTH, 0.f}, {Compressor::ATTACK_TIME, attackMs},
                         {Compressor::RELEASE_TIME, 100.f}});
    auto out = run(*inst, constant((unsigned)sr * 2, 1.0f), 1, sr);
    const double finalGr = toDb(out.back());
    for (unsigned i = 0; i < out.size(); i++)
        if (toDb(out[i]) <= 0.9 * finalGr) return i;
    return (unsigned)out.size();
}

void caseAttack() {
    // From silence (envelope -160 dB) to 0 dB with T = -20: the envelope is
    // -160 * a^n dB, the reduction reaches 90% when it is -2 dB, after
    // tau * ln(80) samples.
    unsigned prev = 0;
    for (float ms : {1.f, 10.f, 50.f, 100.f}) {
        const unsigned n = attackFrames(ms);
        const double expect = ms * 1e-3 * kSr * std::log(80.0);
        std::printf("    attack %5.1f ms: 90%% reduction after %u frames (expected %.0f)\n", ms,
                    n, expect);
        CHECK(std::fabs(n - expect) <= expect * 0.05 + 2, "attack %f ms: %u frames, expected %f",
              ms, n, expect);
        CHECK(n > prev, "longer attack (%f ms) is not slower", ms);
        prev = n;
    }
    // attack 0: instantaneous.
    const unsigned n0 = attackFrames(0.f);
    CHECK(n0 <= 1, "attack 0 ms: %u frames", n0);
}

void caseRelease() {
    Compressor c(44100);
    for (float ms : {20.f, 100.f, 500.f}) {
        auto inst = make(c, {{Compressor::THRESHOLD, -20.f}, {Compressor::RATIO, 4.f},
                             {Compressor::KNEE_WIDTH, 0.f}, {Compressor::ATTACK_TIME, 1.f},
                             {Compressor::RELEASE_TIME, ms}});
        // 1 s at 0 dB, then 2 s at -40 dB.
        std::vector<float> in(44100 * 3, 0.01f);
        for (unsigned i = 0; i < 44100; i++) in[i] = 1.0f;
        auto out = run(*inst, in, 1);
        unsigned back = 0;
        for (unsigned i = 44100; i < out.size(); i++)
            if (std::fabs(out[i] / in[i] - 1.0f) < 1e-4f) { back = i - 44100; break; }
        // The envelope goes -40 + 40 r^n; it drops below T = -20 after
        // tau ln 2.
        const double expect = ms * 1e-3 * kSr * std::log(2.0);
        std::printf("    release %5.0f ms: unity gain after %u frames (expected %.0f)\n", ms,
                    back, expect);
        CHECK(back > 0 && std::fabs(back - expect) <= expect * 0.05 + 2,
              "release %f ms: %u frames, expected %f", ms, back, expect);
        // While releasing the gain only rises.
        bool rising = true;
        for (unsigned i = 44101; i < 44100 + back; i++)
            if (out[i] / in[i] < out[i - 1] / in[i - 1] - 1e-6f) rising = false;
        CHECK(rising, "release %f ms: gain not monotonically recovering", ms);
    }
}

void caseWet() {
    Compressor c(44100);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-1, 1);
    std::vector<float> in(2 * 22050);
    for (auto &v : in) v = d(rng);
    const Params hot = {{Compressor::THRESHOLD, -30.f}, {Compressor::RATIO, 10.f},
                        {Compressor::MAKEUP_GAIN, 6.f}};
    Params dry = hot;
    dry.push_back({Compressor::WET, 0.f});
    auto o0 = run(*make(c, dry), in, 2);
    float diff = 0;
    for (size_t i = 0; i < in.size(); i++) diff = std::max(diff, std::fabs(o0[i] - in[i]));
    CHECK(diff == 0.f, "wet 0 is not an exact bypass (%g)", diff);

    auto o1 = run(*make(c, hot), in, 2);
    Params half = hot;
    half.push_back({Compressor::WET, 0.5f});
    auto oh = run(*make(c, half), in, 2);
    float mixErr = 0;
    for (size_t i = 0; i < in.size(); i++)
        mixErr = std::max(mixErr, std::fabs(oh[i] - (0.5f * in[i] + 0.5f * o1[i])));
    std::printf("    wet 0 max diff %g, wet 0.5 vs 0.5*dry+0.5*wet max diff %g\n", diff, mixErr);
    CHECK(mixErr < 1e-5f, "wet 0.5 is not the dry/wet average (%g)", mixErr);
}

void caseChannels() {
    Compressor c(44100);
    // Left at 0 dB, right at -40 dB.
    std::vector<float> in(2 * 44100);
    for (unsigned i = 0; i < 44100; i++) {
        in[i] = 1.0f;
        in[44100 + i] = 0.01f * sinf(2 * (float)M_PI * 300 * i / kSr);
    }
    auto out = run(*make(c, {{Compressor::THRESHOLD, -20.f}, {Compressor::RATIO, 4.f},
                             {Compressor::KNEE_WIDTH, 0.f}}),
                   in, 2);
    float rightDiff = 0;
    for (unsigned i = 0; i < 44100; i++)
        rightDiff = std::max(rightDiff, std::fabs(out[44100 + i] - in[44100 + i]));
    std::printf("    left %.2f dB, right max diff %g\n", toDb(out[44099]), rightDiff);
    CHECK(std::fabs(toDb(out[44099]) - (-15.0)) < 0.05, "left not compressed: %f dB",
          toDb(out[44099]));
    CHECK(rightDiff < 1e-6f, "quiet right channel was affected (%g)", rightDiff);
}

void caseParams() {
    Compressor c(44100);
    std::unique_ptr<SoLoud::FilterInstance> inst(c.createInstance());
    // Defaults reach the instance.
    CHECK(inst->getFilterParameter(Compressor::THRESHOLD) == -6.f, "default threshold");
    CHECK(inst->getFilterParameter(Compressor::RATIO) == 3.f, "default ratio");
    CHECK(inst->getFilterParameter(Compressor::WET) == 1.f, "default wet");
    // Out-of-range values are ignored.
    const struct { unsigned id; float bad; } bad[] = {
        {Compressor::WET, 1.5f}, {Compressor::WET, -0.1f}, {Compressor::THRESHOLD, 1.f},
        {Compressor::THRESHOLD, -100.f}, {Compressor::MAKEUP_GAIN, 41.f},
        {Compressor::KNEE_WIDTH, -1.f}, {Compressor::RATIO, 0.f}, {Compressor::RATIO, 0.5f},
        {Compressor::RATIO, 11.f}, {Compressor::ATTACK_TIME, -1.f},
        {Compressor::ATTACK_TIME, 101.f}, {Compressor::RELEASE_TIME, 1001.f},
    };
    for (auto &b : bad) {
        const float before = inst->getFilterParameter(b.id);
        inst->setFilterParameter(b.id, b.bad);
        CHECK(inst->getFilterParameter(b.id) == before, "param %u accepted out-of-range %f",
              b.id, b.bad);
    }
    // In-range values are taken; min/max metadata are consistent.
    for (unsigned id = 0; id <= Compressor::RELEASE_TIME; id++) {
        CHECK(c.getParamMin(id) < c.getParamMax(id), "param %u min >= max", id);
        inst->setFilterParameter(id, c.getParamMax(id));
        CHECK(inst->getFilterParameter(id) == c.getParamMax(id), "param %u max not accepted", id);
        inst->setFilterParameter(id, c.getParamMin(id));
        CHECK(inst->getFilterParameter(id) == c.getParamMin(id), "param %u min not accepted", id);
    }
    // Parent setParam reaches new instances; invalid values report an error.
    CHECK(c.setParam(Compressor::THRESHOLD, -30.f) == SoLoud::SO_NO_ERROR, "setParam threshold");
    CHECK(c.setParam(Compressor::RATIO, 0.f) == SoLoud::INVALID_PARAMETER, "setParam ratio 0");
    CHECK(c.setParam(Compressor::WET, 0.25f) == SoLoud::SO_NO_ERROR, "setParam wet");
    std::unique_ptr<SoLoud::FilterInstance> i2(c.createInstance());
    CHECK(i2->getFilterParameter(Compressor::THRESHOLD) == -30.f, "parent threshold not used");
    CHECK(i2->getFilterParameter(Compressor::WET) == 0.25f, "parent wet not used");
    // One parameter per FILTERATTRIBUTE, no phantom trailing slot (bug F5).
    CHECK(c.getParamCount() == Compressor::RELEASE_TIME + 1, "getParamCount() = %d, want %d",
          c.getParamCount(), Compressor::RELEASE_TIME + 1);
    for (int i = 1; i < c.getParamCount(); i++)
        CHECK(std::strcmp(c.getParamName(i), "Wet") != 0, "param %d is named 'Wet'", i);
    std::printf("    getParamCount() = %d, names:", c.getParamCount());
    for (int i = 0; i < c.getParamCount(); i++) std::printf(" '%s'", c.getParamName(i));
    std::printf("\n");
}

void caseSamplerate() {
    // Constructed for 44100 but mixed at 48000 / 22050: the coefficients are
    // recomputed from the rate filter() is called with.
    for (float sr : {48000.f, 22050.f}) {
        const unsigned n = attackFrames(10.f, sr, 44100);
        const double expect = 10e-3 * sr * std::log(80.0);
        std::printf("    attack 10 ms at %.0f Hz: %u frames (expected %.0f)\n", sr, n, expect);
        CHECK(std::fabs(n - expect) <= expect * 0.05 + 2, "sr %f: %u frames vs %f", sr, n, expect);
    }
}

void caseSweep() {
    Compressor c(44100);
    std::mt19937 rng(42);
    int bad = 0;
    for (int t = 0; t < 40; t++) {
        std::unique_ptr<SoLoud::FilterInstance> inst(c.createInstance());
        for (unsigned id = 0; id <= Compressor::RELEASE_TIME; id++) {
            std::uniform_real_distribution<float> d(c.getParamMin(id), c.getParamMax(id));
            inst->setFilterParameter(id, d(rng));
        }
        const unsigned ch = 1 + t % 8;
        std::uniform_real_distribution<float> d(-2, 2);
        std::vector<float> in((size_t)ch * 4410);
        for (auto &v : in) v = (t % 4 == 0) ? 0.f : d(rng); // some all-silent runs
        auto out = run(*inst, in, ch, 44100.f, t % 3 ? kBlock : 100);
        for (float v : out)
            if (!std::isfinite(v) || std::fabs(v) > 2 * 100.f * 2) { bad++; break; }
    }
    CHECK(bad == 0, "%d sweep runs produced NaN/Inf or exploded", bad);
}

} // namespace

int main(int argc, char **argv) {
    const struct { const char *name; void (*fn)(); } cases[] = {
        {"unity", caseUnity}, {"static", caseStatic}, {"knee", caseKnee},
        {"attack", caseAttack}, {"release", caseRelease}, {"wet", caseWet},
        {"channels", caseChannels}, {"params", caseParams},
        {"samplerate", caseSamplerate}, {"sweep", caseSweep},
    };
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0) continue;
        ran++;
        const int before = gFailures;
        std::printf("[%s]\n", c.name);
        c.fn();
        std::printf("  %s %s\n", gFailures == before ? "PASS" : "FAIL", c.name);
    }
    if (!ran) {
        std::fprintf(stderr, "unknown case '%s'\n", only);
        return 2;
    }
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    if (gFailures) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("ALL PASSED\n");
    return 0;
}

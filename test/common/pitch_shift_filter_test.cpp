// Standalone native tests for the plugin's PitchShift filter
// (src/filters/pitch_shift_filter.cpp, built on signalsmith-stretch), driving
// PitchShiftInstance::filter() directly on synthetic planar buffers.
//
// Cases (pass one name to run only that one):
//
//   ratio      a sine's frequency is multiplied by SHIFT (0.5, 0.75, 1, 1.5,
//              2), measured by zero crossings and cross-checked with a
//              Goertzel peak search; the output keeps the input length and
//              is not silent;
//   semitones  SEMITONES sets SHIFT = 2^(s/12) (and vice versa) and moves the
//              frequency accordingly;
//   wet        WET = 0 is an exact bypass; WET = 0.5 contains both the
//              original and the shifted tone;
//   blocks     blocks larger than SAMPLE_GRANULARITY (processed in chunks)
//              and odd block sizes give the same pitch; mono and 8 channels
//              work, each channel shifted independently;
//   params     out-of-range values are rejected; changing SHIFT while
//              running takes effect;
//   probes     a new instance takes the parent's WET; the SEMITONES range is
//              the SHIFT range in semitones and each maps into the other.
//
// No audio device and no assets.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/pitch_shift_filter_test.sh [--asan] [case]

#include "filters/pitch_shift_filter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
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
constexpr double kPi = 3.14159265358979323846;

using Params = std::vector<std::pair<unsigned, float>>;

/// Planar signal, channel c at [c * frames].
struct Sig {
    unsigned ch, frames;
    std::vector<float> d;
    Sig(unsigned c, unsigned f) : ch(c), frames(f), d((size_t)c * f, 0.f) {}
    float *chan(unsigned c) { return d.data() + (size_t)c * frames; }
    const float *chan(unsigned c) const { return d.data() + (size_t)c * frames; }
};

Sig sine(unsigned ch, unsigned frames, const std::vector<double> &freqs, float amp = 0.5f,
         float sr = kSr) {
    Sig s(ch, frames);
    for (unsigned c = 0; c < ch; c++) {
        const double f = freqs[c % freqs.size()];
        for (unsigned i = 0; i < frames; i++)
            s.chan(c)[i] = amp * (float)std::sin(2 * kPi * f * i / sr);
    }
    return s;
}

Sig run(SoLoud::FilterInstance &inst, const Sig &in, unsigned block = SAMPLE_GRANULARITY,
        float sr = kSr) {
    Sig out = in;
    std::vector<float> buf((size_t)block * in.ch);
    for (unsigned pos = 0; pos < in.frames; pos += block) {
        const unsigned n = std::min(block, in.frames - pos);
        for (unsigned c = 0; c < in.ch; c++)
            std::memcpy(&buf[(size_t)c * block], in.chan(c) + pos, n * sizeof(float));
        inst.filter(buf.data(), n, block, in.ch, sr, pos / sr);
        for (unsigned c = 0; c < in.ch; c++)
            std::memcpy(out.chan(c) + pos, &buf[(size_t)c * block], n * sizeof(float));
    }
    return out;
}

std::unique_ptr<SoLoud::FilterInstance> make(PitchShift &f, const Params &p) {
    std::unique_ptr<SoLoud::FilterInstance> inst(f.createInstance());
    for (auto &kv : p) inst->setFilterParameter(kv.first, kv.second);
    return inst;
}

/// Frequency from positive-going zero crossings (linear interpolation) in
/// [from, to).
double zeroCrossingFreq(const float *x, unsigned from, unsigned to, float sr = kSr) {
    double first = -1, last = -1;
    int count = 0;
    for (unsigned i = from + 1; i < to; i++) {
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = (i - 1) + (-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    }
    if (count < 2) return 0;
    return (count - 1) * sr / (last - first);
}

double goertzel(const float *x, unsigned from, unsigned to, double f, float sr = kSr) {
    double re = 0, im = 0;
    for (unsigned i = from; i < to; i++) {
        // Hann window to limit leakage between neighbouring candidates.
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i - from) / (to - from - 1));
        re += w * x[i] * std::cos(2 * kPi * f * i / sr);
        im += w * x[i] * std::sin(2 * kPi * f * i / sr);
    }
    return 4 * std::sqrt(re * re + im * im) / (to - from); // Hann coherent gain 0.5
}

/// Frequency with the largest Goertzel magnitude in [lo, hi], 1 Hz steps then
/// 0.05 Hz around the best.
double peakFreq(const float *x, unsigned from, unsigned to, double lo, double hi) {
    double best = lo, bestMag = -1;
    for (double f = lo; f <= hi; f += 1.0) {
        const double m = goertzel(x, from, to, f);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    const double c = best;
    for (double f = c - 1; f <= c + 1; f += 0.05) {
        const double m = goertzel(x, from, to, f);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    return best;
}

double rms(const float *x, unsigned from, unsigned to) {
    double a = 0;
    for (unsigned i = from; i < to; i++) a += (double)x[i] * x[i];
    return std::sqrt(a / (to - from));
}

bool allFinite(const Sig &s) {
    for (float v : s.d)
        if (!std::isfinite(v)) return false;
    return true;
}

/// Shift a 440 Hz stereo sine and check the measured frequency.
void expectShift(const char *label, const Params &p, double expectRatio,
                 unsigned block = SAMPLE_GRANULARITY, unsigned channels = 2,
                 double base = 440.0) {
    PitchShift f;
    auto inst = make(f, p);
    const unsigned frames = (unsigned)(kSr * 2);
    const Sig in = sine(channels, frames, {base});
    const Sig out = run(*inst, in, block);
    CHECK(out.frames == in.frames && out.d.size() == in.d.size(), "%s: length changed", label);
    CHECK(allFinite(out), "%s: NaN/Inf", label);
    // Skip the first second (stretcher latency / settling).
    const unsigned a = frames / 2, b = frames;
    const double expect = base * expectRatio;
    for (unsigned c = 0; c < channels; c++) {
        const double zc = zeroCrossingFreq(out.chan(c), a, b);
        const double pk = peakFreq(out.chan(c), a, b, expect * 0.8, expect * 1.2);
        const double level = rms(out.chan(c), a, b);
        if (c == 0)
            std::printf("    %-28s expected %7.2f Hz: zero-crossings %7.2f, peak %7.2f, rms %.3f"
                        " (input %.3f)\n",
                        label, expect, zc, pk, level, rms(in.chan(c), a, b));
        CHECK(std::fabs(zc - expect) < expect * 0.01, "%s ch%u: zero-crossing freq %f vs %f",
              label, c, zc, expect);
        CHECK(std::fabs(pk - expect) < expect * 0.005, "%s ch%u: peak freq %f vs %f", label, c,
              pk, expect);
        CHECK(level > 0.15 && level < 0.6, "%s ch%u: output rms %f", label, c, level);
    }
}

void caseRatio() {
    for (float r : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
        char label[64];
        std::snprintf(label, sizeof label, "shift %.2f", r);
        expectShift(label, {{PitchShift::SHIFT, r}}, r);
    }
}

void caseSemitones() {
    PitchShift f;
    std::unique_ptr<SoLoud::FilterInstance> inst(f.createInstance());
    inst->setFilterParameter(PitchShift::SEMITONES, 12.f);
    CHECK(std::fabs(inst->getFilterParameter(PitchShift::SHIFT) - 2.f) < 1e-5f,
          "semitones 12 -> shift %f", inst->getFilterParameter(PitchShift::SHIFT));
    inst->setFilterParameter(PitchShift::SHIFT, 0.5f);
    CHECK(std::fabs(inst->getFilterParameter(PitchShift::SEMITONES) + 12.f) < 1e-4f,
          "shift 0.5 -> semitones %f", inst->getFilterParameter(PitchShift::SEMITONES));

    expectShift("semitones +7", {{PitchShift::SEMITONES, 7.f}}, std::pow(2.0, 7 / 12.0));
    expectShift("semitones -5", {{PitchShift::SEMITONES, -5.f}}, std::pow(2.0, -5 / 12.0));
    expectShift("semitones +12", {{PitchShift::SEMITONES, 12.f}}, 2.0);

    // Parent setParam keeps SHIFT and SEMITONES in step.
    CHECK(f.setParam(PitchShift::SEMITONES, 12.f) == SoLoud::SO_NO_ERROR, "setParam semitones");
    CHECK(std::fabs(f.mShift - 2.f) < 1e-5f, "parent mShift %f", f.mShift);
    std::unique_ptr<SoLoud::FilterInstance> i2(f.createInstance());
    CHECK(std::fabs(i2->getFilterParameter(PitchShift::SHIFT) - 2.f) < 1e-5f,
          "new instance shift %f", i2->getFilterParameter(PitchShift::SHIFT));
}

void caseWet() {
    PitchShift f;
    const Sig in = sine(2, (unsigned)kSr, {440.0, 330.0});
    auto dry = make(f, {{PitchShift::SHIFT, 1.5f}, {PitchShift::WET, 0.f}});
    const Sig o0 = run(*dry, in);
    float diff = 0;
    for (size_t i = 0; i < in.d.size(); i++) diff = std::max(diff, std::fabs(o0.d[i] - in.d[i]));
    std::printf("    wet 0: max diff %g\n", diff);
    CHECK(diff == 0.f, "wet 0 is not an exact bypass (%g)", diff);

    auto half = make(f, {{PitchShift::SHIFT, 1.5f}, {PitchShift::WET, 0.5f}});
    const Sig oh = run(*half, sine(1, (unsigned)(kSr * 2), {440.0}));
    const unsigned a = (unsigned)kSr, b = (unsigned)(kSr * 2);
    const double orig = goertzel(oh.chan(0), a, b, 440.0);
    // The stretcher lands within ~0.3% of the target, so search around it.
    const double shiftedFreq = peakFreq(oh.chan(0), a, b, 640.0, 680.0);
    const double shifted = goertzel(oh.chan(0), a, b, shiftedFreq);
    std::printf("    wet 0.5: 440 Hz amplitude %.3f, shifted (%.2f Hz) amplitude %.3f\n", orig,
                shiftedFreq, shifted);
    CHECK(std::fabs(orig - 0.25) < 0.03, "wet 0.5: dry 440 Hz amplitude %f", orig);
    CHECK(shifted > 0.12 && shifted < 0.35, "wet 0.5: shifted 660 Hz amplitude %f", shifted);
}

void caseBlocks() {
    expectShift("shift 1.5, 2048-frame blocks", {{PitchShift::SHIFT, 1.5f}}, 1.5, 2048);
    expectShift("shift 1.5, 333-frame blocks", {{PitchShift::SHIFT, 1.5f}}, 1.5, 333);
    expectShift("shift 1.5, 64-frame blocks", {{PitchShift::SHIFT, 1.5f}}, 1.5, 64);
    expectShift("shift 1.5, mono", {{PitchShift::SHIFT, 1.5f}}, 1.5, 512, 1);
    expectShift("shift 0.8, 8 channels", {{PitchShift::SHIFT, 0.8f}}, 0.8, 512, 8);

    // Channels are independent: different input tones stay apart.
    PitchShift f;
    auto inst = make(f, {{PitchShift::SHIFT, 2.f}});
    const Sig in = sine(2, (unsigned)(kSr * 2), {300.0, 500.0});
    const Sig out = run(*inst, in);
    const unsigned a = (unsigned)kSr, b = (unsigned)(kSr * 2);
    const double l = zeroCrossingFreq(out.chan(0), a, b);
    const double r = zeroCrossingFreq(out.chan(1), a, b);
    std::printf("    stereo 300/500 Hz x2: left %.2f, right %.2f\n", l, r);
    CHECK(std::fabs(l - 600) < 6 && std::fabs(r - 1000) < 10, "per-channel shift %f/%f", l, r);

    // Changing the channel count mid-stream reconfigures the stretcher.
    const Sig mono = sine(1, (unsigned)kSr, {440.0});
    const Sig o2 = run(*inst, mono);
    CHECK(allFinite(o2), "channel count change produced NaN/Inf");
}

void caseParams() {
    PitchShift f;
    std::unique_ptr<SoLoud::FilterInstance> inst(f.createInstance());
    const struct { unsigned id; float v; } bad[] = {
        {PitchShift::SHIFT, 0.05f}, {PitchShift::SHIFT, 3.5f},
        {PitchShift::SEMITONES, -40.f}, {PitchShift::SEMITONES, 40.f},
        {PitchShift::WET, -0.5f}, {PitchShift::WET, 2.f},
    };
    for (auto &b : bad) {
        const float s = inst->getFilterParameter(PitchShift::SHIFT);
        const float st = inst->getFilterParameter(PitchShift::SEMITONES);
        const float w = inst->getFilterParameter(PitchShift::WET);
        inst->setFilterParameter(b.id, b.v);
        CHECK(inst->getFilterParameter(PitchShift::SHIFT) == s &&
                  inst->getFilterParameter(PitchShift::SEMITONES) == st &&
                  inst->getFilterParameter(PitchShift::WET) == w,
              "param %u accepted out-of-range %f", b.id, b.v);
    }
    CHECK(f.setParam(PitchShift::SHIFT, 5.f) == SoLoud::INVALID_PARAMETER, "setParam shift 5");

    // SHIFT changed while running: first half at 1.0, then 2.0.
    auto live = make(f, {{PitchShift::SHIFT, 1.f}});
    const unsigned frames = (unsigned)(kSr * 3);
    const Sig in = sine(1, frames, {440.0});
    Sig out = in;
    const unsigned blk = SAMPLE_GRANULARITY;
    std::vector<float> buf(blk);
    for (unsigned pos = 0; pos < frames; pos += blk) {
        if (pos >= frames / 3 && pos < frames / 3 + blk)
            live->setFilterParameter(PitchShift::SHIFT, 2.f);
        const unsigned n = std::min(blk, frames - pos);
        std::memcpy(buf.data(), in.chan(0) + pos, n * sizeof(float));
        live->filter(buf.data(), n, blk, 1, kSr, pos / kSr);
        std::memcpy(out.chan(0) + pos, buf.data(), n * sizeof(float));
    }
    const double before = zeroCrossingFreq(out.chan(0), (unsigned)(kSr * 0.5), frames / 3);
    const double after = zeroCrossingFreq(out.chan(0), frames / 3 + (unsigned)(kSr * 0.5), frames);
    std::printf("    live change 1.0 -> 2.0: before %.2f Hz, after %.2f Hz\n", before, after);
    CHECK(std::fabs(before - 440) < 4.4, "before change %f", before);
    CHECK(std::fabs(after - 880) < 8.8, "after change %f", after);
}

void caseProbes() {
    PitchShift f;
    // 1. The parent's WET is copied into new instances, like SHIFT and
    //    SEMITONES (bug F4).
    f.setParam(PitchShift::WET, 0.3f);
    std::unique_ptr<SoLoud::FilterInstance> inst(f.createInstance());
    const float w = inst->getFilterParameter(PitchShift::WET);
    std::printf("    parent setParam(WET, 0.3) -> new instance WET = %g\n", w);
    CHECK(std::fabs(w - 0.3f) < 1e-6f, "parent WET not used by a new instance (%g)", w);
    // 2. SEMITONES and SHIFT describe the same value: the SEMITONES range is
    //    the SHIFT range in semitones, and both ends map into each other
    //    (bug F4).
    PitchShift fresh; // the instance keeps a pointer to its parent
    const float sMin = fresh.getParamMin(PitchShift::SHIFT);
    const float sMax = fresh.getParamMax(PitchShift::SHIFT);
    const float stMin = fresh.getParamMin(PitchShift::SEMITONES);
    const float stMax = fresh.getParamMax(PitchShift::SEMITONES);
    std::printf("    SHIFT %g..%g, SEMITONES %g..%g\n", sMin, sMax, stMin, stMax);
    CHECK(std::fabs(stMin - 12.f * std::log2(sMin)) < 1e-3f, "SEMITONES min %g", stMin);
    CHECK(std::fabs(stMax - 12.f * std::log2(sMax)) < 1e-3f, "SEMITONES max %g", stMax);
    std::unique_ptr<SoLoud::FilterInstance> i2(fresh.createInstance());
    for (float st : {stMin, stMax}) {
        i2->setFilterParameter(PitchShift::SEMITONES, st);
        const float s = i2->getFilterParameter(PitchShift::SHIFT);
        std::printf("    SEMITONES %g -> SHIFT %g\n", st, s);
        CHECK(i2->getFilterParameter(PitchShift::SEMITONES) == st, "SEMITONES %g rejected", st);
        CHECK(s >= sMin && s <= sMax, "SEMITONES %g maps outside SHIFT range (%g)", st, s);
    }
    for (float sh : {sMin, sMax}) {
        i2->setFilterParameter(PitchShift::SHIFT, sh);
        const float st = i2->getFilterParameter(PitchShift::SEMITONES);
        CHECK(i2->getFilterParameter(PitchShift::SHIFT) == sh, "SHIFT %g rejected", sh);
        CHECK(st >= stMin && st <= stMax, "SHIFT %g maps outside SEMITONES range (%g)", sh, st);
    }
    // The parent setParam too.
    CHECK(fresh.setParam(PitchShift::SEMITONES, stMax) == SoLoud::SO_NO_ERROR,
          "setParam semitones max");
    CHECK(fresh.mShift <= sMax, "setParam semitones max -> shift %g", fresh.mShift);
    CHECK(fresh.setParam(PitchShift::SEMITONES, 36.f) == SoLoud::INVALID_PARAMETER,
          "setParam semitones 36 (shift 8) accepted");
    // It must still produce finite audio at the extreme.
    i2->setFilterParameter(PitchShift::SEMITONES, stMax);
    const Sig out = run(*i2, sine(2, (unsigned)kSr, {440.0}));
    CHECK(allFinite(out), "SEMITONES max produced NaN/Inf");
}

} // namespace

int main(int argc, char **argv) {
    const struct { const char *name; void (*fn)(); } cases[] = {
        {"ratio", caseRatio}, {"semitones", caseSemitones}, {"wet", caseWet},
        {"blocks", caseBlocks}, {"params", caseParams}, {"probes", caseProbes},
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

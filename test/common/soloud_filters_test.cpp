// Standalone native smoke tests for the stock SoLoud filters that the plugin
// exposes through FilterType (src/enums.h, created in
// src/filters/filters.cpp addFilter()):
//
//   BiquadResonant, Echo, Lofi, Flanger, Bassboost, WaveShaper, Robotize,
//   Freeverb.
//
// (Eq, DCRemoval and Duck are compiled into SoLoud but have no FilterType, so
// the plugin cannot create them; they are not tested here. The plugin's own
// filters -- PitchShift, Limiter, Compressor, ParametricEQ, AmplitudeModulator
// -- have their own tests.)
//
// Every filter instance is driven directly on synthetic planar stereo blocks
// (sine + noise), exactly as SoLoud's mixer calls FilterInstance::filter().
// For each filter:
//
//   finite    output has no NaN/Inf and its energy stays bounded, with the
//             default and with "effective" parameters;
//   effect    effective parameters change the signal (output != input);
//   bypass    wet = 0 returns the input unchanged;
//   sweep     random parameter values inside [getParamMin, getParamMax], at
//             several sample rates and mono/stereo, never yield NaN/Inf;
//   semantic  one behaviour specific to the filter (lowpass attenuates highs,
//             echo repeats an impulse after DELAY, lofi quantizes, ...).
//
// No audio device and no assets are used.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/soloud_filters_test.sh [--asan] [filter-name]
//
// --asan builds with AddressSanitizer + UndefinedBehaviorSanitizer. A filter
// name (e.g. "echo") runs only that filter's cases.

#include "soloud.h"
#include "soloud_bassboostfilter.h"
#include "soloud_biquadresonantfilter.h"
#include "soloud_echofilter.h"
#include "soloud_filter.h"
#include "soloud_flangerfilter.h"
#include "soloud_freeverbfilter.h"
#include "soloud_lofifilter.h"
#include "soloud_robotizefilter.h"
#include "soloud_waveshaperfilter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <set>
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

constexpr unsigned int kBlock = SAMPLE_GRANULARITY; // 512
constexpr float kPi = 3.14159265358979f;

/// A planar multi-channel signal: channel c, frame i is data[c * frames + i].
struct Signal {
    unsigned int channels = 0;
    unsigned int frames = 0;
    std::vector<float> data;
    Signal() = default;
    Signal(unsigned int ch, unsigned int fr)
        : channels(ch), frames(fr), data(static_cast<size_t>(ch) * fr, 0.f) {}
    float &at(unsigned int c, unsigned int i) { return data[(size_t)c * frames + i]; }
    float at(unsigned int c, unsigned int i) const { return data[(size_t)c * frames + i]; }
};

/// Left: 0.5 * sin(440 Hz). Right: 0.3 * sin(1000 Hz) + 0.1 noise. Extra
/// channels repeat the pattern.
Signal makeTestSignal(unsigned int channels, unsigned int frames, float sr,
                      unsigned int seed = 1) {
    Signal s(channels, frames);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    for (unsigned int c = 0; c < channels; c++) {
        for (unsigned int i = 0; i < frames; i++) {
            const float t = (float)i / sr;
            s.at(c, i) = (c % 2 == 0)
                ? 0.5f * sinf(2 * kPi * 440.f * t)
                : 0.3f * sinf(2 * kPi * 1000.f * t) + 0.1f * noise(rng);
        }
    }
    return s;
}

Signal makeSine(unsigned int channels, unsigned int frames, float sr, float freq,
                float amp) {
    Signal s(channels, frames);
    for (unsigned int c = 0; c < channels; c++)
        for (unsigned int i = 0; i < frames; i++)
            s.at(c, i) = amp * sinf(2 * kPi * freq * (float)i / sr);
    return s;
}

/// Runs [in] through a fresh instance of [filter], block by block, after
/// applying [params] (attribute id -> value) to the instance, like
/// Soloud::setFilterParameter does on a live voice.
Signal process(SoLoud::Filter &filter, const Signal &in, float sr,
               const std::vector<std::pair<unsigned int, float>> &params,
               unsigned int block = kBlock) {
    std::unique_ptr<SoLoud::FilterInstance> inst(filter.createInstance());
    for (const auto &p : params) inst->setFilterParameter(p.first, p.second);

    Signal out = in;
    std::vector<float> buf(static_cast<size_t>(block) * in.channels);
    for (unsigned int pos = 0; pos < in.frames; pos += block) {
        const unsigned int n = std::min(block, in.frames - pos);
        for (unsigned int c = 0; c < in.channels; c++)
            std::memcpy(&buf[(size_t)c * block], &in.data[(size_t)c * in.frames + pos],
                        n * sizeof(float));
        inst->filter(buf.data(), n, block, in.channels, sr, (double)pos / sr);
        for (unsigned int c = 0; c < in.channels; c++)
            std::memcpy(&out.data[(size_t)c * in.frames + pos], &buf[(size_t)c * block],
                        n * sizeof(float));
    }
    return out;
}

bool allFinite(const Signal &s) {
    for (float v : s.data)
        if (!std::isfinite(v)) return false;
    return true;
}

double rms(const Signal &s, unsigned int c, unsigned int from = 0,
           unsigned int to = ~0u) {
    if (to > s.frames) to = s.frames;
    double acc = 0;
    for (unsigned int i = from; i < to; i++) acc += (double)s.at(c, i) * s.at(c, i);
    return to > from ? std::sqrt(acc / (to - from)) : 0.0;
}

double rmsAll(const Signal &s) {
    double acc = 0;
    for (float v : s.data) acc += (double)v * v;
    return s.data.empty() ? 0 : std::sqrt(acc / s.data.size());
}

float peak(const Signal &s) {
    float p = 0;
    for (float v : s.data) p = std::max(p, std::fabs(v));
    return p;
}

float maxDiff(const Signal &a, const Signal &b) {
    float d = 0;
    for (size_t i = 0; i < a.data.size(); i++)
        d = std::max(d, std::fabs(a.data[i] - b.data[i]));
    return d;
}

/// Amplitude of [freq] in channel [c] (single-bin DFT, Goertzel style).
double toneAmplitude(const Signal &s, unsigned int c, float freq, float sr,
                     unsigned int from, unsigned int to) {
    double re = 0, im = 0;
    for (unsigned int i = from; i < to; i++) {
        const double ph = 2 * M_PI * freq * (double)i / sr;
        re += s.at(c, i) * cos(ph);
        im += s.at(c, i) * sin(ph);
    }
    return 2 * std::sqrt(re * re + im * im) / (to - from);
}

// ---------------------------------------------------------------------------

using Params = std::vector<std::pair<unsigned int, float>>;

struct FilterSpec {
    const char *name;
    std::function<SoLoud::Filter *()> make;
    /// Parameter values (wet left at 1) that audibly change the test signal.
    Params effective;
    /// Bound on output RMS / input RMS with [effective] params.
    double maxGain;
    /// Filter-specific check.
    std::function<void(SoLoud::Filter &)> semantic;
};

void genericChecks(const FilterSpec &spec) {
    const float sr = 44100.f;
    const Signal in = makeTestSignal(2, 44100, sr);
    std::unique_ptr<SoLoud::Filter> f(spec.make());

    // Parameter metadata sanity.
    const int count = f->getParamCount();
    CHECK(count >= 1, "%s: getParamCount() = %d", spec.name, count);
    for (int p = 0; p < count; p++) {
        CHECK(f->getParamMin(p) <= f->getParamMax(p), "%s: param %d min %f > max %f",
              spec.name, p, f->getParamMin(p), f->getParamMax(p));
        CHECK(f->getParamName(p) != nullptr, "%s: param %d has no name", spec.name, p);
    }
    CHECK(std::string(f->getParamName(0)) == "Wet", "%s: param 0 is '%s', not Wet",
          spec.name, f->getParamName(0));

    // finite: defaults.
    {
        const Signal out = process(*f, in, sr, {});
        CHECK(allFinite(out), "%s: default params produced NaN/Inf", spec.name);
        CHECK(rmsAll(out) < 8 * rmsAll(in), "%s: default params energy %f vs input %f",
              spec.name, rmsAll(out), rmsAll(in));
    }

    // finite + effect: effective params.
    {
        const Signal out = process(*f, in, sr, spec.effective);
        CHECK(allFinite(out), "%s: effective params produced NaN/Inf", spec.name);
        const double gain = rmsAll(out) / rmsAll(in);
        CHECK(gain < spec.maxGain, "%s: energy gain %f exceeds %f", spec.name, gain,
              spec.maxGain);
        CHECK(peak(out) < 20.f, "%s: peak %f", spec.name, peak(out));
        const float d = maxDiff(out, in);
        CHECK(d > 0.01f, "%s: effective params left the signal unchanged (max diff %g)",
              spec.name, d);
    }

    // bypass: wet = 0 with the same effective params.
    {
        Params p = spec.effective;
        p.push_back({0, 0.f});
        const Signal out = process(*f, in, sr, p);
        CHECK(allFinite(out), "%s: wet=0 produced NaN/Inf", spec.name);
        const float d = maxDiff(out, in);
        CHECK(d < 1e-5f, "%s: wet=0 is not a bypass (max diff %g)", spec.name, d);
    }

    // sweep: random in-range params, sample rates, channel counts.
    {
        std::mt19937 rng(1234);
        const float rates[] = {22050.f, 44100.f, 48000.f, 96000.f};
        const unsigned int chans[] = {1, 2, 6};
        int bad = 0;
        for (int trial = 0; trial < 24; trial++) {
            const float rate = rates[trial % 4];
            const unsigned int ch = chans[trial % 3];
            Params p;
            for (int i = 0; i < count; i++) {
                std::uniform_real_distribution<float> d(f->getParamMin(i), f->getParamMax(i));
                float v = d(rng);
                if (f->getParamType(i) != SoLoud::Filter::FLOAT_PARAM) v = std::round(v);
                p.push_back({(unsigned)i, v});
            }
            // Odd block sizes too: the mixer can hand a filter less than a
            // full granule.
            const unsigned int blk = (trial % 5 == 0) ? 333 : kBlock;
            const Signal sin = makeTestSignal(ch, (unsigned)(rate / 4), rate, trial);
            const Signal out = process(*f, sin, rate, p, blk);
            if (!allFinite(out) || peak(out) > 1e3f) {
                ++bad;
                std::fprintf(stderr, "    sweep trial %d (sr %.0f, ch %u) params:", trial,
                             rate, ch);
                for (auto &q : p) std::fprintf(stderr, " %u=%g", q.first, q.second);
                std::fprintf(stderr, " -> finite=%d peak=%g\n", allFinite(out), peak(out));
            }
        }
        CHECK(bad == 0, "%s: %d sweep trials produced NaN/Inf or exploded", spec.name, bad);
    }
}

// ----- semantic checks -----------------------------------------------------

void biquadSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    const unsigned n = 22050;
    // Low 200 Hz + high 6000 Hz, 0.4 each.
    Signal in(1, n);
    for (unsigned i = 0; i < n; i++)
        in.at(0, i) = 0.4f * sinf(2 * kPi * 200.f * i / sr) +
                      0.4f * sinf(2 * kPi * 6000.f * i / sr);
    using B = SoLoud::BiquadResonantFilter;
    const Signal lp = process(f, in, sr, {{B::TYPE, B::LOWPASS}, {B::FREQUENCY, 800.f},
                                          {B::RESONANCE, 0.7f}});
    const Signal hp = process(f, in, sr, {{B::TYPE, B::HIGHPASS}, {B::FREQUENCY, 2000.f},
                                          {B::RESONANCE, 0.7f}});
    const Signal bp = process(f, in, sr, {{B::TYPE, B::BANDPASS}, {B::FREQUENCY, 6000.f},
                                          {B::RESONANCE, 2.f}});
    const unsigned a = 4410, b = n;
    double lpLow = toneAmplitude(lp, 0, 200, sr, a, b), lpHigh = toneAmplitude(lp, 0, 6000, sr, a, b);
    double hpLow = toneAmplitude(hp, 0, 200, sr, a, b), hpHigh = toneAmplitude(hp, 0, 6000, sr, a, b);
    double bpLow = toneAmplitude(bp, 0, 200, sr, a, b), bpHigh = toneAmplitude(bp, 0, 6000, sr, a, b);
    std::printf("    lowpass 800: 200Hz %.3f 6kHz %.4f | highpass 2k: 200Hz %.4f 6kHz %.3f"
                " | bandpass 6k: 200Hz %.4f 6kHz %.3f\n",
                lpLow, lpHigh, hpLow, hpHigh, bpLow, bpHigh);
    CHECK(lpLow > 0.3 && lpHigh < 0.02, "lowpass: 200Hz %f, 6kHz %f", lpLow, lpHigh);
    CHECK(hpHigh > 0.3 && hpLow < 0.02, "highpass: 200Hz %f, 6kHz %f", hpLow, hpHigh);
    CHECK(bpHigh > 0.2 && bpLow < 0.02, "bandpass: 200Hz %f, 6kHz %f", bpLow, bpHigh);
}

void echoSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using E = SoLoud::EchoFilter;
    Signal in(2, 44100);
    in.at(0, 100) = 1.f;
    in.at(1, 100) = 1.f;
    const float delay = 0.1f, decay = 0.5f;
    const Signal out = process(f, in, sr, {{E::DELAY, delay}, {E::DECAY, decay}, {E::FILTER, 0.f}});
    const unsigned d = (unsigned)std::ceil(delay * sr);
    for (unsigned c = 0; c < 2; c++) {
        CHECK(std::fabs(out.at(c, 100) - 1.f) < 1e-6f, "echo ch%u: direct %f", c, out.at(c, 100));
        CHECK(std::fabs(out.at(c, 100 + d) - decay) < 1e-4f, "echo ch%u: 1st echo %f at +%u",
              c, out.at(c, 100 + d), d);
        CHECK(std::fabs(out.at(c, 100 + 2 * d) - decay * decay) < 1e-4f,
              "echo ch%u: 2nd echo %f", c, out.at(c, 100 + 2 * d));
        CHECK(std::fabs(out.at(c, 100 + d / 2)) < 1e-6f, "echo ch%u: silence between echoes %f",
              c, out.at(c, 100 + d / 2));
    }
}

void lofiSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using L = SoLoud::LofiFilter;
    const Signal in = makeSine(1, 8820, sr, 440.f, 0.9f);
    // 2 bits: floor(q * x) / q with q = 4 -> values in {-1, -0.75, ..., 0.75}.
    const Signal out = process(f, in, sr, {{L::SAMPLERATE, 22000.f}, {L::BITDEPTH, 2.f}});
    std::set<float> levels;
    for (float v : out.data) levels.insert(std::round(v * 4) / 4);
    CHECK(levels.size() <= 8, "lofi 2 bits: %zu distinct levels", levels.size());
    float offGrid = 0;
    for (float v : out.data) offGrid = std::max(offGrid, std::fabs(v * 4 - std::round(v * 4)));
    CHECK(offGrid < 1e-5f, "lofi 2 bits: a value is %g off the 1/4 grid", offGrid);
    // Sample rate reduction: at 1000 Hz every value is held ~44 frames.
    const Signal held = process(f, in, sr, {{L::SAMPLERATE, 1000.f}, {L::BITDEPTH, 16.f}});
    unsigned changes = 0;
    for (unsigned i = 1; i < held.frames; i++)
        // Not exact equality: dry + (held - dry) * 1 may round by an ulp.
        if (std::fabs(held.at(0, i) - held.at(0, i - 1)) > 1e-4f) changes++;
    // 0.2 s at 1 kHz is ~200 updates.
    CHECK(changes >= 150 && changes <= 230, "lofi 1000 Hz: %u value changes in 0.2 s", changes);
}

void flangerSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using F = SoLoud::FlangerFilter;
    // A flanger is a comb filter: a constant (DC) input passes at unity once
    // the delay line is full, while it modulates a tone.
    Signal dc(1, 22050);
    for (auto &v : dc.data) v = 0.5f;
    const Signal out = process(f, dc, sr, {{F::DELAY, 0.005f}, {F::FREQ, 2.f}});
    float maxDev = 0;
    for (unsigned i = 1000; i < out.frames; i++)
        maxDev = std::max(maxDev, std::fabs(out.at(0, i) - 0.5f));
    CHECK(maxDev < 1e-5f, "flanger DC deviation %g", maxDev);
    // On a 2 kHz tone the sweeping notch modulates the envelope.
    const Signal tone = makeSine(1, 44100, sr, 2000.f, 0.5f);
    const Signal t = process(f, tone, sr, {{F::DELAY, 0.005f}, {F::FREQ, 2.f}});
    double lo = 1e9, hi = 0;
    for (unsigned blk = 1000; blk + 441 <= t.frames; blk += 441) {
        const double r = rms(t, 0, blk, blk + 441);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    std::printf("    flanger 2kHz envelope rms min %.3f max %.3f\n", lo, hi);
    CHECK(hi - lo > 0.1, "flanger did not modulate the tone (rms %f..%f)", lo, hi);
}

void bassboostSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using B = SoLoud::BassboostFilter;
    Signal in(1, 44100);
    for (unsigned i = 0; i < in.frames; i++)
        in.at(0, i) = 0.2f * sinf(2 * kPi * 60.f * i / sr) + 0.2f * sinf(2 * kPi * 5000.f * i / sr);
    const Signal out = process(f, in, sr, {{B::BOOST, 4.f}});
    const unsigned a = 4410, b = 44100;
    const double low = toneAmplitude(out, 0, 60, sr, a, b) / 0.2;
    const double high = toneAmplitude(out, 0, 5000, sr, a, b) / 0.2;
    std::printf("    bassboost 4: 60Hz gain %.2f, 5kHz gain %.2f\n", low, high);
    CHECK(low > 2.0, "bassboost: 60 Hz gain %f", low);
    CHECK(high > 0.7 && high < 1.3, "bassboost: 5 kHz gain %f", high);
}

void waveshaperSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using W = SoLoud::WaveShaperFilter;
    // Positive amount: more gain at low level, saturates towards +-1; the
    // curve is odd-symmetric and monotonic for |x| <= 1.
    Signal ramp(1, 2001);
    for (unsigned i = 0; i < ramp.frames; i++) ramp.at(0, i) = -1.f + i / 1000.f;
    const Signal out = process(f, ramp, sr, {{W::AMOUNT, 0.8f}});
    bool monotonic = true, odd = true, bounded = true;
    for (unsigned i = 1; i < out.frames; i++)
        if (out.at(0, i) < out.at(0, i - 1) - 1e-6f) monotonic = false;
    for (unsigned i = 0; i < out.frames; i++) {
        if (std::fabs(out.at(0, i) + out.at(0, out.frames - 1 - i)) > 1e-5f) odd = false;
        if (std::fabs(out.at(0, i)) > 1.f + 1e-5f) bounded = false;
    }
    CHECK(monotonic, "waveshaper curve not monotonic");
    CHECK(odd, "waveshaper curve not odd-symmetric");
    CHECK(bounded, "waveshaper output beyond +-1 for |x| <= 1");
    CHECK(std::fabs(out.at(0, 1100)) > 0.1f * 3, "waveshaper 0.8 should boost x=0.1 (got %f)",
          out.at(0, 1100));
    // amount = 0 is the identity.
    const Signal id = process(f, ramp, sr, {{W::AMOUNT, 0.f}});
    CHECK(maxDiff(id, ramp) < 1e-6f, "waveshaper amount 0 is not identity (%g)", maxDiff(id, ramp));
}

void robotizeSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using R = SoLoud::RobotizeFilter;
    // Square wave modulator at 10 Hz: half of each 100 ms period is muted
    // (multiplier 0), the other half passes (multiplier 1).
    Signal dc(1, 44100);
    for (auto &v : dc.data) v = 0.5f;
    const Signal out = process(f, dc, sr, {{R::FREQ, 10.f}, {R::WAVE, (float)SoLoud::Soloud::WAVE_SQUARE}});
    unsigned zeros = 0, pass = 0;
    for (float v : out.data) {
        if (std::fabs(v) < 1e-6f) zeros++;
        else if (std::fabs(v - 0.5f) < 1e-6f) pass++;
    }
    std::printf("    robotize square 10 Hz: %u muted, %u passed of %u\n", zeros, pass, out.frames);
    CHECK(zeros + pass == out.frames, "robotize square: unexpected levels");
    CHECK(zeros > out.frames * 0.45 && zeros < out.frames * 0.55, "robotize square: %u muted", zeros);
    // The modulation period is continuous across blocks (it follows aTime):
    // count mute/pass transitions, ~2 per period -> ~20 in 1 s.
    unsigned edges = 0;
    for (unsigned i = 1; i < out.frames; i++)
        if ((std::fabs(out.at(0, i)) < 1e-6f) != (std::fabs(out.at(0, i - 1)) < 1e-6f)) edges++;
    CHECK(edges >= 18 && edges <= 21, "robotize square 10 Hz: %u transitions in 1 s", edges);
}

void freeverbSemantic(SoLoud::Filter &f) {
    const float sr = 44100.f;
    using V = SoLoud::FreeverbFilter;
    // An impulse leaves a decaying tail; with a larger room the tail is
    // longer.
    Signal in(2, 88200);
    in.at(0, 10) = 1.f;
    in.at(1, 10) = 1.f;
    const Signal small = process(f, in, sr, {{V::ROOMSIZE, 0.2f}, {V::DAMP, 0.5f}, {V::WIDTH, 1.f}});
    const Signal large = process(f, in, sr, {{V::ROOMSIZE, 0.95f}, {V::DAMP, 0.5f}, {V::WIDTH, 1.f}});
    const double tailSmall = rms(small, 0, 22050, 44100);
    const double tailLarge = rms(large, 0, 22050, 44100);
    const double early = rms(large, 0, 2000, 8000);
    const double late = rms(large, 0, 66150, 88200);
    std::printf("    freeverb tail rms (0.5-1 s): room 0.2 %.2e, room 0.95 %.2e; "
                "large early %.2e late %.2e\n", tailSmall, tailLarge, early, late);
    CHECK(early > 1e-4, "freeverb: no reverb tail (%g)", early);
    CHECK(tailLarge > tailSmall * 2, "freeverb: room size has no effect (%g vs %g)", tailLarge,
          tailSmall);
    CHECK(late < early, "freeverb: tail does not decay (%g -> %g)", early, late);
    // Mono goes through the plugin's mono fallback path.
    Signal mono(1, 22050);
    mono.at(0, 10) = 1.f;
    const Signal m = process(f, mono, sr, {{V::ROOMSIZE, 0.8f}});
    CHECK(allFinite(m) && rms(m, 0, 2000, 22050) > 1e-5, "freeverb mono: no tail");
}

/// A WaveShaper edge case at the edge of its parameter range: with a
/// negative AMOUNT, the curve's denominator 1 + k*|x| reaches 0 for
/// |x| = -1/k, i.e. at +-1 for AMOUNT = -1 (k = -1) and just outside it for
/// AMOUNT close to -1. Mixed voices routinely exceed 1.0 before clipping, so a
/// global WaveShaper can see such input. The output must stay finite, map
/// +-1 to +-1 and not grow beyond the input (bug F2).
void waveshaperHotInputProbe() {
    SoLoud::WaveShaperFilter f;
    using W = SoLoud::WaveShaperFilter;
    const float x[] = {1.0f, -1.0f, 1.05f, 1.5f, -3.0f, 0.5f, 0.0f, 0.99f};
    const unsigned n = sizeof(x) / sizeof(x[0]);
    Signal in(1, n);
    for (unsigned i = 0; i < n; i++) in.at(0, i) = x[i];
    for (float amount : {-1.f, -0.9f, -0.5f}) {
        const Signal o = process(f, in, 44100.f, {{W::AMOUNT, amount}}, n);
        std::printf("    waveshaper AMOUNT=%g: in {1, -1, 1.05, 1.5, -3} -> {%g, %g, %g, %g, %g}\n",
                    amount, o.at(0, 0), o.at(0, 1), o.at(0, 2), o.at(0, 3), o.at(0, 4));
        CHECK(allFinite(o), "waveshaper AMOUNT=%g: NaN/Inf output", amount);
        for (unsigned i = 0; i < n; i++) {
            CHECK(std::fabs(o.at(0, i)) <= std::fabs(x[i]) + 1e-3f,
                  "waveshaper AMOUNT=%g: |out(%g)| = %g exceeds the input", amount, x[i],
                  o.at(0, i));
            // The curve is odd and sign-preserving.
            CHECK(o.at(0, i) * x[i] >= 0.f, "waveshaper AMOUNT=%g: sign flipped at %g -> %g",
                  amount, x[i], o.at(0, i));
        }
        CHECK(std::fabs(o.at(0, 0) - 1.f) < 1e-3f && std::fabs(o.at(0, 1) + 1.f) < 1e-3f,
              "waveshaper AMOUNT=%g: +-1 -> %g, %g (want +-1)", amount, o.at(0, 0), o.at(0, 1));
    }
    // A positive AMOUNT keeps soft-saturating hot input (it never had the
    // problem): 1.5 maps to (1+k)*1.5 / (1+1.5k), between 1 and 1.5.
    const Signal pos = process(f, in, 44100.f, {{W::AMOUNT, 0.5f}}, n);
    const float k = 2 * 0.5f / (1 - 0.5f);
    CHECK(std::fabs(pos.at(0, 3) - (1 + k) * 1.5f / (1 + k * 1.5f)) < 1e-4f,
          "waveshaper AMOUNT=0.5: 1.5 -> %g", pos.at(0, 3));
}

/// The biquad's FREQUENCY range (10..8000 Hz) is fixed, but the device sample
/// rate is not: at 11025 Hz an 8000 Hz cutoff is above Nyquist. The cutoff
/// must be kept below Nyquist so the output stays finite and bounded (bug F3).
void biquadAboveNyquistProbe() {
    SoLoud::BiquadResonantFilter f;
    using B = SoLoud::BiquadResonantFilter;
    const Signal in = makeTestSignal(1, 11025, 11025.f);
    for (int type = 0; type <= 2; type++) {
        for (float freq : {5512.5f, 8000.f}) {
            const Signal out = process(f, in, 11025.f, {{B::TYPE, (float)type},
                                                         {B::FREQUENCY, freq},
                                                         {B::RESONANCE, 20.f}});
            std::printf("    biquad type %d, %g Hz @ 11025 Hz sr, res 20: finite=%d peak=%g\n",
                        type, freq, allFinite(out), peak(out));
            CHECK(allFinite(out) && peak(out) <= 100.f,
                  "biquad type %d, %g Hz above Nyquist: finite=%d peak=%g", type, freq,
                  allFinite(out), peak(out));
        }
    }
    // The parameter itself keeps the value that was set.
    std::unique_ptr<SoLoud::FilterInstance> inst(f.createInstance());
    inst->setFilterParameter(B::FREQUENCY, 8000.f);
    CHECK(inst->getFilterParameter(B::FREQUENCY) == 8000.f, "biquad FREQUENCY not kept");
}

} // namespace

int main(int argc, char **argv) {
    using SoLoud::Soloud;
    const std::vector<FilterSpec> specs = {
        {"biquad", [] { return new SoLoud::BiquadResonantFilter(); },
         {{SoLoud::BiquadResonantFilter::TYPE, 0}, {SoLoud::BiquadResonantFilter::FREQUENCY, 300.f},
          {SoLoud::BiquadResonantFilter::RESONANCE, 2.f}},
         2.0, biquadSemantic},
        {"echo", [] { return new SoLoud::EchoFilter(); },
         {{SoLoud::EchoFilter::DELAY, 0.05f}, {SoLoud::EchoFilter::DECAY, 0.5f}},
         3.0, echoSemantic},
        {"lofi", [] { return new SoLoud::LofiFilter(); },
         {{SoLoud::LofiFilter::SAMPLERATE, 2000.f}, {SoLoud::LofiFilter::BITDEPTH, 3.f}},
         1.5, lofiSemantic},
        {"flanger", [] { return new SoLoud::FlangerFilter(); },
         {{SoLoud::FlangerFilter::DELAY, 0.01f}, {SoLoud::FlangerFilter::FREQ, 5.f}},
         1.5, flangerSemantic},
        {"bassboost", [] { return new SoLoud::BassboostFilter(); },
         {{SoLoud::BassboostFilter::BOOST, 5.f}},
         15.0, bassboostSemantic},
        {"waveshaper", [] { return new SoLoud::WaveShaperFilter(); },
         {{SoLoud::WaveShaperFilter::AMOUNT, 0.9f}},
         4.0, waveshaperSemantic},
        {"robotize", [] { return new SoLoud::RobotizeFilter(); },
         {{SoLoud::RobotizeFilter::FREQ, 30.f}, {SoLoud::RobotizeFilter::WAVE, (float)Soloud::WAVE_SAW}},
         1.01, robotizeSemantic},
        {"freeverb", [] { return new SoLoud::FreeverbFilter(); },
         {{SoLoud::FreeverbFilter::ROOMSIZE, 0.8f}, {SoLoud::FreeverbFilter::DAMP, 0.3f},
          {SoLoud::FreeverbFilter::WIDTH, 1.f}},
         8.0, freeverbSemantic},
    };

    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const FilterSpec &spec : specs) {
        if (only && std::strcmp(only, spec.name) != 0) continue;
        ran++;
        const int before = gFailures;
        std::printf("[%s] generic (finite, effect, bypass, sweep)\n", spec.name);
        genericChecks(spec);
        std::printf("[%s] semantic\n", spec.name);
        std::unique_ptr<SoLoud::Filter> f(spec.make());
        spec.semantic(*f);
        std::printf("  %s %s\n", gFailures == before ? "PASS" : "FAIL", spec.name);
    }
    if (!only || std::strcmp(only, "probes") == 0) {
        ran++;
        std::printf("[probes] parameter-range edge cases\n");
        waveshaperHotInputProbe();
        biquadAboveNyquistProbe();
    }
    if (ran == 0) {
        std::fprintf(stderr, "unknown filter '%s'\n", only);
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

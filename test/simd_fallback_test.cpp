// Standalone native test for the portable scalar fallback of the SIMD layer
// (src/simd/soloud_simd.h), which -DDISABLE_SIMD selects and targets without
// SSE2, NEON or WASM SIMD build. test/run_simd_fallback_test.sh builds this
// file, the engine core and the filters twice, with the host's SIMD backend
// and with -DDISABLE_SIMD, so the fallback must compile as well as compute.
//
//   simd_fallback_test                checks the f32x4 helpers against
//                                     scalar math
//   simd_fallback_test --dump <file>  also mixes a scene and writes it
//   simd_fallback_test --compare <file>
//                                     also mixes the scene and compares it
//                                     with one written by the other build
//
// The scene is a tone and a noise burst through biquad, echo, freeverb,
// compressor and parametric EQ voice filters, a limiter as a global filter,
// on the null driver.
//
// Run with ./test/run_simd_fallback_test.sh from the repository root.

#include "simd/soloud_simd.h"

#include "filters/compressor.h"
#include "filters/limiter.h"
#include "filters/parametric_eq_filter.h"
#include "soloud.h"
#include "soloud_biquadresonantfilter.h"
#include "soloud_echofilter.h"
#include "soloud_freeverbfilter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

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

using soloud_simd::f32x4;

const char *backendName() {
#if defined(SOLOUD_SIMD_SSE2)
    return "SSE2";
#elif defined(SOLOUD_SIMD_NEON)
    return "NEON";
#elif defined(SOLOUD_SIMD_WASM)
    return "WASM SIMD";
#else
    return "scalar fallback";
#endif
}

void lanes(f32x4 v, float out[4]) { v.storeu(out); }

// ---- helpers ----------------------------------------------------------------

void testTranscendentals() {
    std::printf("transcendentals: fast_log2 and fast_exp2 (%s)\n", backendName());

    // log2 over 12 decades, in groups of four distinct lanes so a lane mixup
    // shows as well as an approximation error.
    float worstLog = 0.0f;
    float worstLogScalar = 0.0f;
    for (int i = 0; i < 4800; i += 4) {
        float in[4], out[4];
        for (int l = 0; l < 4; ++l)
            in[l] = std::pow(10.0f, -6.0f + static_cast<float>(i + l) * 0.0025f);
        lanes(soloud_simd::fast_log2(f32x4::loadu(in)), out);
        for (int l = 0; l < 4; ++l) {
            const float exact = std::log2(in[l]);
            worstLog = std::fmax(worstLog, std::fabs(out[l] - exact));
            worstLogScalar = std::fmax(
                worstLogScalar, std::fabs(soloud_simd::fast_log2(in[l]) - exact));
        }
    }
    // The minimax polynomial is good to about 2.2e-4 (0.0013 dB).
    EXPECT(worstLog < 5e-4f, "fast_log2(f32x4) is off by %g", worstLog);
    EXPECT(worstLogScalar < 5e-4f, "fast_log2(float) is off by %g", worstLogScalar);

    // exp2 over [-20, 20], relative error.
    float worstExp = 0.0f;
    float worstExpScalar = 0.0f;
    for (int i = 0; i < 4000; i += 4) {
        float in[4], out[4];
        for (int l = 0; l < 4; ++l)
            in[l] = -20.0f + static_cast<float>(i + l) * 0.01f;
        lanes(soloud_simd::fast_exp2(f32x4::loadu(in)), out);
        for (int l = 0; l < 4; ++l) {
            const float exact = std::exp2(in[l]);
            worstExp = std::fmax(worstExp, std::fabs(out[l] - exact) / exact);
            worstExpScalar = std::fmax(
                worstExpScalar,
                std::fabs(soloud_simd::fast_exp2(in[l]) - exact) / exact);
        }
    }
    EXPECT(worstExp < 1e-4f, "fast_exp2(f32x4) is off by %g (relative)", worstExp);
    EXPECT(worstExpScalar < 1e-4f, "fast_exp2(float) is off by %g (relative)",
           worstExpScalar);

    // The clamps: zero and subnormal inputs to log2, huge exponents to exp2.
    float out[4];
    lanes(soloud_simd::fast_log2(f32x4::set(0.0f, 1e-45f, -1.0f, 1.0f)), out);
    for (int l = 0; l < 3; ++l)
        EXPECT(std::isfinite(out[l]) && out[l] < -125.0f,
               "fast_log2 lane %d of a non-positive input is %g", l, out[l]);
    EXPECT(std::fabs(out[3]) < 1e-6f, "fast_log2(1) is %g", out[3]);
    lanes(soloud_simd::fast_exp2(f32x4::set(-1000.0f, 1000.0f, 0.0f, 1.0f)), out);
    EXPECT(std::isfinite(out[0]) && out[0] > 0.0f && out[0] < 1e-37f,
           "fast_exp2(-1000) is %g", out[0]);
    EXPECT(std::isfinite(out[1]) && out[1] > 1e37f, "fast_exp2(1000) is %g", out[1]);
    EXPECT(out[2] == 1.0f, "fast_exp2(0) is %g", out[2]);
    EXPECT(std::fabs(out[3] - 2.0f) < 1e-4f, "fast_exp2(1) is %g", out[3]);
}

void testArithmetic() {
    std::printf("arithmetic: f32x4 helpers against scalar math (%s)\n", backendName());

    const float a[4] = {1.5f, -2.25f, 3.0f, -0.5f};
    const float b[4] = {0.5f, 4.0f, -1.5f, -0.25f};
    const float c[4] = {2.0f, -1.0f, 0.75f, 8.0f};
    const f32x4 va = f32x4::loadu(a), vb = f32x4::loadu(b), vc = f32x4::loadu(c);
    float out[4];
    int wrong = 0;
    auto check = [&](const char *name, f32x4 v, float (*ref)(float, float, float)) {
        lanes(v, out);
        for (int l = 0; l < 4; ++l)
            if (std::fabs(out[l] - ref(a[l], b[l], c[l])) > 1e-6f) {
                std::fprintf(stderr, "    %s lane %d: %g, expected %g\n", name, l,
                             out[l], ref(a[l], b[l], c[l]));
                ++wrong;
            }
    };
    check("add", soloud_simd::add(va, vb), [](float x, float y, float) { return x + y; });
    check("sub", soloud_simd::sub(va, vb), [](float x, float y, float) { return x - y; });
    check("mul", soloud_simd::mul(va, vb), [](float x, float y, float) { return x * y; });
    check("div", soloud_simd::div(va, vb), [](float x, float y, float) { return x / y; });
    check("fma", soloud_simd::fma(vc, va, vb),
          [](float x, float y, float z) { return z + x * y; });
    check("fms", soloud_simd::fms(vc, va, vb),
          [](float x, float y, float z) { return z - x * y; });
    check("min", soloud_simd::min(va, vb),
          [](float x, float y, float) { return x < y ? x : y; });
    check("max", soloud_simd::max(va, vb),
          [](float x, float y, float) { return x > y ? x : y; });
    check("abs", soloud_simd::abs(va), [](float x, float, float) { return std::fabs(x); });
    check("neg", soloud_simd::neg(va), [](float x, float, float) { return -x; });
    check("sqrt", soloud_simd::sqrt(soloud_simd::abs(va)),
          [](float x, float, float) { return std::sqrt(std::fabs(x)); });
    check("clamp", soloud_simd::clamp(va, f32x4::splat(-1.0f), f32x4::splat(2.0f)),
          [](float x, float, float) { return x < -1.0f ? -1.0f : (x > 2.0f ? 2.0f : x); });
    EXPECT(wrong == 0, "%d lanes differ from scalar math", wrong);
    EXPECT(soloud_simd::reduce_add(va) == 1.75f, "reduce_add is %g",
           soloud_simd::reduce_add(va));
    EXPECT(soloud_simd::reduce_max(va) == 3.0f, "reduce_max is %g",
           soloud_simd::reduce_max(va));
}

// ---- scene ------------------------------------------------------------------

constexpr unsigned int kRate = 44100;
constexpr unsigned int kBlock = 512;
constexpr unsigned int kChannels = 2;
constexpr unsigned int kBlocks = 160;

class SceneInstance : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float *buffer, unsigned int samples,
                          unsigned int bufferSize) override {
        for (unsigned int ch = 0; ch < mChannels; ++ch)
            for (unsigned int i = 0; i < samples; ++i) {
                const unsigned int n = mFrame + i;
                // A tone, plus a noise burst every 8192 frames.
                float s = 0.4f * std::sin(0.0314f * static_cast<float>(n) *
                                          static_cast<float>(ch + 1));
                if ((n & 8191) < 1024) {
                    mNoise = mNoise * 1664525u + 1013904223u;
                    s += 0.5f * (static_cast<float>(mNoise >> 8) / 16777216.0f - 0.5f);
                }
                buffer[ch * bufferSize + i] = s;
            }
        mFrame += samples;
        return samples;
    }
    bool hasEnded() override { return false; }

private:
    unsigned int mFrame = 0;
    unsigned int mNoise = 12345u;
};

class Scene : public SoLoud::AudioSource {
public:
    Scene() {
        mBaseSamplerate = static_cast<float>(kRate);
        mChannels = kChannels;
    }
    SoLoud::AudioSourceInstance *createInstance() override { return new SceneInstance(); }
};

std::vector<float> mixScene() {
    std::vector<float> all;
    SoLoud::Soloud soloud;
    if (soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, kRate,
                    kBlock, kChannels) != SoLoud::SO_NO_ERROR) {
        EXPECT(false, "the null-driver engine should initialize");
        return all;
    }
    Scene scene;
    SoLoud::BiquadResonantFilter biquad;
    biquad.setParams(SoLoud::BiquadResonantFilter::LOWPASS, 3000.0f, 2.0f);
    SoLoud::EchoFilter echo;
    echo.setParams(0.05f, 0.5f, 0.2f);
    SoLoud::FreeverbFilter reverb;
    Compressor compressor(kRate);
    ParametricEq eq(&soloud);
    Limiter limiter(kRate);
    scene.setFilter(0, &biquad);
    scene.setFilter(1, &echo);
    scene.setFilter(2, &reverb);
    scene.setFilter(3, &compressor);
    scene.setFilter(4, &eq);
    soloud.setGlobalFilter(0, &limiter);

    const SoLoud::handle h = soloud.play(scene, 0.8f, -0.3f);
    // Exercise the parameter faders and the resampler too.
    soloud.fadeFilterParameter(h, 3, Compressor::THRESHOLD, -24.0f, 1.0);
    soloud.setRelativePlaySpeed(h, 1.1f);

    std::vector<float> block(kBlock * kChannels);
    for (unsigned int b = 0; b < kBlocks; ++b) {
        soloud.mix(block.data(), kBlock);
        all.insert(all.end(), block.begin(), block.end());
    }
    soloud.stopAll();
    soloud.setGlobalFilter(0, nullptr);
    soloud.deinit();
    return all;
}

void testScene(const char *mode, const char *path) {
    std::printf("scene: filters and the mixer (%s)\n", backendName());
    const std::vector<float> mixed = mixScene();
    if (mixed.empty())
        return;
    int nonFinite = 0;
    float peak = 0.0f;
    for (float s : mixed) {
        if (!std::isfinite(s))
            ++nonFinite;
        else
            peak = std::fmax(peak, std::fabs(s));
    }
    EXPECT(nonFinite == 0, "%d mixed samples are not finite", nonFinite);
    EXPECT(peak > 0.05f && peak <= 1.0f, "the mix peaks at %g", peak);

    if (std::strcmp(mode, "--dump") == 0) {
        FILE *f = std::fopen(path, "wb");
        EXPECT(f != nullptr, "cannot write %s", path);
        if (f != nullptr) {
            std::fwrite(mixed.data(), sizeof(float), mixed.size(), f);
            std::fclose(f);
        }
        return;
    }
    std::vector<float> other(mixed.size());
    FILE *f = std::fopen(path, "rb");
    EXPECT(f != nullptr, "cannot read %s", path);
    if (f == nullptr)
        return;
    const size_t got = std::fread(other.data(), sizeof(float), other.size(), f);
    std::fclose(f);
    EXPECT(got == other.size(), "the other build wrote %zu samples, expected %zu", got,
           other.size());
    float worst = 0.0f;
    size_t worstAt = 0;
    for (size_t i = 0; i < got; ++i) {
        const float d = std::fabs(mixed[i] - other[i]);
        if (d > worst) {
            worst = d;
            worstAt = i;
        }
    }
    std::printf("  largest difference from the other build: %g (sample %zu)\n", worst,
                worstAt);
    EXPECT(worst < 1e-3f, "the scene differs from the other build by %g at sample %zu",
           worst, worstAt);
}

} // namespace

int main(int argc, char **argv) {
    testTranscendentals();
    testArithmetic();
    if (argc > 2 && (std::strcmp(argv[1], "--dump") == 0 ||
                     std::strcmp(argv[1], "--compare") == 0))
        testScene(argv[1], argv[2]);

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

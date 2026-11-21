// Standalone native tests for the mixer-output PCM converter
// (src/mixeroutput/pcm_converter.cpp): convertPcm() and bytesPerSample().
//
//   * bytes_per_sample: 4/1/2/4 for F32LE/S8/S16LE/S32LE, 0 for the
//     compressed formats;
//   * f32: F32LE is a bit-exact copy (NaN payloads included);
//   * s8 / s16 / s32: every value matches a scalar reference: NaN -> 0, the
//     input is clamped to [-1, 1] (so +-Inf and out-of-range values saturate),
//     then scaled by 127 / 32767 / 2^31 and truncated toward zero (no
//     rounding). S32 clamps the positive peak to 2147483520 (largest float
//     below 2^31) so +1.0 never wraps to INT32_MIN;
//   * lengths: counts from 1 to 1000 so both the SIMD body and the scalar tail
//     run, at aligned and misaligned output addresses (the misaligned S16/S32
//     path goes through a 256-sample bounce buffer);
//   * guards: zero samples, null input/output and compressed formats leave
//     the output untouched.
//
// No audio device and no assets are needed.
//
//   ./test/common/pcm_converter_test.sh [--asan] [scenario]
//
// --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
// scenario name runs only that scenario.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "mixeroutput/pcm_converter.h"

namespace {

int gFailures = 0;
int gAssertions = 0;

#define CHECK(condition, format, ...)                                     \
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

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// Independent scalar references.
float clampUnit(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }

int8_t refS8(float x)
{
    if (std::isnan(x)) return 0;
    return static_cast<int8_t>(std::trunc(clampUnit(x) * 127.0f));
}

int16_t refS16(float x)
{
    if (std::isnan(x)) return 0;
    return static_cast<int16_t>(std::trunc(clampUnit(x) * 32767.0f));
}

int32_t refS32(float x)
{
    if (std::isnan(x)) return 0;
    double s = static_cast<double>(static_cast<float>(x * 2147483648.0f));
    if (s < -2147483648.0) s = -2147483648.0;
    if (s > 2147483520.0) s = 2147483520.0;
    return static_cast<int32_t>(std::trunc(s));
}

// Edge values interleaved with an irregular ramp so every SIMD lane sees
// both edge and ordinary values.
std::vector<float> makeInput(size_t n)
{
    static const float edges[] = {
        0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f,
        kInf, -kInf, kNaN, 1.0f + 1e-6f, -1.0f - 1e-6f, 0.99999f, -0.99999f,
        1.0f / 32767.0f, 0.999f / 32767.0f, -0.999f / 32767.0f,
        1.0f / 127.0f, 0.999f / 127.0f, 1e-30f, -1e-30f, 1e-40f /* denormal */,
        0.25f, -0.75f, 1000.0f, -1000.0f};
    const size_t ne = sizeof(edges) / sizeof(edges[0]);
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
    {
        if (i % 3 == 0)
            v[i] = edges[(i / 3) % ne];
        else
            v[i] = std::sin(static_cast<float>(i) * 0.37f) * 1.1f;
    }
    return v;
}

const size_t kLengths[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                           37, 64, 255, 256, 257, 300, 513, 1000};

template <typename T>
T readLE(const uint8_t *p)
{
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

void testBytesPerSample()
{
    CHECK(bytesPerSample(MIXER_OUTPUT_PCM_F32LE) == 4, "F32LE");
    CHECK(bytesPerSample(MIXER_OUTPUT_PCM_S8) == 1, "S8");
    CHECK(bytesPerSample(MIXER_OUTPUT_PCM_S16LE) == 2, "S16LE");
    CHECK(bytesPerSample(MIXER_OUTPUT_PCM_S32LE) == 4, "S32LE");
    CHECK(bytesPerSample(MIXER_OUTPUT_OPUS) == 0, "OPUS");
    CHECK(bytesPerSample(MIXER_OUTPUT_VORBIS) == 0, "VORBIS");
    CHECK(bytesPerSample(MIXER_OUTPUT_FLAC) == 0, "FLAC");
    CHECK(bytesPerSample(MIXER_OUTPUT_WAV) == 0, "WAV");
}

void testF32()
{
    for (size_t n : kLengths)
    {
        for (size_t misalign = 0; misalign < 4; ++misalign)
        {
            std::vector<float> in = makeInput(n);
            std::vector<uint8_t> out(n * 4 + 8, 0xAB);
            convertPcm(in.data(), out.data() + misalign, n,
                       MIXER_OUTPUT_PCM_F32LE);
            CHECK(std::memcmp(out.data() + misalign, in.data(), n * 4) == 0,
                  "n=%zu misalign=%zu not bit-exact", n, misalign);
            CHECK(out[misalign + n * 4] == 0xAB, "n=%zu wrote past end", n);
            for (size_t i = 0; i < misalign; ++i)
                CHECK(out[i] == 0xAB, "n=%zu wrote before start", n);
        }
    }
}

template <typename T>
void checkFormat(MixerOutputFormat fmt, T (*ref)(float), const char *name)
{
    for (size_t n : kLengths)
    {
        for (size_t misalign = 0; misalign < sizeof(T); ++misalign)
        {
            std::vector<float> in = makeInput(n);
            std::vector<uint8_t> out(n * sizeof(T) + 16, 0xCD);
            uint8_t *dst = out.data() + misalign;
            convertPcm(in.data(), dst, n, fmt);
            int bad = 0;
            for (size_t i = 0; i < n; ++i)
            {
                T got = readLE<T>(dst + i * sizeof(T));
                T want = ref(in[i]);
                if (got != want && bad < 5)
                {
                    ++bad;
                    CHECK(false, "%s n=%zu misalign=%zu i=%zu in=%.9g got=%lld want=%lld",
                          name, n, misalign, i, static_cast<double>(in[i]),
                          static_cast<long long>(got), static_cast<long long>(want));
                }
            }
            ++gAssertions;
            CHECK(dst[n * sizeof(T)] == 0xCD, "%s n=%zu wrote past end", name, n);
            for (size_t i = 0; i < misalign; ++i)
                CHECK(out[i] == 0xCD, "%s n=%zu wrote before start", name, n);
        }
    }
}

// Explicit values, so the reference itself is pinned down.
void testKnownValues()
{
    const float in[] = {0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f,
                        kInf, -kInf, kNaN, 0.999f / 32767.0f, -0.999f / 32767.0f};
    const size_t n = sizeof(in) / sizeof(in[0]);

    const int8_t s8[] = {0, 127, -127, 63, -63, 127, -127, 127, -127, 0, 0, 0};
    const int16_t s16[] = {0, 32767, -32767, 16383, -16383, 32767, -32767,
                           32767, -32767, 0, 0, 0};
    const int32_t s32[] = {0, 2147483520, INT32_MIN, 1073741824, -1073741824,
                           2147483520, INT32_MIN, 2147483520, INT32_MIN, 0,
                           /* sub-LSB-of-S16 values: see below */ 0, 0};

    int8_t o8[n];
    convertPcm(in, reinterpret_cast<uint8_t *>(o8), n, MIXER_OUTPUT_PCM_S8);
    for (size_t i = 0; i < n; ++i)
        CHECK(o8[i] == s8[i], "S8 in=%g got=%d want=%d", in[i], o8[i], s8[i]);

    alignas(4) int16_t o16[n];
    convertPcm(in, reinterpret_cast<uint8_t *>(o16), n, MIXER_OUTPUT_PCM_S16LE);
    for (size_t i = 0; i < n; ++i)
        CHECK(o16[i] == s16[i], "S16 in=%g got=%d want=%d", in[i], o16[i], s16[i]);

    alignas(4) int32_t o32[n];
    convertPcm(in, reinterpret_cast<uint8_t *>(o32), n, MIXER_OUTPUT_PCM_S32LE);
    for (size_t i = 0; i < n; ++i)
    {
        // The last two are not tiny in S32 (about +-65470): use the reference.
        int32_t want = (i >= 10) ? refS32(in[i]) : s32[i];
        CHECK(o32[i] == want, "S32 in=%g got=%d want=%d", in[i], o32[i], want);
    }
    // No wrap: the positive peak must stay positive.
    CHECK(o32[1] > 0 && o32[5] > 0 && o32[7] > 0, "S32 peak wrapped negative");

    // Truncation toward zero, not rounding: 0.99999 LSB -> 0, 1.5 LSB -> 1.
    float lsb[] = {0.99999f / 32767.0f, 1.5f / 32767.0f, -1.5f / 32767.0f};
    int16_t ol[3];
    convertPcm(lsb, reinterpret_cast<uint8_t *>(ol), 3, MIXER_OUTPUT_PCM_S16LE);
    CHECK(ol[0] == 0 && ol[1] == 1 && ol[2] == -1,
          "S16 truncation got %d %d %d want 0 1 -1", ol[0], ol[1], ol[2]);
}

void testGuards()
{
    std::vector<float> in = makeInput(64);
    std::vector<uint8_t> out(64 * 4, 0x5A);
    const MixerOutputFormat all[] = {MIXER_OUTPUT_PCM_F32LE, MIXER_OUTPUT_PCM_S8,
                                     MIXER_OUTPUT_PCM_S16LE, MIXER_OUTPUT_PCM_S32LE,
                                     MIXER_OUTPUT_OPUS, MIXER_OUTPUT_VORBIS,
                                     MIXER_OUTPUT_FLAC, MIXER_OUTPUT_WAV};
    for (MixerOutputFormat f : all)
    {
        convertPcm(in.data(), out.data(), 0, f);
        convertPcm(nullptr, out.data(), 64, f);
        convertPcm(in.data(), nullptr, 64, f);  // must not crash
    }
    convertPcm(in.data(), out.data(), 64, MIXER_OUTPUT_OPUS);
    convertPcm(in.data(), out.data(), 64, MIXER_OUTPUT_VORBIS);
    convertPcm(in.data(), out.data(), 64, MIXER_OUTPUT_FLAC);
    convertPcm(in.data(), out.data(), 64, MIXER_OUTPUT_WAV);
    bool untouched = true;
    for (uint8_t b : out) untouched = untouched && b == 0x5A;
    CHECK(untouched, "output modified by a no-op call");
}

struct Scenario
{
    const char *name;
    void (*run)();
};

void testS8() { checkFormat<int8_t>(MIXER_OUTPUT_PCM_S8, refS8, "S8"); }
void testS16() { checkFormat<int16_t>(MIXER_OUTPUT_PCM_S16LE, refS16, "S16"); }
void testS32() { checkFormat<int32_t>(MIXER_OUTPUT_PCM_S32LE, refS32, "S32"); }

const Scenario kScenarios[] = {
    {"bytes_per_sample", testBytesPerSample},
    {"f32", testF32},
    {"known_values", testKnownValues},
    {"s8", testS8},
    {"s16", testS16},
    {"s32", testS32},
    {"guards", testGuards},
};

}  // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const Scenario &s : kScenarios)
    {
        if (only != nullptr && std::strcmp(only, s.name) != 0) continue;
        const int before = gFailures;
        s.run();
        ++ran;
        std::printf("%s %s\n", gFailures == before ? "PASS" : "FAIL", s.name);
    }
    if (ran == 0)
    {
        std::fprintf(stderr, "unknown scenario: %s\n", only);
        return 2;
    }
    std::printf("%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

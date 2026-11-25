// Standalone tests for src/simd/simd_pcm.h: the SIMD kernels against the
// same header built with -DDISABLE_SIMD (test/support/simd_pcm_scalar_ref.cpp)
// and against a per-sample reference that follows the header's own scalar
// tail loops.
//
//   - interleave / deinterleave: stereo, quad, 1..9 channels into a planar
//     buffer with a stride, int16 stereo, planar int32 -> interleaved float;
//   - int -> float: every int8 / int16 value, int32 extremes;
//   - float -> int with clamp (s8, s16, s32, s32 scaled): NaN, +-Inf, +-0,
//     values beyond +-1, denormals, huge values;
//   - reductions (sum of squares, peak) and in-place clippers;
//   - lengths 0..33 (tails that are not a multiple of 4/8/16) and source /
//     destination pointers offset by 1..3 elements (not 16-byte aligned).
//
// Data movement and conversions must be bit-identical across the three.
// Reductions and the roundoff clipper are compared with a tolerance, since
// the SIMD path sums in a different order; their NaN behaviour must still
// agree.
//
//   ./test/common/simd_pcm_test.sh [--asan] [--strict]

#include "simd/simd_pcm.h"

#define SIMD_PCM_VARIANT simd_pcm_simd
#define SIMD_PCM_DEFINE_WRAPPERS
#include "../support/simd_pcm_variant.inc"
#undef SIMD_PCM_DEFINE_WRAPPERS
#undef SIMD_PCM_VARIANT
#define SIMD_PCM_VARIANT simd_pcm_scalar
#include "../support/simd_pcm_variant.inc"

#include "../support/robustness_checks.h"

#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace S = simd_pcm_simd;
namespace R = simd_pcm_scalar;

namespace {

constexpr size_t kMaxLen = 33;
constexpr size_t kPad = 8;
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

std::mt19937 gRng(12345);

bool sameBits(float a, float b) {
  uint32_t x, y;
  std::memcpy(&x, &a, 4);
  std::memcpy(&y, &b, 4);
  if (std::isnan(a) && std::isnan(b)) return true; // any NaN payload
  return x == y;
}

const char *simdName() {
#if defined(SOLOUD_SIMD_NEON)
  return "NEON";
#elif defined(SOLOUD_SIMD_SSE2)
  return "SSE2";
#elif defined(SOLOUD_SIMD_WASM)
  return "WASM";
#else
  return "none";
#endif
}

// Test floats: normal audio, specials, beyond-range values.
float sampleValue(size_t i, bool specials) {
  static const float kSpecial[] = {
      kNaN,      kInf,         -kInf,     1.0f,      -1.0f,
      0.0f,      -0.0f,        1.0000001f, -1.0000001f, 1.5f,
      -1.5f,     1e30f,        -1e30f,    1e-40f,    -1e-40f,
      0.999999f, -0.999999f,   0.5f,      -0.5f,     2.0f};
  if (specials && (gRng() % 3) == 0)
    return kSpecial[(i * 7 + gRng()) % (sizeof(kSpecial) / sizeof(float))];
  std::uniform_real_distribution<float> d(-1.2f, 1.2f);
  return d(gRng);
}

// --- per-sample references (the header's scalar tail loops) ---------------
int8_t refS8(float x) {
  if (std::isnan(x)) return 0;
  x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
  return static_cast<int8_t>(x * 127.0f);
}
int16_t refS16(float x) {
  if (std::isnan(x)) return 0;
  x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
  return static_cast<int16_t>(x * 32767.0f);
}
int32_t refS32Scaled(float x, float scale) {
  if (std::isnan(x)) return 0;
  x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
  return static_cast<int32_t>(x * scale);
}
int32_t refS32(float x) {
  if (std::isnan(x)) return 0;
  float s = x * 2147483648.0f;
  if (s < -2147483648.0f) s = -2147483648.0f;
  if (s > 2147483520.0f) s = 2147483520.0f;
  return static_cast<int32_t>(s);
}
float refRoundoff(float c, float post) {
  if (std::isnan(c))
    c = 0.0f;
  else if (c <= -1.65f)
    c = -0.9862875f;
  else if (c >= 1.65f)
    c = 0.9862875f;
  else
    c = 0.87f * c - 0.1f * c * c * c;
  return c * post;
}

// Run [body] for every length 0..kMaxLen and pointer offset 0..3.
template <typename F> void forLengths(F body) {
  for (size_t off = 0; off < 4; ++off)
    for (size_t n = 0; n <= kMaxLen; ++n) body(n, off);
}

int gReportedFirst = 0;
#define FIRST_DIFF(name, n, off, i)                                            \
  do {                                                                         \
    if (gReportedFirst++ < 20)                                                 \
      std::printf("    first diff in %s: n=%zu off=%zu i=%zu\n", name, n, off, \
                  i);                                                          \
  } while (0)

void testDeinterleave() {
  std::printf("deinterleave / interleave\n");
  int bad = 0, guard = 0;
  forLengths([&](size_t n, size_t off) {
    // Stereo / quad.
    std::vector<float> src(kPad + n * 4 + kPad);
    for (auto &v : src) v = sampleValue(0, true);
    std::vector<float> a[4], b[4];
    for (int c = 0; c < 4; ++c) {
      a[c].assign(kPad + n + kPad, -7.0f);
      b[c].assign(kPad + n + kPad, -7.0f);
    }
    const float *s = src.data() + off;
    S::deinterleaveStereoFloats(s, a[0].data() + off, a[1].data() + off, n);
    R::deinterleaveStereoFloats(s, b[0].data() + off, b[1].data() + off, n);
    for (size_t i = 0; i < n; ++i) {
      const bool ok = sameBits(a[0][off + i], s[i * 2]) &&
                      sameBits(a[1][off + i], s[i * 2 + 1]) &&
                      sameBits(b[0][off + i], s[i * 2]) &&
                      sameBits(b[1][off + i], s[i * 2 + 1]);
      if (!ok) { ++bad; FIRST_DIFF("deinterleaveStereoFloats", n, off, i); }
    }
    for (int c = 0; c < 2; ++c)
      if (a[c][off + n] != -7.0f || b[c][off + n] != -7.0f ||
          (off > 0 && a[c][off - 1] != -7.0f))
        ++guard;

    for (int c = 0; c < 4; ++c) {
      a[c].assign(kPad + n + kPad, -7.0f);
      b[c].assign(kPad + n + kPad, -7.0f);
    }
    S::deinterleaveQuadFloats(s, a[0].data() + off, a[1].data() + off,
                              a[2].data() + off, a[3].data() + off, n);
    R::deinterleaveQuadFloats(s, b[0].data() + off, b[1].data() + off,
                              b[2].data() + off, b[3].data() + off, n);
    for (size_t i = 0; i < n; ++i)
      for (int c = 0; c < 4; ++c)
        if (!sameBits(a[c][off + i], s[i * 4 + c]) ||
            !sameBits(b[c][off + i], s[i * 4 + c])) {
          ++bad;
          FIRST_DIFF("deinterleaveQuadFloats", n, off, i);
        }
    for (int c = 0; c < 4; ++c)
      if (a[c][off + n] != -7.0f || b[c][off + n] != -7.0f) ++guard;

    // Interleave stereo.
    std::vector<float> outA(kPad + 2 * n + kPad, -7.0f), outB = outA;
    const float *l = src.data() + off, *r = src.data() + off + n;
    S::interleaveStereoFloats(l, r, outA.data() + off, n);
    R::interleaveStereoFloats(l, r, outB.data() + off, n);
    for (size_t i = 0; i < n; ++i)
      if (!sameBits(outA[off + 2 * i], l[i]) ||
          !sameBits(outA[off + 2 * i + 1], r[i]) ||
          !sameBits(outB[off + 2 * i], l[i]) ||
          !sameBits(outB[off + 2 * i + 1], r[i])) {
        ++bad;
        FIRST_DIFF("interleaveStereoFloats", n, off, i);
      }
    if (outA[off + 2 * n] != -7.0f || outB[off + 2 * n] != -7.0f) ++guard;

    // N channels into planar with a stride, 1..9 channels.
    for (uint32_t ch = 1; ch <= 9; ++ch) {
      std::vector<float> isrc(kPad + n * ch + kPad);
      for (auto &v : isrc) v = sampleValue(0, true);
      const size_t stride = n + 3;
      std::vector<float> pa(kPad + stride * ch + kPad, -7.0f), pb = pa;
      S::deinterleaveFloatsToPlanar(isrc.data() + off, pa.data() + off, stride,
                                    n, ch);
      R::deinterleaveFloatsToPlanar(isrc.data() + off, pb.data() + off, stride,
                                    n, ch);
      for (uint32_t c = 0; c < ch; ++c) {
        for (size_t i = 0; i < n; ++i) {
          const float want = isrc[off + i * ch + c];
          if (!sameBits(pa[off + c * stride + i], want) ||
              !sameBits(pb[off + c * stride + i], want)) {
            ++bad;
            FIRST_DIFF("deinterleaveFloatsToPlanar", n, off, i);
          }
        }
        // The stride gap after each channel is untouched.
        for (size_t i = n; i < stride; ++i)
          if (pa[off + c * stride + i] != -7.0f ||
              pb[off + c * stride + i] != -7.0f)
            ++guard;
      }
    }
  });
  CHECK(bad == 0, "%d interleave/deinterleave samples differ", bad);
  CHECK(guard == 0, "%d writes outside the destination range", guard);
}

void testIntToFloat() {
  std::printf("int -> float\n");
  int bad = 0;
  // Every int8 / int16 value, at every offset and tail length.
  std::vector<int8_t> s8(256 + kPad);
  for (int i = 0; i < 256; ++i) s8[i] = static_cast<int8_t>(i - 128);
  std::vector<int16_t> s16(65536 + kPad);
  for (int i = 0; i < 65536; ++i) s16[i] = static_cast<int16_t>(i - 32768);
  for (size_t off = 0; off < 4; ++off) {
    for (size_t n : {size_t(0), size_t(1), size_t(15), size_t(17),
                     size_t(256 - off)}) {
      std::vector<float> a(n + kPad, -7.0f), b = a;
      S::convertS8ToFloats(s8.data() + off, a.data() + (off & 1), n);
      R::convertS8ToFloats(s8.data() + off, b.data() + (off & 1), n);
      for (size_t i = 0; i < n; ++i) {
        const float want = static_cast<float>(s8[off + i]) / 128.0f;
        if (!sameBits(a[(off & 1) + i], want) ||
            !sameBits(b[(off & 1) + i], want)) {
          ++bad;
          FIRST_DIFF("convertS8ToFloats", n, off, i);
        }
      }
    }
    for (size_t n : {size_t(0), size_t(7), size_t(9), size_t(33),
                     size_t(65536 - off)}) {
      std::vector<float> a(n + kPad, -7.0f), b = a;
      S::convertInt16ToFloats(s16.data() + off, a.data() + (off & 1), n);
      R::convertInt16ToFloats(s16.data() + off, b.data() + (off & 1), n);
      for (size_t i = 0; i < n; ++i) {
        const float want = static_cast<float>(s16[off + i]) / 32768.0f;
        if (!sameBits(a[(off & 1) + i], want) ||
            !sameBits(b[(off & 1) + i], want)) {
          ++bad;
          FIRST_DIFF("convertInt16ToFloats", n, off, i);
        }
      }
      const size_t frames = n / 2;
      std::vector<float> la(frames + kPad, -7.0f), ra = la, lb = la, rb = la;
      S::deinterleaveStereoInt16ToFloats(s16.data() + off, la.data(),
                                         ra.data() + (off & 1), frames);
      R::deinterleaveStereoInt16ToFloats(s16.data() + off, lb.data(),
                                         rb.data() + (off & 1), frames);
      for (size_t i = 0; i < frames; ++i) {
        const float wl = static_cast<float>(s16[off + 2 * i]) / 32768.0f;
        const float wr = static_cast<float>(s16[off + 2 * i + 1]) / 32768.0f;
        if (!sameBits(la[i], wl) || !sameBits(lb[i], wl) ||
            !sameBits(ra[(off & 1) + i], wr) ||
            !sameBits(rb[(off & 1) + i], wr)) {
          ++bad;
          FIRST_DIFF("deinterleaveStereoInt16ToFloats", frames, off, i);
        }
      }
      if (la[frames] != -7.0f || lb[frames] != -7.0f) ++bad;
    }
  }

  // int32: extremes and random values with the three documented scales.
  const float scales[] = {1.0f / 2147483648.0f, 1.0f / 8388608.0f,
                          1.0f / 32768.0f};
  forLengths([&](size_t n, size_t off) {
    std::vector<int32_t> src(n + kPad);
    for (size_t i = 0; i < src.size(); ++i) {
      switch (gRng() % 5) {
      case 0: src[i] = std::numeric_limits<int32_t>::min(); break;
      case 1: src[i] = std::numeric_limits<int32_t>::max(); break;
      case 2: src[i] = static_cast<int32_t>(gRng() % 65536) - 32768; break;
      default: src[i] = static_cast<int32_t>(gRng());
      }
    }
    for (float inv : scales) {
      std::vector<float> a(n + kPad, -7.0f), b = a;
      S::convertInt32ToFloats(src.data() + (off & 3) % (kPad / 2),
                              a.data() + off, n, inv);
      R::convertInt32ToFloats(src.data() + (off & 3) % (kPad / 2),
                              b.data() + off, n, inv);
      for (size_t i = 0; i < n; ++i) {
        const float want =
            static_cast<float>(src[(off & 3) % (kPad / 2) + i]) * inv;
        if (!sameBits(a[off + i], want) || !sameBits(b[off + i], want)) {
          ++bad;
          FIRST_DIFF("convertInt32ToFloats", n, off, i);
        }
      }
      if (a[off + n] != -7.0f) ++bad;
      // Planar int32 -> interleaved float, 1..6 channels.
      for (uint32_t ch = 1; ch <= 6; ++ch) {
        std::vector<std::vector<int32_t>> planes(ch);
        std::vector<const int32_t *> ptrs(ch);
        for (uint32_t c = 0; c < ch; ++c) {
          planes[c].resize(n + kPad);
          for (auto &v : planes[c]) v = static_cast<int32_t>(gRng());
          ptrs[c] = planes[c].data() + ((off + c) & 3);
        }
        std::vector<float> ia(n * ch + kPad, -7.0f), ib = ia;
        S::interleavePlanarInt32ToFloats(ptrs.data(), ia.data() + (off & 1), n,
                                         ch, inv);
        R::interleavePlanarInt32ToFloats(ptrs.data(), ib.data() + (off & 1), n,
                                         ch, inv);
        for (size_t i = 0; i < n; ++i)
          for (uint32_t c = 0; c < ch; ++c) {
            const float want = static_cast<float>(ptrs[c][i]) * inv;
            if (!sameBits(ia[(off & 1) + i * ch + c], want) ||
                !sameBits(ib[(off & 1) + i * ch + c], want)) {
              ++bad;
              FIRST_DIFF("interleavePlanarInt32ToFloats", n, off, i);
            }
          }
        if (ia[(off & 1) + n * ch] != -7.0f) ++bad;
      }
    }
  });
  CHECK(bad == 0, "%d int->float samples differ", bad);
}

void testFloatToInt() {
  std::printf("float -> int with clamp (NaN, Inf, out of range)\n");
  int badS8 = 0, badS16 = 0, badS32 = 0, badScaled = 0, guard = 0;
  // Longer runs too, so the 16-wide NEON / SSE bodies are reached.
  for (size_t off = 0; off < 4; ++off) {
    for (size_t n = 0; n <= 70; ++n) {
      std::vector<float> src(n + kPad);
      for (size_t i = 0; i < src.size(); ++i) src[i] = sampleValue(i, true);
      const float *s = src.data() + off;

      std::vector<int8_t> a8(n + kPad, 99), b8 = a8;
      S::convertFloatsToS8Clamped(s, a8.data() + (off & 1), n);
      R::convertFloatsToS8Clamped(s, b8.data() + (off & 1), n);
      for (size_t i = 0; i < n; ++i)
        if (a8[(off & 1) + i] != refS8(s[i]) ||
            b8[(off & 1) + i] != refS8(s[i])) {
          ++badS8;
          if (badS8 < 4)
            std::printf("    s8 x=%g simd=%d scalar=%d ref=%d\n", s[i],
                        a8[(off & 1) + i], b8[(off & 1) + i], refS8(s[i]));
        }
      if (a8[(off & 1) + n] != 99 || b8[(off & 1) + n] != 99) ++guard;

      std::vector<int16_t> a16(n + kPad, 999), b16 = a16;
      S::convertFloatsToInt16Clamped(s, a16.data() + (off & 1), n);
      R::convertFloatsToInt16Clamped(s, b16.data() + (off & 1), n);
      for (size_t i = 0; i < n; ++i)
        if (a16[(off & 1) + i] != refS16(s[i]) ||
            b16[(off & 1) + i] != refS16(s[i])) {
          ++badS16;
          if (badS16 < 4)
            std::printf("    s16 x=%g simd=%d scalar=%d ref=%d\n", s[i],
                        a16[(off & 1) + i], b16[(off & 1) + i], refS16(s[i]));
        }
      if (a16[(off & 1) + n] != 999 || b16[(off & 1) + n] != 999) ++guard;

      std::vector<int32_t> a32(n + kPad, 999), b32 = a32;
      S::convertFloatsToInt32Clamped(s, a32.data() + (off & 1), n);
      R::convertFloatsToInt32Clamped(s, b32.data() + (off & 1), n);
      for (size_t i = 0; i < n; ++i)
        if (a32[(off & 1) + i] != refS32(s[i]) ||
            b32[(off & 1) + i] != refS32(s[i])) {
          ++badS32;
          if (badS32 < 4)
            std::printf("    s32 x=%g simd=%d scalar=%d ref=%d\n", s[i],
                        a32[(off & 1) + i], b32[(off & 1) + i], refS32(s[i]));
        }
      if (a32[(off & 1) + n] != 999 || b32[(off & 1) + n] != 999) ++guard;

      for (float scale : {32767.0f, 8388607.0f}) {
        std::vector<int32_t> c32(n + kPad, 999), d32 = c32;
        S::convertFloatsToInt32ScaledClamped(s, c32.data() + (off & 1), n,
                                             scale);
        R::convertFloatsToInt32ScaledClamped(s, d32.data() + (off & 1), n,
                                             scale);
        for (size_t i = 0; i < n; ++i)
          if (c32[(off & 1) + i] != refS32Scaled(s[i], scale) ||
              d32[(off & 1) + i] != refS32Scaled(s[i], scale)) {
            ++badScaled;
            if (badScaled < 4)
              std::printf("    s32 scaled %g x=%g simd=%d scalar=%d ref=%d\n",
                          scale, s[i], c32[(off & 1) + i], d32[(off & 1) + i],
                          refS32Scaled(s[i], scale));
          }
        if (c32[(off & 1) + n] != 999) ++guard;
      }
    }
  }
  CHECK(badS8 == 0, "%d s8 samples differ", badS8);
  CHECK(badS16 == 0, "%d s16 samples differ", badS16);
  CHECK(badS32 == 0, "%d s32 samples differ", badS32);
  CHECK(badScaled == 0, "%d scaled s32 samples differ", badScaled);
  CHECK(guard == 0, "%d writes past the end", guard);
}

bool close(double a, double b, double rel) {
  if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
  if (std::isinf(a) || std::isinf(b)) return a == b;
  return std::fabs(a - b) <= rel * std::max(1.0, std::fabs(b));
}

void testReductionsAndClip() {
  std::printf("reductions and clippers\n");
  int badSum = 0, badPeak = 0, badHard = 0, badRound = 0;
  int nanPeak = 0, nanHard = 0, nanRound = 0;
  for (size_t off = 0; off < 4; ++off) {
    for (size_t n = 0; n <= kMaxLen; ++n) {
      for (int withSpecials = 0; withSpecials < 2; ++withSpecials) {
        std::vector<float> src(n + kPad);
        for (auto &v : src) v = sampleValue(0, false) * 1.6f;
        // A single special value at a random position, so a mismatch can be
        // attributed to it.
        bool hasNaN = false;
        if (withSpecials && n > 0) {
          const float sp[] = {kNaN, kInf, -kInf, 3.0f, -3.0f};
          const float v = sp[gRng() % 5];
          src[off + gRng() % n] = v;
          hasNaN = std::isnan(v);
        }
        const float *s = src.data() + off;

        // Reference sum / peak in double, per element.
        double refSum = 0.0;
        float refPeak = 0.0f;
        for (size_t i = 0; i < n; ++i) {
          refSum += static_cast<double>(s[i]) * s[i];
          const float a = std::fabs(s[i]);
          if (a > refPeak) refPeak = a;
        }
        const double sa = S::sumOfSquaresF32(s, n);
        const double sb = R::sumOfSquaresF32(s, n);
        if (!close(sa, refSum, 1e-5) || !close(sb, refSum, 1e-5)) ++badSum;

        const float pa = S::peakAbsF32(s, n);
        const float pb = R::peakAbsF32(s, n);
        if (hasNaN) {
          // A NaN counts as 0 (refPeak skips it) in both builds.
          if (!sameBits(pa, pb) || !sameBits(pa, refPeak)) {
            ++nanPeak;
            if (nanPeak < 3)
              std::printf("    peakAbs with NaN n=%zu: simd=%g scalar=%g "
                          "ref=%g\n", n, pa, pb, refPeak);
          }
        } else if (!sameBits(pa, refPeak) || !sameBits(pb, refPeak)) {
          ++badPeak;
        }

        for (float post : {1.0f, 0.5f}) {
          std::vector<float> ha(src), hb(src);
          S::clipHardInPlaceF32(ha.data() + off, n, -1.0f, 1.0f, post);
          R::clipHardInPlaceF32(hb.data() + off, n, -1.0f, 1.0f, post);
          for (size_t i = 0; i < n; ++i) {
            float x = s[i];
            if (std::isnan(x)) x = 0.0f;
            if (x < -1.0f) x = -1.0f;
            if (x > 1.0f) x = 1.0f;
            const float want = post == 1.0f ? x : x * post;
            if (std::isnan(s[i])) {
              if (!sameBits(ha[off + i], hb[off + i]) ||
                  !sameBits(ha[off + i], want)) {
                ++nanHard;
                if (nanHard < 3)
                  std::printf("    clipHard NaN n=%zu i=%zu: simd=%g "
                              "scalar=%g tail=%g\n",
                              n, i, ha[off + i], hb[off + i], want);
              }
            } else if (!sameBits(ha[off + i], want) ||
                       !sameBits(hb[off + i], want)) {
              ++badHard;
            }
          }
          if (ha[off + n] != src[off + n]) ++badHard; // no write past end

          std::vector<float> ra(src), rb(src);
          S::clipRoundoffInPlaceF32(ra.data() + off, n, post);
          R::clipRoundoffInPlaceF32(rb.data() + off, n, post);
          for (size_t i = 0; i < n; ++i) {
            const float want = refRoundoff(s[i], post);
            if (std::isnan(s[i])) {
              if (!sameBits(ra[off + i], rb[off + i]) ||
                  !sameBits(ra[off + i], want)) {
                ++nanRound;
                if (nanRound < 3)
                  std::printf("    clipRoundoff NaN n=%zu i=%zu: simd=%g "
                              "scalar=%g tail=%g\n",
                              n, i, ra[off + i], rb[off + i], want);
              }
            } else if (!close(ra[off + i], want, 1e-6) ||
                       !close(rb[off + i], want, 1e-6)) {
              ++badRound;
            }
          }
          if (ra[off + n] != src[off + n]) ++badRound;
        }
      }
    }
  }
  CHECK(badSum == 0, "%d sumOfSquares results off", badSum);
  CHECK(badPeak == 0, "%d peakAbs results differ", badPeak);
  CHECK(badHard == 0, "%d clipHard samples differ", badHard);
  CHECK(badRound == 0, "%d clipRoundoff samples off", badRound);
  // A NaN sample is 0 whether it lands in the SIMD body or in the scalar
  // tail, and in the SIMD and the scalar build.
  CHECK(nanPeak == 0,
        "peakAbsF32: %d buffers with a NaN give a different peak in the SIMD "
        "and the scalar build, or not the peak of the other samples",
        nanPeak);
  CHECK(nanHard == 0,
        "clipHardInPlaceF32: %d NaN samples not clipped to 0 by the vector "
        "body and the scalar tail alike",
        nanHard);
  CHECK(nanRound == 0,
        "clipRoundoffInPlaceF32: %d NaN samples not clipped to 0 by the "
        "vector body and the scalar tail alike",
        nanRound);
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  std::printf("SIMD backend: %s\n", simdName());
  testDeinterleave();
  testIntToFloat();
  testFloatToInt();
  testReductionsAndClip();
  return robustness::finish("simd_pcm_test");
}

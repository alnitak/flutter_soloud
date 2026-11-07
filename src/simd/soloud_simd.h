#pragma once

#ifndef FLUTTER_SOLOUD_SIMD_H
#define FLUTTER_SOLOUD_SIMD_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

// -----------------------------------------------------------------------------
// Multiplatform SIMD ISA Detection
// IMPORTANT: On Windows ARM64EC, MSVC defines BOTH _M_X64 and _M_ARM64EC.
// Always check ARM64 / ARM64EC BEFORE x86_64!
// -----------------------------------------------------------------------------
#if !defined(DISABLE_SIMD)
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) ||    \
    defined(_M_ARM64) || defined(_M_ARM64EC)
#include <arm_neon.h>
#define SOLOUD_SIMD_NEON 1
#if defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)
#define SOLOUD_SIMD_NEON_AARCH64 1
#endif
#elif defined(__SSE2__) || defined(_M_X64) ||                                  \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#include <xmmintrin.h>
#if defined(__FMA__) || defined(__AVX2__)
#include <immintrin.h>
#endif
#define SOLOUD_SIMD_SSE2 1
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define SOLOUD_SIMD_WASM 1
#endif
#endif

#if defined(_MSC_VER)
#include <float.h>
#include <intrin.h>
#endif

namespace soloud_simd {

/// Bitwise IEEE-754 finite check (returns false for NaN and +/-Inf, true for
/// all normal, subnormal, and +/-0.0f values).
/// Immune to compiler `-ffast-math` / `-ffinite-math-only` / `/fp:fast` flags
/// that fold `std::isnan` and `std::isfinite` to compile-time constants.
inline bool isFiniteBits(float x) noexcept {
  uint32_t bits = 0;
  std::memcpy(&bits, &x, sizeof(uint32_t));
  return (bits & 0x7F800000u) != 0x7F800000u;
}

inline bool isNanBits(float x) noexcept {
  uint32_t bits = 0;
  std::memcpy(&bits, &x, sizeof(uint32_t));
  return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0u;
}

inline float sanitizeFinite(float x, float fallback = 0.0f) noexcept {
  return isFiniteBits(x) ? x : fallback;
}

// -----------------------------------------------------------------------------
// RAII Hardware Subnormal Flushing (FTZ / DAZ)
// Prevents 10x-100x CPU stalls in IIR filters, reverbs, and compressors when
// audio decays toward silence across x86_64, x86, ARM64, and ARMv7.
// -----------------------------------------------------------------------------
class ScopedFlushDenormals {
 public:
  explicit ScopedFlushDenormals(bool enabled = true) noexcept {
    if (!enabled) return;
#if defined(SOLOUD_SIMD_SSE2)
    mPrev = _mm_getcsr();
    constexpr unsigned int kFtzDaz = 0x8040u;  // FTZ (bit 15) | DAZ (bit 6)
    if ((mPrev & kFtzDaz) != kFtzDaz) {
      _mm_setcsr(mPrev | kFtzDaz);
      mChanged = true;
    }
#elif defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))
#if defined(ARM64_FPCR)
    mPrev = static_cast<uint64_t>(_ReadStatusReg(ARM64_FPCR));
    constexpr uint64_t kFz = (1ULL << 24);
    if ((mPrev & kFz) == 0) {
      _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(mPrev | kFz));
      mChanged = true;
    }
#else
    unsigned int current = 0;
    if (_controlfp_s(&current, 0, 0) == 0) {
      mPrev = current;
      if ((current & _MCW_DN) != _DN_FLUSH) {
        unsigned int dummy = 0;
        _controlfp_s(&dummy, _DN_FLUSH, _MCW_DN);
        mChanged = true;
      }
    }
#endif
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    mPrev = fpcr;
    constexpr uint64_t kFz = (1ULL << 24);
    if ((fpcr & kFz) == 0) {
      const uint64_t next = fpcr | kFz;
      __asm__ __volatile__("msr fpcr, %0" : : "r"(next));
      mChanged = true;
    }
#elif defined(__arm__) && (defined(__ARM_FP) || defined(__VFP_FP__)) &&        \
    (defined(__GNUC__) || defined(__clang__))
    uint32_t fpscr = 0;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    mPrev = fpscr;
    constexpr uint32_t kFz = (1u << 24);
    if ((fpscr & kFz) == 0) {
      const uint32_t next = fpscr | kFz;
      __asm__ __volatile__("vmsr fpscr, %0" : : "r"(next));
      mChanged = true;
    }
#endif
  }

  ~ScopedFlushDenormals() noexcept {
    if (!mChanged) return;
#if defined(SOLOUD_SIMD_SSE2)
    _mm_setcsr(static_cast<unsigned int>(mPrev));
#elif defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))
#if defined(ARM64_FPCR)
    _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(mPrev));
#else
    unsigned int dummy = 0;
    _controlfp_s(&dummy, static_cast<unsigned int>(mPrev) & _MCW_DN, _MCW_DN);
#endif
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    __asm__ __volatile__("msr fpcr, %0" : : "r"(mPrev));
#elif defined(__arm__) && (defined(__ARM_FP) || defined(__VFP_FP__)) &&        \
    (defined(__GNUC__) || defined(__clang__))
    const uint32_t prev32 = static_cast<uint32_t>(mPrev);
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(prev32));
#endif
  }

  ScopedFlushDenormals(const ScopedFlushDenormals &) = delete;
  ScopedFlushDenormals &operator=(const ScopedFlushDenormals &) = delete;

 private:
  uint64_t mPrev = 0;
  bool mChanged = false;
};

// -----------------------------------------------------------------------------
// Cache-Line / SIMD Aligned Memory Allocation (64-byte aligned by default)
// -----------------------------------------------------------------------------
inline void *alignedAllocBytes(size_t sizeBytes,
                               size_t alignment = 64) noexcept {
  if (sizeBytes == 0) sizeBytes = alignment;
  if (alignment < sizeof(void *)) alignment = sizeof(void *);
  const size_t total = sizeBytes + alignment + sizeof(void *);
  unsigned char *raw =
      static_cast<unsigned char *>(std::malloc(total));
  if (!raw) return nullptr;
  const uintptr_t alignedAddr =
      (reinterpret_cast<uintptr_t>(raw) + sizeof(void *) + (alignment - 1)) &
      ~(alignment - 1);
  void **slot = reinterpret_cast<void **>(alignedAddr) - 1;
  *slot = raw;
  return reinterpret_cast<void *>(alignedAddr);
}

inline void alignedFreeBytes(void *alignedPtr) noexcept {
  if (!alignedPtr) return;
  void **slot = static_cast<void **>(alignedPtr) - 1;
  std::free(*slot);
}

inline float *alignedAllocFloats(size_t count,
                                 size_t alignment = 64,
                                 bool zeroInit = true) noexcept {
  const size_t bytes = count * sizeof(float);
  float *ptr = static_cast<float *>(alignedAllocBytes(bytes, alignment));
  if (ptr && zeroInit && bytes > 0) {
    std::memset(ptr, 0, bytes);
  }
  return ptr;
}

struct AlignedFloatDeleter {
  void operator()(float *p) const noexcept { alignedFreeBytes(p); }
};

using AlignedFloatPtr = std::unique_ptr<float[], AlignedFloatDeleter>;

inline AlignedFloatPtr makeAlignedFloats(size_t count,
                                         size_t alignment = 64,
                                         bool zeroInit = true) noexcept {
  return AlignedFloatPtr(alignedAllocFloats(count, alignment, zeroInit));
}

// -----------------------------------------------------------------------------
// Portable 4-Wide Float32 SIMD Vector (`f32x4`)
// -----------------------------------------------------------------------------
struct f32x4 {
#if defined(SOLOUD_SIMD_SSE2)
  __m128 v;
  f32x4() = default;
  f32x4(__m128 x) noexcept : v(x) {}
#elif defined(SOLOUD_SIMD_NEON)
  float32x4_t v;
  f32x4() = default;
  f32x4(float32x4_t x) noexcept : v(x) {}
#elif defined(SOLOUD_SIMD_WASM)
  v128_t v;
  f32x4() = default;
  f32x4(v128_t x) noexcept : v(x) {}
#else
  float v[4];
  f32x4() = default;
  f32x4(float x0, float x1, float x2, float x3) noexcept : v{x0, x1, x2, x3} {}
#endif

  static inline f32x4 zero() noexcept {
#if defined(SOLOUD_SIMD_SSE2)
    return _mm_setzero_ps();
#elif defined(SOLOUD_SIMD_NEON)
    return vdupq_n_f32(0.0f);
#elif defined(SOLOUD_SIMD_WASM)
    return wasm_f32x4_const_splat(0.0f);
#else
    return f32x4(0.0f, 0.0f, 0.0f, 0.0f);
#endif
  }

  static inline f32x4 splat(float x) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
    return _mm_set1_ps(x);
#elif defined(SOLOUD_SIMD_NEON)
    return vdupq_n_f32(x);
#elif defined(SOLOUD_SIMD_WASM)
    return wasm_f32x4_splat(x);
#else
    return f32x4(x, x, x, x);
#endif
  }

  static inline f32x4 set(float x0, float x1, float x2, float x3) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
    return _mm_set_ps(x3, x2, x1, x0);
#elif defined(SOLOUD_SIMD_NEON)
    const float arr[4] = {x0, x1, x2, x3};
    return vld1q_f32(arr);
#elif defined(SOLOUD_SIMD_WASM)
    return wasm_f32x4_make(x0, x1, x2, x3);
#else
    return f32x4(x0, x1, x2, x3);
#endif
  }

  static inline f32x4 make(float x0, float x1, float x2, float x3) noexcept {
    return set(x0, x1, x2, x3);
  }

  static inline f32x4 loadu(const float *p) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
    return _mm_loadu_ps(p);
#elif defined(SOLOUD_SIMD_NEON)
    return vld1q_f32(p);
#elif defined(SOLOUD_SIMD_WASM)
    return wasm_v128_load(p);
#else
    return f32x4(p[0], p[1], p[2], p[3]);
#endif
  }

  inline void storeu(float *p) const noexcept {
#if defined(SOLOUD_SIMD_SSE2)
    _mm_storeu_ps(p, v);
#elif defined(SOLOUD_SIMD_NEON)
    vst1q_f32(p, v);
#elif defined(SOLOUD_SIMD_WASM)
    wasm_v128_store(p, v);
#else
    p[0] = v[0];
    p[1] = v[1];
    p[2] = v[2];
    p[3] = v[3];
#endif
  }
};

inline f32x4 neg(f32x4 a) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_xor_ps(a.v, _mm_set1_ps(-0.0f));
#elif defined(SOLOUD_SIMD_NEON)
  return vnegq_f32(a.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_neg(a.v);
#else
  return f32x4(-a.v[0], -a.v[1], -a.v[2], -a.v[3]);
#endif
}

inline f32x4 add(f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_add_ps(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vaddq_f32(a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_add(a.v, b.v);
#else
  return f32x4(a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2],
               a.v[3] + b.v[3]);
#endif
}

inline f32x4 sub(f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_sub_ps(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vsubq_f32(a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_sub(a.v, b.v);
#else
  return f32x4(a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2],
               a.v[3] - b.v[3]);
#endif
}

inline f32x4 mul(f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_mul_ps(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vmulq_f32(a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_mul(a.v, b.v);
#else
  return f32x4(a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2],
               a.v[3] * b.v[3]);
#endif
}

/// Fused multiply-add: `acc + a * b`.
inline f32x4 fma(f32x4 acc, f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
#if defined(__FMA__) || defined(__AVX2__)
  return _mm_fmadd_ps(a.v, b.v, acc.v);
#else
  return _mm_add_ps(acc.v, _mm_mul_ps(a.v, b.v));
#endif
#elif defined(SOLOUD_SIMD_NEON_AARCH64)
  return vfmaq_f32(acc.v, a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vmlaq_f32(acc.v, a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_add(acc.v, wasm_f32x4_mul(a.v, b.v));
#else
  return f32x4(acc.v[0] + a.v[0] * b.v[0], acc.v[1] + a.v[1] * b.v[1],
               acc.v[2] + a.v[2] * b.v[2], acc.v[3] + a.v[3] * b.v[3]);
#endif
}

/// Fused multiply-subtract: `acc - a * b`.
inline f32x4 fms(f32x4 acc, f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
#if defined(__FMA__) || defined(__AVX2__)
  return _mm_fnmadd_ps(a.v, b.v, acc.v);
#else
  return _mm_sub_ps(acc.v, _mm_mul_ps(a.v, b.v));
#endif
#elif defined(SOLOUD_SIMD_NEON_AARCH64)
  return vfmsq_f32(acc.v, a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vmlsq_f32(acc.v, a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_sub(acc.v, wasm_f32x4_mul(a.v, b.v));
#else
  return f32x4(acc.v[0] - a.v[0] * b.v[0], acc.v[1] - a.v[1] * b.v[1],
               acc.v[2] - a.v[2] * b.v[2], acc.v[3] - a.v[3] * b.v[3]);
#endif
}

inline f32x4 abs(f32x4 a) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
  return _mm_and_ps(a.v, mask);
#elif defined(SOLOUD_SIMD_NEON)
  return vabsq_f32(a.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_abs(a.v);
#else
  return f32x4(std::fabs(a.v[0]), std::fabs(a.v[1]), std::fabs(a.v[2]),
               std::fabs(a.v[3]));
#endif
}

inline f32x4 min(f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_min_ps(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vminq_f32(a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_pmin(a.v, b.v);
#else
  return f32x4(a.v[0] < b.v[0] ? a.v[0] : b.v[0],
               a.v[1] < b.v[1] ? a.v[1] : b.v[1],
               a.v[2] < b.v[2] ? a.v[2] : b.v[2],
               a.v[3] < b.v[3] ? a.v[3] : b.v[3]);
#endif
}

inline f32x4 max(f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_max_ps(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  return vmaxq_f32(a.v, b.v);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_pmax(a.v, b.v);
#else
  return f32x4(a.v[0] > b.v[0] ? a.v[0] : b.v[0],
               a.v[1] > b.v[1] ? a.v[1] : b.v[1],
               a.v[2] > b.v[2] ? a.v[2] : b.v[2],
               a.v[3] > b.v[3] ? a.v[3] : b.v[3]);
#endif
}

inline f32x4 clamp(f32x4 x, f32x4 lo, f32x4 hi) noexcept {
  return min(max(x, lo), hi);
}

inline f32x4 div(f32x4 a, f32x4 b) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_div_ps(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON_AARCH64)
  return vdivq_f32(a.v, b.v);
#elif defined(SOLOUD_SIMD_NEON)
  float32x4_t rec = vrecpeq_f32(b.v);
  rec = vmulq_f32(vrecpsq_f32(b.v, rec), rec);
  rec = vmulq_f32(vrecpsq_f32(b.v, rec), rec);
  return vmulq_f32(a.v, rec);
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_div(a.v, b.v);
#else
  return f32x4(a.v[0] / b.v[0], a.v[1] / b.v[1], a.v[2] / b.v[2],
               a.v[3] / b.v[3]);
#endif
}

inline f32x4 sqrt(f32x4 x) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  return _mm_sqrt_ps(x.v);
#elif defined(SOLOUD_SIMD_NEON_AARCH64)
  return vsqrtq_f32(x.v);
#elif defined(SOLOUD_SIMD_NEON)
  // ARMv7 NEON lacks hardware vsqrtq_f32: use reciprocal-sqrt + 2 NR steps
  // and mask out <= 0 lanes so sqrt(0.0f) == 0.0f.
  const uint32x4_t posMask = vcgtq_f32(x.v, vdupq_n_f32(0.0f));
  float32x4_t est = vrsqrteq_f32(x.v);
  est = vmulq_f32(est, vrsqrtsq_f32(vmulq_f32(x.v, est), est));
  est = vmulq_f32(est, vrsqrtsq_f32(vmulq_f32(x.v, est), est));
  return vreinterpretq_f32_u32(
      vandq_u32(posMask, vreinterpretq_u32_f32(vmulq_f32(x.v, est))));
#elif defined(SOLOUD_SIMD_WASM)
  return wasm_f32x4_sqrt(x.v);
#else
  return f32x4(std::sqrt(x.v[0]), std::sqrt(x.v[1]), std::sqrt(x.v[2]),
               std::sqrt(x.v[3]));
#endif
}

inline float reduce_add(f32x4 x) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  __m128 shuf = _mm_shuffle_ps(x.v, x.v, _MM_SHUFFLE(2, 3, 0, 1));
  __m128 sums = _mm_add_ps(x.v, shuf);
  shuf = _mm_movehl_ps(shuf, sums);
  sums = _mm_add_ss(sums, shuf);
  return _mm_cvtss_f32(sums);
#elif defined(SOLOUD_SIMD_NEON_AARCH64)
  return vaddvq_f32(x.v);
#elif defined(SOLOUD_SIMD_NEON)
  float32x2_t pair = vadd_f32(vget_low_f32(x.v), vget_high_f32(x.v));
  pair = vpadd_f32(pair, pair);
  return vget_lane_f32(pair, 0);
#elif defined(SOLOUD_SIMD_WASM)
  v128_t shuf = wasm_i32x4_shuffle(x.v, x.v, 1, 0, 3, 2);
  v128_t sums = wasm_f32x4_add(x.v, shuf);
  shuf = wasm_i32x4_shuffle(sums, sums, 2, 3, 0, 1);
  sums = wasm_f32x4_add(sums, shuf);
  return wasm_f32x4_extract_lane(sums, 0);
#else
  return (x.v[0] + x.v[1]) + (x.v[2] + x.v[3]);
#endif
}

inline float reduce_max(f32x4 x) noexcept {
#if defined(SOLOUD_SIMD_SSE2)
  __m128 shuf = _mm_shuffle_ps(x.v, x.v, _MM_SHUFFLE(2, 3, 0, 1));
  __m128 m = _mm_max_ps(x.v, shuf);
  shuf = _mm_movehl_ps(shuf, m);
  m = _mm_max_ss(m, shuf);
  return _mm_cvtss_f32(m);
#elif defined(SOLOUD_SIMD_NEON_AARCH64)
  return vmaxvq_f32(x.v);
#elif defined(SOLOUD_SIMD_NEON)
  float32x2_t pair = vmax_f32(vget_low_f32(x.v), vget_high_f32(x.v));
  pair = vpmax_f32(pair, pair);
  return vget_lane_f32(pair, 0);
#elif defined(SOLOUD_SIMD_WASM)
  v128_t shuf = wasm_i32x4_shuffle(x.v, x.v, 1, 0, 3, 2);
  v128_t m = wasm_f32x4_pmax(x.v, shuf);
  shuf = wasm_i32x4_shuffle(m, m, 2, 3, 0, 1);
  m = wasm_f32x4_pmax(m, shuf);
  return wasm_f32x4_extract_lane(m, 0);
#else
  float m0 = x.v[0] > x.v[1] ? x.v[0] : x.v[1];
  float m1 = x.v[2] > x.v[3] ? x.v[2] : x.v[3];
  return m0 > m1 ? m0 : m1;
#endif
}

// -----------------------------------------------------------------------------
// Fast Vectorized Audio Transcendentals (`fast_log2`, `fast_exp2`)
// Minimax degree-5 approximations with < 2e-5 error (< 0.0002 dB).
// -----------------------------------------------------------------------------
inline f32x4 fast_log2(f32x4 x) noexcept {
  // Clamp to smallest normal float to avoid -Inf/NaN on zero/subnormal inputs.
  x = max(x, f32x4::splat(1.17549435e-38f));
#if defined(SOLOUD_SIMD_SSE2)
  __m128i bits = _mm_castps_si128(x.v);
  __m128i expInt = _mm_sub_epi32(_mm_srli_epi32(bits, 23), _mm_set1_epi32(126));
  __m128 m = _mm_or_ps(
      _mm_and_ps(x.v, _mm_castsi128_ps(_mm_set1_epi32(0x007FFFFF))),
      _mm_set1_ps(0.5f));
  const __m128 sqrtHalf = _mm_set1_ps(0.707106781186547524f);
  const __m128 mask = _mm_cmplt_ps(m, sqrtHalf);
  m = _mm_add_ps(m, _mm_and_ps(mask, m));
  __m128 e = _mm_sub_ps(_mm_cvtepi32_ps(expInt),
                        _mm_and_ps(mask, _mm_set1_ps(1.0f)));
  f32x4 y(_mm_sub_ps(m, _mm_set1_ps(1.0f)));
  f32x4 ef(e);
#elif defined(SOLOUD_SIMD_NEON)
  int32x4_t bits = vreinterpretq_s32_f32(x.v);
  int32x4_t expInt = vsubq_s32(vshrq_n_s32(bits, 23), vdupq_n_s32(126));
  float32x4_t m = vreinterpretq_f32_s32(vorrq_s32(
      vandq_s32(bits, vdupq_n_s32(0x007FFFFF)),
      vreinterpretq_s32_f32(vdupq_n_f32(0.5f))));
  const uint32x4_t mask = vcltq_f32(m, vdupq_n_f32(0.707106781186547524f));
  m = vaddq_f32(
      m, vreinterpretq_f32_u32(vandq_u32(mask, vreinterpretq_u32_f32(m))));
  float32x4_t e = vsubq_f32(
      vcvtq_f32_s32(expInt),
      vreinterpretq_f32_u32(
          vandq_u32(mask, vreinterpretq_u32_f32(vdupq_n_f32(1.0f)))));
  f32x4 y(vsubq_f32(m, vdupq_n_f32(1.0f)));
  f32x4 ef(e);
#elif defined(SOLOUD_SIMD_WASM)
  v128_t expInt =
      wasm_i32x4_sub(wasm_u32x4_shr(x.v, 23), wasm_i32x4_splat(126));
  v128_t m = wasm_v128_or(wasm_v128_and(x.v, wasm_i32x4_splat(0x007FFFFF)),
                          wasm_f32x4_splat(0.5f));
  v128_t mask = wasm_f32x4_lt(m, wasm_f32x4_splat(0.707106781186547524f));
  m = wasm_f32x4_add(m, wasm_v128_and(mask, m));
  v128_t e = wasm_f32x4_sub(wasm_f32x4_convert_i32x4(expInt),
                            wasm_v128_and(mask, wasm_f32x4_splat(1.0f)));
  f32x4 y(wasm_f32x4_sub(m, wasm_f32x4_splat(1.0f)));
  f32x4 ef(e);
#else
  return f32x4(std::log2(x.v[0]), std::log2(x.v[1]), std::log2(x.v[2]),
               std::log2(x.v[3]));
#endif
  // Minimax polynomial for log2(1 + y) on [-0.29289322, +0.41421356]:
  f32x4 p = f32x4::splat(-0.20599502f);
  p = fma(f32x4::splat(0.28116748f), p, y);
  p = fma(f32x4::splat(-0.36021863f), p, y);
  p = fma(f32x4::splat(0.48089766f), p, y);
  p = fma(f32x4::splat(-0.72134752f), p, y);
  p = fma(f32x4::splat(1.44269504f), p, y);
  return fma(ef, p, y);
}

inline f32x4 fast_exp2(f32x4 x) noexcept {
  x = clamp(x, f32x4::splat(-126.0f), f32x4::splat(126.0f));
#if defined(SOLOUD_SIMD_SSE2)
  // Round to nearest integer via magic float bias so f = x - n in [-0.5, 0.5].
  const __m128 bias = _mm_set1_ps(12582912.0f);  // 1.5 * 2^23
  __m128 nFloat = _mm_sub_ps(_mm_add_ps(x.v, bias), bias);
  __m128 fVec = _mm_sub_ps(x.v, nFloat);
  __m128i nInt = _mm_cvttps_epi32(nFloat);
  __m128 pow2n =
      _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(nInt, _mm_set1_epi32(127)), 23));
  f32x4 f(fVec);
  f32x4 scale(pow2n);
#elif defined(SOLOUD_SIMD_NEON)
  const float32x4_t bias = vdupq_n_f32(12582912.0f);
  float32x4_t nFloat = vsubq_f32(vaddq_f32(x.v, bias), bias);
  float32x4_t fVec = vsubq_f32(x.v, nFloat);
  int32x4_t nInt = vcvtq_s32_f32(nFloat);
  float32x4_t pow2n =
      vreinterpretq_f32_s32(vshlq_n_s32(vaddq_s32(nInt, vdupq_n_s32(127)), 23));
  f32x4 f(fVec);
  f32x4 scale(pow2n);
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t nFloat = wasm_f32x4_nearest(x.v);
  const v128_t fVec = wasm_f32x4_sub(x.v, nFloat);
  const v128_t nInt = wasm_i32x4_trunc_sat_f32x4(nFloat);
  const v128_t pow2n =
      wasm_i32x4_shl(wasm_i32x4_add(nInt, wasm_i32x4_splat(127)), 23);
  f32x4 f(fVec);
  f32x4 scale(pow2n);
#else
  return f32x4(std::exp2(x.v[0]), std::exp2(x.v[1]), std::exp2(x.v[2]),
               std::exp2(x.v[3]));
#endif
  // Minimax polynomial for 2^f on [-0.5, 0.5] (exact 1.0f at f == 0.0f):
  f32x4 p = f32x4::splat(0.0013333558f);
  p = fma(f32x4::splat(0.0096181291f), p, f);
  p = fma(f32x4::splat(0.0555041087f), p, f);
  p = fma(f32x4::splat(0.2402265070f), p, f);
  p = fma(f32x4::splat(0.6931471806f), p, f);
  p = fma(f32x4::splat(1.0f), p, f);
  return mul(scale, p);
}

/// Fast scalar companions using the same minimax polynomials for tail loops.
inline float fast_log2_scalar(float x) noexcept {
  if (x < 1.17549435e-38f) x = 1.17549435e-38f;
  uint32_t bits = 0;
  std::memcpy(&bits, &x, sizeof(uint32_t));
  int32_t expInt = static_cast<int32_t>(bits >> 23) - 126;
  uint32_t mBits = (bits & 0x007FFFFFu) | 0x3F000000u;
  float m = 0.0f;
  std::memcpy(&m, &mBits, sizeof(float));
  if (m < 0.707106781186547524f) {
    m += m;
    expInt -= 1;
  }
  const float y = m - 1.0f;
  float p = -0.20599502f;
  p = 0.28116748f + p * y;
  p = -0.36021863f + p * y;
  p = 0.48089766f + p * y;
  p = -0.72134752f + p * y;
  p = 1.44269504f + p * y;
  return static_cast<float>(expInt) + p * y;
}

inline float fast_exp2_scalar(float x) noexcept {
  if (x < -126.0f) x = -126.0f;
  if (x > 126.0f) x = 126.0f;
  const float bias = 12582912.0f;
  const float nFloat = (x + bias) - bias;
  const float f = x - nFloat;
  const uint32_t pow2Bits =
      static_cast<uint32_t>(static_cast<int32_t>(nFloat) + 127) << 23;
  float scale = 0.0f;
  std::memcpy(&scale, &pow2Bits, sizeof(float));
  float p = 0.0013333558f;
  p = 0.0096181291f + p * f;
  p = 0.0555041087f + p * f;
  p = 0.2402265070f + p * f;
  p = 0.6931471806f + p * f;
  p = 1.0f + p * f;
  return scale * p;
}

inline float fast_log2(float x) noexcept { return fast_log2_scalar(x); }
inline float fast_exp2(float x) noexcept { return fast_exp2_scalar(x); }

}  // namespace soloud_simd

#endif  // FLUTTER_SOLOUD_SIMD_H

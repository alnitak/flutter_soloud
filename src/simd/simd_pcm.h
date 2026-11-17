#pragma once

#ifndef FLUTTER_SOLOUD_SIMD_PCM_H
#define FLUTTER_SOLOUD_SIMD_PCM_H

#include "soloud_simd.h"

namespace SoLoud {

/// De-interleave `frames` stereo float frames (`L0 R0 L1 R1 ...`) into two
/// separate planar channel buffers `dstL` and `dstR`.
inline void deinterleaveStereoFloats(const float *src, float *dstL, float *dstR,
                                     size_t frames) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  for (; i + 4 <= frames; i += 4) {
    __m128 v0 = _mm_loadu_ps(src + i * 2);
    __m128 v1 = _mm_loadu_ps(src + i * 2 + 4);
    __m128 left = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(2, 0, 2, 0));
    __m128 right = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(3, 1, 3, 1));
    _mm_storeu_ps(dstL + i, left);
    _mm_storeu_ps(dstR + i, right);
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 4 <= frames; i += 4) {
    float32x4x2_t lr = vld2q_f32(src + i * 2);
    vst1q_f32(dstL + i, lr.val[0]);
    vst1q_f32(dstR + i, lr.val[1]);
  }
#elif defined(SOLOUD_SIMD_WASM)
  for (; i + 4 <= frames; i += 4) {
    v128_t v0 = wasm_v128_load(src + i * 2);
    v128_t v1 = wasm_v128_load(src + i * 2 + 4);
    v128_t left = wasm_i32x4_shuffle(v0, v1, 0, 2, 4, 6);
    v128_t right = wasm_i32x4_shuffle(v0, v1, 1, 3, 5, 7);
    wasm_v128_store(dstL + i, left);
    wasm_v128_store(dstR + i, right);
  }
#endif
  for (; i < frames; ++i) {
    dstL[i] = src[i * 2];
    dstR[i] = src[i * 2 + 1];
  }
}

/// De-interleave `frames` quad (4-channel) float frames into four planar
/// channel buffers using 4x4 SIMD matrix transpose.
inline void deinterleaveQuadFloats(const float *src, float *dst0, float *dst1,
                                   float *dst2, float *dst3,
                                   size_t frames) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  for (; i + 4 <= frames; i += 4) {
    __m128 r0 = _mm_loadu_ps(src + i * 4);
    __m128 r1 = _mm_loadu_ps(src + i * 4 + 4);
    __m128 r2 = _mm_loadu_ps(src + i * 4 + 8);
    __m128 r3 = _mm_loadu_ps(src + i * 4 + 12);
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    _mm_storeu_ps(dst0 + i, r0);
    _mm_storeu_ps(dst1 + i, r1);
    _mm_storeu_ps(dst2 + i, r2);
    _mm_storeu_ps(dst3 + i, r3);
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 4 <= frames; i += 4) {
    float32x4x4_t q = vld4q_f32(src + i * 4);
    vst1q_f32(dst0 + i, q.val[0]);
    vst1q_f32(dst1 + i, q.val[1]);
    vst1q_f32(dst2 + i, q.val[2]);
    vst1q_f32(dst3 + i, q.val[3]);
  }
#elif defined(SOLOUD_SIMD_WASM)
  for (; i + 4 <= frames; i += 4) {
    v128_t r0 = wasm_v128_load(src + i * 4);
    v128_t r1 = wasm_v128_load(src + i * 4 + 4);
    v128_t r2 = wasm_v128_load(src + i * 4 + 8);
    v128_t r3 = wasm_v128_load(src + i * 4 + 12);
    v128_t t0 = wasm_i32x4_shuffle(r0, r1, 0, 4, 1, 5);
    v128_t t1 = wasm_i32x4_shuffle(r2, r3, 0, 4, 1, 5);
    v128_t t2 = wasm_i32x4_shuffle(r0, r1, 2, 6, 3, 7);
    v128_t t3 = wasm_i32x4_shuffle(r2, r3, 2, 6, 3, 7);
    wasm_v128_store(dst0 + i, wasm_i32x4_shuffle(t0, t1, 0, 1, 4, 5));
    wasm_v128_store(dst1 + i, wasm_i32x4_shuffle(t0, t1, 2, 3, 6, 7));
    wasm_v128_store(dst2 + i, wasm_i32x4_shuffle(t2, t3, 0, 1, 4, 5));
    wasm_v128_store(dst3 + i, wasm_i32x4_shuffle(t2, t3, 2, 3, 6, 7));
  }
#endif
  for (; i < frames; ++i) {
    dst0[i] = src[i * 4];
    dst1[i] = src[i * 4 + 1];
    dst2[i] = src[i * 4 + 2];
    dst3[i] = src[i * 4 + 3];
  }
}

/// De-interleave `frames` multi-channel float frames into SoLoud's planar
/// layout (`planarDst[ch * channelStride + frame]`).
inline void deinterleaveFloatsToPlanar(const float *interleavedSrc,
                                       float *planarDst, size_t channelStride,
                                       size_t frames,
                                       uint32_t channels) noexcept {
  if (frames == 0 || channels == 0) return;
  if (channels == 1) {
    std::memcpy(planarDst, interleavedSrc, frames * sizeof(float));
    return;
  }
  if (channels == 2) {
    deinterleaveStereoFloats(interleavedSrc, planarDst,
                             planarDst + channelStride, frames);
    return;
  }
  if (channels == 4) {
    deinterleaveQuadFloats(interleavedSrc, planarDst,
                           planarDst + channelStride,
                           planarDst + channelStride * 2,
                           planarDst + channelStride * 3, frames);
    return;
  }
  // Cache-friendly L1-tiled transpose for 3, 5, 6, 8+ channels:
  constexpr size_t kTile = 256;
  for (size_t base = 0; base < frames; base += kTile) {
    const size_t chunk = std::min(kTile, frames - base);
    const float *srcTile = interleavedSrc + base * channels;
    for (size_t i = 0; i < chunk; ++i) {
      const float *framePtr = srcTile + i * channels;
      for (uint32_t ch = 0; ch < channels; ++ch) {
        planarDst[ch * channelStride + base + i] = framePtr[ch];
      }
    }
  }
}

/// Interleave `frames` stereo planar float frames (`srcL`, `srcR`) into `dst`.
inline void interleaveStereoFloats(const float *srcL, const float *srcR,
                                   float *dst, size_t frames) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  for (; i + 4 <= frames; i += 4) {
    __m128 l = _mm_loadu_ps(srcL + i);
    __m128 r = _mm_loadu_ps(srcR + i);
    __m128 lo = _mm_unpacklo_ps(l, r);
    __m128 hi = _mm_unpackhi_ps(l, r);
    _mm_storeu_ps(dst + i * 2, lo);
    _mm_storeu_ps(dst + i * 2 + 4, hi);
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 4 <= frames; i += 4) {
    float32x4x2_t lr;
    lr.val[0] = vld1q_f32(srcL + i);
    lr.val[1] = vld1q_f32(srcR + i);
    vst2q_f32(dst + i * 2, lr);
  }
#elif defined(SOLOUD_SIMD_WASM)
  for (; i + 4 <= frames; i += 4) {
    v128_t l = wasm_v128_load(srcL + i);
    v128_t r = wasm_v128_load(srcR + i);
    v128_t lo = wasm_i32x4_shuffle(l, r, 0, 4, 1, 5);
    v128_t hi = wasm_i32x4_shuffle(l, r, 2, 6, 3, 7);
    wasm_v128_store(dst + i * 2, lo);
    wasm_v128_store(dst + i * 2 + 4, hi);
  }
#endif
  for (; i < frames; ++i) {
    dst[i * 2] = srcL[i];
    dst[i * 2 + 1] = srcR[i];
  }
}

/// Convert `sampleCount` signed 8-bit PCM samples (`[-128, 127]`) to float
/// (`[-1.0f, 1.0f)`).
inline void convertS8ToFloats(const int8_t *src, float *dst,
                              size_t sampleCount) noexcept {
  constexpr float kScale = 1.0f / 128.0f;
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 scale = _mm_set1_ps(kScale);
  const __m128i zero = _mm_setzero_si128();
  for (; i + 16 <= sampleCount; i += 16) {
    __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
    __m128i sign8 = _mm_cmpgt_epi8(zero, b);
    __m128i w0 = _mm_unpacklo_epi8(b, sign8);
    __m128i w1 = _mm_unpackhi_epi8(b, sign8);
    __m128i d0 = _mm_unpacklo_epi16(w0, _mm_cmpgt_epi16(zero, w0));
    __m128i d1 = _mm_unpackhi_epi16(w0, _mm_cmpgt_epi16(zero, w0));
    __m128i d2 = _mm_unpacklo_epi16(w1, _mm_cmpgt_epi16(zero, w1));
    __m128i d3 = _mm_unpackhi_epi16(w1, _mm_cmpgt_epi16(zero, w1));
    _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_cvtepi32_ps(d0), scale));
    _mm_storeu_ps(dst + i + 4, _mm_mul_ps(_mm_cvtepi32_ps(d1), scale));
    _mm_storeu_ps(dst + i + 8, _mm_mul_ps(_mm_cvtepi32_ps(d2), scale));
    _mm_storeu_ps(dst + i + 12, _mm_mul_ps(_mm_cvtepi32_ps(d3), scale));
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 8 <= sampleCount; i += 8) {
    int8x8_t b = vld1_s8(src + i);
    int16x8_t w = vmovl_s8(b);
    int32x4_t lo = vmovl_s16(vget_low_s16(w));
    int32x4_t hi = vmovl_s16(vget_high_s16(w));
    vst1q_f32(dst + i, vmulq_n_f32(vcvtq_f32_s32(lo), kScale));
    vst1q_f32(dst + i + 4, vmulq_n_f32(vcvtq_f32_s32(hi), kScale));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t scale = wasm_f32x4_splat(kScale);
  for (; i + 16 <= sampleCount; i += 16) {
    v128_t b = wasm_v128_load(src + i);
    v128_t w0 = wasm_i16x8_extend_low_i8x16(b);
    v128_t w1 = wasm_i16x8_extend_high_i8x16(b);
    wasm_v128_store(
        dst + i,
        wasm_f32x4_mul(wasm_f32x4_convert_i32x4(wasm_i32x4_extend_low_i16x8(w0)),
                       scale));
    wasm_v128_store(
        dst + i + 4,
        wasm_f32x4_mul(
            wasm_f32x4_convert_i32x4(wasm_i32x4_extend_high_i16x8(w0)), scale));
    wasm_v128_store(
        dst + i + 8,
        wasm_f32x4_mul(wasm_f32x4_convert_i32x4(wasm_i32x4_extend_low_i16x8(w1)),
                       scale));
    wasm_v128_store(
        dst + i + 12,
        wasm_f32x4_mul(
            wasm_f32x4_convert_i32x4(wasm_i32x4_extend_high_i16x8(w1)), scale));
  }
#endif
  for (; i < sampleCount; ++i) {
    dst[i] = static_cast<float>(src[i]) * kScale;
  }
}

/// Convert `sampleCount` signed 16-bit PCM samples (`[-32768, 32767]`) to
/// normalized floats (`[-1.0f, 1.0f)`).
inline void convertInt16ToFloats(const int16_t *src, float *dst,
                                 size_t sampleCount) noexcept {
  constexpr float kScale = 1.0f / 32768.0f;
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 scale = _mm_set1_ps(kScale);
  for (; i + 8 <= sampleCount; i += 8) {
    __m128i s16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
    __m128i sign = _mm_srai_epi16(s16, 15);
    __m128i lo32 = _mm_unpacklo_epi16(s16, sign);
    __m128i hi32 = _mm_unpackhi_epi16(s16, sign);
    _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_cvtepi32_ps(lo32), scale));
    _mm_storeu_ps(dst + i + 4, _mm_mul_ps(_mm_cvtepi32_ps(hi32), scale));
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 8 <= sampleCount; i += 8) {
    int16x8_t s16 = vld1q_s16(src + i);
    int32x4_t lo32 = vmovl_s16(vget_low_s16(s16));
    int32x4_t hi32 = vmovl_s16(vget_high_s16(s16));
    vst1q_f32(dst + i, vmulq_n_f32(vcvtq_f32_s32(lo32), kScale));
    vst1q_f32(dst + i + 4, vmulq_n_f32(vcvtq_f32_s32(hi32), kScale));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t scale = wasm_f32x4_splat(kScale);
  for (; i + 8 <= sampleCount; i += 8) {
    v128_t s16 = wasm_v128_load(src + i);
    v128_t lo32 = wasm_i32x4_extend_low_i16x8(s16);
    v128_t hi32 = wasm_i32x4_extend_high_i16x8(s16);
    wasm_v128_store(dst + i,
                    wasm_f32x4_mul(wasm_f32x4_convert_i32x4(lo32), scale));
    wasm_v128_store(dst + i + 4,
                    wasm_f32x4_mul(wasm_f32x4_convert_i32x4(hi32), scale));
  }
#endif
  for (; i < sampleCount; ++i) {
    dst[i] = static_cast<float>(src[i]) * kScale;
  }
}

/// Convert and de-interleave `frames` stereo int16 PCM frames directly into
/// planar float channel buffers `dstL` and `dstR` in a single pass.
inline void deinterleaveStereoInt16ToFloats(const int16_t *src, float *dstL,
                                            float *dstR,
                                            size_t frames) noexcept {
  constexpr float kScale = 1.0f / 32768.0f;
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 scale = _mm_set1_ps(kScale);
  for (; i + 4 <= frames; i += 4) {
    __m128i s16 =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i * 2));
    __m128i sign = _mm_srai_epi16(s16, 15);
    __m128 f0 = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(s16, sign)), scale);
    __m128 f1 = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(s16, sign)), scale);
    _mm_storeu_ps(dstL + i, _mm_shuffle_ps(f0, f1, _MM_SHUFFLE(2, 0, 2, 0)));
    _mm_storeu_ps(dstR + i, _mm_shuffle_ps(f0, f1, _MM_SHUFFLE(3, 1, 3, 1)));
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 4 <= frames; i += 4) {
    int16x4x2_t lr16 = vld2_s16(src + i * 2);
    vst1q_f32(dstL + i,
              vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(lr16.val[0])), kScale));
    vst1q_f32(dstR + i,
              vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(lr16.val[1])), kScale));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t scale = wasm_f32x4_splat(kScale);
  for (; i + 4 <= frames; i += 4) {
    v128_t s16 = wasm_v128_load(src + i * 2);
    v128_t f0 = wasm_f32x4_mul(
        wasm_f32x4_convert_i32x4(wasm_i32x4_extend_low_i16x8(s16)), scale);
    v128_t f1 = wasm_f32x4_mul(
        wasm_f32x4_convert_i32x4(wasm_i32x4_extend_high_i16x8(s16)), scale);
    wasm_v128_store(dstL + i, wasm_i32x4_shuffle(f0, f1, 0, 2, 4, 6));
    wasm_v128_store(dstR + i, wasm_i32x4_shuffle(f0, f1, 1, 3, 5, 7));
  }
#endif
  for (; i < frames; ++i) {
    dstL[i] = static_cast<float>(src[i * 2]) * kScale;
    dstR[i] = static_cast<float>(src[i * 2 + 1]) * kScale;
  }
}

/// Convert `sampleCount` signed 32-bit integers to float with `invScale`
/// (defaults to `1.0f / 2147483648.0f` for S32LE PCM; pass `1.0f / 8388608.0f`
/// or `1.0f / 32768.0f` for FLAC 24-bit / 16-bit int32 containers).
inline void convertInt32ToFloats(const int32_t *src, float *dst,
                                 size_t sampleCount,
                                 float invScale = 1.0f / 2147483648.0f) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 scale = _mm_set1_ps(invScale);
  for (; i + 4 <= sampleCount; i += 4) {
    __m128i s32 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
    _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_cvtepi32_ps(s32), scale));
  }
#elif defined(SOLOUD_SIMD_NEON)
  for (; i + 4 <= sampleCount; i += 4) {
    int32x4_t s32 = vld1q_s32(src + i);
    vst1q_f32(dst + i, vmulq_n_f32(vcvtq_f32_s32(s32), invScale));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t scale = wasm_f32x4_splat(invScale);
  for (; i + 4 <= sampleCount; i += 4) {
    v128_t s32 = wasm_v128_load(src + i);
    wasm_v128_store(dst + i,
                    wasm_f32x4_mul(wasm_f32x4_convert_i32x4(s32), scale));
  }
#endif
  for (; i < sampleCount; ++i) {
    dst[i] = static_cast<float>(src[i]) * invScale;
  }
}

/// Convert and interleave planar `int32_t` channel arrays (such as libFLAC's
/// `const FLAC__int32 * const buffer[]`) directly into `interleavedDst`.
inline void interleavePlanarInt32ToFloats(const int32_t *const *planarSrc,
                                          float *interleavedDst, size_t frames,
                                          uint32_t channels,
                                          float invScale) noexcept {
  if (frames == 0 || channels == 0) return;
  if (channels == 1) {
    convertInt32ToFloats(planarSrc[0], interleavedDst, frames, invScale);
    return;
  }
  if (channels == 2) {
    const int32_t *srcL = planarSrc[0];
    const int32_t *srcR = planarSrc[1];
    size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
    const __m128 scale = _mm_set1_ps(invScale);
    for (; i + 4 <= frames; i += 4) {
      __m128 l = _mm_mul_ps(
          _mm_cvtepi32_ps(
              _mm_loadu_si128(reinterpret_cast<const __m128i *>(srcL + i))),
          scale);
      __m128 r = _mm_mul_ps(
          _mm_cvtepi32_ps(
              _mm_loadu_si128(reinterpret_cast<const __m128i *>(srcR + i))),
          scale);
      _mm_storeu_ps(interleavedDst + i * 2, _mm_unpacklo_ps(l, r));
      _mm_storeu_ps(interleavedDst + i * 2 + 4, _mm_unpackhi_ps(l, r));
    }
#elif defined(SOLOUD_SIMD_NEON)
    for (; i + 4 <= frames; i += 4) {
      float32x4x2_t lr;
      lr.val[0] = vmulq_n_f32(vcvtq_f32_s32(vld1q_s32(srcL + i)), invScale);
      lr.val[1] = vmulq_n_f32(vcvtq_f32_s32(vld1q_s32(srcR + i)), invScale);
      vst2q_f32(interleavedDst + i * 2, lr);
    }
#elif defined(SOLOUD_SIMD_WASM)
    const v128_t scale = wasm_f32x4_splat(invScale);
    for (; i + 4 <= frames; i += 4) {
      v128_t l = wasm_f32x4_mul(
          wasm_f32x4_convert_i32x4(wasm_v128_load(srcL + i)), scale);
      v128_t r = wasm_f32x4_mul(
          wasm_f32x4_convert_i32x4(wasm_v128_load(srcR + i)), scale);
      wasm_v128_store(interleavedDst + i * 2,
                      wasm_i32x4_shuffle(l, r, 0, 4, 1, 5));
      wasm_v128_store(interleavedDst + i * 2 + 4,
                      wasm_i32x4_shuffle(l, r, 2, 6, 3, 7));
    }
#endif
    for (; i < frames; ++i) {
      interleavedDst[i * 2] = static_cast<float>(srcL[i]) * invScale;
      interleavedDst[i * 2 + 1] = static_cast<float>(srcR[i]) * invScale;
    }
    return;
  }
  for (size_t i = 0; i < frames; ++i) {
    float *frameDst = interleavedDst + i * channels;
    for (uint32_t ch = 0; ch < channels; ++ch) {
      frameDst[ch] = static_cast<float>(planarSrc[ch][i]) * invScale;
    }
  }
}

// -----------------------------------------------------------------------------
// Clamped & NaN-Safe Float -> Integer PCM Kernels
// - NaN inputs safely map to 0 (instead of +1.0f DC via naive std::min).
// - S32LE clamps positive bound to 2147483520.0f (largest float < 2^31) so
//   +1.0f never rounds up to 2^31 and wraps around to INT32_MIN (-2147483648)!
// -----------------------------------------------------------------------------
inline void convertFloatsToS8Clamped(const float *src, int8_t *dst,
                                     size_t count) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 lo = _mm_set1_ps(-1.0f);
  const __m128 hi = _mm_set1_ps(1.0f);
  const __m128 scale = _mm_set1_ps(127.0f);
  for (; i + 16 <= count; i += 16) {
    auto loadClampScale = [&](const float *p) -> __m128i {
      __m128 x = _mm_loadu_ps(p);
      // Zero NaNs: x == x is all-ones for non-NaN, all-zeros for NaN.
      x = _mm_and_ps(x, _mm_cmpeq_ps(x, x));
      x = _mm_min_ps(hi, _mm_max_ps(lo, x));
      return _mm_cvttps_epi32(_mm_mul_ps(x, scale));
    };
    __m128i d0 = loadClampScale(src + i);
    __m128i d1 = loadClampScale(src + i + 4);
    __m128i d2 = loadClampScale(src + i + 8);
    __m128i d3 = loadClampScale(src + i + 12);
    __m128i w0 = _mm_packs_epi32(d0, d1);
    __m128i w1 = _mm_packs_epi32(d2, d3);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i),
                     _mm_packs_epi16(w0, w1));
  }
#elif defined(SOLOUD_SIMD_NEON)
  // ARM NEON FCVTZS (vcvtq_s32_f32) maps NaN -> 0 and saturates +-Inf / out-of-range
  // values to [INT32_MIN, INT32_MAX] in hardware, and vqmovn saturates to int16/int8.
  const int8x16_t minS8 = vdupq_n_s8(-127);
  for (; i + 16 <= count; i += 16) {
    int16x4_t d0 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i), 127.0f)));
    int16x4_t d1 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 4), 127.0f)));
    int16x4_t d2 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 8), 127.0f)));
    int16x4_t d3 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 12), 127.0f)));
    int8x8_t b0 = vqmovn_s16(vcombine_s16(d0, d1));
    int8x8_t b1 = vqmovn_s16(vcombine_s16(d2, d3));
    vst1q_s8(dst + i, vmaxq_s8(vcombine_s8(b0, b1), minS8));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t lo = wasm_f32x4_splat(-1.0f);
  const v128_t hi = wasm_f32x4_splat(1.0f);
  const v128_t scale = wasm_f32x4_splat(127.0f);
  for (; i + 16 <= count; i += 16) {
    auto loadClampScale = [&](const float *p) -> v128_t {
      v128_t x = wasm_v128_load(p);
      x = wasm_v128_and(x, wasm_f32x4_eq(x, x));
      x = wasm_f32x4_pmin(hi, wasm_f32x4_pmax(lo, x));
      return wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_mul(x, scale));
    };
    v128_t w0 =
        wasm_i16x8_narrow_i32x4(loadClampScale(src + i), loadClampScale(src + i + 4));
    v128_t w1 = wasm_i16x8_narrow_i32x4(loadClampScale(src + i + 8),
                                        loadClampScale(src + i + 12));
    wasm_v128_store(dst + i, wasm_i8x16_narrow_i16x8(w0, w1));
  }
#endif
  for (; i < count; ++i) {
    float x = src[i];
    if (soloud_simd::isNanBits(x)) {
      dst[i] = 0;
    } else {
      x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
      dst[i] = static_cast<int8_t>(x * 127.0f);
    }
  }
}

inline void convertFloatsToInt16Clamped(const float *src, int16_t *dst,
                                        size_t count) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 lo = _mm_set1_ps(-1.0f);
  const __m128 hi = _mm_set1_ps(1.0f);
  const __m128 scale = _mm_set1_ps(32767.0f);
  for (; i + 8 <= count; i += 8) {
    __m128 x0 = _mm_loadu_ps(src + i);
    __m128 x1 = _mm_loadu_ps(src + i + 4);
    x0 = _mm_and_ps(x0, _mm_cmpeq_ps(x0, x0));
    x1 = _mm_and_ps(x1, _mm_cmpeq_ps(x1, x1));
    x0 = _mm_min_ps(hi, _mm_max_ps(lo, x0));
    x1 = _mm_min_ps(hi, _mm_max_ps(lo, x1));
    __m128i d0 = _mm_cvttps_epi32(_mm_mul_ps(x0, scale));
    __m128i d1 = _mm_cvttps_epi32(_mm_mul_ps(x1, scale));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i),
                     _mm_packs_epi32(d0, d1));
  }
#elif defined(SOLOUD_SIMD_NEON)
  const int16x8_t minS16 = vdupq_n_s16(-32767);
  for (; i + 16 <= count; i += 16) {
    int16x4_t s0 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i), 32767.0f)));
    int16x4_t s1 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 4), 32767.0f)));
    int16x4_t s2 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 8), 32767.0f)));
    int16x4_t s3 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 12), 32767.0f)));
    vst1q_s16(dst + i, vmaxq_s16(vcombine_s16(s0, s1), minS16));
    vst1q_s16(dst + i + 8, vmaxq_s16(vcombine_s16(s2, s3), minS16));
  }
  for (; i + 8 <= count; i += 8) {
    int16x4_t s0 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i), 32767.0f)));
    int16x4_t s1 = vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 4), 32767.0f)));
    vst1q_s16(dst + i, vmaxq_s16(vcombine_s16(s0, s1), minS16));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t lo = wasm_f32x4_splat(-1.0f);
  const v128_t hi = wasm_f32x4_splat(1.0f);
  const v128_t scale = wasm_f32x4_splat(32767.0f);
  for (; i + 8 <= count; i += 8) {
    v128_t x0 = wasm_v128_load(src + i);
    v128_t x1 = wasm_v128_load(src + i + 4);
    x0 = wasm_v128_and(x0, wasm_f32x4_eq(x0, x0));
    x1 = wasm_v128_and(x1, wasm_f32x4_eq(x1, x1));
    x0 = wasm_f32x4_pmin(hi, wasm_f32x4_pmax(lo, x0));
    x1 = wasm_f32x4_pmin(hi, wasm_f32x4_pmax(lo, x1));
    v128_t d0 = wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_mul(x0, scale));
    v128_t d1 = wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_mul(x1, scale));
    wasm_v128_store(dst + i, wasm_i16x8_narrow_i32x4(d0, d1));
  }
#endif
  for (; i < count; ++i) {
    float x = src[i];
    if (soloud_simd::isNanBits(x)) {
      dst[i] = 0;
    } else {
      x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
      dst[i] = static_cast<int16_t>(x * 32767.0f);
    }
  }
}

/// Convert `count` floats in `[-1.0f, 1.0f]` to `int32_t` scaled by `scale`
/// (e.g. `32767.0f` for 16-bit FLAC containers or `8388607.0f` for 24-bit).
inline void convertFloatsToInt32ScaledClamped(const float *src, int32_t *dst,
                                              size_t count,
                                              float scale) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 lo = _mm_set1_ps(-1.0f);
  const __m128 hi = _mm_set1_ps(1.0f);
  const __m128 vScale = _mm_set1_ps(scale);
  for (; i + 4 <= count; i += 4) {
    __m128 x = _mm_loadu_ps(src + i);
    x = _mm_and_ps(x, _mm_cmpeq_ps(x, x));
    x = _mm_min_ps(hi, _mm_max_ps(lo, x));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i),
                     _mm_cvttps_epi32(_mm_mul_ps(x, vScale)));
  }
#elif defined(SOLOUD_SIMD_NEON)
  const float32x4_t lo = vdupq_n_f32(-1.0f);
  const float32x4_t hi = vdupq_n_f32(1.0f);
  for (; i + 4 <= count; i += 4) {
    float32x4_t x = vld1q_f32(src + i);
    x = vreinterpretq_f32_u32(
        vandq_u32(vreinterpretq_u32_f32(x), vceqq_f32(x, x)));
    x = vminq_f32(hi, vmaxq_f32(lo, x));
    vst1q_s32(dst + i, vcvtq_s32_f32(vmulq_n_f32(x, scale)));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t lo = wasm_f32x4_splat(-1.0f);
  const v128_t hi = wasm_f32x4_splat(1.0f);
  const v128_t vScale = wasm_f32x4_splat(scale);
  for (; i + 4 <= count; i += 4) {
    v128_t x = wasm_v128_load(src + i);
    x = wasm_v128_and(x, wasm_f32x4_eq(x, x));
    x = wasm_f32x4_pmin(hi, wasm_f32x4_pmax(lo, x));
    wasm_v128_store(dst + i,
                    wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_mul(x, vScale)));
  }
#endif
  for (; i < count; ++i) {
    float x = src[i];
    if (soloud_simd::isNanBits(x)) {
      dst[i] = 0;
    } else {
      x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
      dst[i] = static_cast<int32_t>(x * scale);
    }
  }
}

/// Convert `count` floats in `[-1.0f, 1.0f]` to full-scale signed 32-bit PCM
/// without overflowing `INT32_MAX` at `+1.0f`!
inline void convertFloatsToInt32Clamped(const float *src, int32_t *dst,
                                        size_t count) noexcept {
  // 2147483520.0f (0x1.fffffep+30f) is the largest IEEE-754 float32 strictly
  // less than 2^31 (2147483648.0f). Using 2147483647.0f rounds up to 2^31 in
  // float32 and causes cvttss2si / cvttps2dq on x86 to emit 0x80000000
  // (-2147483648), inverting peak polarity!
  constexpr float kMinScaled = -2147483648.0f;
  constexpr float kMaxScaled = 2147483520.0f;
  constexpr float kScale = 2147483648.0f;
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 lo = _mm_set1_ps(kMinScaled);
  const __m128 hi = _mm_set1_ps(kMaxScaled);
  const __m128 scale = _mm_set1_ps(kScale);
  for (; i + 4 <= count; i += 4) {
    __m128 x = _mm_loadu_ps(src + i);
    x = _mm_and_ps(x, _mm_cmpeq_ps(x, x));
    __m128 s = _mm_min_ps(hi, _mm_max_ps(lo, _mm_mul_ps(x, scale)));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), _mm_cvttps_epi32(s));
  }
#elif defined(SOLOUD_SIMD_NEON)
  const int32x4_t maxS32 = vdupq_n_s32(2147483520);
  for (; i + 16 <= count; i += 16) {
    vst1q_s32(dst + i, vminq_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i), kScale)), maxS32));
    vst1q_s32(dst + i + 4, vminq_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 4), kScale)), maxS32));
    vst1q_s32(dst + i + 8, vminq_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 8), kScale)), maxS32));
    vst1q_s32(dst + i + 12, vminq_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i + 12), kScale)), maxS32));
  }
  for (; i + 4 <= count; i += 4) {
    vst1q_s32(dst + i, vminq_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(src + i), kScale)), maxS32));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t lo = wasm_f32x4_splat(kMinScaled);
  const v128_t hi = wasm_f32x4_splat(kMaxScaled);
  const v128_t scale = wasm_f32x4_splat(kScale);
  for (; i + 4 <= count; i += 4) {
    v128_t x = wasm_v128_load(src + i);
    x = wasm_v128_and(x, wasm_f32x4_eq(x, x));
    v128_t s = wasm_f32x4_pmin(hi, wasm_f32x4_pmax(lo, wasm_f32x4_mul(x, scale)));
    wasm_v128_store(dst + i, wasm_i32x4_trunc_sat_f32x4(s));
  }
#endif
  for (; i < count; ++i) {
    const float x = src[i];
    if (soloud_simd::isNanBits(x)) {
      dst[i] = 0;
    } else {
      float s = x * kScale;
      if (s < kMinScaled) s = kMinScaled;
      if (s > kMaxScaled) s = kMaxScaled;
      dst[i] = static_cast<int32_t>(s);
    }
  }
}

// -----------------------------------------------------------------------------
// SIMD Reductions & In-Place Clipping Kernels
// -----------------------------------------------------------------------------
inline double sumOfSquaresF32(const float *src, size_t count) noexcept {
  double total = 0.0;
  constexpr size_t kBlock = 4096;
  size_t offset = 0;
  while (offset < count) {
    const size_t chunk = std::min(kBlock, count - offset);
    const float *p = src + offset;
    soloud_simd::f32x4 acc0 = soloud_simd::f32x4::zero();
    soloud_simd::f32x4 acc1 = soloud_simd::f32x4::zero();
    soloud_simd::f32x4 acc2 = soloud_simd::f32x4::zero();
    soloud_simd::f32x4 acc3 = soloud_simd::f32x4::zero();
    size_t i = 0;
    for (; i + 16 <= chunk; i += 16) {
      const auto v0 = soloud_simd::f32x4::loadu(p + i);
      const auto v1 = soloud_simd::f32x4::loadu(p + i + 4);
      const auto v2 = soloud_simd::f32x4::loadu(p + i + 8);
      const auto v3 = soloud_simd::f32x4::loadu(p + i + 12);
      acc0 = soloud_simd::fma(acc0, v0, v0);
      acc1 = soloud_simd::fma(acc1, v1, v1);
      acc2 = soloud_simd::fma(acc2, v2, v2);
      acc3 = soloud_simd::fma(acc3, v3, v3);
    }
    for (; i + 4 <= chunk; i += 4) {
      const auto v0 = soloud_simd::f32x4::loadu(p + i);
      acc0 = soloud_simd::fma(acc0, v0, v0);
    }
    acc0 = soloud_simd::add(soloud_simd::add(acc0, acc1),
                            soloud_simd::add(acc2, acc3));
    double blockSum = static_cast<double>(soloud_simd::reduce_add(acc0));
    for (; i < chunk; ++i) {
      const double s = static_cast<double>(p[i]);
      blockSum += s * s;
    }
    total += blockSum;
    offset += chunk;
  }
  return total;
}

inline float peakAbsF32(const float *src, size_t count) noexcept {
  soloud_simd::f32x4 m0 = soloud_simd::f32x4::zero();
  soloud_simd::f32x4 m1 = soloud_simd::f32x4::zero();
  size_t i = 0;
  for (; i + 8 <= count; i += 8) {
    m0 = soloud_simd::max(m0,
                          soloud_simd::abs(soloud_simd::f32x4::loadu(src + i)));
    m1 = soloud_simd::max(
        m1, soloud_simd::abs(soloud_simd::f32x4::loadu(src + i + 4)));
  }
  for (; i + 4 <= count; i += 4) {
    m0 = soloud_simd::max(m0,
                          soloud_simd::abs(soloud_simd::f32x4::loadu(src + i)));
  }
  float peak = soloud_simd::reduce_max(soloud_simd::max(m0, m1));
  for (; i < count; ++i) {
    const float a = std::fabs(src[i]);
    if (a > peak) peak = a;
  }
  return peak;
}

inline void clipHardInPlaceF32(float *buf, size_t count, float lo = -1.0f,
                               float hi = 1.0f,
                               float postScale = 1.0f) noexcept {
  const auto vLo = soloud_simd::f32x4::splat(lo);
  const auto vHi = soloud_simd::f32x4::splat(hi);
  if (postScale == 1.0f) {
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
      soloud_simd::clamp(soloud_simd::f32x4::loadu(buf + i), vLo, vHi)
          .storeu(buf + i);
      soloud_simd::clamp(soloud_simd::f32x4::loadu(buf + i + 4), vLo, vHi)
          .storeu(buf + i + 4);
    }
    for (; i + 4 <= count; i += 4) {
      soloud_simd::clamp(soloud_simd::f32x4::loadu(buf + i), vLo, vHi)
          .storeu(buf + i);
    }
    for (; i < count; ++i) {
      float x = buf[i];
      if (x < lo) x = lo;
      if (x > hi) x = hi;
      buf[i] = x;
    }
  } else {
    const auto vScale = soloud_simd::f32x4::splat(postScale);
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
      soloud_simd::mul(
          soloud_simd::clamp(soloud_simd::f32x4::loadu(buf + i), vLo, vHi),
          vScale)
          .storeu(buf + i);
      soloud_simd::mul(
          soloud_simd::clamp(soloud_simd::f32x4::loadu(buf + i + 4), vLo, vHi),
          vScale)
          .storeu(buf + i + 4);
    }
    for (; i + 4 <= count; i += 4) {
      soloud_simd::mul(
          soloud_simd::clamp(soloud_simd::f32x4::loadu(buf + i), vLo, vHi),
          vScale)
          .storeu(buf + i);
    }
    for (; i < count; ++i) {
      float x = buf[i];
      if (x < lo) x = lo;
      if (x > hi) x = hi;
      buf[i] = x * postScale;
    }
  }
}

/// Vectorized in-place SoLoud roundoff clipper (`[-1.65f, 1.65f]` ->
/// `0.87f * x - 0.1f * x^3`, clamped to `[-0.9862875f, 0.9862875f]`).
inline void clipRoundoffInPlaceF32(float *buf, size_t count,
                                   float postScale = 1.0f) noexcept {
  size_t i = 0;
#if defined(SOLOUD_SIMD_SSE2)
  const __m128 negbound = _mm_set1_ps(-1.65f);
  const __m128 posbound = _mm_set1_ps(1.65f);
  const __m128 linearscale = _mm_set1_ps(0.87f);
  const __m128 cubicscale = _mm_set1_ps(-0.1f);
  const __m128 negwall = _mm_set1_ps(-0.9862875f);
  const __m128 poswall = _mm_set1_ps(0.9862875f);
  const __m128 postscale = _mm_set1_ps(postScale);
  for (; i + 4 <= count; i += 4) {
    __m128 f = _mm_loadu_ps(buf + i);
    __m128 u = _mm_cmpgt_ps(f, negbound);
    __m128 o = _mm_cmplt_ps(f, posbound);
    __m128 lin = _mm_mul_ps(f, linearscale);
    __m128 cubic = _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(f, f), f), cubicscale);
    f = _mm_add_ps(cubic, lin);
    f = _mm_add_ps(_mm_andnot_ps(u, negwall), _mm_and_ps(u, f));
    f = _mm_add_ps(_mm_andnot_ps(o, poswall), _mm_and_ps(o, f));
    _mm_storeu_ps(buf + i, _mm_mul_ps(f, postscale));
  }
#elif defined(SOLOUD_SIMD_NEON)
  const float32x4_t negbound = vdupq_n_f32(-1.65f);
  const float32x4_t posbound = vdupq_n_f32(1.65f);
  const float32x4_t linearscale = vdupq_n_f32(0.87f);
  const float32x4_t cubicscale = vdupq_n_f32(-0.1f);
  const float32x4_t negwall = vdupq_n_f32(-0.9862875f);
  const float32x4_t poswall = vdupq_n_f32(0.9862875f);
  const float32x4_t postscale = vdupq_n_f32(postScale);
  for (; i + 4 <= count; i += 4) {
    float32x4_t f = vld1q_f32(buf + i);
    uint32x4_t u = vcgtq_f32(f, negbound);
    uint32x4_t o = vcltq_f32(f, posbound);
    float32x4_t lin = vmulq_f32(f, linearscale);
    float32x4_t cubic = vmulq_f32(vmulq_f32(vmulq_f32(f, f), f), cubicscale);
    f = vaddq_f32(cubic, lin);
    f = vbslq_f32(u, f, negwall);
    f = vbslq_f32(o, f, poswall);
    vst1q_f32(buf + i, vmulq_f32(f, postscale));
  }
#elif defined(SOLOUD_SIMD_WASM)
  const v128_t negbound = wasm_f32x4_splat(-1.65f);
  const v128_t posbound = wasm_f32x4_splat(1.65f);
  const v128_t linearscale = wasm_f32x4_splat(0.87f);
  const v128_t cubicscale = wasm_f32x4_splat(-0.1f);
  const v128_t negwall = wasm_f32x4_splat(-0.9862875f);
  const v128_t poswall = wasm_f32x4_splat(0.9862875f);
  const v128_t postscale = wasm_f32x4_splat(postScale);
  for (; i + 4 <= count; i += 4) {
    v128_t f = wasm_v128_load(buf + i);
    v128_t u = wasm_f32x4_gt(f, negbound);
    v128_t o = wasm_f32x4_lt(f, posbound);
    v128_t lin = wasm_f32x4_mul(f, linearscale);
    v128_t cubic =
        wasm_f32x4_mul(wasm_f32x4_mul(wasm_f32x4_mul(f, f), f), cubicscale);
    f = wasm_f32x4_add(cubic, lin);
    f = wasm_v128_bitselect(f, negwall, u);
    f = wasm_v128_bitselect(f, poswall, o);
    wasm_v128_store(buf + i, wasm_f32x4_mul(f, postscale));
  }
#endif
  for (; i < count; ++i) {
    float c = buf[i];
    if (c <= -1.65f) {
      c = -0.9862875f;
    } else if (c >= 1.65f) {
      c = 0.9862875f;
    } else {
      c = 0.87f * c - 0.1f * c * c * c;
    }
    buf[i] = c * postScale;
  }
}

}  // namespace SoLoud

#endif  // FLUTTER_SOLOUD_SIMD_PCM_H

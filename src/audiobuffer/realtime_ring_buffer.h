#pragma once

#ifndef REALTIME_RING_BUFFER_H
#define REALTIME_RING_BUFFER_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define SOLOUD_RT_SIMD_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define SOLOUD_RT_SIMD_NEON 1
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define SOLOUD_RT_SIMD_WASM 1
#endif

namespace SoLoud {

/// De-interleave `frames` stereo float frames (`L0 R0 L1 R1 ...`) into two
/// separate planar channel buffers `dstL` and `dstR`.
inline void deinterleaveStereoFloats(const float *src, float *dstL, float *dstR,
                                     size_t frames) noexcept {
  size_t i = 0;
#if defined(SOLOUD_RT_SIMD_SSE2)
  for (; i + 4 <= frames; i += 4) {
    // v0 = [L0, R0, L1, R1], v1 = [L2, R2, L3, R3]
    __m128 v0 = _mm_loadu_ps(src + i * 2);
    __m128 v1 = _mm_loadu_ps(src + i * 2 + 4);
    __m128 left = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(2, 0, 2, 0));
    __m128 right = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(3, 1, 3, 1));
    _mm_storeu_ps(dstL + i, left);
    _mm_storeu_ps(dstR + i, right);
  }
#elif defined(SOLOUD_RT_SIMD_NEON)
  for (; i + 4 <= frames; i += 4) {
    float32x4x2_t lr = vld2q_f32(src + i * 2);
    vst1q_f32(dstL + i, lr.val[0]);
    vst1q_f32(dstR + i, lr.val[1]);
  }
#elif defined(SOLOUD_RT_SIMD_WASM)
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

/// De-interleave `frames` multi-channel float frames into a planar buffer where
/// channel `ch` starts at `dstPlanar + ch * dstChannelStride`.
inline void deinterleaveFloatsToPlanar(const float *src, float *dstPlanar,
                                       size_t dstChannelStride, size_t frames,
                                       unsigned int channels) noexcept {
  if (frames == 0 || channels == 0) {
    return;
  }
  if (channels == 1) {
    std::memcpy(dstPlanar, src, frames * sizeof(float));
    return;
  }
  if (channels == 2) {
    deinterleaveStereoFloats(src, dstPlanar, dstPlanar + dstChannelStride,
                             frames);
    return;
  }
  for (unsigned int ch = 0; ch < channels; ++ch) {
    float *dst = dstPlanar + ch * dstChannelStride;
    for (size_t i = 0; i < frames; ++i) {
      dst[i] = src[i * channels + ch];
    }
  }
}

/// Convert and de-interleave `frames` stereo 16-bit signed integer frames into
/// normalized float planar buffers `dstL` and `dstR`.
inline void deinterleaveStereoInt16ToFloats(const int16_t *src, float *dstL,
                                            float *dstR,
                                            size_t frames) noexcept {
  constexpr float kScale = 1.0f / 32768.0f;
  for (size_t i = 0; i < frames; ++i) {
    dstL[i] = static_cast<float>(src[i * 2]) * kScale;
    dstR[i] = static_cast<float>(src[i * 2 + 1]) * kScale;
  }
}

/// Wait-free Single-Producer Single-Consumer (SPSC) planar float ring buffer
/// with a preserved rewind history window (`mHistoryFrames`) so SoLoud's
/// retroactive checkpoint rollback can re-read recently mixed frames.
class RealtimeRingBuffer {
public:
  RealtimeRingBuffer() = default;
  ~RealtimeRingBuffer() = default;

  void init(unsigned int channels, size_t minCapacityFrames,
            size_t historyFrames = 4096) {
    mChannels = channels > 0 ? channels : 1;
    mHistoryFrames = historyFrames;
    const size_t totalRequired =
        std::max<size_t>(minCapacityFrames + mHistoryFrames, 256);
    size_t cap = 256;
    while (cap < totalRequired) {
      cap <<= 1;
    }
    mCapacityFrames = cap;
    mMask = cap - 1;
    mStorage.assign(mCapacityFrames * mChannels, 0.0f);
    mWriteFrame.store(0, std::memory_order_relaxed);
    mReadFrame.store(0, std::memory_order_relaxed);
  }

  void reset() noexcept {
    const uint64_t w = mWriteFrame.load(std::memory_order_relaxed);
    mReadFrame.store(w, std::memory_order_release);
  }

  unsigned int channels() const noexcept { return mChannels; }
  size_t capacityFrames() const noexcept {
    return mCapacityFrames > mHistoryFrames ? (mCapacityFrames - mHistoryFrames)
                                            : mCapacityFrames;
  }
  size_t rawCapacityFrames() const noexcept { return mCapacityFrames; }
  size_t historyFrames() const noexcept { return mHistoryFrames; }

  uint64_t getWriteFrame() const noexcept {
    return mWriteFrame.load(std::memory_order_acquire);
  }

  uint64_t getReadFrame() const noexcept {
    return mReadFrame.load(std::memory_order_acquire);
  }

  /// Restore the read cursor during a retroactive checkpoint rollback.
  void setReadFrame(uint64_t readFrame) noexcept {
    const uint64_t w = mWriteFrame.load(std::memory_order_acquire);
    if (readFrame > w) {
      readFrame = w;
    } else if (w - readFrame > mCapacityFrames) {
      readFrame = w - mCapacityFrames;
    }
    mReadFrame.store(readFrame, std::memory_order_release);
  }

  size_t availableToRead() const noexcept {
    const uint64_t w = mWriteFrame.load(std::memory_order_acquire);
    const uint64_t r = mReadFrame.load(std::memory_order_relaxed);
    return (w >= r) ? static_cast<size_t>(w - r) : 0;
  }

  size_t availableToWrite() const noexcept {
    const uint64_t w = mWriteFrame.load(std::memory_order_relaxed);
    const uint64_t r = mReadFrame.load(std::memory_order_acquire);
    const size_t unread = (w >= r) ? static_cast<size_t>(w - r) : 0;
    const size_t usable = capacityFrames();
    return (unread < usable) ? (usable - unread) : 0;
  }

  /// Producer: push interleaved float32 frames, de-interleaving directly into
  /// the internal planar slices. Returns the number of frames written.
  size_t pushInterleavedFloat32(const float *interleaved,
                                size_t frames) noexcept {
    if (interleaved == nullptr || frames == 0 || mCapacityFrames == 0) {
      return 0;
    }
    const size_t toWrite = std::min(frames, availableToWrite());
    if (toWrite == 0) {
      return 0;
    }

    const uint64_t w = mWriteFrame.load(std::memory_order_relaxed);
    const size_t startIdx = static_cast<size_t>(w) & mMask;
    const size_t firstPart = std::min(toWrite, mCapacityFrames - startIdx);
    const size_t secondPart = toWrite - firstPart;

    deinterleaveSliceFloat32(interleaved, startIdx, firstPart);
    if (secondPart > 0) {
      deinterleaveSliceFloat32(interleaved + firstPart * mChannels, 0,
                               secondPart);
    }

    mWriteFrame.store(w + toWrite, std::memory_order_release);
    return toWrite;
  }

  /// Producer: push planar float32 frames (`channels` contiguous blocks of
  /// `frames` floats each). Returns the number of frames written.
  size_t pushPlanarFloat32(const float *planar, size_t frames) noexcept {
    if (planar == nullptr || frames == 0 || mCapacityFrames == 0) {
      return 0;
    }
    const size_t toWrite = std::min(frames, availableToWrite());
    if (toWrite == 0) {
      return 0;
    }

    const uint64_t w = mWriteFrame.load(std::memory_order_relaxed);
    const size_t startIdx = static_cast<size_t>(w) & mMask;
    const size_t firstPart = std::min(toWrite, mCapacityFrames - startIdx);
    const size_t secondPart = toWrite - firstPart;

    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      const float *srcCh = planar + ch * frames;
      float *dstPlane = mStorage.data() + ch * mCapacityFrames;
      std::memcpy(dstPlane + startIdx, srcCh, firstPart * sizeof(float));
      if (secondPart > 0) {
        std::memcpy(dstPlane, srcCh + firstPart, secondPart * sizeof(float));
      }
    }

    mWriteFrame.store(w + toWrite, std::memory_order_release);
    return toWrite;
  }

  /// Producer: push interleaved int16 frames, converting to float32 [-1.0, 1.0]
  /// and de-interleaving into planar storage.
  size_t pushInterleavedInt16(const int16_t *interleaved,
                              size_t frames) noexcept {
    if (interleaved == nullptr || frames == 0 || mCapacityFrames == 0) {
      return 0;
    }
    const size_t toWrite = std::min(frames, availableToWrite());
    if (toWrite == 0) {
      return 0;
    }

    const uint64_t w = mWriteFrame.load(std::memory_order_relaxed);
    const size_t startIdx = static_cast<size_t>(w) & mMask;
    const size_t firstPart = std::min(toWrite, mCapacityFrames - startIdx);
    const size_t secondPart = toWrite - firstPart;

    deinterleaveSliceInt16(interleaved, startIdx, firstPart);
    if (secondPart > 0) {
      deinterleaveSliceInt16(interleaved + firstPart * mChannels, 0,
                             secondPart);
    }

    mWriteFrame.store(w + toWrite, std::memory_order_release);
    return toWrite;
  }

  /// Consumer (Audio Thread): copy up to `framesToRead` planar frames into
  /// SoLoud's `aBuffer` where each channel is separated by `aBufferStride`.
  size_t popPlanar(float *aBuffer, size_t framesToRead,
                   size_t aBufferStride) noexcept {
    if (aBuffer == nullptr || framesToRead == 0 || mCapacityFrames == 0) {
      return 0;
    }
    const size_t avail = availableToRead();
    const size_t toRead = std::min(framesToRead, avail);
    if (toRead == 0) {
      return 0;
    }

    const uint64_t r = mReadFrame.load(std::memory_order_relaxed);
    const size_t startIdx = static_cast<size_t>(r) & mMask;
    const size_t firstPart = std::min(toRead, mCapacityFrames - startIdx);
    const size_t secondPart = toRead - firstPart;

    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      const float *srcPlane = mStorage.data() + ch * mCapacityFrames;
      float *dstCh = aBuffer + ch * aBufferStride;
      std::memcpy(dstCh, srcPlane + startIdx, firstPart * sizeof(float));
      if (secondPart > 0) {
        std::memcpy(dstCh + firstPart, srcPlane, secondPart * sizeof(float));
      }
    }

    mReadFrame.store(r + toRead, std::memory_order_release);
    return toRead;
  }

private:
  void deinterleaveSliceFloat32(const float *src, size_t dstOffset,
                                size_t frames) noexcept {
    if (frames == 0) {
      return;
    }
    if (mChannels == 1) {
      std::memcpy(mStorage.data() + dstOffset, src, frames * sizeof(float));
      return;
    }
    if (mChannels == 2) {
      float *dstL = mStorage.data() + dstOffset;
      float *dstR = mStorage.data() + mCapacityFrames + dstOffset;
      deinterleaveStereoFloats(src, dstL, dstR, frames);
      return;
    }
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      float *dst = mStorage.data() + ch * mCapacityFrames + dstOffset;
      for (size_t i = 0; i < frames; ++i) {
        dst[i] = src[i * mChannels + ch];
      }
    }
  }

  void deinterleaveSliceInt16(const int16_t *src, size_t dstOffset,
                              size_t frames) noexcept {
    if (frames == 0) {
      return;
    }
    constexpr float kScale = 1.0f / 32768.0f;
    if (mChannels == 1) {
      float *dst = mStorage.data() + dstOffset;
      for (size_t i = 0; i < frames; ++i) {
        dst[i] = static_cast<float>(src[i]) * kScale;
      }
      return;
    }
    if (mChannels == 2) {
      float *dstL = mStorage.data() + dstOffset;
      float *dstR = mStorage.data() + mCapacityFrames + dstOffset;
      deinterleaveStereoInt16ToFloats(src, dstL, dstR, frames);
      return;
    }
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      float *dst = mStorage.data() + ch * mCapacityFrames + dstOffset;
      for (size_t i = 0; i < frames; ++i) {
        dst[i] = static_cast<float>(src[i * mChannels + ch]) * kScale;
      }
    }
  }

  std::vector<float> mStorage;
  size_t mCapacityFrames = 0;
  size_t mHistoryFrames = 0;
  size_t mMask = 0;
  unsigned int mChannels = 2;

  alignas(64) std::atomic<uint64_t> mWriteFrame{0};
  alignas(64) std::atomic<uint64_t> mReadFrame{0};
};

} // namespace SoLoud

#endif // REALTIME_RING_BUFFER_H

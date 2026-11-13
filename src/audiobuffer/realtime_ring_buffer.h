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

#include "../simd/simd_pcm.h"

namespace SoLoud {

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
    deinterleaveFloatsToPlanar(src, mStorage.data() + dstOffset,
                               static_cast<unsigned int>(mCapacityFrames),
                               static_cast<unsigned int>(frames), mChannels);
  }

  void deinterleaveSliceInt16(const int16_t *src, size_t dstOffset,
                              size_t frames) noexcept {
    if (frames == 0) {
      return;
    }
    constexpr float kScale = 1.0f / 32768.0f;
    if (mChannels == 1) {
      convertInt16ToFloats(src, mStorage.data() + dstOffset,
                           static_cast<unsigned int>(frames));
      return;
    }
    if (mChannels == 2) {
      float *dstL = mStorage.data() + dstOffset;
      float *dstR = mStorage.data() + mCapacityFrames + dstOffset;
      deinterleaveStereoInt16ToFloats(src, dstL, dstR,
                                      static_cast<unsigned int>(frames));
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

#pragma once

#ifndef OUTPUT_CAPTURE_H
#define OUTPUT_CAPTURE_H

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

class OutputCaptureBuffer {
public:
  OutputCaptureBuffer() = default;

  bool init(size_t capacityFrames, size_t channels) {
    if (capacityFrames == 0 || channels == 0) {
      return false;
    }

    size_t capacity = 1;
    while (capacity < capacityFrames) {
      capacity <<= 1;
    }

    mChannels = channels;
    mBuffer.assign(capacity * channels, 0.0f);
    mMask = capacity - 1;
    clear();
    return true;
  }

  void clear() {
    mHead.store(0, std::memory_order_relaxed);
    mTail.store(0, std::memory_order_relaxed);
  }

  size_t available() const {
    const size_t head = mHead.load(std::memory_order_acquire);
    const size_t tail = mTail.load(std::memory_order_acquire);
    return head - tail;
  }

  void push(const float *data, size_t frames, size_t channels) {
    if (mChannels != channels || mChannels == 0 || data == nullptr ||
        frames == 0) {
      return;
    }

    const size_t capacityFrames = mBuffer.size() / mChannels;
    if (capacityFrames == 0) {
      return;
    }

    if (frames > capacityFrames) {
      const size_t skipped = frames - capacityFrames;
      data += skipped * mChannels;
      frames = capacityFrames;
    }

    const size_t head = mHead.load(std::memory_order_relaxed);
    size_t tail = mTail.load(std::memory_order_acquire);
    const size_t used = head - tail;
    const size_t free = capacityFrames - used;
    if (frames > free) {
      const size_t skipped = frames - free;
      data += skipped * mChannels;
      frames = free;
      if (frames == 0) {
        return;
      }
    }

    const size_t startFrame = head & mMask;
    const size_t start = startFrame * mChannels;
    const size_t firstFrames =
        frames < capacityFrames - startFrame
            ? frames
            : capacityFrames - startFrame;
    std::memcpy(&mBuffer[start], data,
                firstFrames * mChannels * sizeof(float));
    if (frames > firstFrames) {
      std::memcpy(&mBuffer[0], data + firstFrames * mChannels,
                  (frames - firstFrames) * mChannels * sizeof(float));
    }

    mHead.store(head + frames, std::memory_order_release);
  }

  size_t pop(float *out, size_t frames, size_t channels) {
    if (mChannels != channels || out == nullptr || frames == 0) {
      return 0;
    }

    size_t tail = mTail.load(std::memory_order_relaxed);
    const size_t head = mHead.load(std::memory_order_acquire);
    const size_t availableFrames = head - tail;
    const size_t toRead = frames < availableFrames ? frames : availableFrames;
    if (toRead == 0) {
      return 0;
    }

    const size_t capacityFrames = mBuffer.size() / mChannels;
    const size_t startFrame = tail & mMask;
    const size_t start = startFrame * mChannels;
    const size_t first = toRead < capacityFrames - startFrame
                             ? toRead
                             : capacityFrames - startFrame;
    std::memcpy(out, &mBuffer[start], first * mChannels * sizeof(float));
    if (toRead > first) {
      std::memcpy(out + first * mChannels, &mBuffer[0],
                  (toRead - first) * mChannels * sizeof(float));
    }

    mTail.store(tail + toRead, std::memory_order_release);
    return toRead;
  }

private:
  std::vector<float> mBuffer;
  size_t mChannels = 0;
  size_t mMask = 0;
  std::atomic<size_t> mHead{0};
  std::atomic<size_t> mTail{0};
};

#endif

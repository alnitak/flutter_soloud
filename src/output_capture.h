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

  bool init(size_t capacitySamples) {
    if (capacitySamples == 0) {
      return false;
    }

    size_t capacity = 1;
    while (capacity < capacitySamples) {
      capacity <<= 1;
    }

    mBuffer.assign(capacity, 0.0f);
    mMask = capacity - 1;
    clear();
    mDroppedSamples.store(0, std::memory_order_relaxed);
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

  size_t droppedSamples() const {
    return mDroppedSamples.load(std::memory_order_acquire);
  }

  void push(const float *data, size_t count) {
    const size_t capacity = mBuffer.size();
    if (capacity == 0 || data == nullptr || count == 0) {
      return;
    }

    if (count > capacity) {
      const size_t skipped = count - capacity;
      data += skipped;
      count = capacity;
      mDroppedSamples.fetch_add(skipped, std::memory_order_release);
    }

    size_t head = mHead.load(std::memory_order_relaxed);
    size_t tail = mTail.load(std::memory_order_acquire);
    const size_t used = head - tail;
    const size_t free = capacity - used;
    if (count > free) {
      const size_t dropped = count - free;
      tail += dropped;
      mTail.store(tail, std::memory_order_release);
      mDroppedSamples.fetch_add(dropped, std::memory_order_release);
    }

    const size_t start = head & mMask;
    const size_t first = count < capacity - start ? count : capacity - start;
    std::memcpy(&mBuffer[start], data, first * sizeof(float));
    if (count > first) {
      std::memcpy(&mBuffer[0], data + first, (count - first) * sizeof(float));
    }

    mHead.store(head + count, std::memory_order_release);
  }

  size_t pop(float *out, size_t count) {
    if (out == nullptr || count == 0) {
      return 0;
    }

    size_t tail = mTail.load(std::memory_order_relaxed);
    const size_t head = mHead.load(std::memory_order_acquire);
    const size_t availableSamples = head - tail;
    const size_t toRead = count < availableSamples ? count : availableSamples;
    if (toRead == 0) {
      return 0;
    }

    const size_t capacity = mBuffer.size();
    const size_t start = tail & mMask;
    const size_t first =
        toRead < capacity - start ? toRead : capacity - start;
    std::memcpy(out, &mBuffer[start], first * sizeof(float));
    if (toRead > first) {
      std::memcpy(out + first, &mBuffer[0], (toRead - first) * sizeof(float));
    }

    mTail.store(tail + toRead, std::memory_order_release);
    return toRead;
  }

private:
  std::vector<float> mBuffer;
  size_t mMask = 0;
  std::atomic<size_t> mHead{0};
  std::atomic<size_t> mTail{0};
  std::atomic<size_t> mDroppedSamples{0};
};

#endif

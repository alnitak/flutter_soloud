#include "circular_float_buffer.h"

#include <algorithm>
#include <cstring>

#include "../simd/simd_pcm.h"

namespace SoLoud {

CircularFloatBuffer::CircularFloatBuffer(size_t capacitySamples) {
  setCapacity(capacitySamples);
}

void CircularFloatBuffer::setCapacity(size_t capacitySamples) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_capacity = capacitySamples;
  m_buffer.assign(m_capacity, 0.0f);
  m_readOffset = 0;
  m_writeOffset = 0;
  m_size.store(0, std::memory_order_relaxed);
  m_totalWritten = 0;
  m_hasWrapped = false;
}

size_t CircularFloatBuffer::available() const {
  return m_size.load(std::memory_order_relaxed);
}

size_t CircularFloatBuffer::freeSpace() const {
  return m_capacity - m_size.load(std::memory_order_relaxed);
}

size_t CircularFloatBuffer::contiguousFree() const {
  if (m_writeOffset > m_readOffset) return m_capacity - m_writeOffset;
  if (m_writeOffset == m_readOffset) {
    const size_t size = m_size.load(std::memory_order_relaxed);
    return size == m_capacity ? 0 : m_capacity - m_writeOffset;
  }
  return m_readOffset - m_writeOffset;
}

size_t CircularFloatBuffer::write(const float *data, size_t count) {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::mutex> lock(m_mutex);
  size_t written = 0;
  while (written < count) {
    const size_t free = contiguousFree();
    if (free == 0) break;
    const size_t toWrite = std::min(count - written, free);
    std::memcpy(m_buffer.data() + m_writeOffset, data + written,
                toWrite * sizeof(float));
    m_writeOffset = (m_writeOffset + toWrite) % m_capacity;
    m_size.fetch_add(toWrite, std::memory_order_relaxed);
    // Once the cumulative writes reach the capacity, every slot has been
    // written at least once: the whole free gap behind the read pointer is
    // valid recently-consumed data that rewindRead() can restore.
    m_totalWritten += toWrite;
    if (m_totalWritten >= m_capacity) {
      m_hasWrapped = true;
    }
    written += toWrite;
  }

  return written;
}

size_t CircularFloatBuffer::read(float *out, size_t count) {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::mutex> lock(m_mutex);
  size_t readTotal = 0;
  while (readTotal < count) {
    const size_t currentSize = m_size.load(std::memory_order_relaxed);
    if (currentSize == 0) break;

    size_t segment;
    if (m_writeOffset > m_readOffset) {
      segment = m_writeOffset - m_readOffset;
    } else if (m_writeOffset < m_readOffset) {
      segment = m_capacity - m_readOffset;
    } else { // write == read
      // Buffer is full; read from the read pointer to the end of the buffer.
      segment = m_capacity - m_readOffset;
    }

    const size_t toRead = std::min({count - readTotal, segment, currentSize});
    if (toRead == 0) break;
    std::memcpy(out + readTotal, m_buffer.data() + m_readOffset,
                toRead * sizeof(float));
    m_readOffset = (m_readOffset + toRead) % m_capacity;
    // Update the size immediately so contiguousFree() sees the correct state
    // after this segment is consumed.
    m_size.fetch_sub(toRead, std::memory_order_relaxed);
    readTotal += toRead;
  }

  return readTotal;
}

size_t CircularFloatBuffer::readPlanar(float *dstPlanar,
                                       size_t dstChannelStride, size_t frames,
                                       unsigned int channels) {
  if (dstPlanar == nullptr || frames == 0 || channels == 0 || m_capacity == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  size_t framesRead = 0;
  while (framesRead < frames) {
    const size_t currentSize = m_size.load(std::memory_order_relaxed);
    if (currentSize < channels) break;

    size_t segment;
    if (m_writeOffset > m_readOffset) {
      segment = m_writeOffset - m_readOffset;
    } else {
      segment = m_capacity - m_readOffset;
    }
    segment = std::min(segment, currentSize);

    if (segment >= channels) {
      const size_t segFrames =
          std::min(frames - framesRead, segment / channels);
      const size_t floatsConsumed = segFrames * channels;
      deinterleaveFloatsToPlanar(
          m_buffer.data() + m_readOffset, dstPlanar + framesRead,
          static_cast<unsigned int>(dstChannelStride),
          static_cast<unsigned int>(segFrames), channels);
      m_readOffset = (m_readOffset + floatsConsumed) % m_capacity;
      m_size.fetch_sub(floatsConsumed, std::memory_order_relaxed);
      framesRead += segFrames;
    } else {
      // Unaligned ring capacity fallback: 1 frame straddling wrap boundary.
      for (unsigned int ch = 0; ch < channels; ++ch) {
        dstPlanar[ch * dstChannelStride + framesRead] = m_buffer[m_readOffset];
        m_readOffset = (m_readOffset + 1) % m_capacity;
      }
      m_size.fetch_sub(channels, std::memory_order_relaxed);
      framesRead += 1;
    }
  }
  return framesRead;
}

void CircularFloatBuffer::advanceRead(size_t count) {
  if (m_capacity == 0) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  const size_t currentSize = m_size.load(std::memory_order_relaxed);
  const size_t toAdvance = std::min(count, currentSize);
  m_readOffset = (m_readOffset + toAdvance) % m_capacity;
  m_size.fetch_sub(toAdvance, std::memory_order_relaxed);
}

size_t CircularFloatBuffer::rewindRead(size_t count) {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::mutex> lock(m_mutex);
  const size_t currentSize = m_size.load(std::memory_order_relaxed);
  const size_t free = m_capacity - currentSize;
  // Before the write pointer has lapped the ring, the free area behind the
  // read pointer is a mix of valid already-read data and never-written space;
  // limit the rewind to the valid region. After lapping, the whole free gap
  // is valid recent data. Note the buffer rarely becomes completely full in
  // practice, so lapping (not fullness) is the correct condition.
  const size_t maxRewind = m_hasWrapped ? free : std::min(m_readOffset, free);
  const size_t toRewind = std::min(count, maxRewind);
  m_readOffset = (m_readOffset + m_capacity - toRewind) % m_capacity;
  m_size.fetch_add(toRewind, std::memory_order_relaxed);
  return toRewind;
}

void CircularFloatBuffer::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_readOffset = 0;
  m_writeOffset = 0;
  m_size.store(0, std::memory_order_relaxed);
  m_totalWritten = 0;
  m_hasWrapped = false;
}

} // namespace SoLoud

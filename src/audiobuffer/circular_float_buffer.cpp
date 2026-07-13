#include "circular_float_buffer.h"

#include <algorithm>
#include <cstring>

namespace SoLoud {

CircularFloatBuffer::CircularFloatBuffer(size_t capacitySamples) {
  setCapacity(capacitySamples);
}

void CircularFloatBuffer::setCapacity(size_t capacitySamples) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_capacity = capacitySamples > 0 ? capacitySamples : 0;
  m_buffer.assign(m_capacity, 0.0f);
  m_readOffset.store(0);
  m_writeOffset.store(0);
  m_size.store(0);
}

size_t CircularFloatBuffer::available() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return m_size.load(std::memory_order_relaxed);
}

size_t CircularFloatBuffer::freeSpace() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return m_capacity - m_size.load(std::memory_order_relaxed);
}

size_t CircularFloatBuffer::contiguousAvailable() const {
  const size_t write = m_writeOffset.load(std::memory_order_relaxed);
  const size_t read = m_readOffset.load(std::memory_order_relaxed);
  if (write > read) return write - read;
  if (write == read) {
    const size_t size = m_size.load(std::memory_order_relaxed);
    return size == m_capacity ? m_capacity - read : 0;
  }
  return m_capacity - read;
}

size_t CircularFloatBuffer::contiguousFree() const {
  const size_t write = m_writeOffset.load(std::memory_order_relaxed);
  const size_t read = m_readOffset.load(std::memory_order_relaxed);
  if (write > read) return m_capacity - write;
  if (write == read) {
    const size_t size = m_size.load(std::memory_order_relaxed);
    return size == m_capacity ? 0 : m_capacity - write;
  }
  return read - write;
}

size_t CircularFloatBuffer::write(const float *data, size_t count) {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  size_t written = 0;
  while (written < count) {
    const size_t free = contiguousFree();
    if (free == 0) break;
    const size_t toWrite = std::min(count - written, free);
    const size_t write = m_writeOffset.load(std::memory_order_relaxed);
    std::memcpy(m_buffer.data() + write, data + written,
                toWrite * sizeof(float));
    m_writeOffset.store((write + toWrite) % m_capacity,
                        std::memory_order_release);
    // Track writes immediately so subsequent contiguousFree()/contiguousAvailable()
    // calls can distinguish a full buffer from an empty one when write == read.
    m_size.fetch_add(toWrite, std::memory_order_release);
    written += toWrite;
  }

  return written;
}

size_t CircularFloatBuffer::read(float *out, size_t count) {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  size_t readTotal = 0;
  while (readTotal < count) {
    const size_t avail = contiguousAvailable();
    if (avail == 0) break;
    const size_t toRead = std::min(count - readTotal, avail);
    const size_t read = m_readOffset.load(std::memory_order_relaxed);
    std::memcpy(out + readTotal, m_buffer.data() + read,
                toRead * sizeof(float));
    m_readOffset.store((read + toRead) % m_capacity,
                       std::memory_order_release);
    // Update size immediately so contiguousAvailable()/contiguousFree() see the
    // correct state after this segment is consumed.
    m_size.fetch_sub(toRead, std::memory_order_release);
    readTotal += toRead;
  }

  return readTotal;
}

size_t CircularFloatBuffer::peek(float *out, size_t count) const {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  const size_t maxRead =
      std::min(count, m_size.load(std::memory_order_relaxed));
  size_t readTotal = 0;
  size_t tempOffset = m_readOffset.load(std::memory_order_relaxed);
  while (readTotal < maxRead) {
    const size_t write = m_writeOffset.load(std::memory_order_relaxed);
    size_t avail;
    if (write >= tempOffset)
      avail = write - tempOffset;
    else
      avail = m_capacity - tempOffset;
    if (avail == 0) break;
    const size_t toRead = std::min(maxRead - readTotal, avail);
    std::memcpy(out + readTotal, m_buffer.data() + tempOffset,
                toRead * sizeof(float));
    tempOffset = (tempOffset + toRead) % m_capacity;
    readTotal += toRead;
  }
  return readTotal;
}

void CircularFloatBuffer::advanceRead(size_t count) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  const size_t currentSize = m_size.load(std::memory_order_relaxed);
  const size_t toAdvance = std::min(count, currentSize);
  const size_t read = m_readOffset.load(std::memory_order_relaxed);
  m_readOffset.store((read + toAdvance) % m_capacity, std::memory_order_release);
  m_size.fetch_sub(toAdvance, std::memory_order_release);
}

size_t CircularFloatBuffer::rewindRead(size_t count) {
  if (count == 0 || m_capacity == 0) return 0;

  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  const size_t read = m_readOffset.load(std::memory_order_relaxed);
  const size_t currentSize = m_size.load(std::memory_order_relaxed);
  const size_t free = m_capacity - currentSize;
  const size_t toRewind = std::min(count, free);
  m_readOffset.store((read + m_capacity - toRewind) % m_capacity,
                     std::memory_order_release);
  m_size.fetch_add(toRewind, std::memory_order_release);
  return toRewind;
}

void CircularFloatBuffer::clear() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_readOffset.store(0);
  m_writeOffset.store(0);
  m_size.store(0);
  std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
}

} // namespace SoLoud

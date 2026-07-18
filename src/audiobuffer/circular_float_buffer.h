#pragma once

#ifndef CIRCULAR_FLOAT_BUFFER_H
#define CIRCULAR_FLOAT_BUFFER_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace SoLoud {

/// A mutex-protected circular buffer of 32-bit floats with a single producer
/// (the Dart thread via addAudioDataStream) and a single consumer (the audio
/// thread via getAudio). All mutating methods are serialized by the mutex;
/// available()/freeSpace()/empty() are wait-free reads of an atomic counter.
///
/// The buffer stores interleaved float samples. Capacity is expressed in
/// samples, not bytes, to make the channel math trivial.
class CircularFloatBuffer {
public:
  CircularFloatBuffer() = default;

  explicit CircularFloatBuffer(size_t capacitySamples);

  ~CircularFloatBuffer() = default;

  /// Resize the buffer. This clears any existing data.
  void setCapacity(size_t capacitySamples);

  /// Total number of float samples the buffer can hold.
  size_t capacity() const { return m_capacity; }

  /// Number of float samples currently available to read.
  size_t available() const;

  /// Number of free float sample slots.
  size_t freeSpace() const;

  /// Return true if there is no data to read.
  bool empty() const { return available() == 0; }

  /// Write [count] floats from [data] into the buffer. Returns the number of
  /// floats actually written. If the buffer is full, zero is returned.
  size_t write(const float *data, size_t count);

  /// Read up to [count] floats into [out], interleaved as they are stored.
  /// Returns the number of floats actually read. The read pointer is advanced.
  size_t read(float *out, size_t count);

  /// Advance the read pointer by [count] samples. Must not exceed available().
  void advanceRead(size_t count);

  /// Move the read pointer backward by [count] samples, restoring samples
  /// that were previously consumed but are still physically in the buffer.
  /// Returns the number of samples actually rewound, which may be less than
  /// [count] if the data behind the read pointer has been overwritten.
  size_t rewindRead(size_t count);

  /// Reset to empty. Previously stored samples are not zeroed; the
  /// bookkeeping alone makes them unreachable.
  void clear();

private:
  std::vector<float> m_buffer;
  size_t m_capacity = 0;
  mutable std::mutex m_mutex;
  size_t m_readOffset = 0;       // Guarded by m_mutex.
  size_t m_writeOffset = 0;      // Guarded by m_mutex.
  std::atomic<size_t> m_size{0}; // Number of floats currently stored.
  size_t m_totalWritten = 0;     // Guarded by m_mutex. Cumulative floats ever written.
  bool m_hasWrapped = false;     // Guarded by m_mutex. True once the write pointer
                                 // has lapped (m_totalWritten >= m_capacity), i.e.
                                 // every slot holds valid data.

  size_t contiguousFree() const;
};

} // namespace SoLoud

#endif // CIRCULAR_FLOAT_BUFFER_H

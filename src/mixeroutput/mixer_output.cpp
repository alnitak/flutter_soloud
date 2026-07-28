#include "mixer_output.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "pcm_converter.h"
#include "wav_output_encoder.h"

MixerOutput &MixerOutput::instance() {
  static MixerOutput instance;
  return instance;
}

MixerOutput::~MixerOutput() { stop(); }

bool MixerOutput::isRunning() const { return m_running.load(); }

bool MixerOutput::isCompressedFormat() const {
  return m_format == MIXER_OUTPUT_OPUS || m_format == MIXER_OUTPUT_VORBIS ||
         m_format == MIXER_OUTPUT_FLAC || m_format == MIXER_OUTPUT_WAV;
}

PlayerErrors MixerOutput::start(MixerOutputFormat format, int sampleRate,
                                int channels, size_t bufferSizeBytes,
                                size_t notificationThresholdBytes,
                                int chunkPCMFrames) {
  if (m_running.load()) {
    return playerAlreadyInited;
  }

  if (bufferSizeBytes == 0 || notificationThresholdBytes == 0 ||
      notificationThresholdBytes > bufferSizeBytes) {
    return invalidParameter;
  }

  if (sampleRate <= 0 || channels <= 0) {
    return invalidParameter;
  }

  const bool isCompressed = format == MIXER_OUTPUT_OPUS ||
                            format == MIXER_OUTPUT_VORBIS ||
                            format == MIXER_OUTPUT_FLAC ||
                            format == MIXER_OUTPUT_WAV;

  const size_t bps = bytesPerSample(format);
  if (!isCompressed && bps == 0) {
    return audioFormatNotSupported;
  }
  const size_t bytesPerFrame = bps * channels;

  if (isCompressed && chunkPCMFrames > 0) {
    return invalidParameter;
  }

  if (chunkPCMFrames > 0) {
    if (chunkPCMFrames < 2048) {
      return invalidParameter;
    }
    const size_t chunkBytes =
        static_cast<size_t>(chunkPCMFrames) * bytesPerFrame;
    if (chunkBytes == 0 || chunkBytes > bufferSizeBytes) {
      return invalidParameter;
    }
  }

  m_format = format;
  m_sampleRate = sampleRate;
  m_channels = channels;
  m_bufferSize = bufferSizeBytes;
  m_notificationThreshold = notificationThresholdBytes;
  m_wavHeader.clear();

  // Increment the capture id so any in-flight callbacks from a previous
  // capture session can be identified and discarded by the Dart side.
  m_captureId.fetch_add(1);

  if (isCompressedFormat()) {
    m_encoder.reset(MixerOutputEncoder::create(format));
    if (m_encoder == nullptr || !m_encoder->initialize(sampleRate, channels)) {
      m_encoder.reset();
      m_format = MIXER_OUTPUT_PCM_F32LE;
      return audioFormatNotSupported;
    }

    m_bytesPerSample = m_encoder->inputBytesPerSample();
    m_bytesPerFrame = m_bytesPerSample * channels;

#ifndef __EMSCRIPTEN__
    m_pcmQueue = std::make_unique<PcmChunkQueue>();
#endif
    m_buffer.assign(m_bufferSize, 0);
    m_writeOffset.store(0);
    m_readOffset.store(0);
    m_notified.store(false);
    m_shouldStop.store(false);
    m_running.store(true);

    m_chunkPCMFrames = 0;
    m_chunkBytes = 0;
    m_chunkBuffer.clear();

#ifdef __EMSCRIPTEN__
    // On web there are no worker threads: encoding runs inline in
    // onAudioData(), which is called on the main browser thread (the audio
    // thread there). Write the stream header up front like the encoder
    // thread does on native.
    std::vector<uint8_t> header;
    if (m_encoder->writeCurrentHeader(header) && !header.empty()) {
      writeToBuffer(header.data(), header.size());
    }
#else
    m_encoderThread = std::thread(&MixerOutput::encoderThreadFunc, this);
    m_notificationThread =
        std::thread(&MixerOutput::notificationThreadFunc, this);
#endif
  } else {
    m_bytesPerSample = bps;
    m_bytesPerFrame = bytesPerFrame;

    if (chunkPCMFrames > 0) {
      m_chunkPCMFrames = static_cast<size_t>(chunkPCMFrames);
      m_chunkBytes = m_chunkPCMFrames * m_bytesPerFrame;
      m_chunkBuffer.assign(m_chunkBytes, 0);
    } else {
      m_chunkPCMFrames = 0;
      m_chunkBytes = 0;
      m_chunkBuffer.clear();
    }

    m_buffer.assign(m_bufferSize, 0);
    m_writeOffset.store(0);
    m_readOffset.store(0);
    m_notified.store(false);
    m_shouldStop.store(false);
    m_running.store(true);

#ifndef __EMSCRIPTEN__
    m_notificationThread =
        std::thread(&MixerOutput::notificationThreadFunc, this);
#endif
  }

  return noError;
}

void MixerOutput::stop() {
  if (!m_running.load()) {
    return;
  }

  m_running.store(false);
  m_shouldStop.store(true);

  if (m_pcmQueue != nullptr) {
    m_pcmQueue->stop();
  }

#ifdef __EMSCRIPTEN__
  // On web there is no encoder thread: flush the encoder tail inline so the
  // circular buffer is complete when this returns. The Dart side reads the
  // remaining data synchronously right after stop (_flushRemaining), and
  // joining a pthread here would block the main browser thread.
  if (m_encoder != nullptr) {
    m_encodeScratch.clear();
    if (m_encoder->finalize(m_encodeScratch) && !m_encodeScratch.empty()) {
      writeToBuffer(m_encodeScratch.data(), m_encodeScratch.size());
    }
  }
#endif

  if (m_encoderThread.joinable()) {
    m_encoderThread.join();
  }

  if (m_notificationThread.joinable()) {
    m_notificationThread.join();
  }

  // Cache the final WAV header after the encoder thread has finished so the
  // total sample count reflects all queued PCM data.
  if (m_encoder != nullptr) {
    m_encoder->writeCurrentHeader(m_wavHeader);
  }

  m_encoder.reset();
  m_pcmQueue.reset();

  // Keep the circular buffer and offsets intact so the Dart side can read
  // any tail data that did not reach the notification threshold.
  m_notified.store(false);
}

void MixerOutput::onAudioData(const float *data, unsigned int frames) {
  if (!m_running.load()) {
    return;
  }

  const size_t totalSamples = static_cast<size_t>(frames) * m_channels;

  if (totalSamples == 0) {
    return;
  }

  if (isCompressedFormat()) {
#ifdef __EMSCRIPTEN__
    // On web the audio callback runs on the main browser thread, so there
    // are no queue/encoder threads: encode inline and notify directly.
    if (m_encoder != nullptr) {
      m_encodeScratch.clear();
      if (m_encoder->encode(data, totalSamples, m_encodeScratch) &&
          !m_encodeScratch.empty()) {
        writeToBuffer(m_encodeScratch.data(), m_encodeScratch.size());
      }
    }
    processNotifications();
#else
    std::vector<float> chunk(data, data + totalSamples);
    m_pcmQueue->push(std::move(chunk));
#endif
    return;
  }

  const size_t bytesToWrite = totalSamples * m_bytesPerSample;

  // Make sure we do not overwrite unread data. If the user chose a buffer
  // that is too small, drop the oldest data by advancing the read offset.
  size_t writeOffset = m_writeOffset.load(std::memory_order_relaxed);
  size_t readOffset = m_readOffset.load(std::memory_order_acquire);
  size_t available = (writeOffset >= readOffset)
                         ? writeOffset - readOffset
                         : m_bufferSize - (readOffset - writeOffset);

  size_t freeSpace = m_bufferSize - available;
  if (bytesToWrite > freeSpace) {
    const size_t drop = bytesToWrite - freeSpace;
    readOffset = (readOffset + drop) % m_bufferSize;
    m_readOffset.store(readOffset, std::memory_order_release);
  }

  // Write the converted PCM data into the circular buffer, handling wrap.
  size_t firstChunk = std::min(bytesToWrite, m_bufferSize - writeOffset);
  convertPcm(data, m_buffer.data() + writeOffset, firstChunk / m_bytesPerSample,
             m_format);

  if (firstChunk < bytesToWrite) {
    size_t secondChunk = bytesToWrite - firstChunk;
    convertPcm(data + (firstChunk / m_bytesPerSample), m_buffer.data(),
               secondChunk / m_bytesPerSample, m_format);
  }

  m_writeOffset.store((writeOffset + bytesToWrite) % m_bufferSize,
                      std::memory_order_release);

#ifdef __EMSCRIPTEN__
  processNotifications();
#endif
}

#ifdef __EMSCRIPTEN__
void MixerOutput::processNotifications() {
  if (!m_callback) {
    return;
  }

  // Fixed-size PCM chunk mode: emit every complete chunk. The data is copied
  // into m_chunkBuffer, the read position is advanced immediately, and the
  // callback is invoked. Dart must not advance the circular buffer again for
  // these chunks.
  if (m_chunkBytes > 0) {
    while (getAvailableBytes() >= m_chunkBytes) {
      const size_t readOffset = m_readOffset.load(std::memory_order_relaxed);
      const size_t firstPart =
          std::min(m_chunkBytes, m_bufferSize - readOffset);
      std::memcpy(m_chunkBuffer.data(), m_buffer.data() + readOffset,
                  firstPart);

      if (firstPart < m_chunkBytes) {
        const size_t secondPart = m_chunkBytes - firstPart;
        std::memcpy(m_chunkBuffer.data() + firstPart, m_buffer.data(),
                    secondPart);
      }

      m_readOffset.store((readOffset + m_chunkBytes) % m_bufferSize,
                         std::memory_order_release);
      m_callback(m_chunkBuffer.data(), m_chunkBytes);
    }
    m_notified.store(false, std::memory_order_release);
    return;
  }

  // Threshold-based mode (used by default and by compressed formats):
  // notify when at least notificationThresholdBytes are available.
  const size_t available = getAvailableBytes();
  if (available >= m_notificationThreshold && !m_notified.load()) {
    m_notified.store(true, std::memory_order_release);
    const size_t readOffset = m_readOffset.load(std::memory_order_relaxed);
    const size_t length = std::min(available, m_bufferSize - readOffset);
    m_callback(m_buffer.data() + readOffset, length);
  }

  if (available < m_notificationThreshold) {
    m_notified.store(false, std::memory_order_release);
  }
}
#endif

void MixerOutput::writeToBuffer(const uint8_t *data, size_t bytes) {
  if (bytes == 0 || data == nullptr || m_bufferSize == 0) {
    return;
  }

  size_t writeOffset = m_writeOffset.load(std::memory_order_relaxed);
  size_t readOffset = m_readOffset.load(std::memory_order_acquire);
  size_t available = (writeOffset >= readOffset)
                         ? writeOffset - readOffset
                         : m_bufferSize - (readOffset - writeOffset);

  size_t freeSpace = m_bufferSize - available;
  if (bytes > freeSpace) {
    const size_t drop = bytes - freeSpace;
    readOffset = (readOffset + drop) % m_bufferSize;
    m_readOffset.store(readOffset, std::memory_order_release);
  }

  size_t firstChunk = std::min(bytes, m_bufferSize - writeOffset);
  std::memcpy(m_buffer.data() + writeOffset, data, firstChunk);

  if (firstChunk < bytes) {
    size_t secondChunk = bytes - firstChunk;
    std::memcpy(m_buffer.data(), data + firstChunk, secondChunk);
  }

  m_writeOffset.store((writeOffset + bytes) % m_bufferSize,
                      std::memory_order_release);
}

void MixerOutput::encoderThreadFunc() {
  if (m_encoder == nullptr || m_pcmQueue == nullptr) {
    return;
  }

  std::vector<float> chunk;
  std::vector<uint8_t> encoded;

  // Write the WAV header at the start of the stream so the file begins with
  // a valid-ish RIFF/WAVE header. The size fields are patched after capture.
  std::vector<uint8_t> header;
  if (m_encoder->writeCurrentHeader(header) && !header.empty()) {
    writeToBuffer(header.data(), header.size());
  }

  while (m_pcmQueue->pop(chunk)) {
    if (!m_encoder->encode(chunk.data(), chunk.size(), encoded)) {
      continue;
    }

    if (!encoded.empty()) {
      writeToBuffer(encoded.data(), encoded.size());
      encoded.clear();
    }
  }

  // Flush remaining encoded data.
  if (m_encoder->finalize(encoded) && !encoded.empty()) {
    writeToBuffer(encoded.data(), encoded.size());
  }
}

uint8_t *MixerOutput::getBufferPointer() {
  if (m_buffer.empty()) {
    return nullptr;
  }
  return m_buffer.data();
}

size_t MixerOutput::getBufferSize() const { return m_bufferSize; }

size_t MixerOutput::getAvailableBytes() const {
  const size_t writeOffset = m_writeOffset.load(std::memory_order_acquire);
  const size_t readOffset = m_readOffset.load(std::memory_order_acquire);
  if (writeOffset >= readOffset) {
    return writeOffset - readOffset;
  }
  return m_bufferSize - (readOffset - writeOffset);
}

size_t MixerOutput::getReadOffset() const {
  return m_readOffset.load(std::memory_order_acquire);
}

void MixerOutput::advanceReadPosition(size_t bytes) {
  if (bytes == 0 || m_bufferSize == 0) {
    return;
  }

  const size_t available = getAvailableBytes();
  const size_t toAdvance = std::min(bytes, available);
  const size_t readOffset = m_readOffset.load(std::memory_order_relaxed);
  m_readOffset.store((readOffset + toAdvance) % m_bufferSize,
                     std::memory_order_release);

  if (toAdvance > 0) {
    m_notified.store(false, std::memory_order_release);
  }
}

void MixerOutput::setDataCallback(MixerOutputDataCallback callback) {
  m_callback = callback;
}

void MixerOutput::notificationThreadFunc() {
  while (!m_shouldStop.load()) {
    const size_t available = getAvailableBytes();

    // Fixed-size PCM chunk mode: emit exactly m_chunkBytes at a time. The data
    // is copied into a local contiguous buffer, the read position is advanced
    // immediately, and the callback is invoked. Dart must not advance the
    // circular buffer again for these chunks.
    if (m_chunkBytes > 0) {
      if (available >= m_chunkBytes && !m_notified.load()) {
        m_notified.store(true, std::memory_order_release);

        size_t readOffset = m_readOffset.load(std::memory_order_relaxed);
        size_t firstPart = std::min(m_chunkBytes, m_bufferSize - readOffset);
        std::memcpy(m_chunkBuffer.data(), m_buffer.data() + readOffset,
                    firstPart);

        if (firstPart < m_chunkBytes) {
          size_t secondPart = m_chunkBytes - firstPart;
          std::memcpy(m_chunkBuffer.data() + firstPart, m_buffer.data(),
                      secondPart);
        }

        const size_t newReadOffset =
            (readOffset + m_chunkBytes) % m_bufferSize;
        m_readOffset.store(newReadOffset, std::memory_order_release);

        if (m_callback) {
          m_callback(m_chunkBuffer.data(), m_chunkBytes);
        }
      }

      if (available < m_chunkBytes) {
        m_notified.store(false, std::memory_order_release);
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    // Threshold-based mode (used by default and by compressed formats):
    // notify when at least notificationThresholdBytes are available.
    if (available >= m_notificationThreshold && !m_notified.load()) {
      m_notified.store(true, std::memory_order_release);

      if (m_callback) {
        const size_t readOffset = m_readOffset.load(std::memory_order_relaxed);
        const size_t length = std::min(available, m_bufferSize - readOffset);
        m_callback(m_buffer.data() + readOffset, length);
      }
    }

    if (available < m_notificationThreshold) {
      m_notified.store(false, std::memory_order_release);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

size_t MixerOutput::contiguousAvailable() const {
  const size_t available = getAvailableBytes();
  const size_t readOffset = m_readOffset.load(std::memory_order_relaxed);
  return std::min(available, m_bufferSize - readOffset);
}

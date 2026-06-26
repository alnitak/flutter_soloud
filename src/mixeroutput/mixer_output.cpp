#include "mixer_output.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "pcm_converter.h"

MixerOutput &MixerOutput::instance() {
  static MixerOutput instance;
  return instance;
}

MixerOutput::~MixerOutput() { stop(); }

bool MixerOutput::isRunning() const { return m_running.load(); }

bool MixerOutput::isCompressedFormat() const {
  return m_format == MIXER_OUTPUT_OPUS || m_format == MIXER_OUTPUT_VORBIS ||
         m_format == MIXER_OUTPUT_FLAC;
}

PlayerErrors MixerOutput::start(MixerOutputFormat format, int sampleRate,
                                int channels, size_t bufferSizeBytes,
                                size_t notificationThresholdBytes) {
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

  m_format = format;
  m_sampleRate = sampleRate;
  m_channels = channels;
  m_bufferSize = bufferSizeBytes;
  m_notificationThreshold = notificationThresholdBytes;

  const size_t bps = bytesPerSample(format);
  if (isCompressedFormat()) {
    m_encoder.reset(MixerOutputEncoder::create(format));
    if (m_encoder == nullptr || !m_encoder->initialize(sampleRate, channels)) {
      m_encoder.reset();
      m_format = MIXER_OUTPUT_PCM_F32LE;
      return audioFormatNotSupported;
    }

    m_bytesPerSample = m_encoder->inputBytesPerSample();
    m_bytesPerFrame = m_bytesPerSample * channels;

    m_pcmQueue = std::make_unique<PcmChunkQueue>();
    m_buffer.assign(m_bufferSize, 0);
    m_writeOffset.store(0);
    m_readOffset.store(0);
    m_notified.store(false);
    m_shouldStop.store(false);
    m_running.store(true);

    m_encoderThread = std::thread(&MixerOutput::encoderThreadFunc, this);
    m_notificationThread =
        std::thread(&MixerOutput::notificationThreadFunc, this);
  } else {
    if (bps == 0) {
      return audioFormatNotSupported;
    }

    m_bytesPerSample = bps;
    m_bytesPerFrame = bps * channels;

    m_buffer.assign(m_bufferSize, 0);
    m_writeOffset.store(0);
    m_readOffset.store(0);
    m_notified.store(false);
    m_shouldStop.store(false);
    m_running.store(true);

    m_notificationThread =
        std::thread(&MixerOutput::notificationThreadFunc, this);
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

  if (m_encoderThread.joinable()) {
    m_encoderThread.join();
  }

  if (m_notificationThread.joinable()) {
    m_notificationThread.join();
  }

  m_encoder.reset();
  m_pcmQueue.reset();

  m_buffer.clear();
  m_bufferSize = 0;
  m_writeOffset.store(0);
  m_readOffset.store(0);
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
    std::vector<float> chunk(data, data + totalSamples);
    m_pcmQueue->push(std::move(chunk));
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
}

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

  if (toAdvance > 0 && getAvailableBytes() < m_notificationThreshold) {
    m_notified.store(false, std::memory_order_release);
  }
}

void MixerOutput::setDataCallback(MixerOutputDataCallback callback) {
  m_callback = callback;
}

void MixerOutput::notificationThreadFunc() {
  while (!m_shouldStop.load()) {
    const size_t available = getAvailableBytes();

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

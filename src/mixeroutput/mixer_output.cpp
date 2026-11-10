#include "mixer_output.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "../device_lifecycle_test_hooks.h"
#include "pcm_converter.h"
#include "wav_output_encoder.h"

MixerOutput &MixerOutput::instance() {
  static MixerOutput instance;
  return instance;
}

MixerOutput::~MixerOutput() { stop(); }

bool MixerOutput::isRunning() const { return m_running.load(); }

uint64_t MixerOutput::enterAudioCallback() {
  const uint64_t session = m_activeSession.load(std::memory_order_acquire);
  if (session == kNoCaptureSession) {
    return kNoCaptureSession;
  }

  m_audioCallbacksInFlight.fetch_add(1, std::memory_order_acq_rel);

  // Re-read after registering. If a stop slipped in between the two loads it
  // either sees this callback in the count and waits for it, or it retired
  // before the increment -- in which case the session no longer matches and
  // the callback backs out without touching anything.
  if (m_activeSession.load(std::memory_order_acquire) != session) {
    m_audioCallbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
    return kNoCaptureSession;
  }

  return session;
}

void MixerOutput::leaveAudioCallback() {
  m_audioCallbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

void MixerOutput::retireSessionAndDrain() {
  m_activeSession.store(kNoCaptureSession, std::memory_order_release);

  // Spin rather than block: this runs on a control thread, the audio callback
  // holds the count for a few microseconds at most, and there is nothing here
  // a real-time thread could be made to wait on.
  while (m_audioCallbacksInFlight.load(std::memory_order_acquire) != 0) {
    std::this_thread::yield();
  }
}

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

#if defined(NO_XIPH_LIBS)
  // Without the Xiph libraries only the PCM formats (and the WAV encoder,
  // which does not depend on them) are supported.
  if (format == MIXER_OUTPUT_OPUS || format == MIXER_OUTPUT_VORBIS ||
      format == MIXER_OUTPUT_FLAC) {
    return xiphLibsNotFound;
  }
#endif

  const bool isCompressed = format == MIXER_OUTPUT_OPUS ||
                            format == MIXER_OUTPUT_VORBIS ||
                            format == MIXER_OUTPUT_FLAC ||
                            format == MIXER_OUTPUT_WAV;

  const size_t bps = bytesPerSample(format);
  if (!isCompressed && bps == 0) {
    return audioFormatNotSupported;
  }
  const size_t bytesPerFrame = bps * channels;

  // For uncompressed PCM formats, keep the circular buffer an exact multiple
  // of one interleaved PCM frame so buffer wrap-arounds never split multi-byte
  // samples or swap stereo/multichannel alignment.
  const size_t effectiveBufferSize =
      isCompressed ? bufferSizeBytes
                   : (bufferSizeBytes / bytesPerFrame) * bytesPerFrame;
  if (effectiveBufferSize == 0) {
    return invalidParameter;
  }

  if (isCompressed && chunkPCMFrames > 0) {
    return invalidParameter;
  }

  if (chunkPCMFrames > 0) {
    if (chunkPCMFrames < 2048) {
      return invalidParameter;
    }
    const size_t chunkBytes =
        static_cast<size_t>(chunkPCMFrames) * bytesPerFrame;
    if (chunkBytes == 0 || chunkBytes > effectiveBufferSize) {
      return invalidParameter;
    }
  }

  m_format = format;
  m_sampleRate = sampleRate;
  m_channels = channels;
  m_bufferSize = effectiveBufferSize;
  m_notificationThreshold =
      std::min(notificationThresholdBytes, effectiveBufferSize);
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

    // Published last, and only now: every buffer, pointer and worker this
    // session needs is in place, so an audio callback admitted from here on
    // cannot observe half-built state.
    m_activeSession.store(m_nextSession.fetch_add(1, std::memory_order_acq_rel),
                          std::memory_order_release);
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

    // See the compressed branch above: publish last.
    m_activeSession.store(m_nextSession.fetch_add(1, std::memory_order_acq_rel),
                          std::memory_order_release);
  }

  return noError;
}

void MixerOutput::stop() {
  if (!m_running.load()) {
    return;
  }

  // Retire the session and wait out any audio callback already inside it
  // *before* touching the state it is using. Everything below this line --
  // the encoder, the PCM queue, the buffers -- is owned by the session that
  // was just retired.
  retireSessionAndDrain();

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
  // Admission, not a bare `m_running` check: everything read below belongs to
  // the capture session and stop() must be able to wait this callback out
  // before destroying it.
  const CapturePass pass(*this);
  if (!pass) {
    return;
  }

  SOLOUD_TEST_BARRIER(mixerCaptureCallbackAdmitted);

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
  {
    std::lock_guard<MixerOutputMutex> lock(m_bufferMutex);
    const uint64_t oldWrite = m_writeOffset.load(std::memory_order_relaxed);
    const uint64_t newWrite = oldWrite + bytesToWrite;
    // A block larger than the ring keeps only its newest bytes. The ring and
    // the block are both whole frames, so what is skipped is too.
    const size_t kept = std::min(bytesToWrite, m_bufferSize);
    const size_t skipped = bytesToWrite - kept;
    const size_t offset =
        static_cast<size_t>((oldWrite + skipped) % m_bufferSize);
    const size_t first = std::min(kept, m_bufferSize - offset);
    convertPcm(data + skipped / m_bytesPerSample, m_buffer.data() + offset,
               first / m_bytesPerSample, m_format);
    if (first < kept) {
      convertPcm(data + (skipped + first) / m_bytesPerSample, m_buffer.data(),
                 (kept - first) / m_bytesPerSample, m_format);
    }
    // An overrun drops only the oldest bytes: the reader keeps the newest
    // m_bufferSize of them.
    advanceReadForWriteLocked(newWrite);
    m_writeOffset.store(newWrite, std::memory_order_release);
  }

#ifdef __EMSCRIPTEN__
  processNotifications();
#endif
}

void MixerOutput::advanceReadForWriteLocked(uint64_t newWrite) {
  if (newWrite <= m_bufferSize) {
    return;
  }
  const uint64_t oldest = newWrite - m_bufferSize;
  if (m_readOffset.load(std::memory_order_relaxed) < oldest) {
    m_readOffset.store(oldest, std::memory_order_release);
  }
}

bool MixerOutput::notifyOnePacket(size_t minimum, size_t fixedBytes) {
  const MixerOutputDataCallback callback =
      m_callback.load(std::memory_order_acquire);
  if (callback == nullptr) {
    return false;
  }

  uint8_t *data = nullptr;
  size_t length = 0;
  {
    std::lock_guard<MixerOutputMutex> lock(m_bufferMutex);
    const uint64_t read = m_readOffset.load(std::memory_order_relaxed);
    const size_t available = static_cast<size_t>(
        m_writeOffset.load(std::memory_order_relaxed) - read);
    if (available < minimum || (fixedBytes == 0 && m_notified.load())) {
      return false;
    }
    const size_t offset = static_cast<size_t>(read % m_bufferSize);
    if (fixedBytes > 0) {
      // Fixed-size PCM chunk mode: copy exactly one chunk into a contiguous
      // buffer and consume it here. Dart must not advance the read position
      // again for these chunks.
      length = fixedBytes;
      const size_t first = std::min(length, m_bufferSize - offset);
      std::memcpy(m_chunkBuffer.data(), m_buffer.data() + offset, first);
      if (first < length) {
        std::memcpy(m_chunkBuffer.data() + first, m_buffer.data(),
                    length - first);
      }
      m_readOffset.store(read + length, std::memory_order_release);
      data = m_chunkBuffer.data();
    } else {
      // Threshold mode (the default, and every compressed format): hand over
      // the contiguous unread bytes. The consumer acknowledges them with
      // advanceReadPosition(), which re-arms the notification.
      length = std::min(available, m_bufferSize - offset);
      data = m_buffer.data() + offset;
      m_notified.store(true, std::memory_order_release);
    }
  }
  callback(data, length);
  return true;
}

#ifdef __EMSCRIPTEN__
void MixerOutput::processNotifications() {
  if (m_chunkBytes > 0) {
    // Emit every complete chunk.
    while (notifyOnePacket(m_chunkBytes, m_chunkBytes)) {
    }
    return;
  }
  notifyOnePacket(m_notificationThreshold, 0);
}
#endif

void MixerOutput::writeToBuffer(const uint8_t *data, size_t bytes) {
  if (bytes == 0 || data == nullptr || m_bufferSize == 0) {
    return;
  }

  std::lock_guard<MixerOutputMutex> lock(m_bufferMutex);
  const uint64_t oldWrite = m_writeOffset.load(std::memory_order_relaxed);
  const uint64_t newWrite = oldWrite + bytes;
  // Bytes that do not fit at all are the oldest of this write: skip them
  // rather than write past the ring.
  const size_t kept = std::min(bytes, m_bufferSize);
  const size_t skipped = bytes - kept;
  const size_t offset =
      static_cast<size_t>((oldWrite + skipped) % m_bufferSize);
  const size_t first = std::min(kept, m_bufferSize - offset);
  std::memcpy(m_buffer.data() + offset, data + skipped, first);
  if (first < kept) {
    std::memcpy(m_buffer.data(), data + skipped + first, kept - first);
  }
  advanceReadForWriteLocked(newWrite);
  m_writeOffset.store(newWrite, std::memory_order_release);
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
  std::lock_guard<MixerOutputMutex> lock(m_bufferMutex);
  return static_cast<size_t>(m_writeOffset.load(std::memory_order_relaxed) -
                             m_readOffset.load(std::memory_order_relaxed));
}

size_t MixerOutput::getReadOffset() const {
  if (m_bufferSize == 0) {
    return 0;
  }
  return static_cast<size_t>(m_readOffset.load(std::memory_order_acquire) %
                             m_bufferSize);
}

void MixerOutput::advanceReadPosition(size_t bytes) {
  if (bytes == 0 || m_bufferSize == 0) {
    return;
  }

  std::lock_guard<MixerOutputMutex> lock(m_bufferMutex);
  const uint64_t readOffset = m_readOffset.load(std::memory_order_relaxed);
  const size_t available = static_cast<size_t>(
      m_writeOffset.load(std::memory_order_relaxed) - readOffset);
  const size_t toAdvance = std::min(bytes, available);
  m_readOffset.store(readOffset + toAdvance, std::memory_order_release);

  if (toAdvance > 0) {
    m_notified.store(false, std::memory_order_release);
  }
}

void MixerOutput::setDataCallback(MixerOutputDataCallback callback) {
  m_callback.store(callback, std::memory_order_release);
}

void MixerOutput::notificationThreadFunc() {
  while (!m_shouldStop.load()) {
    // Fixed-size PCM chunk mode: emit exactly m_chunkBytes at a time, every
    // complete chunk that is buffered, as the web path does. A chunk is
    // consumed when it is emitted, so a device block of several chunks, or a
    // pass that runs late, must not leave a backlog behind.
    if (m_chunkBytes > 0) {
      while (!m_shouldStop.load() &&
             notifyOnePacket(m_chunkBytes, m_chunkBytes)) {
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    // Threshold-based mode (used by default and by compressed formats):
    // notify when at least notificationThresholdBytes are available. The
    // consumer's acknowledgement re-arms it.
    notifyOnePacket(m_notificationThreshold, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

size_t MixerOutput::contiguousAvailable() const {
  const size_t available = getAvailableBytes();
  return std::min(available, m_bufferSize - getReadOffset());
}

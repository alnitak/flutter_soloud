#include "wav_stream_decoder.h"
#include "../soloud_common.h"
#include <algorithm>
#include <cmath>
#include <cstring>

size_t WavDecoderWrapper::on_read(void *pUserData, void *pBufferOut,
                                  size_t bytesToRead) {
  WavDecoderWrapper *decoder = (WavDecoderWrapper *)pUserData;
  size_t bytes_remaining = decoder->audioData.size() - decoder->m_read_pos;
  size_t bytes_to_copy = MIN(bytesToRead, bytes_remaining);

  if (bytes_to_copy > 0) {
    memcpy(pBufferOut, decoder->audioData.data() + decoder->m_read_pos,
           bytes_to_copy);
    decoder->m_read_pos += bytes_to_copy;
  }

  return bytes_to_copy;
}

drwav_bool32 WavDecoderWrapper::on_seek(void *pUserData, int offset,
                                        drwav_seek_origin origin) {
  WavDecoderWrapper *decoder = (WavDecoderWrapper *)pUserData;
  size_t new_pos;

  if (origin == DRWAV_SEEK_SET) {
    new_pos = static_cast<size_t>(offset);
  } else { // DRWAV_SEEK_CUR
    new_pos = decoder->m_read_pos + static_cast<size_t>(offset);
  }

  if (new_pos > decoder->audioData.size()) {
    return DRWAV_FALSE;
  }

  decoder->m_read_pos = new_pos;
  return DRWAV_TRUE;
}

WavDecoderWrapper::WavDecoderWrapper()
    : isInitialized(false), audioData({}), m_read_pos(0), mDataEnded(false) {}

WavDecoderWrapper::~WavDecoderWrapper() { cleanup(); }

void WavDecoderWrapper::cleanup() {
  if (isInitialized) {
    drwav_uninit(&decoder);
    isInitialized = false;
  }
  audioData.clear();
  m_read_pos = 0;
  mDataEnded = false;
}

void WavDecoderWrapper::setDataEnded() { mDataEnded = true; }

bool WavDecoderWrapper::initializeDecoder(int engineSamplerate,
                                          int engineChannels) {
  // Initialization is done lazily in decode() once enough data is available.
  return true;
}

bool WavDecoderWrapper::checkForValidWavHeader(
    const std::vector<unsigned char> &buffer) {
  if (buffer.size() < 12) {
    return false;
  }

  // Standard RIFF (little-endian) or RIFX (big-endian) container
  if (std::memcmp(buffer.data(), "RIFF", 4) != 0 &&
      std::memcmp(buffer.data(), "RIFX", 4) != 0) {
    return false;
  }

  // WAVE format identifier
  if (std::memcmp(buffer.data() + 8, "WAVE", 4) != 0) {
    return false;
  }

  return true;
}

std::pair<std::vector<float>, DecoderError>
WavDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *sampleRate,
                          int *channels, size_t maxOutputSamples) {
  // Append all new data from the input buffer to the internal audioData buffer.
  if (!buffer.empty()) {
    audioData.insert(audioData.end(), buffer.begin(), buffer.end());
    buffer.clear();
  }

  // Only return early if audioData is empty AND either:
  // - decoder is not initialized (need data to init), OR
  // - mDataEnded is false (more data may come)
  // When mDataEnded is true and decoder is initialized, dr_wav may still have
  // data in its internal buffer that hasn't been decoded yet.
  if (audioData.empty() && (!isInitialized || !mDataEnded)) {
    return {{}, DecoderError::NoError};
  }

  // --- Decoder Initialization ---
  if (!isInitialized) {
    size_t read_pos_before = m_read_pos;

    // drwav_init will read some initial data via the on_read callback to parse
    // the header and initialize the decoder with stream info (channels, sample
    // rate). If it fails, it is likely because there is not enough data yet.
    if (!drwav_init(&decoder, WavDecoderWrapper::on_read,
                    WavDecoderWrapper::on_seek, nullptr, this, nullptr)) {
      // Reset the read position so the next decode() call retries from the
      // beginning of the currently buffered data.
      m_read_pos = read_pos_before;
      return {{}, DecoderError::NoError};
    }
    isInitialized = true;

    if (onTrackChange) {
      AudioMetadata metadata;
      metadata.type = DetectedType::BUFFER_WAV;
      onTrackChange(metadata);
    }
  }

  // --- Decoding Loop ---
  std::vector<float> decodedData;
  const int MAX_FRAMES_PER_RUN = 4096;
  float pcm_frames[MAX_FRAMES_PER_RUN];
  drwav_uint64 frames_read;

  while (true) {
    // If the decoder's sampleRate is 0, it means it hasn't successfully parsed
    // the stream yet. Wait until the sampleRate is known.
    if (decoder.sampleRate == 0 || decoder.channels == 0) {
      break;
    }

    int framesToRequest = MAX_FRAMES_PER_RUN / decoder.channels;

    if (maxOutputSamples > 0) {
      const size_t remainingSamples =
          maxOutputSamples - decodedData.size();
      const size_t remainingFrames = remainingSamples / decoder.channels;
      if (remainingFrames == 0) {
        break;
      }
      framesToRequest =
          std::min(framesToRequest, static_cast<int>(remainingFrames));
    }

    frames_read =
        drwav_read_pcm_frames_f32(&decoder, framesToRequest, pcm_frames);
    if (frames_read > 0) {
      decodedData.insert(decodedData.end(), pcm_frames,
                         pcm_frames + frames_read * decoder.channels);
    } else {
      // Decoder returned 0 frames; no more data available.
      break;
    }

    // If the read position has reached the end of internal buffer and data
    // is NOT ended, stop and wait for more data. When mDataEnded is true,
    // continue the loop to let dr_wav fully drain its internal buffers.
    if (m_read_pos >= audioData.size() && !mDataEnded) {
      break;
    }
  }

  *sampleRate = static_cast<int>(decoder.sampleRate);
  *channels = static_cast<int>(decoder.channels);

  // --- Buffer Cleanup ---
  // After decoding, erase the portion of audioData that has been successfully
  // read. Any remaining data is a partial frame, which will be used in the next
  // decode() call. When mDataEnded is true and we've consumed all data, clear
  // everything.
  if (m_read_pos > 0) {
    if (mDataEnded && m_read_pos >= audioData.size()) {
      audioData.clear();
      m_read_pos = 0;
    } else {
      audioData.erase(audioData.begin(), audioData.begin() + m_read_pos);
      m_read_pos = 0;
    }
  }

  return {decodedData, DecoderError::NoError};
}

bool WavDecoderWrapper::canSeekToTime(double seconds) const {
  return isInitialized && seconds >= 0.0;
}

uint64_t WavDecoderWrapper::timeToByteOffset(double seconds) {
  if (!isInitialized || seconds <= 0.0) return 0;
  const uint64_t frame = static_cast<uint64_t>(
      std::floor(seconds * decoder.sampleRate));
  const uint32_t bitsPerSample =
      decoder.bitsPerSample > 0 ? decoder.bitsPerSample : 16;
  return decoder.dataChunkDataPos +
         frame * decoder.channels * (bitsPerSample / 8);
}

double WavDecoderWrapper::getDuration() const {
  if (!isInitialized || decoder.sampleRate == 0) return -1.0;
  if (decoder.totalPCMFrameCount == 0) return -1.0;
  return static_cast<double>(decoder.totalPCMFrameCount) /
         static_cast<double>(decoder.sampleRate);
}

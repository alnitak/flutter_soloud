#include "wav_stream_decoder.h"
#include "../soloud_common.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// Same as dr_wav's drwav_get_bytes_per_pcm_frame(), which is not exported.
drwav_uint32 bytesPerPcmFrame(const drwav &wav) {
  if ((wav.bitsPerSample & 0x7) == 0) {
    return (wav.bitsPerSample * wav.fmt.channels) >> 3;
  }
  return wav.fmt.blockAlign;
}
} // namespace

size_t WavDecoderWrapper::on_read(void *pUserData, void *pBufferOut,
                                  size_t bytesToRead) {
  WavDecoderWrapper *decoder = (WavDecoderWrapper *)pUserData;
  size_t bytes_remaining = decoder->audioData.size() - decoder->m_read_pos;
  // Once the header is parsed, a read that is only partly buffered returns
  // nothing while more data may come: dr_wav counts the bytes of a short read
  // as consumed and drops a partial frame, which would shift every later
  // sample. The read is retried once more data has arrived.
  if (decoder->isInitialized && !decoder->mDataEnded &&
      bytes_remaining < bytesToRead) {
    return 0;
  }
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
    : isInitialized(false), audioData({}), m_read_pos(0), mDataEnded(false),
      mDrained(false) {}

WavDecoderWrapper::~WavDecoderWrapper() { cleanup(); }

void WavDecoderWrapper::cleanup() {
  if (isInitialized) {
    drwav_uninit(&decoder);
    isInitialized = false;
  }
  audioData.clear();
  m_read_pos = 0;
  mDataEnded = false;
  mDrained = false;
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
  if (m_read_pos >= audioData.size() && (!isInitialized || !mDataEnded)) {
    // No data and none will come: a decoder that never initialized is done.
    if (!isInitialized && mDataEnded)
      mDrained = true;
    return {{}, DecoderError::NoError};
  }

  // --- Decoder Initialization ---
  if (!isInitialized) {
    // Out-of-buffer seeks feed data starting deep in the WAV data chunk. The
    // dr_wav parser needs the RIFF header to initialize, so prepend the captured
    // header if the current buffer doesn't contain one.
    if (!audioData.empty() &&
        !WavDecoderWrapper::checkForValidWavHeader(audioData)) {
      if (!mHeader.empty()) {
        audioData.insert(audioData.begin(), mHeader.begin(), mHeader.end());
      }
    }

    size_t read_pos_before = m_read_pos;

    // drwav_init will read some initial data via the on_read callback to parse
    // the header and initialize the decoder with stream info (channels, sample
    // rate). If it fails, it is likely because there is not enough data yet.
    if (!drwav_init(&decoder, WavDecoderWrapper::on_read,
                    WavDecoderWrapper::on_seek, nullptr, this, nullptr)) {
      // Reset the read position so the next decode() call retries from the
      // beginning of the currently buffered data.
      m_read_pos = read_pos_before;
      if (mDataEnded) {
        // No more data will come, so the decoder can never initialize: end
        // the stream, or a drain loop on hasPendingData() never ends.
        audioData.clear();
        m_read_pos = 0;
        mDrained = true;
      }
      return {{}, DecoderError::NoError};
    }
    isInitialized = true;

    // Capture the header bytes so future seeks can reinitialize from a
    // mid-stream chunk. The header spans from the start of the file to the
    // beginning of the data chunk.
    if (mHeader.empty() && decoder.dataChunkDataPos > 0 &&
        static_cast<uint64_t>(decoder.dataChunkDataPos) <= audioData.size()) {
      mHeader.assign(audioData.begin(),
                       audioData.begin() + decoder.dataChunkDataPos);
    }

    // A "data" chunk size of 0xFFFFFFFF marks a stream of unknown length.
    // dr_wav replaces it by the number of bytes buffered at init, which would
    // stop the stream after the first piece: let it read until the data ends.
    mUnknownLength = false;
    if (!mDataEnded && decoder.dataChunkDataPos >= 4 &&
        static_cast<uint64_t>(decoder.dataChunkDataPos) <= audioData.size() &&
        (decoder.container == drwav_container_riff ||
         decoder.container == drwav_container_rifx)) {
      const unsigned char *sz = audioData.data() + decoder.dataChunkDataPos - 4;
      const uint32_t rawSize =
          decoder.container == drwav_container_riff
              ? (uint32_t)sz[0] | ((uint32_t)sz[1] << 8) |
                    ((uint32_t)sz[2] << 16) | ((uint32_t)sz[3] << 24)
              : (uint32_t)sz[3] | ((uint32_t)sz[2] << 8) |
                    ((uint32_t)sz[1] << 16) | ((uint32_t)sz[0] << 24);
      const drwav_uint32 bpf = bytesPerPcmFrame(decoder);
      if (rawSize == 0xFFFFFFFF && bpf > 0) {
        const bool compressed =
            decoder.translatedFormatTag == DR_WAVE_FORMAT_ADPCM ||
            decoder.translatedFormatTag == DR_WAVE_FORMAT_DVI_ADPCM;
        drwav_uint64 size = 0xFFFFFFFFull;
        if (!compressed) size -= size % bpf;
        decoder.dataChunkDataSize = size;
        decoder.bytesRemaining = size;
        // An upper bound: ADPCM holds at most two frames per byte.
        decoder.totalPCMFrameCount = compressed ? size * 2 : size / bpf;
        mUnknownLength = true;
      }
    }

    // After an out-of-buffer seek the data fed starts at the byte offset
    // given by timeToByteOffset(), right behind the re-prepended header: the
    // decoder is already at the target frame. Account for the skipped bytes
    // so the data chunk still ends where it does in the file.
    if (mPendingSeekTargetSample > 0 &&
        mPendingSeekByteOffset > decoder.dataChunkDataPos) {
      const drwav_uint64 skipped =
          std::min<drwav_uint64>(decoder.bytesRemaining,
                                 mPendingSeekByteOffset - decoder.dataChunkDataPos);
      decoder.bytesRemaining -= skipped;
    }
    mPendingSeekTargetSample = 0;
    mPendingSeekByteOffset = 0;

    if (onTrackChange) {
      AudioMetadata metadata;
      metadata.type = DetectedType::BUFFER_WAV;
      onTrackChange(metadata);
    }
  }

  // --- Decoding Loop ---
  std::vector<float> decodedData;
  if (maxOutputSamples > 0) {
    decodedData.reserve(maxOutputSamples);
  }
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

    // Request only the whole frames that are buffered (uncompressed formats),
    // so the read does not wait for a full chunk of input.
    const drwav_uint32 bytesPerFrame = bytesPerPcmFrame(decoder);
    if (!mDataEnded && bytesPerFrame > 0 &&
        decoder.translatedFormatTag != DR_WAVE_FORMAT_ADPCM &&
        decoder.translatedFormatTag != DR_WAVE_FORMAT_DVI_ADPCM) {
      const size_t bufferedFrames =
          (audioData.size() - m_read_pos) / bytesPerFrame;
      if (bufferedFrames == 0) {
        break;
      }
      framesToRequest =
          static_cast<int>(std::min<size_t>(framesToRequest, bufferedFrames));
    }

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
      if (mDataEnded) {
        mDrained = true;
      }
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
    if (m_read_pos >= audioData.size()) {
      audioData.clear();
      m_read_pos = 0;
    } else if (m_read_pos >= 65536 && m_read_pos * 2 >= audioData.size()) {
      audioData.erase(audioData.begin(), audioData.begin() + m_read_pos);
      m_read_pos = 0;
    }
  }

  if (mDataEnded && m_read_pos >= audioData.size()) {
    mDrained = true;
  }

  return {std::move(decodedData), DecoderError::NoError};
}

bool WavDecoderWrapper::hasPendingData() const {
  if (mDataEnded && mDrained) {
    return false;
  }
  if (m_read_pos < audioData.size()) {
    return true;
  }
  if (mDataEnded && !mDrained) {
    return true;
  }
  return false;
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
  if (decoder.totalPCMFrameCount == 0 || mUnknownLength) return -1.0;
  return static_cast<double>(decoder.totalPCMFrameCount) /
         static_cast<double>(decoder.sampleRate);
}

void WavDecoderWrapper::prepareForSeek(uint64_t targetSample,
                                       uint64_t byteOffset) {
  if (isInitialized) {
    // Capture the header before uninitializing, if it wasn't already captured
    // during a previous decode() call.
    if (mHeader.empty() && decoder.dataChunkDataPos > 0) {
      // audioData has already been consumed, so we cannot recover the header
      // here. It should have been captured right after drwav_init succeeded.
    }
    drwav_uninit(&decoder);
    isInitialized = false;
  }
  mPendingSeekTargetSample = targetSample;
  mPendingSeekByteOffset = byteOffset;
  m_read_pos = 0;
  audioData.clear();
  mDataEnded = false;
  mDrained = false;
}

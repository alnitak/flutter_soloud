#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#include "../soloud/src/audiosource/wav/dr_mp3.h"

#include "../soloud_common.h"
#include "mp3_stream_decoder.h"
#include <algorithm>
#include <cmath>
#include <fstream>

size_t MP3DecoderWrapper::on_read(void *pUserData, void *pBufferOut,
                                  size_t bytesToRead) {
  MP3DecoderWrapper *decoder = (MP3DecoderWrapper *)pUserData;
  size_t bytes_remaining = decoder->audioData.size() - decoder->m_read_pos;
  size_t bytes_to_copy = MIN(bytesToRead, bytes_remaining);

  if (bytes_to_copy > 0) {
    memcpy(pBufferOut, decoder->audioData.data() + decoder->m_read_pos,
           bytes_to_copy);
    decoder->m_read_pos += bytes_to_copy;
  }

  return bytes_to_copy;
}

drmp3_bool32 MP3DecoderWrapper::on_seek(void *pUserData, int offset,
                                        drmp3_seek_origin origin) {
  // This is a streaming decoder, seeking is not practical.
  // However, dr_mp3 might use it internally for skipping tags, so here a basic
  // implementation.
  MP3DecoderWrapper *decoder = (MP3DecoderWrapper *)pUserData;
  size_t new_pos;

  if (origin == DRMP3_SEEK_SET) {
    new_pos = offset;
  } else { // DRMP3_SEEK_CUR
    new_pos = decoder->m_read_pos + offset;
  }

  if (new_pos > decoder->audioData.size()) {
    return DRMP3_FALSE; // out of bounds
  }

  decoder->m_read_pos = new_pos;
  return DRMP3_TRUE;
}

void MP3DecoderWrapper::on_meta(void *pUserData,
                                const drmp3_metadata *pMetadata) {
  MP3DecoderWrapper *decoder = (MP3DecoderWrapper *)pUserData;

  if (decoder == nullptr || decoder->onTrackChange == nullptr) {
    return;
  }

  if (pMetadata->type == DRMP3_METADATA_TYPE_ID3V2 && !decoder->ID3TagsFound) {
    AudioMetadata metadata;
    metadata.type = DetectedType::BUFFER_MP3_WITH_ID3;

    const unsigned char *rawData = (const unsigned char *)pMetadata->pRawData;
    size_t totalTagSize = pMetadata->rawDataSize;

    if (totalTagSize < 20)
      return;

    size_t pos = 10; // Skip ID3v2 header

    while (pos + 10 < totalTagSize) {
      char frame_id[5] = {0};
      memcpy(frame_id, rawData + pos, 4);

      if (frame_id[0] == 0)
        break; // Padding or end of tags

      // Frame size is a synchsafe integer in ID3v2.3/4
      uint32_t frame_size = ((rawData[pos + 4] & 0x7f) << 21) |
                            ((rawData[pos + 5] & 0x7f) << 14) |
                            ((rawData[pos + 6] & 0x7f) << 7) |
                            (rawData[pos + 7] & 0x7f);

      pos += 10; // Move to frame content

      if (pos + frame_size > totalTagSize)
        break; // Malformed tag

      if (frame_id[0] == 'T') {      // Common text frames
        size_t text_start = pos + 1; // Skip encoding byte
        if (text_start < pos + frame_size) {
          std::string value(
              reinterpret_cast<const char *>(rawData + text_start),
              frame_size - 1);

          // TODO: add artwork?
          if (strcmp(frame_id, "TIT2") == 0)
            metadata.mp3Metadata.title = value;
          else if (strcmp(frame_id, "TPE1") == 0)
            metadata.mp3Metadata.artist = value;
          else if (strcmp(frame_id, "TALB") == 0)
            metadata.mp3Metadata.album = value;
          else if (strcmp(frame_id, "TYER") == 0)
            metadata.mp3Metadata.date = value;
          else if (strcmp(frame_id, "TCON") == 0)
            metadata.mp3Metadata.genre = value;
        }
      }

      pos += frame_size;
    }

    decoder->onTrackChange(metadata);
    decoder->ID3TagsFound = true; // Prevent firing multiple times
  }
}

MP3DecoderWrapper::MP3DecoderWrapper()
    : isInitialized(false), audioData({}), m_read_pos(0),
      bytes_until_meta(0), // no metadata expected by default
      lastMetadata(""), mIcyMetaInt(0), ID3TagsFound(false),
      mDataEnded(false) {}

MP3DecoderWrapper::~MP3DecoderWrapper() { cleanup(); }

void MP3DecoderWrapper::cleanup() {
  if (isInitialized) {
    drmp3_uninit(&decoder);
    isInitialized = false;
  }
  audioData.clear();
  m_read_pos = 0;
  bytes_until_meta = mIcyMetaInt;
  metadata_buffer.clear();
  lastMetadata = "";
  ID3TagsFound = false;
  mDataEnded = false;
}

void MP3DecoderWrapper::setDataEnded() { mDataEnded = true; }

void MP3DecoderWrapper::setIcyMetaInt(int icyMetaInt) {
  if (mIcyMetaInt == icyMetaInt)
    return;

  mIcyMetaInt = icyMetaInt;
  bytes_until_meta = mIcyMetaInt;
}

// Processes a buffer for an ICY stream (internet radio).
// It strips out metadata, appends the audio data to our internal buffer,
// and leaves any unprocessed data in the input buffer for the next call.
void MP3DecoderWrapper::processIcyStream(std::vector<unsigned char> &buffer) {
  size_t bufferSize = buffer.size();
  size_t readingPos = 0;

  while (readingPos < bufferSize) {
    size_t bytes_to_read =
        MIN((size_t)bytes_until_meta, bufferSize - readingPos);

    // Append audio data to our internal buffer
    audioData.insert(audioData.end(), buffer.begin() + readingPos,
                     buffer.begin() + readingPos + bytes_to_read);
    readingPos += bytes_to_read;
    bytes_until_meta -= bytes_to_read;

    if (bytes_until_meta == 0 && readingPos < bufferSize) {
      // Time for metadata
      int len_byte = buffer[readingPos];
      int metadata_len = len_byte * 16;
      readingPos++; // Skip metadata length byte

      if (readingPos + metadata_len <= bufferSize) {
        if (len_byte > 0) {
          // Extract and process metadata
          std::string title(
              reinterpret_cast<const char *>(buffer.data() + readingPos),
              metadata_len);
          if (lastMetadata != title) {
            AudioMetadata metadata;
            metadata.type = DetectedType::BUFFER_MP3_STREAM;
            metadata.mp3Metadata.title = title;
            lastMetadata = title;
            if (onTrackChange)
              onTrackChange(metadata);
          }
        }
        readingPos += metadata_len;
        bytes_until_meta = mIcyMetaInt;
      } else {
        // Not enough data for the full metadata block.
        readingPos--; // Rewind to include the metadata length byte for the next
                      // round.
        break;
      }
    }
  }

  // Remove the processed part from the input buffer. The rest will be appended
  // to audioData.
  buffer.erase(buffer.begin(), buffer.begin() + readingPos);
}

std::pair<std::vector<float>, DecoderError>
MP3DecoderWrapper::decode(std::vector<unsigned char> &buffer, int *samplerate,
                          int *channels, size_t maxOutputSamples) {
  // For ICY streams, process the buffer to strip metadata first.
  if (detectedType == DetectedType::BUFFER_MP3_STREAM && mIcyMetaInt > 0) {
    processIcyStream(buffer);
  }

  // Append all new (or remaining) data from the input buffer to the internal
  // audioData buffer.
  if (!buffer.empty()) {
    audioData.insert(audioData.end(), buffer.begin(), buffer.end());
    buffer.clear(); // Signal to the caller that we've consumed the buffer.
  }

  // Only return early if audioData is empty AND either:
  // - decoder is not initialized (need data to init), OR
  // - mDataEnded is false (more data may come)
  // When mDataEnded is true and decoder is initialized, dr_mp3 may still have
  // data in its internal buffer that needs to be flushed.
  if (audioData.empty() && (!isInitialized || !mDataEnded)) {
    return {{}, DecoderError::NoError};
  }

  // --- Decoder Initialization ---
  if (!isInitialized) {
    // Before initializing, check if we have a large ID3 tag that might not be
    // fully buffered yet.
    if (detectedType == DetectedType::BUFFER_MP3_WITH_ID3) {
      if (audioData.size() >= 10 && memcmp(audioData.data(), "ID3", 3) == 0) {
        uint32_t tagSize = ((audioData[6] & 0x7f) << 21) |
                           ((audioData[7] & 0x7f) << 14) |
                           ((audioData[8] & 0x7f) << 7) | (audioData[9] & 0x7f);
        uint32_t totalTagLength = tagSize + 10;

        // If we don't have the full tag yet, return and wait for more data.
        if (audioData.size() < totalTagLength) {
          return {{}, DecoderError::NoError};
        }
      }
    }

    // drmp3_init will read some initial data via the on_read callback to find
    // the first valid frame and initialize the decoder with stream info
    // (channels, sample rate).
    if (!drmp3_init(&decoder, MP3DecoderWrapper::on_read,
                    MP3DecoderWrapper::on_seek, nullptr,
                    MP3DecoderWrapper::on_meta, this, nullptr)) {
      // If init fails, it might be because there's not enough data to find a
      // valid frame yet. This is not a fatal error in a streaming context.
      return {{}, DecoderError::NoError};
    }
    isInitialized = true;
    buildSeekTable();
  }

  // --- Decoding Loop ---
  std::vector<float> decodedData;
  const int MAX_FRAMES_PER_RUN = 4096;
  float pcm_frames[MAX_FRAMES_PER_RUN];
  drmp3_uint64 frames_read;

  // Loop while the decoder can produce frames.
  // Continue until decoder returns 0 frames (no more data available).
  while (true) {
    // If the decoder's sampleRate is 0, it means it hasn't successfully decoded
    // a frame header yet. In this state, channels will also be 0, causing a
    // division by zero. We can wait until the sampleRate is known.
    if (decoder.sampleRate == 0) {
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
        drmp3_read_pcm_frames_f32(&decoder, framesToRequest, pcm_frames);
    if (frames_read > 0) {
      decodedData.insert(decodedData.end(), pcm_frames,
                         pcm_frames + frames_read * decoder.channels);
    } else {
      // Decoder returned 0 frames; no more data available.
      break;
    }

    // If the read position has reached the end of internal buffer and data
    // is NOT ended, we must stop and wait for more data. The next on_read()
    // call would return 0 and stop decoding.
    // When mDataEnded is true, we continue the loop to let dr_mp3 fully
    // drain its internal buffers.
    if (m_read_pos >= audioData.size() && !mDataEnded) {
      break;
    }
  }

  *samplerate = decoder.sampleRate;
  *channels = decoder.channels;

  // --- Buffer Cleanup ---
  // dr_mp3 buffers some input internally, so we cannot safely discard bytes
  // that have been passed to on_read() and reset m_read_pos. Keep the entire
  // accumulated stream so that dr_mp3 can seek back for frame synchronization
  // and buffering. The caller manages the overall decoded circular buffer; this
  // encoded scratch buffer is temporary and will be freed when the decoder is
  // destroyed.
  // (If memory growth becomes a concern, a sliding window can be added here,
  // but it must stay at least a few MP3 frames behind m_read_pos.)

  return {decodedData, DecoderError::NoError};
}

void MP3DecoderWrapper::buildSeekTable() {
  if (!isInitialized || !mSeekPoints.empty()) {
    return;
  }

  drmp3_uint32 desiredCount = 4096;
  std::vector<drmp3_seek_point> seekPoints(desiredCount);
  if (drmp3_calculate_seek_points(&decoder, &desiredCount, seekPoints.data())) {
    seekPoints.resize(desiredCount);
    mSeekPoints = std::move(seekPoints);
    drmp3_bind_seek_table(
        &decoder,
        static_cast<drmp3_uint32>(mSeekPoints.size()),
        mSeekPoints.data());
  }
}

bool MP3DecoderWrapper::canSeekToTime(double seconds) const {
  return isInitialized && !mSeekPoints.empty() && seconds > 0.0;
}

uint64_t MP3DecoderWrapper::timeToByteOffset(double seconds) {
  if (mSeekPoints.empty()) {
    buildSeekTable();
  }
  if (mSeekPoints.empty()) {
    return 0;
  }

  const uint64_t targetFrame = static_cast<uint64_t>(
      std::floor(seconds * decoder.sampleRate));

  const drmp3_seek_point *best = nullptr;
  for (const auto &sp : mSeekPoints) {
    if (sp.pcmFrameIndex <= targetFrame) {
      best = &sp;
    } else {
      break;
    }
  }
  if (best == nullptr) {
    return 0;
  }

  // Block out-of-buffer seeks until the table covers the target.
  if (targetFrame > mSeekPoints.back().pcmFrameIndex && !mDataEnded) {
    return 0;
  }

  return best->seekPosInBytes;
}

bool MP3DecoderWrapper::initializeDecoder(int engineSamplerate,
                                          int engineChannels) {
  // Initialization is now done lazily in the decode function.
  return true;
}

bool MP3DecoderWrapper::checkForValidFrames(
    const std::vector<unsigned char> &buffer) {
  if (buffer.size() < 4) { // A single MP3 header is 4 bytes.
    return false;
  }

  drmp3 temp_decoder;
  struct TempData {
    const unsigned char *buffer;
    size_t size;
    size_t pos;
  };
  TempData temp_data = {buffer.data(), buffer.size(), 0};

  auto temp_on_read = [](void *pUserData, void *pBufferOut,
                         size_t bytesToRead) -> size_t {
    TempData *data = (TempData *)pUserData;
    size_t bytes_remaining = data->size - data->pos;
    size_t bytes_to_copy = MIN(bytesToRead, bytes_remaining);
    if (bytes_to_copy > 0) {
      memcpy(pBufferOut, data->buffer + data->pos, bytes_to_copy);
      data->pos += bytes_to_copy;
    }
    return bytes_to_copy;
  };

  if (!drmp3_init(&temp_decoder, temp_on_read, nullptr, nullptr, nullptr,
                  &temp_data, nullptr)) {
    return false;
  }

  // Try to read just one frame to confirm validity.
  drmp3_uint64 frames_read =
      drmp3_read_pcm_frames_f32(&temp_decoder, 1, nullptr);

  drmp3_uninit(&temp_decoder);

  return frames_read > 0;
}

double MP3DecoderWrapper::getDuration() const {
  if (!isInitialized || decoder.sampleRate == 0) return -1.0;
  if (decoder.totalPCMFrameCount != DRMP3_UINT64_MAX) {
    return static_cast<double>(decoder.totalPCMFrameCount) /
           static_cast<double>(decoder.sampleRate);
  }
  return parseDurationFromXingVbri();
}

double MP3DecoderWrapper::parseDurationFromXingVbri() const {
  // Skip ID3v2 tag if present.
  size_t pos = 0;
  if (audioData.size() >= 10 && std::memcmp(audioData.data(), "ID3", 3) == 0) {
    const uint32_t tagSize = ((audioData[6] & 0x7f) << 21) |
                             ((audioData[7] & 0x7f) << 14) |
                             ((audioData[8] & 0x7f) << 7) |
                             (audioData[9] & 0x7f);
    pos = tagSize + 10;
    if (pos >= audioData.size()) return -1.0;
  }

  // Find first MP3 frame sync word.
  while (pos + 4 < audioData.size()) {
    if (audioData[pos] == 0xff && (audioData[pos + 1] & 0xe0) == 0xe0) {
      const uint8_t byte1 = audioData[pos + 1];
      const uint8_t version = (byte1 >> 3) & 0x3; // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
      const uint8_t layer = (byte1 >> 1) & 0x3;    // 1=Layer3, 2=Layer2, 3=Layer1
      const bool protection = (byte1 & 0x1) != 0;  // false = CRC present
      const uint8_t byte2 = audioData[pos + 2];
      const uint8_t sampleRateIndex = (byte2 >> 2) & 0x3;
      const uint8_t byte3 = audioData[pos + 3];
      const uint8_t channelMode = (byte3 >> 6) & 0x3; // 3=mono

      static const int kSampleRatesMpeg1[4] = {44100, 48000, 32000, 0};
      static const int kSampleRatesMpeg2[4] = {22050, 24000, 16000, 0};
      static const int kSampleRatesMpeg25[4] = {11025, 12000, 8000, 0};
      int sampleRate = 0;
      if (version == 3)
        sampleRate = kSampleRatesMpeg1[sampleRateIndex];
      else if (version == 2)
        sampleRate = kSampleRatesMpeg2[sampleRateIndex];
      else
        sampleRate = kSampleRatesMpeg25[sampleRateIndex];
      if (sampleRate == 0) return -1.0;

      int sideInfoSize = 0;
      if (layer == 1) { // Layer3
        if (version == 3) { // MPEG1
          sideInfoSize = (channelMode == 3) ? 17 : 32;
        } else { // MPEG2/2.5
          sideInfoSize = (channelMode == 3) ? 9 : 17;
        }
      }
      const int headerOffset = 4 + sideInfoSize + (protection ? 0 : 2);
      if (pos + headerOffset + 4 > audioData.size()) return -1.0;

      const size_t xingOffset = pos + headerOffset;
      // Check Xing/Info
      if (std::memcmp(audioData.data() + xingOffset, "Xing", 4) == 0 ||
          std::memcmp(audioData.data() + xingOffset, "Info", 4) == 0) {
        if (xingOffset + 16 > audioData.size()) return -1.0;
        const uint32_t flags = (audioData[xingOffset + 4] << 24) |
                               (audioData[xingOffset + 5] << 16) |
                               (audioData[xingOffset + 6] << 8) |
                               audioData[xingOffset + 7];
        if (flags & 0x1) {
          const uint32_t frames = (audioData[xingOffset + 8] << 24) |
                                  (audioData[xingOffset + 9] << 16) |
                                  (audioData[xingOffset + 10] << 8) |
                                  audioData[xingOffset + 11];
          int samplesPerFrame = 0;
          if (layer == 1) { // Layer3
            samplesPerFrame = (version == 3) ? 1152 : 576;
          } else if (layer == 2) { // Layer2
            samplesPerFrame = 1152;
          } else { // Layer1
            samplesPerFrame = 384;
          }
          if (frames > 0 && samplesPerFrame > 0) {
            return static_cast<double>(frames * samplesPerFrame) /
                   static_cast<double>(sampleRate);
          }
        }
      }

      // Check VBRI (commonly placed at the end of the side info).
      if (pos + headerOffset + 4 <= audioData.size()) {
        const size_t vbriOffset = pos + headerOffset;
        if (std::memcmp(audioData.data() + vbriOffset, "VBRI", 4) == 0) {
          if (vbriOffset + 18 > audioData.size()) return -1.0;
          const uint32_t frames = (audioData[vbriOffset + 14] << 24) |
                                  (audioData[vbriOffset + 15] << 16) |
                                  (audioData[vbriOffset + 16] << 8) |
                                  audioData[vbriOffset + 17];
          int samplesPerFrame = 0;
          if (layer == 1)
            samplesPerFrame = (version == 3) ? 1152 : 576;
          else if (layer == 2)
            samplesPerFrame = 1152;
          else
            samplesPerFrame = 384;
          if (frames > 0 && samplesPerFrame > 0) {
            return static_cast<double>(frames * samplesPerFrame) /
                   static_cast<double>(sampleRate);
          }
        }
      }
      return -1.0;
    }
    ++pos;
  }
  return -1.0;
}

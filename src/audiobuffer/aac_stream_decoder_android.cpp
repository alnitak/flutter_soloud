#if defined(__ANDROID__)

#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <deque>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>

#define LOG_TAG "flutter_soloud"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

// Standard AC-3 frame sizes in 16-bit words: [frmsizecod >> 1][fscod] (ATSC A/52 Table 5.18)
static const uint16_t kAc3FrameSizes[19][3] = {
    { 64,   69,   96 },
    { 80,   87,  120 },
    { 96,  104,  144 },
    { 112, 121,  168 },
    { 128, 139,  192 },
    { 160, 174,  240 },
    { 192, 208,  288 },
    { 224, 243,  336 },
    { 256, 278,  384 },
    { 320, 348,  480 },
    { 384, 417,  576 },
    { 448, 487,  672 },
    { 512, 557,  768 },
    { 640, 696,  960 },
    { 768, 835, 1152 },
    { 896, 975, 1344 },
    { 1024, 1114, 1536 },
    { 1152, 1253, 1728 },
    { 1280, 1392, 1920 }
};

static const int kAacSampleRates[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

class AndroidAACImpl : public AACDecoderWrapper::Impl {
public:
  explicit AndroidAACImpl(DetectedType format = DetectedType::BUFFER_AAC)
      : mFormat(format) {}

  ~AndroidAACImpl() override {
    cleanup();
  }

  bool initialize(int engineSamplerate, int engineChannels) override {
    cleanup();

    mTargetSampleRate = engineSamplerate;
    mTargetChannels = engineChannels;

    const char *mime = getMimeType();
    if (!mime) {
      LOGE("Unsupported audio format for Android streaming: %d", static_cast<int>(mFormat));
      return false;
    }

    // Verify the decoder codec is available on this Android device.
    AMediaCodec *testCodec = AMediaCodec_createDecoderByType(mime);
    if (!testCodec) {
      LOGE("AMediaCodec decoder not available on this device for MIME: %s", mime);
      return false;
    }
    AMediaCodec_delete(testCodec);

    return true;
  }

  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *samplerate,
         int *channels, size_t maxOutputSamples) override {
    std::vector<float> decodedData;
    if (maxOutputSamples > 0) {
      decodedData.reserve(maxOutputSamples);
    }

    // 1. Drain previously buffered remainder samples
    while (!mDecodedRemainder.empty() &&
           (maxOutputSamples == 0 || decodedData.size() < maxOutputSamples)) {
      decodedData.push_back(mDecodedRemainder.front());
      mDecodedRemainder.pop_front();
    }

    if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
      if (samplerate && mSampleRate > 0) *samplerate = mSampleRate;
      if (channels && mChannels > 0) *channels = mChannels;
      return {std::move(decodedData), DecoderError::NoError};
    }

    // 2. Append incoming raw bytes to internal stream buffer
    if (!buffer.empty()) {
      if (mStreamReadOffset >= mStreamBytes.size()) {
        mStreamBytes.clear();
        mStreamReadOffset = 0;
      } else if (mStreamReadOffset >= 128 * 1024) {
        mStreamBytes.erase(mStreamBytes.begin(), mStreamBytes.begin() + mStreamReadOffset);
        mStreamReadOffset = 0;
      }
      mStreamBytes.insert(mStreamBytes.end(), buffer.begin(), buffer.end());
      buffer.clear();
    }

    // 3. Process and queue stream frames into AMediaCodec while actively draining output
    if (mFormat == DetectedType::BUFFER_AAC) {
      DecoderError err = queueAacFrames(decodedData, maxOutputSamples);
      if (err != DecoderError::NoError) {
        return {std::move(decodedData), err};
      }
    } else if (mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3) {
      DecoderError err = queueAc3Frames(decodedData, maxOutputSamples);
      if (err != DecoderError::NoError) {
        return {std::move(decodedData), err};
      }
    }

    // 4. Drain any output buffers that became available with 0 timeout (non-blocking)
    // so decoded audio is returned immediately without stalling the caller thread.
    drainOutputBuffers(decodedData, maxOutputSamples, 0);

    // 5. Handle end-of-stream: queue EOS buffer and drain everything until EOS reached
    if (mDataEnded && mCodecInitialized && mCodec) {
      const size_t available = (mStreamBytes.size() > mStreamReadOffset)
                                   ? (mStreamBytes.size() - mStreamReadOffset)
                                   : 0;
      // Only queue EOS once all complete frames in the stream buffer have been submitted!
      if (available < 7) {
        mStreamBytes.clear();
        mStreamReadOffset = 0;

        if (!mEosQueued) {
          for (int retry = 0; retry < 10 && !mEosQueued; ++retry) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(mCodec, 200);
            if (inIdx >= 0) {
              AMediaCodec_queueInputBuffer(mCodec, inIdx, 0, 0, mPresentationTimeUs,
                                           AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
              mEosQueued = true;
              break;
            }
            drainOutputBuffers(decodedData, maxOutputSamples, 200);
          }
        }
      }

      if (mEosQueued && !mEosReached) {
        int idleCount = 0;
        while (!mEosReached && idleCount < 15) {
          bool gotOutput = drainOutputBuffers(decodedData, maxOutputSamples, 200);
          if (gotOutput) {
            idleCount = 0;
          } else {
            idleCount++;
          }
          if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
            break;
          }
        }
      }
    }

    if (samplerate && mSampleRate > 0) *samplerate = mSampleRate;
    if (channels && mChannels > 0) *channels = mChannels;

    return {std::move(decodedData), DecoderError::NoError};
  }

  void setDataEnded() override {
    mDataEnded = true;
  }

  bool hasPendingData() const override {
    const size_t available = (mStreamBytes.size() > mStreamReadOffset)
                                 ? (mStreamBytes.size() - mStreamReadOffset)
                                 : 0;
    return available >= 7 ||
           !mDecodedRemainder.empty() ||
           (mCodecInitialized && mCodec != nullptr && !mEosReached);
  }

private:
  const char *getMimeType() const {
    switch (mFormat) {
    case DetectedType::BUFFER_AAC:
      return "audio/mp4a-latm";
    case DetectedType::BUFFER_AC3:
      return "audio/ac3";
    case DetectedType::BUFFER_EAC3:
      return "audio/eac3";
    default:
      return nullptr;
    }
  }

  bool drainOutputBuffers(std::vector<float> &decodedData, size_t maxOutputSamples, int64_t timeoutUs) {
    if (!mCodecInitialized || !mCodec) return false;

    bool gotAnyOutput = false;
    AMediaCodecBufferInfo info;
    while (true) {
      ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(mCodec, &info, timeoutUs);
      if (outIdx >= 0) {
        gotAnyOutput = true;
        size_t outBufSize = 0;
        uint8_t *outBuf = AMediaCodec_getOutputBuffer(mCodec, outIdx, &outBufSize);
        if (outBuf && info.size > 0) {
          size_t availableSpace = (maxOutputSamples > 0 && decodedData.size() < maxOutputSamples)
                                      ? (maxOutputSamples - decodedData.size())
                                      : static_cast<size_t>(-1);
          if (mIsFloatOutput) {
            const float *samplesFloat = reinterpret_cast<const float *>(outBuf + info.offset);
            size_t count = info.size / sizeof(float);
            size_t toAppend = std::min(count, availableSpace);
            decodedData.insert(decodedData.end(), samplesFloat, samplesFloat + toAppend);
            for (size_t s = toAppend; s < count; ++s) {
              mDecodedRemainder.push_back(samplesFloat[s]);
            }
          } else {
            const int16_t *samples16 = reinterpret_cast<const int16_t *>(outBuf + info.offset);
            size_t count = info.size / sizeof(int16_t);
            size_t toAppend = std::min(count, availableSpace);
            decodedData.reserve(decodedData.size() + toAppend);
            for (size_t s = 0; s < toAppend; ++s) {
              decodedData.push_back(samples16[s] / 32768.0f);
            }
            for (size_t s = toAppend; s < count; ++s) {
              mDecodedRemainder.push_back(samples16[s] / 32768.0f);
            }
          }
        }

        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
          mEosReached = true;
        }

        AMediaCodec_releaseOutputBuffer(mCodec, outIdx, false);
        // After the first buffer, make subsequent dequeues in this pass non-blocking
        timeoutUs = 0;

        if (mEosReached) {
          break;
        }
        if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
          break;
        }
      } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat *outFmt = AMediaCodec_getOutputFormat(mCodec);
        int32_t sr = 0, ch = 0, encoding = 0;
        if (AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr) && sr > 0) {
          mSampleRate = sr;
        }
        if (AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch) && ch > 0) {
          mChannels = ch;
        }
        if (AMediaFormat_getInt32(outFmt, "pcm-encoding", &encoding)) {
          mIsFloatOutput = (encoding == 4);
        }
        AMediaFormat_delete(outFmt);
        gotAnyOutput = true;
      } else {
        break; // AMEDIACODEC_INFO_TRY_AGAIN_LATER
      }
    }
    return gotAnyOutput;
  }

  DecoderError queueAacFrames(std::vector<float> &decodedData, size_t maxOutputSamples) {
    int retryCount = 0;
    while (true) {
      if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
        break;
      }

      const size_t available = mStreamBytes.size() - mStreamReadOffset;
      if (available < 7) {
        break;
      }
      const uint8_t *streamData = mStreamBytes.data() + mStreamReadOffset;

      // Find valid ADTS syncword 0xFFF with verified header fields and frame chaining
      size_t syncIdx = 0;
      bool found = false;
      while (syncIdx + 7 <= available) {
        if (streamData[syncIdx] == 0xFF && (streamData[syncIdx + 1] & 0xF6) == 0xF0) {
          uint8_t srIdx = (streamData[syncIdx + 2] >> 2) & 0x0F;
          uint8_t chCfg = ((streamData[syncIdx + 2] & 0x01) << 2) | ((streamData[syncIdx + 3] >> 6) & 0x03);
          size_t frameLen = ((streamData[syncIdx + 3] & 0x03) << 11) |
                            (streamData[syncIdx + 4] << 3) |
                            ((streamData[syncIdx + 5] >> 5) & 0x07);

          if (srIdx < 13 && chCfg >= 1 && chCfg <= 7 && frameLen >= 7 && frameLen <= 8192) {
            // If the buffer contains enough bytes to see the next frame start, verify its syncword
            if (syncIdx + frameLen + 2 <= available) {
              if (streamData[syncIdx + frameLen] == 0xFF &&
                  (streamData[syncIdx + frameLen + 1] & 0xF6) == 0xF0) {
                found = true;
                break;
              } else {
                // False syncword inside payload; continue searching
                syncIdx++;
                continue;
              }
            } else {
              // Frame extends to or beyond the current buffer; accept as valid
              found = true;
              break;
            }
          }
        }
        syncIdx++;
      }

      if (!found) {
        // Discard scanned bytes, keeping at most 6 trailing bytes for syncword spanning chunks
        if (available > 6) {
          mStreamReadOffset += (available - 6);
        }
        break;
      }

      if (syncIdx > 0) {
        mStreamReadOffset += syncIdx;
      }

      const size_t currentAvailable = mStreamBytes.size() - mStreamReadOffset;
      if (currentAvailable < 7) {
        break; // Wait for more data
      }

      const uint8_t *frameData = mStreamBytes.data() + mStreamReadOffset;
      size_t frameLen = ((frameData[3] & 0x03) << 11) |
                        (frameData[4] << 3) |
                        ((frameData[5] >> 5) & 0x07);
      if (currentAvailable < frameLen) {
        break; // Frame incomplete, wait for remaining bytes
      }

      // Initialize AMediaCodec on the first valid ADTS frame
      if (!mCodecInitialized) {
        uint8_t profile = (frameData[2] >> 6) & 0x03;
        uint8_t srIdx = (frameData[2] >> 2) & 0x0F;
        uint8_t chCfg = ((frameData[2] & 0x01) << 2) | ((frameData[3] >> 6) & 0x03);

        mSampleRate = (srIdx < 13) ? kAacSampleRates[srIdx] : 44100;
        mChannels = (chCfg >= 1 && chCfg <= 7) ? (chCfg == 7 ? 8 : chCfg) : 2;

        mCodec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
        if (!mCodec) {
          LOGE("Failed to create AMediaCodec for audio/mp4a-latm");
          return DecoderError::FailedToCreateDecoder;
        }

        AMediaFormat *fmt = AMediaFormat_new();
        AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, mSampleRate);
        AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, mChannels);

        // 2-byte AudioSpecificConfig (csd-0) for raw AAC frame decoding
        uint8_t aot = profile + 1; // AAC-LC is AOT 2
        uint8_t csd[2];
        csd[0] = (aot << 3) | ((srIdx >> 1) & 0x07);
        csd[1] = ((srIdx & 0x01) << 7) | ((chCfg & 0x0F) << 3);
        AMediaFormat_setBuffer(fmt, "csd-0", csd, sizeof(csd));

        media_status_t status = AMediaCodec_configure(mCodec, fmt, nullptr, nullptr, 0);
        AMediaFormat_delete(fmt);
        if (status != AMEDIA_OK) {
          LOGE("AMediaCodec_configure failed for audio/mp4a-latm (status: %d)", status);
          AMediaCodec_delete(mCodec);
          mCodec = nullptr;
          return DecoderError::FailedToCreateDecoder;
        }

        status = AMediaCodec_start(mCodec);
        if (status != AMEDIA_OK) {
          LOGE("AMediaCodec_start failed for audio/mp4a-latm (status: %d)", status);
          AMediaCodec_delete(mCodec);
          mCodec = nullptr;
          return DecoderError::FailedToCreateDecoder;
        }
        mCodecInitialized = true;
      }

      // Queue raw AAC frame into codec (strip ADTS header)
      size_t headerSize = (frameData[1] & 0x01) ? 7 : 9;
      if (frameLen <= headerSize) {
        mStreamReadOffset += frameLen;
        continue;
      }
      size_t payloadSize = frameLen - headerSize;

      // Try to acquire an input buffer without blocking
      ssize_t inIdx = AMediaCodec_dequeueInputBuffer(mCodec, 0);
      if (inIdx < 0) {
        // Input buffers full; drain output to free an input slot.
        // A brief 100-200us timeout allows the codec to complete a frame and return PCM.
        int64_t drainTimeout = mDataEnded ? 200 : 100;
        drainOutputBuffers(decodedData, maxOutputSamples, drainTimeout);
        inIdx = AMediaCodec_dequeueInputBuffer(mCodec, mDataEnded ? 200 : 0);
      }

      if (inIdx >= 0) {
        size_t inBufSize = 0;
        uint8_t *inBuf = AMediaCodec_getInputBuffer(mCodec, inIdx, &inBufSize);
        if (inBuf && inBufSize >= payloadSize) {
          std::memcpy(inBuf, frameData + headerSize, payloadSize);
          AMediaCodec_queueInputBuffer(mCodec, inIdx, 0, payloadSize, mPresentationTimeUs, 0);
          mPresentationTimeUs += (1024ULL * 1000000ULL) / (mSampleRate > 0 ? mSampleRate : 44100);
          mStreamReadOffset += frameLen;
          retryCount = 0;
          // Drain any output buffers that become available (non-blocking)
          drainOutputBuffers(decodedData, maxOutputSamples, 0);
        } else {
          break;
        }
      } else {
        if (!mDataEnded) {
          // Input buffers full; leave remaining frames in mStreamBytes for the next pass
          break;
        }
        retryCount++;
        if (retryCount >= 20) {
          break;
        }
      }
    }

    // Compact buffer if consumed offset is large
    if (mStreamReadOffset > 0) {
      if (mStreamReadOffset >= mStreamBytes.size()) {
        mStreamBytes.clear();
        mStreamReadOffset = 0;
      } else if (mStreamReadOffset >= 128 * 1024) {
        mStreamBytes.erase(mStreamBytes.begin(), mStreamBytes.begin() + mStreamReadOffset);
        mStreamReadOffset = 0;
      }
    }
    return DecoderError::NoError;
  }

  DecoderError queueAc3Frames(std::vector<float> &decodedData, size_t maxOutputSamples) {
    int retryCount = 0;
    while (true) {
      if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
        break;
      }

      const size_t available = mStreamBytes.size() - mStreamReadOffset;
      if (available < 6) break;
      const uint8_t *streamData = mStreamBytes.data() + mStreamReadOffset;

      int sync = NativeAudioDecoder::findAc3Syncword(streamData, available);
      if (sync < 0) {
        if (available > 5) {
          mStreamReadOffset += (available - 5);
        }
        break;
      }
      if (sync > 0) {
        mStreamReadOffset += sync;
      }

      const size_t currentAvailable = mStreamBytes.size() - mStreamReadOffset;
      if (currentAvailable < 6) break;
      const uint8_t *frameData = mStreamBytes.data() + mStreamReadOffset;

      size_t frameLen = 0;
      int sampleRate = 48000;

      if (mFormat == DetectedType::BUFFER_EAC3) {
        size_t frmsiz = ((frameData[2] & 0x07) << 8) | frameData[3];
        frameLen = (frmsiz + 1) * 2;
        uint8_t fscod = (frameData[4] >> 6) & 0x03;
        if (fscod == 0) sampleRate = 48000;
        else if (fscod == 1) sampleRate = 44100;
        else if (fscod == 2) sampleRate = 32000;
      } else {
        // AC-3
        uint8_t fscod = (frameData[4] >> 6) & 0x03;
        uint8_t frmsizecod = frameData[4] & 0x3F;
        if (fscod < 3 && (frmsizecod >> 1) < 19) {
          frameLen = kAc3FrameSizes[frmsizecod >> 1][fscod] * 2;
          if (fscod == 1 && (frmsizecod & 1)) {
            frameLen += 2;
          }
        }
        if (fscod == 0) sampleRate = 48000;
        else if (fscod == 1) sampleRate = 44100;
        else if (fscod == 2) sampleRate = 32000;
      }

      // Fallback: search for next syncword
      if (frameLen == 0 || frameLen > 4096) {
        int nextSync = NativeAudioDecoder::findAc3Syncword(frameData + 2, currentAvailable - 2);
        if (nextSync >= 0) {
          frameLen = nextSync + 2;
        } else if (mDataEnded) {
          frameLen = currentAvailable;
        } else {
          break; // wait for more data
        }
      }

      // Verify next syncword if available
      if (frameLen + 2 <= currentAvailable) {
        int nextSync = NativeAudioDecoder::findAc3Syncword(frameData + frameLen, 2);
        if (nextSync != 0) {
          // False syncword; skip 2 bytes and continue
          mStreamReadOffset += 2;
          continue;
        }
      }

      if (currentAvailable < frameLen) {
        break; // Incomplete frame
      }

      // Initialize AMediaCodec on the first valid frame
      if (!mCodecInitialized) {
        const char *mime = (mFormat == DetectedType::BUFFER_AC3) ? "audio/ac3" : "audio/eac3";
        mCodec = AMediaCodec_createDecoderByType(mime);
        if (!mCodec) {
          LOGE("AMediaCodec decoder not available on this device for %s", mime);
          return DecoderError::FormatNotSupported;
        }

        mSampleRate = sampleRate;
        mChannels = 2; // Decoder updates on format change

        AMediaFormat *fmt = AMediaFormat_new();
        AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, mime);
        AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, mSampleRate);
        AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, mChannels);

        media_status_t status = AMediaCodec_configure(mCodec, fmt, nullptr, nullptr, 0);
        AMediaFormat_delete(fmt);
        if (status != AMEDIA_OK) {
          LOGE("AMediaCodec_configure failed for %s (status: %d)", mime, status);
          AMediaCodec_delete(mCodec);
          mCodec = nullptr;
          return DecoderError::FailedToCreateDecoder;
        }

        status = AMediaCodec_start(mCodec);
        if (status != AMEDIA_OK) {
          LOGE("AMediaCodec_start failed for %s (status: %d)", mime, status);
          AMediaCodec_delete(mCodec);
          mCodec = nullptr;
          return DecoderError::FailedToCreateDecoder;
        }
        mCodecInitialized = true;
      }

      // Queue sync frame into codec
      ssize_t inIdx = AMediaCodec_dequeueInputBuffer(mCodec, 0);
      if (inIdx < 0) {
        int64_t drainTimeout = mDataEnded ? 200 : 100;
        drainOutputBuffers(decodedData, maxOutputSamples, drainTimeout);
        inIdx = AMediaCodec_dequeueInputBuffer(mCodec, mDataEnded ? 200 : 0);
      }

      if (inIdx >= 0) {
        size_t inBufSize = 0;
        uint8_t *inBuf = AMediaCodec_getInputBuffer(mCodec, inIdx, &inBufSize);
        if (inBuf && inBufSize >= frameLen) {
          std::memcpy(inBuf, frameData, frameLen);
          AMediaCodec_queueInputBuffer(mCodec, inIdx, 0, frameLen, mPresentationTimeUs, 0);
          mPresentationTimeUs += (1536ULL * 1000000ULL) / (mSampleRate > 0 ? mSampleRate : 48000);
          mStreamReadOffset += frameLen;
          retryCount = 0;
          // Drain any output buffers that become available (non-blocking)
          drainOutputBuffers(decodedData, maxOutputSamples, 0);
        } else {
          break;
        }
      } else {
        if (!mDataEnded) {
          // Input buffers full; leave remaining frames in mStreamBytes for the next pass
          break;
        }
        retryCount++;
        if (retryCount >= 20) {
          break;
        }
      }
    }

    // Compact buffer if consumed offset is large
    if (mStreamReadOffset > 0) {
      if (mStreamReadOffset >= mStreamBytes.size()) {
        mStreamBytes.clear();
        mStreamReadOffset = 0;
      } else if (mStreamReadOffset >= 128 * 1024) {
        mStreamBytes.erase(mStreamBytes.begin(), mStreamBytes.begin() + mStreamReadOffset);
        mStreamReadOffset = 0;
      }
    }
    return DecoderError::NoError;
  }

  void cleanup() {
    if (mCodec) {
      AMediaCodec_stop(mCodec);
      AMediaCodec_delete(mCodec);
      mCodec = nullptr;
    }
    mCodecInitialized = false;
    mDataEnded = false;
    mEosQueued = false;
    mEosReached = false;
    mIsFloatOutput = false;
    mSampleRate = 0;
    mChannels = 0;
    mPresentationTimeUs = 0;
    mStreamBytes.clear();
    mStreamReadOffset = 0;
    mDecodedRemainder.clear();
  }

  DetectedType mFormat;
  int mTargetSampleRate = 0;
  int mTargetChannels = 0;
  int mSampleRate = 0;
  int mChannels = 0;
  bool mIsFloatOutput = false;
  bool mCodecInitialized = false;
  bool mDataEnded = false;
  bool mEosQueued = false;
  bool mEosReached = false;
  int64_t mPresentationTimeUs = 0;
  AMediaCodec *mCodec = nullptr;
  std::vector<uint8_t> mStreamBytes;
  size_t mStreamReadOffset = 0;
  std::deque<float> mDecodedRemainder;
};

} // namespace

std::unique_ptr<AACDecoderWrapper::Impl> createAndroidAACDecoderImpl(DetectedType format) {
  return std::make_unique<AndroidAACImpl>(format);
}

#endif // defined(__ANDROID__)

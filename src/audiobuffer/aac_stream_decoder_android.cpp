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

    // Pre-create the codec with default parameters to reduce first-decode latency
    // It will be reconfigured with actual stream parameters when the first frame arrives
    mCodec = AMediaCodec_createDecoderByType(mime);
    if (!mCodec) {
      LOGE("Failed to pre-create AMediaCodec for %s", mime);
      return false;
    }
    mCodecPreCreated = true;

    return true;
  }

  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *samplerate,
         int *channels, size_t maxOutputSamples) override {
    std::vector<float> decodedData;
    // MediaCodec errors leave the codec unusable; keep reporting them.
    if (mCodecFailed) {
      return {std::move(decodedData), DecoderError::CodecError};
    }
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

    // 2a. Prebuffering strategy: accumulate initial frames before starting codec
    // This reduces first-frame latency by ensuring codec has work queued immediately
    if (!mCodecInitialized && !mPrebufferComplete) {
      const size_t available = mStreamBytes.size() - mStreamReadOffset;
      const size_t targetPrebuffer = mFormat == DetectedType::BUFFER_AAC ? 4096 : 8192;

      if (available < targetPrebuffer && !mDataEnded) {
        // Wait for more data before initializing codec
        if (samplerate && mSampleRate > 0) *samplerate = mSampleRate;
        if (channels && mChannels > 0) *channels = mChannels;
        return {std::move(decodedData), DecoderError::NoError};
      }
      mPrebufferComplete = true;
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
      // Only queue EOS once all complete frames in the stream buffer have been
      // submitted. A trailing frame cut short by the end of data can never be
      // completed, so it is dropped rather than holding EOS back forever.
      if (available < 7 || mTrailingFrameIncomplete) {
        mStreamBytes.clear();
        mStreamReadOffset = 0;

        if (!mEosQueued) {
          for (int retry = 0; retry < 15 && !mEosQueued && !mCodecFailed; ++retry) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(mCodec, 2000);
            if (inIdx >= 0) {
              media_status_t status = AMediaCodec_queueInputBuffer(
                  mCodec, inIdx, 0, 0, mPresentationTimeUs, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
              if (status != AMEDIA_OK) {
                fail("AMediaCodec_queueInputBuffer (EOS) failed", status);
                break;
              }
              mEosQueued = true;
              break;
            }
            if (inIdx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
              fail("AMediaCodec_dequeueInputBuffer failed", inIdx);
              break;
            }
            drainOutputBuffers(decodedData, maxOutputSamples, 2000);
          }
        }
      }

      if (mEosQueued && !mEosReached) {
        int idleCount = 0;
        while (!mEosReached && !mCodecFailed && idleCount < 20) {
          bool gotOutput = drainOutputBuffers(decodedData, maxOutputSamples, 2000);
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

    if (mCodecFailed) {
      return {std::move(decodedData), DecoderError::CodecError};
    }
    return {std::move(decodedData), DecoderError::NoError};
  }

  void setDataEnded() override {
    mDataEnded = true;
  }

  bool hasPendingData() const override {
    if (mCodecFailed) return false;
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

  // PCM encodings reported by MediaCodec ("pcm-encoding", AudioFormat constants).
  static constexpr int32_t kPcmEncoding16Bit = 2;
  static constexpr int32_t kPcmEncodingFloat = 4;
  static constexpr int kMaxOutputChannels = 8;
  static constexpr int kMaxOutputSampleRate = 768000;
  // Bound on dequeues per drain call so a codec repeatedly reporting format or
  // buffer changes cannot spin the caller forever.
  static constexpr int kMaxDequeuesPerDrain = 64;

  void fail(const char *what, long code) {
    LOGE("%s (%ld)", what, code);
    mCodecFailed = true;
  }

  /// Appends one output buffer's PCM, validated against the negotiated format.
  bool appendOutputPcm(const uint8_t *src, size_t bytes, std::vector<float> &decodedData,
                       size_t maxOutputSamples) {
    if (mChannels < 1 || mChannels > kMaxOutputChannels) {
      fail("Unsupported decoder output channel count", mChannels);
      return false;
    }
    const size_t bytesPerSample = mIsFloatOutput ? sizeof(float) : sizeof(int16_t);
    if (bytes % (bytesPerSample * static_cast<size_t>(mChannels)) != 0) {
      fail("Decoder output contains a partial PCM frame", static_cast<long>(bytes));
      return false;
    }
    const size_t count = bytes / bytesPerSample;
    const size_t availableSpace = maxOutputSamples == 0
                                      ? count
                                      : (decodedData.size() < maxOutputSamples
                                             ? maxOutputSamples - decodedData.size()
                                             : 0);
    const size_t toAppend = std::min(count, availableSpace);
    decodedData.reserve(decodedData.size() + toAppend);
    for (size_t s = 0; s < count; ++s) {
      float value;
      if (mIsFloatOutput) {
        std::memcpy(&value, src + s * sizeof(float), sizeof(float));
      } else {
        int16_t sample;
        std::memcpy(&sample, src + s * sizeof(int16_t), sizeof(int16_t));
        value = sample / 32768.0f;
      }
      if (s < toAppend) {
        decodedData.push_back(value);
      } else {
        mDecodedRemainder.push_back(value);
      }
    }
    return true;
  }

  bool applyOutputFormat() {
    AMediaFormat *outFmt = AMediaCodec_getOutputFormat(mCodec);
    if (!outFmt) {
      fail("AMediaCodec_getOutputFormat returned null", 0);
      return false;
    }
    int32_t sr = 0, ch = 0;
    int32_t encoding = kPcmEncoding16Bit; // MediaCodec default when the key is absent
    AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
    AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
    AMediaFormat_getInt32(outFmt, "pcm-encoding", &encoding);
    AMediaFormat_delete(outFmt);

    if (sr < 1 || sr > kMaxOutputSampleRate) {
      fail("Unsupported decoder output sample rate", sr);
      return false;
    }
    if (ch < 1 || ch > kMaxOutputChannels) {
      fail("Unsupported decoder output channel count", ch);
      return false;
    }
    if (encoding != kPcmEncoding16Bit && encoding != kPcmEncodingFloat) {
      fail("Unsupported decoder output PCM encoding", encoding);
      return false;
    }
    mSampleRate = sr;
    mChannels = ch;
    mIsFloatOutput = (encoding == kPcmEncodingFloat);
    return true;
  }

  bool drainOutputBuffers(std::vector<float> &decodedData, size_t maxOutputSamples, int64_t timeoutUs) {
    if (!mCodecInitialized || !mCodec || mCodecFailed) return false;

    bool gotAnyOutput = false;
    for (int pass = 0; pass < kMaxDequeuesPerDrain; ++pass) {
      AMediaCodecBufferInfo info{};
      ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(mCodec, &info, timeoutUs);
      // After the first dequeue, make subsequent ones in this pass non-blocking
      timeoutUs = 0;
      if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) break;
      if (outIdx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
      if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        if (!applyOutputFormat()) break;
        gotAnyOutput = true;
        continue;
      }
      if (outIdx < 0) {
        fail("AMediaCodec_dequeueOutputBuffer failed", outIdx);
        break;
      }

      gotAnyOutput = true;
      if (info.size > 0) {
        size_t outBufSize = 0;
        uint8_t *outBuf = AMediaCodec_getOutputBuffer(mCodec, outIdx, &outBufSize);
        if (!outBuf) {
          fail("AMediaCodec_getOutputBuffer returned null", outIdx);
        } else if (info.offset < 0 || static_cast<size_t>(info.offset) > outBufSize ||
                   static_cast<size_t>(info.size) > outBufSize - static_cast<size_t>(info.offset)) {
          fail("Decoder output buffer bounds are invalid", info.offset);
        } else {
          appendOutputPcm(outBuf + info.offset, static_cast<size_t>(info.size), decodedData,
                          maxOutputSamples);
        }
      } else if (info.size < 0) {
        fail("Decoder output buffer size is negative", info.size);
      }

      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        mEosReached = true;
      }

      media_status_t status = AMediaCodec_releaseOutputBuffer(mCodec, outIdx, false);
      if (status != AMEDIA_OK && !mCodecFailed) {
        fail("AMediaCodec_releaseOutputBuffer failed", status);
      }
      if (mCodecFailed || mEosReached) {
        break;
      }
      if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
        break;
      }
    }
    return gotAnyOutput;
  }

  enum class QueueResult { Queued, Busy, Failed };

  /// Copies one compressed frame into a codec input buffer. Busy means no input
  /// buffer is free yet; on Busy or Failed the frame is not consumed.
  QueueResult queueInputFrame(const uint8_t *data, size_t size, std::vector<float> &decodedData,
                              size_t maxOutputSamples) {
    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(mCodec, 0);
    if (inIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      // Input buffers full; drain output to free an input slot.
      // A brief 100-2000us timeout allows the codec to complete a frame and return PCM.
      int64_t drainTimeout = mDataEnded ? 2000 : 100;
      drainOutputBuffers(decodedData, maxOutputSamples, drainTimeout);
      if (mCodecFailed) return QueueResult::Failed;
      inIdx = AMediaCodec_dequeueInputBuffer(mCodec, mDataEnded ? 2000 : 0);
      if (inIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return QueueResult::Busy;
    }
    if (inIdx < 0) {
      fail("AMediaCodec_dequeueInputBuffer failed", inIdx);
      return QueueResult::Failed;
    }

    size_t inBufSize = 0;
    uint8_t *inBuf = AMediaCodec_getInputBuffer(mCodec, static_cast<size_t>(inIdx), &inBufSize);
    if (!inBuf) {
      fail("AMediaCodec_getInputBuffer returned null", inIdx);
      return QueueResult::Failed;
    }
    if (inBufSize < size) {
      fail("Compressed frame is larger than the codec input buffer", static_cast<long>(size));
      return QueueResult::Failed;
    }
    std::memcpy(inBuf, data, size);
    media_status_t status = AMediaCodec_queueInputBuffer(mCodec, static_cast<size_t>(inIdx), 0, size,
                                                         mPresentationTimeUs, 0);
    if (status != AMEDIA_OK) {
      fail("AMediaCodec_queueInputBuffer failed", status);
      return QueueResult::Failed;
    }
    return QueueResult::Queued;
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

      // Simplified ADTS syncword validation: verify 0xFFF syncword and basic header validity
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
            // Only verify next syncword if we have enough data AND we're not yet initialized
            // Once initialized, trust the frame length to reduce overhead
            if (!mCodecInitialized && syncIdx + frameLen + 2 <= available) {
              if (streamData[syncIdx + frameLen] == 0xFF &&
                  (streamData[syncIdx + frameLen + 1] & 0xF6) == 0xF0) {
                found = true;
                break;
              } else {
                syncIdx++;
                continue;
              }
            } else {
              // Accept frame without next-frame verification
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
        mTrailingFrameIncomplete = mDataEnded;
        break; // Frame incomplete, wait for remaining bytes
      }

      // Initialize AMediaCodec on the first valid ADTS frame
      if (!mCodecInitialized) {
        uint8_t profile = (frameData[2] >> 6) & 0x03;
        uint8_t srIdx = (frameData[2] >> 2) & 0x0F;
        uint8_t chCfg = ((frameData[2] & 0x01) << 2) | ((frameData[3] >> 6) & 0x03);

        mSampleRate = (srIdx < 13) ? kAacSampleRates[srIdx] : 44100;
        mChannels = (chCfg >= 1 && chCfg <= 7) ? (chCfg == 7 ? 8 : chCfg) : 2;

        // If codec wasn't pre-created, create it now
        if (!mCodec) {
          mCodec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
          if (!mCodec) {
            LOGE("Failed to create AMediaCodec for audio/mp4a-latm");
            return DecoderError::FailedToCreateDecoder;
          }
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
        mCodecPreCreated = false; // Now configured
      }

      // Queue raw AAC frame into codec (strip ADTS header)
      size_t headerSize = (frameData[1] & 0x01) ? 7 : 9;
      if (frameLen <= headerSize) {
        mStreamReadOffset += frameLen;
        continue;
      }
      size_t payloadSize = frameLen - headerSize;

      const QueueResult queued =
          queueInputFrame(frameData + headerSize, payloadSize, decodedData, maxOutputSamples);
      if (queued == QueueResult::Failed) {
        return DecoderError::CodecError;
      }
      if (queued == QueueResult::Queued) {
        mPresentationTimeUs += (1024ULL * 1000000ULL) / (mSampleRate > 0 ? mSampleRate : 44100);
        mStreamReadOffset += frameLen;
        retryCount = 0;
        // Drain any output buffers that become available (non-blocking)
        drainOutputBuffers(decodedData, maxOutputSamples, 0);
        if (mCodecFailed) {
          return DecoderError::CodecError;
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

      // Only verify next syncword if we're not yet initialized
      // Once initialized, trust the frame length to reduce overhead
      if (!mCodecInitialized && frameLen + 2 <= currentAvailable) {
        if (!NativeAudioDecoder::isAc3OrEac3(frameData + frameLen, currentAvailable - frameLen)) {
          // False syncword; skip 2 bytes and continue
          mStreamReadOffset += 2;
          continue;
        }
      }

      if (currentAvailable < frameLen) {
        mTrailingFrameIncomplete = mDataEnded;
        break; // Incomplete frame
      }

      // Initialize AMediaCodec on the first valid frame
      if (!mCodecInitialized) {
        const char *mime = (mFormat == DetectedType::BUFFER_AC3) ? "audio/ac3" : "audio/eac3";

        // If codec wasn't pre-created, create it now
        if (!mCodec) {
          mCodec = AMediaCodec_createDecoderByType(mime);
          if (!mCodec) {
            LOGE("AMediaCodec decoder not available on this device for %s", mime);
            return DecoderError::FormatNotSupported;
          }
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
        mCodecPreCreated = false; // Now configured
      }

      // Queue sync frame into codec
      const QueueResult queued = queueInputFrame(frameData, frameLen, decodedData, maxOutputSamples);
      if (queued == QueueResult::Failed) {
        return DecoderError::CodecError;
      }
      if (queued == QueueResult::Queued) {
        mPresentationTimeUs += (1536ULL * 1000000ULL) / (mSampleRate > 0 ? mSampleRate : 48000);
        mStreamReadOffset += frameLen;
        retryCount = 0;
        // Drain any output buffers that become available (non-blocking)
        drainOutputBuffers(decodedData, maxOutputSamples, 0);
        if (mCodecFailed) {
          return DecoderError::CodecError;
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
      if (mCodecInitialized) {
        AMediaCodec_stop(mCodec);
      }
      AMediaCodec_delete(mCodec);
      mCodec = nullptr;
    }
    mCodecInitialized = false;
    mCodecPreCreated = false;
    mPrebufferComplete = false;
    mDataEnded = false;
    mEosQueued = false;
    mEosReached = false;
    mTrailingFrameIncomplete = false;
    mCodecFailed = false;
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
  bool mCodecPreCreated = false;
  bool mPrebufferComplete = false;
  bool mDataEnded = false;
  bool mEosQueued = false;
  bool mEosReached = false;
  // Data has ended and the remaining bytes are a frame that cannot complete.
  bool mTrailingFrameIncomplete = false;
  // A MediaCodec call failed or returned malformed output; decoding stops.
  bool mCodecFailed = false;
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

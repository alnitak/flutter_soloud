#if defined(__APPLE__)

#import <AudioToolbox/AudioToolbox.h>
#import <CoreFoundation/CoreFoundation.h>
#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <deque>
#include <vector>
#include <iostream>

namespace {

struct QueuedPacket {
  std::vector<uint8_t> data;
  AudioStreamPacketDescription desc;
};

class AppleAACImpl : public AACDecoderWrapper::Impl {
public:
  explicit AppleAACImpl(DetectedType format = DetectedType::BUFFER_AAC)
      : mFormat(format) {}

  ~AppleAACImpl() override {
    cleanup();
  }

  bool initialize(int engineSamplerate, int engineChannels) override {
    cleanup();

    mTargetSampleRate = engineSamplerate;
    mTargetChannels = engineChannels;

    AudioFileTypeID hint = kAudioFileAAC_ADTSType;
    if (mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3) {
      hint = kAudioFileAC3Type;
    }

    OSStatus status = AudioFileStreamOpen(
        this,
        PropertyListener,
        PacketsListener,
        hint,
        &mAudioFileStream
    );

    if (status != noErr) {
      std::cerr << "AudioFileStreamOpen failed with status: " << status << std::endl;
      return false;
    }

    return true;
  }

  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *samplerate,
         int *channels, size_t maxOutputSamples) override {
    // 1. Parse incoming raw stream bytes first so buffer is never stranded
    if (!buffer.empty() && mAudioFileStream) {
      // If the parser has not yet initialized the format, ensure the buffer
      // is aligned to the first frame syncword to prevent parse errors.
      if (!mFormatInitialized) {
        if (mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3) {
          int syncIdx = NativeAudioDecoder::findAc3Syncword(buffer.data(), buffer.size());
          if (syncIdx > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + syncIdx);
          }
        } else if (mFormat == DetectedType::BUFFER_AAC) {
          if (!NativeAudioDecoder::isAacAdts(buffer.data(), buffer.size())) {
            for (size_t i = 0; i + 2 <= buffer.size(); ++i) {
              if (NativeAudioDecoder::isAacAdts(buffer.data() + i, buffer.size() - i)) {
                buffer.erase(buffer.begin(), buffer.begin() + i);
                break;
              }
            }
          }
        }
      }

      OSStatus parseStatus = AudioFileStreamParseBytes(
          mAudioFileStream,
          static_cast<UInt32>(buffer.size()),
          buffer.data(),
          0
      );
      if (parseStatus != noErr) {
        // Continue trying to parse or process buffered packets
      }
      buffer.clear();
    }

    // 2. Return buffered remainder if any
    std::vector<float> decodedData;
    if (maxOutputSamples > 0) {
      decodedData.reserve(maxOutputSamples);
    }

    while (!mDecodedRemainder.empty() &&
           (maxOutputSamples == 0 || decodedData.size() < maxOutputSamples)) {
      decodedData.push_back(mDecodedRemainder.front());
      mDecodedRemainder.pop_front();
    }

    if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
      if (samplerate && mOutputFormat.mSampleRate > 0) {
        *samplerate = static_cast<int>(mOutputFormat.mSampleRate);
      }
      if (channels && mOutputFormat.mChannelsPerFrame > 0) {
        *channels = static_cast<int>(mOutputFormat.mChannelsPerFrame);
      }
      return {std::move(decodedData), DecoderError::NoError};
    }

    // If format/converter is not yet ready, wait for more data
    if (!mConverterInitialized || !mConverter) {
      return {std::move(decodedData), DecoderError::NoError};
    }

    if (samplerate) {
      *samplerate = static_cast<int>(mOutputFormat.mSampleRate);
    }
    if (channels) {
      *channels = static_cast<int>(mOutputFormat.mChannelsPerFrame);
    }

    // 3. Decode queued packets through AudioConverter
    while ((!mPacketQueue.empty() || (mDataEnded && !mDrained)) &&
           (maxOutputSamples == 0 || decodedData.size() < maxOutputSamples)) {
      constexpr UInt32 kFramesPerChunk = 1024;
      UInt32 ioOutputFrames = kFramesPerChunk;
      std::vector<float> chunk(ioOutputFrames * mOutputFormat.mChannelsPerFrame);

      AudioBufferList outputBufferList;
      outputBufferList.mNumberBuffers = 1;
      outputBufferList.mBuffers[0].mNumberChannels = mOutputFormat.mChannelsPerFrame;
      outputBufferList.mBuffers[0].mDataByteSize =
          static_cast<UInt32>(chunk.size() * sizeof(float));
      outputBufferList.mBuffers[0].mData = chunk.data();

      OSStatus convStatus = AudioConverterFillComplexBuffer(
          mConverter,
          ConverterInputProc,
          this,
          &ioOutputFrames,
          &outputBufferList,
          nullptr
      );

      if (ioOutputFrames > 0) {
        size_t samplesDecoded = ioOutputFrames * mOutputFormat.mChannelsPerFrame;
        size_t availableSpace = (maxOutputSamples > 0 && decodedData.size() < maxOutputSamples)
                                    ? (maxOutputSamples - decodedData.size())
                                    : samplesDecoded;
        size_t toAppend = std::min(samplesDecoded, availableSpace);

        decodedData.insert(decodedData.end(), chunk.data(), chunk.data() + toAppend);

        // Buffer any excess samples into remainder queue
        for (size_t s = toAppend; s < samplesDecoded; ++s) {
          mDecodedRemainder.push_back(chunk[s]);
        }
      }

      if (ioOutputFrames == 0) {
        if (mDataEnded) {
          mDrained = true;
        }
        break;
      }

      if (convStatus != noErr) {
        break;
      }
    }

    return {std::move(decodedData), DecoderError::NoError};
  }

  void setDataEnded() override {
    mDataEnded = true;
  }

  bool hasPendingData() const override {
    if (!mConverterInitialized || !mConverter) {
      return false;
    }
    return !mPacketQueue.empty() || !mDecodedRemainder.empty() || (mDataEnded && !mDrained);
  }

private:
  void cleanup() {
    if (mAudioFileStream) {
      AudioFileStreamClose(mAudioFileStream);
      mAudioFileStream = nullptr;
    }
    if (mConverter) {
      AudioConverterDispose(mConverter);
      mConverter = nullptr;
    }
    mFormatInitialized = false;
    mConverterInitialized = false;
    mPacketQueue.clear();
    mDecodedRemainder.clear();
    mMagicCookie.clear();
    mDataEnded = false;
    mDrained = false;
  }

  void setupConverter() {
    if (mConverterInitialized && mConverter) {
      AudioConverterDispose(mConverter);
      mConverter = nullptr;
      mConverterInitialized = false;
    }

    mOutputFormat = {};
    mOutputFormat.mSampleRate = mInputFormat.mSampleRate;
    mOutputFormat.mFormatID = kAudioFormatLinearPCM;
    mOutputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    mOutputFormat.mBitsPerChannel = 32;
    // Downmix multichannel (e.g. 5.1 surround sound) to target engine channels if specified
    mOutputFormat.mChannelsPerFrame = (mInputFormat.mChannelsPerFrame > 2 && mTargetChannels > 0)
                                          ? static_cast<UInt32>(mTargetChannels)
                                          : mInputFormat.mChannelsPerFrame;
    mOutputFormat.mBytesPerFrame = mOutputFormat.mChannelsPerFrame * sizeof(float);
    mOutputFormat.mFramesPerPacket = 1;
    mOutputFormat.mBytesPerPacket = mOutputFormat.mBytesPerFrame;

    OSStatus status = AudioConverterNew(&mInputFormat, &mOutputFormat, &mConverter);
    if (status == noErr) {
      mConverterInitialized = true;
      if (!mMagicCookie.empty()) {
        AudioConverterSetProperty(
            mConverter,
            kAudioConverterDecompressionMagicCookie,
            static_cast<UInt32>(mMagicCookie.size()),
            mMagicCookie.data()
        );
      }
    } else {
      std::cerr << "AudioConverterNew failed: " << status << std::endl;
    }
  }

  static void PropertyListener(
      void *inClientData,
      AudioFileStreamID inAudioFileStream,
      AudioFileStreamPropertyID inPropertyID,
      AudioFileStreamPropertyFlags *ioFlags) {
    AppleAACImpl *impl = static_cast<AppleAACImpl *>(inClientData);

    if (inPropertyID == kAudioFileStreamProperty_DataFormat) {
      UInt32 size = sizeof(impl->mInputFormat);
      OSStatus status = AudioFileStreamGetProperty(
          inAudioFileStream,
          kAudioFileStreamProperty_DataFormat,
          &size,
          &impl->mInputFormat
      );
      if (status == noErr) {
        impl->mFormatInitialized = true;
        impl->setupConverter();
      }
    } else if (inPropertyID == kAudioFileStreamProperty_MagicCookieData) {
      UInt32 cookieSize = 0;
      Boolean isWritable = false;
      if (AudioFileStreamGetPropertyInfo(
              inAudioFileStream,
              kAudioFileStreamProperty_MagicCookieData,
              &cookieSize,
              &isWritable
          ) == noErr && cookieSize > 0) {
        impl->mMagicCookie.resize(cookieSize);
        if (AudioFileStreamGetProperty(
                inAudioFileStream,
                kAudioFileStreamProperty_MagicCookieData,
                &cookieSize,
                impl->mMagicCookie.data()
            ) == noErr) {
          if (impl->mConverter) {
            AudioConverterSetProperty(
                impl->mConverter,
                kAudioConverterDecompressionMagicCookie,
                cookieSize,
                impl->mMagicCookie.data()
            );
          }
        }
      }
    }
  }

  static void PacketsListener(
      void *inClientData,
      UInt32 inNumberBytes,
      UInt32 inNumberPackets,
      const void *inInputData,
      AudioStreamPacketDescription *inPacketDescriptions) {
    AppleAACImpl *impl = static_cast<AppleAACImpl *>(inClientData);
    const uint8_t *bytes = static_cast<const uint8_t *>(inInputData);

    for (UInt32 i = 0; i < inNumberPackets; ++i) {
      QueuedPacket packet;
      if (inPacketDescriptions) {
        packet.desc = inPacketDescriptions[i];
        packet.data.assign(
            bytes + packet.desc.mStartOffset,
            bytes + packet.desc.mStartOffset + packet.desc.mDataByteSize
        );
        packet.desc.mStartOffset = 0;
      } else {
        UInt32 packetSize = inNumberBytes / inNumberPackets;
        packet.desc.mStartOffset = 0;
        packet.desc.mDataByteSize = packetSize;
        packet.desc.mVariableFramesInPacket = 0;
        packet.data.assign(bytes + i * packetSize, bytes + (i + 1) * packetSize);
      }
      impl->mPacketQueue.push_back(std::move(packet));
    }
  }

  static OSStatus ConverterInputProc(
      AudioConverterRef inAudioConverter,
      UInt32 *ioNumberDataPackets,
      AudioBufferList *ioData,
      AudioStreamPacketDescription **outDataPacketDescription,
      void *inUserData) {
    AppleAACImpl *impl = static_cast<AppleAACImpl *>(inUserData);
    if (impl->mPacketQueue.empty()) {
      *ioNumberDataPackets = 0;
      return impl->mDataEnded ? noErr : 1;
    }

    impl->mCurrentPacket = std::move(impl->mPacketQueue.front());
    impl->mPacketQueue.pop_front();

    *ioNumberDataPackets = 1;
    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mNumberChannels = impl->mInputFormat.mChannelsPerFrame;
    ioData->mBuffers[0].mDataByteSize =
        static_cast<UInt32>(impl->mCurrentPacket.data.size());
    ioData->mBuffers[0].mData = impl->mCurrentPacket.data.data();

    if (outDataPacketDescription) {
      *outDataPacketDescription = &impl->mCurrentPacket.desc;
    }

    return noErr;
  }

  AudioFileStreamID mAudioFileStream = nullptr;
  AudioConverterRef mConverter = nullptr;
  AudioStreamBasicDescription mInputFormat = {};
  AudioStreamBasicDescription mOutputFormat = {};
  bool mFormatInitialized = false;
  bool mConverterInitialized = false;
  int mTargetSampleRate = 0;
  int mTargetChannels = 0;
  std::vector<uint8_t> mMagicCookie;
  std::deque<QueuedPacket> mPacketQueue;
  std::deque<float> mDecodedRemainder;
  QueuedPacket mCurrentPacket;
  DetectedType mFormat = DetectedType::BUFFER_AAC;
  bool mDataEnded = false;
  bool mDrained = false;
};

} // namespace

std::unique_ptr<AACDecoderWrapper::Impl> createAppleAACDecoderImpl(DetectedType format) {
  return std::make_unique<AppleAACImpl>(format);
}

#endif // __APPLE__

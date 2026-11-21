#if defined(__APPLE__)

#import <AudioToolbox/AudioToolbox.h>
#import <CoreFoundation/CoreFoundation.h>
#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <algorithm>
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
      // Until the first frame syncword has been seen, hold the bytes back and
      // drop what comes before it (a stream joined mid-frame). This is done
      // once: after that every byte goes to the parser, which may be holding
      // the start of a frame whose rest is in this chunk.
      if (!mSynced) {
        mSyncBuffer.insert(mSyncBuffer.end(), buffer.begin(), buffer.end());
        buffer.clear();
        const long syncIdx = findSync(mSyncBuffer);
        if (syncIdx < 0) {
          // A syncword may straddle this chunk and the next one.
          constexpr size_t kKeep = 7;
          if (mSyncBuffer.size() > kKeep) {
            mSyncBuffer.erase(mSyncBuffer.begin(), mSyncBuffer.end() - kKeep);
          }
        } else {
          mSyncBuffer.erase(mSyncBuffer.begin(), mSyncBuffer.begin() + syncIdx);
          mSynced = true;
          buffer.swap(mSyncBuffer);
          mSyncBuffer.clear();
        }
      }

      if (!buffer.empty()) {
        // AC-3 / E-AC-3: keep the first frame header, see setDataEnded().
        if (isAc3Family() && mFirstHeader.size() < kAc3HeaderBytes) {
          const size_t need = kAc3HeaderBytes - mFirstHeader.size();
          mFirstHeader.insert(mFirstHeader.end(), buffer.begin(),
                              buffer.begin() + static_cast<long>(std::min(need, buffer.size())));
        }
        if (isAc3Family()) {
          feedWholeAc3Frames(buffer);
        } else {
          AudioFileStreamParseBytes(mAudioFileStream, static_cast<UInt32>(buffer.size()), buffer.data(), 0);
        }
        buffer.clear();
      }
    }

    // Optimization: Prebuffering strategy - accumulate packets before starting decode
    if (mConverterInitialized && !mPrebufferComplete) {
      constexpr size_t kMinPrebufferPackets = 3;
      // Once the data has ended no further packet can arrive: decode and
      // flush what is queued, however few packets that is.
      if (mPacketQueue.size() >= kMinPrebufferPackets || mDataEnded) {
        mPrebufferComplete = true;
      } else {
        // Wait for more packets before starting decode
        return {std::vector<float>(), DecoderError::NoError};
      }
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
    constexpr UInt32 kFramesPerChunk = 1024;
    const size_t chunkFloats = static_cast<size_t>(kFramesPerChunk) * mOutputFormat.mChannelsPerFrame;
    if (mDecodeChunk.size() < chunkFloats) {
      mDecodeChunk.resize(chunkFloats);
    }
    while ((!mPacketQueue.empty() || (mDataEnded && !mDrained)) &&
           (maxOutputSamples == 0 || decodedData.size() < maxOutputSamples)) {
      UInt32 ioOutputFrames = kFramesPerChunk;

      AudioBufferList outputBufferList;
      outputBufferList.mNumberBuffers = 1;
      outputBufferList.mBuffers[0].mNumberChannels = mOutputFormat.mChannelsPerFrame;
      outputBufferList.mBuffers[0].mDataByteSize =
          static_cast<UInt32>(chunkFloats * sizeof(float));
      outputBufferList.mBuffers[0].mData = mDecodeChunk.data();

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

        decodedData.insert(decodedData.end(), mDecodeChunk.data(), mDecodeChunk.data() + toAppend);

        // Buffer any excess samples into remainder queue
        for (size_t s = toAppend; s < samplesDecoded; ++s) {
          mDecodedRemainder.push_back(mDecodeChunk[s]);
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
    // AudioFileStream emits an AC-3 / E-AC-3 frame only once it has seen the
    // syncword of the next one, so the last frame would stay inside the
    // parser. Feed it the header of the first frame as a stand-in for a
    // next frame: the real last frame comes out, the stand-in is never
    // completed and so never emitted.
    if (!mDataEnded && mAudioFileStream && mSynced && isAc3Family()) {
      // A cut-short last frame, if any.
      if (!mAc3Pending.empty()) {
        AudioFileStreamParseBytes(mAudioFileStream, static_cast<UInt32>(mAc3Pending.size()), mAc3Pending.data(), 0);
        mAc3Pending.clear();
      }
    }
    if (!mDataEnded && mAudioFileStream && mSynced && isAc3Family() &&
        mFirstHeader.size() >= kAc3HeaderBytes) {
      AudioFileStreamParseBytes(mAudioFileStream, static_cast<UInt32>(mFirstHeader.size()),
                                mFirstHeader.data(), 0);
    }
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
    mConverterPreCreated = false;
    mPrebufferComplete = false;
    mPacketQueue.clear();
    mDecodedRemainder.clear();
    mMagicCookie.clear();
    mDataEnded = false;
    mDrained = false;
    mSynced = false;
    mSyncBuffer.clear();
    mFirstHeader.clear();
    mAc3Pending.clear();
  }

  bool isAc3Family() const {
    return mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3;
  }

  /// Size of the AC-3 or E-AC-3 frame whose syncword is at [data], 0 when
  /// more bytes are needed to tell, -1 when [data] is not a frame header.
  static long ac3FrameBytes(const unsigned char *data, size_t size) {
    constexpr size_t kHeader = 8;
    if (size < kHeader) return 0;
    if (!((data[0] == 0x0B && data[1] == 0x77) || (data[0] == 0x77 && data[1] == 0x0B))) return -1;
    Ac3Metadata ac3;
    if (AACDecoderWrapper::parseAc3Metadata(data, kHeader, ac3) && ac3.frameSize > 0) return ac3.frameSize;
    Eac3Metadata eac3;
    if (AACDecoderWrapper::parseEac3Metadata(data, kHeader, eac3) && eac3.frameSize > 0) return eac3.frameSize;
    return -1;
  }

  /// Passes [bytes] to the parser in whole frames, holding back the start of
  /// a frame whose end has not arrived yet. AudioFileStream corrupts AC-3
  /// packets (every 64th, when parse calls end mid-frame in small chunks:
  /// seen with 100..3000-byte chunks on macOS 27), so it is only ever
  /// given whole frames.
  void feedWholeAc3Frames(const std::vector<unsigned char> &bytes) {
    mAc3Pending.insert(mAc3Pending.end(), bytes.begin(), bytes.end());
    size_t end = 0;
    while (end < mAc3Pending.size()) {
      const long frame = ac3FrameBytes(mAc3Pending.data() + end, mAc3Pending.size() - end);
      if (frame < 0) {
        // Lost sync (a damaged frame): let the parser resync on its own.
        end = mAc3Pending.size();
        break;
      }
      if (frame == 0 || end + static_cast<size_t>(frame) > mAc3Pending.size()) break;
      end += static_cast<size_t>(frame);
    }
    if (end == 0) return;
    AudioFileStreamParseBytes(mAudioFileStream, static_cast<UInt32>(end), mAc3Pending.data(), 0);
    mAc3Pending.erase(mAc3Pending.begin(), mAc3Pending.begin() + static_cast<long>(end));
  }

  /// Offset of the first frame syncword in [data], or -1.
  long findSync(const std::vector<unsigned char> &data) const {
    if (mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3) {
      return NativeAudioDecoder::findAc3Syncword(data.data(), data.size());
    }
    for (size_t i = 0; i + 2 <= data.size(); ++i) {
      if (NativeAudioDecoder::isAacAdts(data.data() + i, data.size() - i)) return static_cast<long>(i);
    }
    return -1;
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
      mConverterPreCreated = true;
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
        // The ADTS header of HE-AAC (implicit SBR signalling) carries the
        // core rate: kAudioFileStreamProperty_DataFormat says AAC-LC at
        // 22.05 kHz. The format list also has the HE-AAC entry at the SBR
        // output rate; decode that, as ExtAudioFile does, or the converter
        // drops SBR and outputs half the bandwidth at half the rate.
        UInt32 listSize = 0;
        Boolean writable = false;
        if (AudioFileStreamGetPropertyInfo(inAudioFileStream, kAudioFileStreamProperty_FormatList, &listSize,
                                           &writable) == noErr &&
            listSize >= sizeof(AudioFormatListItem)) {
          std::vector<AudioFormatListItem> list(listSize / sizeof(AudioFormatListItem));
          if (AudioFileStreamGetProperty(inAudioFileStream, kAudioFileStreamProperty_FormatList, &listSize,
                                         list.data()) == noErr) {
            UInt32 index = 0;
            UInt32 indexSize = sizeof(index);
            if (AudioFormatGetProperty(kAudioFormatProperty_FirstPlayableFormatFromList,
                                       static_cast<UInt32>(list.size() * sizeof(AudioFormatListItem)), list.data(),
                                       &indexSize, &index) == noErr &&
                index < list.size()) {
              impl->mInputFormat = list[index].mASBD;
            }
          }
        }
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
  bool mConverterPreCreated = false;
  bool mPrebufferComplete = false;
  int mTargetSampleRate = 0;
  int mTargetChannels = 0;
  std::vector<uint8_t> mMagicCookie;
  std::vector<float> mDecodeChunk;
  std::deque<QueuedPacket> mPacketQueue;
  std::deque<float> mDecodedRemainder;
  QueuedPacket mCurrentPacket;
  DetectedType mFormat = DetectedType::BUFFER_AAC;
  bool mDataEnded = false;
  bool mDrained = false;
  bool mSynced = false;
  std::vector<unsigned char> mSyncBuffer;
  static constexpr size_t kAc3HeaderBytes = 16;
  std::vector<unsigned char> mFirstHeader;
  std::vector<unsigned char> mAc3Pending;
};

} // namespace

std::unique_ptr<AACDecoderWrapper::Impl> createAppleAACDecoderImpl(DetectedType format) {
  return std::make_unique<AppleAACImpl>(format);
}

#endif // __APPLE__

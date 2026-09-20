#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <cstring>

#if defined(__APPLE__)
std::unique_ptr<AACDecoderWrapper::Impl> createAppleAACDecoderImpl(DetectedType format);
#elif defined(__ANDROID__)
std::unique_ptr<AACDecoderWrapper::Impl> createAndroidAACDecoderImpl(DetectedType format);
#elif defined(__EMSCRIPTEN__)
std::unique_ptr<AACDecoderWrapper::Impl> createWebAACDecoderImpl(DetectedType format);
#endif

namespace {
class UnsupportedAACImpl : public AACDecoderWrapper::Impl {
public:
  explicit UnsupportedAACImpl(DetectedType format) : mFormat(format) {}
  bool initialize(int /*engineSamplerate*/, int /*engineChannels*/) override {
    fprintf(stderr,
            "[flutter_soloud] Native stream decoding for format %d (AAC/AC-3/E-AC-3) is not supported on this platform.\n",
            static_cast<int>(mFormat));
    return false;
  }
  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &/*buffer*/, int */*samplerate*/,
         int */*channels*/, size_t /*maxOutputSamples*/) override {
    return {{}, DecoderError::FormatNotSupported};
  }
  void setDataEnded() override {}
private:
  DetectedType mFormat;
};
} // namespace

std::unique_ptr<AACDecoderWrapper::Impl> AACDecoderWrapper::createImpl(DetectedType format) {
#if defined(__APPLE__)
  return createAppleAACDecoderImpl(format);
#elif defined(__ANDROID__)
  return createAndroidAACDecoderImpl(format);
#elif defined(__EMSCRIPTEN__)
  return createWebAACDecoderImpl(format);
#else
  return std::make_unique<UnsupportedAACImpl>(format);
#endif
}

AACDecoderWrapper::AACDecoderWrapper(DetectedType format)
    : mImpl(createImpl(format)), mFormat(format), mMetadataParsed(false) {}

AACDecoderWrapper::~AACDecoderWrapper() = default;

bool AACDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels) {
  if (!mImpl) return false;
  return mImpl->initialize(engineSamplerate, engineChannels);
}

void AACDecoderWrapper::setIcyMetaInt(int icyMetaInt) {
  if (mIcyMetaInt == icyMetaInt) return;
  mIcyMetaInt = icyMetaInt;
}

std::pair<std::vector<float>, DecoderError>
AACDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *samplerate,
                          int *channels, size_t maxOutputSamples) {
  if (!mImpl) {
    return {{}, DecoderError::FormatNotSupported};
  }

  // Handle ICY metadata stripping for AAC streams
  if (mIcyMetaInt > 0 && mFormat == DetectedType::BUFFER_AAC && !buffer.empty()) {
    std::vector<unsigned char> cleanAudio;
    stripIcyMetadata(buffer, mIcyMetaInt, mIcy, cleanAudio, [this](const std::string &title) {
      mCachedAacMetadata.title = title;
      size_t dash = title.find(" - ");
      if (dash != std::string::npos) {
        mCachedAacMetadata.artist = title.substr(0, dash);
        mCachedAacMetadata.title = title.substr(dash + 3);
      }
      if (onTrackChange) {
        AudioMetadata meta;
        meta.type = mFormat;
        meta.aacMetadata = mCachedAacMetadata;
        onTrackChange(meta);
      }
    });
    buffer = std::move(cleanAudio);
  }

  // Handle ID3 tags prepended to AAC streams
  if (mFormat == DetectedType::BUFFER_AAC && !buffer.empty()) {
    if (!mId3Parsed && buffer.size() >= 10 && std::memcmp(buffer.data(), "ID3", 3) == 0) {
      size_t id3Size = 0;
      if (parseId3Tags(buffer.data(), buffer.size(), mCachedAacMetadata, id3Size)) {
        mId3Parsed = true;
        if (onTrackChange) {
          AudioMetadata meta;
          meta.type = mFormat;
          meta.aacMetadata = mCachedAacMetadata;
          onTrackChange(meta);
        }
      }
      if (id3Size > 0 && buffer.size() >= id3Size) {
        buffer.erase(buffer.begin(), buffer.begin() + id3Size);
      }
    }
  }

  if (!mMetadataParsed && onTrackChange && !buffer.empty()) {
    AudioMetadata metadata;
    metadata.type = mFormat;
    bool parsed = false;

    if (mFormat == DetectedType::BUFFER_AAC) {
      parsed = parseAacAdtsMetadata(buffer.data(), buffer.size(), mCachedAacMetadata);
      metadata.aacMetadata = mCachedAacMetadata;
    } else if (mFormat == DetectedType::BUFFER_AC3) {
      parsed = parseAc3Metadata(buffer.data(), buffer.size(), metadata.ac3Metadata);
    } else if (mFormat == DetectedType::BUFFER_EAC3) {
      parsed = parseEac3Metadata(buffer.data(), buffer.size(), metadata.eac3Metadata);
    }

    if (parsed) {
      mMetadataParsed = true;
      onTrackChange(metadata);
    }
  }

  return mImpl->decode(buffer, samplerate, channels, maxOutputSamples);
}

void AACDecoderWrapper::setDataEnded() {
  if (mImpl) {
    mImpl->setDataEnded();
  }
}

bool AACDecoderWrapper::checkForValidFrames(const std::vector<unsigned char> &buffer) {
  if (buffer.size() < 7) return false;
  for (size_t i = 0; i + 7 <= buffer.size(); ++i) {
    if (NativeAudioDecoder::isAacAdts(buffer.data() + i, buffer.size() - i)) {
      int frameLength = ((buffer[i + 3] & 0x03) << 11) |
                        (buffer[i + 4] << 3) |
                        ((buffer[i + 5] & 0xE0) >> 5);
      if (frameLength >= 7 && frameLength <= 8192) {
        return true;
      }
    }
  }
  return false;
}

bool AACDecoderWrapper::checkForValidAc3Frames(const std::vector<unsigned char> &buffer) {
  return NativeAudioDecoder::findAc3Syncword(buffer.data(), buffer.size()) >= 0;
}

bool AACDecoderWrapper::parseAacAdtsMetadata(const unsigned char *data, size_t size, AacMetadata &out) {
  if (!data || size < 7) return false;
  for (size_t i = 0; i + 7 <= size; ++i) {
    if (data[i] == 0xFF && (data[i + 1] & 0xF6) == 0xF0) {
      uint8_t profile = (data[i + 2] >> 6) & 0x03;
      uint8_t sf_index = (data[i + 2] >> 2) & 0x0F;
      uint8_t channel_config = ((data[i + 2] & 0x01) << 2) | ((data[i + 3] >> 6) & 0x03);
      uint32_t frame_length = ((data[i + 3] & 0x03) << 11) |
                              (data[i + 4] << 3) |
                              ((data[i + 5] >> 5) & 0x07);
      if (frame_length < 7 || frame_length > 8192) continue;

      static const int kSampleRates[] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
      };
      if (sf_index >= 13) continue;
      int sampleRate = kSampleRates[sf_index];

      static const int kChannels[] = {0, 1, 2, 3, 4, 5, 6, 8};
      int channels = (channel_config < 8) ? kChannels[channel_config] : 2;
      if (channels == 0) channels = 2;

      const char *profileStr = "LC";
      if (profile == 0) profileStr = "Main";
      else if (profile == 1) profileStr = "LC";
      else if (profile == 2) profileStr = "SSR";
      else if (profile == 3) profileStr = "LTP";

      int bitrate = 0;
      if (sampleRate > 0) {
        bitrate = static_cast<int>((static_cast<uint64_t>(frame_length) * 8 * sampleRate) / 1024);
      }

      out.sampleRate = sampleRate;
      out.channels = channels;
      out.profile = profileStr;
      out.bitrate = bitrate;
      out.frameLength = static_cast<int>(frame_length);
      return true;
    }
  }
  return false;
}

bool AACDecoderWrapper::parseAc3Metadata(const unsigned char *data, size_t size, Ac3Metadata &out) {
  int syncIdx = NativeAudioDecoder::findAc3Syncword(data, size);
  if (syncIdx < 0 || static_cast<size_t>(syncIdx) + 7 > size) return false;

  const unsigned char *h = data + syncIdx;
  bool byteSwapped = (h[0] == 0x77 && h[1] == 0x0B);
  unsigned char buf[8];
  if (byteSwapped) {
    for (int i = 0; i < 8; i += 2) {
      buf[i] = h[i + 1];
      buf[i + 1] = h[i];
    }
    h = buf;
  }

  uint8_t fscod = (h[4] >> 6) & 0x03;
  uint8_t frmsizecod = h[4] & 0x3F;
  uint8_t bsid = (h[5] >> 3) & 0x1F;
  uint8_t bsmod = h[5] & 0x07;
  uint8_t acmod = (h[6] >> 5) & 0x07;

  if (fscod == 3 || bsid > 10) return false;

  static const int kSampleRates[] = {48000, 44100, 32000};
  int sampleRate = kSampleRates[fscod];

  static const int kBitratesKbps[] = {
    32, 40, 48, 56, 64, 80, 96, 112, 128, 160,
    192, 224, 256, 320, 384, 448, 512, 576, 640
  };
  int bitrateIdx = frmsizecod >> 1;
  int bitrateBps = 0;
  if (bitrateIdx < 19) {
    bitrateBps = kBitratesKbps[bitrateIdx] * 1000;
  }

  int frameSize = (1536 * (bitrateBps / 1000) * 1000) / (8 * sampleRate);
  if (sampleRate == 44100 && (frmsizecod & 1)) {
    frameSize += 2;
  }

  uint32_t b = (static_cast<uint32_t>(h[6]) << 8) | h[7];
  int curBit = 12;
  if ((acmod & 0x01) && acmod != 1) curBit -= 2;
  if (acmod & 0x04) curBit -= 2;
  if (acmod == 0x02) curBit -= 2;
  bool lfeOn = (b & (1 << curBit)) != 0;

  static const int kChannelsByAcmod[] = {2, 1, 2, 3, 3, 4, 4, 5};
  int channels = kChannelsByAcmod[acmod] + (lfeOn ? 1 : 0);

  out.sampleRate = sampleRate;
  out.channels = channels;
  out.bitrate = bitrateBps;
  out.bsid = bsid;
  out.bsmod = bsmod;
  out.acmod = acmod;
  out.lfeOn = lfeOn;
  out.frameSize = frameSize;
  return true;
}

bool AACDecoderWrapper::parseEac3Metadata(const unsigned char *data, size_t size, Eac3Metadata &out) {
  int syncIdx = NativeAudioDecoder::findAc3Syncword(data, size);
  if (syncIdx < 0 || static_cast<size_t>(syncIdx) + 7 > size) return false;

  const unsigned char *h = data + syncIdx;
  bool byteSwapped = (h[0] == 0x77 && h[1] == 0x0B);
  unsigned char buf[8];
  if (byteSwapped) {
    for (int i = 0; i < 8; i += 2) {
      buf[i] = h[i + 1];
      buf[i + 1] = h[i];
    }
    h = buf;
  }

  uint8_t strmtyp = (h[2] >> 6) & 0x03;
  uint8_t substreamid = (h[2] >> 3) & 0x07;
  uint16_t frmsiz = static_cast<uint16_t>(((h[2] & 0x07) << 8) | h[3]);
  int frameSizeBytes = (frmsiz + 1) * 2;

  uint8_t fscod = (h[4] >> 6) & 0x03;
  int sampleRate = 0;
  int numBlocks = 6;
  uint8_t acmod = 0;
  bool lfeOn = false;

  if (fscod == 3) {
    uint8_t fscod2 = (h[4] >> 4) & 0x03;
    static const int kReducedSampleRates[] = {24000, 22050, 16000, 0};
    sampleRate = kReducedSampleRates[fscod2];
    numBlocks = 6;
    acmod = (h[4] >> 1) & 0x07;
    lfeOn = (h[4] & 0x01) != 0;
  } else {
    static const int kSampleRates[] = {48000, 44100, 32000};
    sampleRate = kSampleRates[fscod];
    uint8_t numblkscod = (h[4] >> 4) & 0x03;
    static const int kBlocks[] = {1, 2, 3, 6};
    numBlocks = kBlocks[numblkscod];
    acmod = (h[4] >> 1) & 0x07;
    lfeOn = (h[4] & 0x01) != 0;
  }

  uint8_t bsid = (h[5] >> 3) & 0x1F;
  if (bsid <= 10) return false;

  static const int kChannelsByAcmod[] = {2, 1, 2, 3, 3, 4, 4, 5};
  int channels = kChannelsByAcmod[acmod] + (lfeOn ? 1 : 0);

  int bitrate = 0;
  int numSamples = numBlocks * 256;
  if (numSamples > 0 && sampleRate > 0) {
    bitrate = static_cast<int>((static_cast<uint64_t>(frameSizeBytes) * 8 * sampleRate) / numSamples);
  }

  out.sampleRate = sampleRate;
  out.channels = channels;
  out.bitrate = bitrate;
  out.bsid = bsid;
  out.streamType = strmtyp;
  out.substreamId = substreamid;
  out.acmod = acmod;
  out.lfeOn = lfeOn;
  out.frameSize = frameSizeBytes;
  out.numBlocks = numBlocks;
  return true;
}

bool AACDecoderWrapper::parseId3Tags(const unsigned char *data, size_t size, AacMetadata &out, size_t &outId3Size) {
  outId3Size = 0;
  if (!data || size < 10 || std::memcmp(data, "ID3", 3) != 0) {
    return false;
  }

  uint8_t major = data[3];
  uint32_t tagSize = ((data[6] & 0x7f) << 21) |
                     ((data[7] & 0x7f) << 14) |
                     ((data[8] & 0x7f) << 7) |
                     (data[9] & 0x7f);
  outId3Size = tagSize + 10;
  size_t totalTagSize = (size < outId3Size) ? size : outId3Size;

  size_t pos = 10;
  if (data[5] & 0x40) {
    if (pos + 4 <= totalTagSize) {
      uint32_t extSize = ((data[pos] & 0x7f) << 21) |
                         ((data[pos + 1] & 0x7f) << 14) |
                         ((data[pos + 2] & 0x7f) << 7) |
                         (data[pos + 3] & 0x7f);
      pos += extSize + (major == 3 ? 4 : 0);
    }
  }

  while (pos + 10 <= totalTagSize) {
    char frame_id[5] = {0};
    std::memcpy(frame_id, data + pos, 4);
    if (frame_id[0] == 0) {
      break; // Padding or end
    }

    uint32_t frame_size = 0;
    if (major >= 4) {
      frame_size = ((data[pos + 4] & 0x7f) << 21) |
                   ((data[pos + 5] & 0x7f) << 14) |
                   ((data[pos + 6] & 0x7f) << 7) |
                   (data[pos + 7] & 0x7f);
    } else {
      frame_size = (static_cast<uint32_t>(data[pos + 4]) << 24) |
                   (static_cast<uint32_t>(data[pos + 5]) << 16) |
                   (static_cast<uint32_t>(data[pos + 6]) << 8) |
                   static_cast<uint32_t>(data[pos + 7]);
    }

    pos += 10;
    if (pos + frame_size > totalTagSize) {
      break;
    }

    if (frame_id[0] == 'T') {
      size_t text_start = pos + 1;
      if (text_start < pos + frame_size) {
        std::string value(reinterpret_cast<const char *>(data + text_start), frame_size - 1);
        if (std::strcmp(frame_id, "TIT2") == 0) {
          out.title = value;
        } else if (std::strcmp(frame_id, "TPE1") == 0) {
          out.artist = value;
        } else if (std::strcmp(frame_id, "TALB") == 0) {
          out.album = value;
        }
      }
    }

    pos += frame_size;
  }

  return true;
}


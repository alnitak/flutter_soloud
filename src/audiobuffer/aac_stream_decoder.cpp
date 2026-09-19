#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <cstring>

#if defined(__APPLE__)
std::unique_ptr<AACDecoderWrapper::Impl> createAppleAACDecoderImpl(DetectedType format);
#endif

namespace {
class UnsupportedAACImpl : public AACDecoderWrapper::Impl {
public:
  bool initialize(int /*engineSamplerate*/, int /*engineChannels*/) override {
    return false;
  }
  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &/*buffer*/, int */*samplerate*/,
         int */*channels*/, size_t /*maxOutputSamples*/) override {
    return {{}, DecoderError::FormatNotSupported};
  }
  void setDataEnded() override {}
};
} // namespace

std::unique_ptr<AACDecoderWrapper::Impl> AACDecoderWrapper::createImpl(DetectedType format) {
#if defined(__APPLE__)
  return createAppleAACDecoderImpl(format);
#else
  return std::make_unique<UnsupportedAACImpl>();
#endif
}

AACDecoderWrapper::AACDecoderWrapper(DetectedType format) : mImpl(createImpl(format)) {}

AACDecoderWrapper::~AACDecoderWrapper() = default;

bool AACDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels) {
  if (!mImpl) return false;
  return mImpl->initialize(engineSamplerate, engineChannels);
}

std::pair<std::vector<float>, DecoderError>
AACDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *samplerate,
                          int *channels, size_t maxOutputSamples) {
  if (!mImpl) {
    return {{}, DecoderError::FormatNotSupported};
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

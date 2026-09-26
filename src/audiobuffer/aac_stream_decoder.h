#ifndef AAC_STREAM_DECODER_H
#define AAC_STREAM_DECODER_H

#include "stream_decoder.h"
#include "icy_metadata.h"
#include <memory>
#include <vector>

/// IDecoderWrapper implementation for streaming ADTS AAC, AC-3, and E-AC-3 audio.
class AACDecoderWrapper : public IDecoderWrapper {
public:
  explicit AACDecoderWrapper(DetectedType format = DetectedType::BUFFER_AAC);
  ~AACDecoderWrapper() override;

  bool initializeDecoder(int engineSamplerate, int engineChannels) override;

  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *samplerate,
         int *channels, size_t maxOutputSamples = 0) override;

  void setDataEnded() override;

  bool hasPendingData() const override {
    return (!mAudioData.empty()) || (mImpl ? mImpl->hasPendingData() : false);
  }

  void setIcyMetaInt(int icyMetaInt);

  static bool checkForValidFrames(const std::vector<unsigned char> &buffer);
  static bool checkForValidAc3Frames(const std::vector<unsigned char> &buffer);
  static bool parseAacAdtsMetadata(const unsigned char *data, size_t size, AacMetadata &out);
  static bool parseAc3Metadata(const unsigned char *data, size_t size, Ac3Metadata &out);
  static bool parseEac3Metadata(const unsigned char *data, size_t size, Eac3Metadata &out);
  static bool parseId3Tags(const unsigned char *data, size_t size, AacMetadata &out, size_t &outId3Size);

  class Impl {
  public:
    virtual ~Impl() = default;
    virtual bool initialize(int engineSamplerate, int engineChannels) = 0;
    virtual std::pair<std::vector<float>, DecoderError>
    decode(std::vector<unsigned char> &buffer, int *samplerate,
           int *channels, size_t maxOutputSamples) = 0;
    virtual void setDataEnded() = 0;
    virtual bool hasPendingData() const { return false; }
  };

private:
  static std::unique_ptr<Impl> createImpl(DetectedType format);
  std::unique_ptr<Impl> mImpl;
  DetectedType mFormat;
  bool mMetadataParsed = false;
  int mIcyMetaInt = 0;
  IcyStripState mIcy;
  bool mId3Parsed = false;
  AacMetadata mCachedAacMetadata;
  std::vector<unsigned char> mAudioData;
};

using NativeStreamDecoderWrapper = AACDecoderWrapper;

#endif // AAC_STREAM_DECODER_H

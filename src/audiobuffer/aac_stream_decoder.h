#ifndef AAC_STREAM_DECODER_H
#define AAC_STREAM_DECODER_H

#include "stream_decoder.h"
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

  static bool checkForValidFrames(const std::vector<unsigned char> &buffer);
  static bool checkForValidAc3Frames(const std::vector<unsigned char> &buffer);

  class Impl {
  public:
    virtual ~Impl() = default;
    virtual bool initialize(int engineSamplerate, int engineChannels) = 0;
    virtual std::pair<std::vector<float>, DecoderError>
    decode(std::vector<unsigned char> &buffer, int *samplerate,
           int *channels, size_t maxOutputSamples) = 0;
    virtual void setDataEnded() = 0;
  };

private:
  static std::unique_ptr<Impl> createImpl(DetectedType format);
  std::unique_ptr<Impl> mImpl;
};

using NativeStreamDecoderWrapper = AACDecoderWrapper;

#endif // AAC_STREAM_DECODER_H

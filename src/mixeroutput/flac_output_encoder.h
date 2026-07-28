#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mixer_output_encoder.h"

#ifndef NO_XIPH_LIBS
#include <FLAC/stream_encoder.h>
#endif

/// FLAC encoder that produces a native FLAC stream.
class FlacOutputEncoder : public MixerOutputEncoder {
 public:
  FlacOutputEncoder();
  ~FlacOutputEncoder() override;

  bool initialize(int sampleRate, int channels) override;
  bool encode(const float *input, size_t samples,
              std::vector<uint8_t> &output) override;
  bool finalize(std::vector<uint8_t> &output) override;
  size_t inputBytesPerSample() const override { return sizeof(float); }

 private:
  bool m_initialized = false;
  int m_sampleRate = 44100;
  int m_channels = 2;
  std::vector<int32_t> m_inputBuffer;
  std::vector<uint8_t> m_outputAccumulator;

  void *m_encoder = nullptr;

#ifndef NO_XIPH_LIBS
  static FLAC__StreamEncoderWriteStatus writeCallback(
      const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[],
      size_t bytes, unsigned int samples, unsigned int currentFrame,
      void *clientData);
#endif
};

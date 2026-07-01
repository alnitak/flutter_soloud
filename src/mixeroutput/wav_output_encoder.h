#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mixer_output_encoder.h"

/// WAV encoder that produces a standard RIFF/WAVE stream with 16-bit PCM.
class WavOutputEncoder : public MixerOutputEncoder {
 public:
  WavOutputEncoder() = default;
  ~WavOutputEncoder() override = default;

  bool initialize(int sampleRate, int channels) override;
  bool encode(const float *input, size_t samples,
              std::vector<uint8_t> &output) override;
  bool finalize(std::vector<uint8_t> &output) override;
  size_t inputBytesPerSample() const override { return sizeof(float); }

 private:
  bool m_initialized = false;
  int m_sampleRate = 44100;
  int m_channels = 2;
  size_t m_totalSamples = 0;
  std::vector<int16_t> m_inputBuffer;
  std::vector<uint8_t> m_pcmData;

  void writeHeader(std::vector<uint8_t> &output);
};

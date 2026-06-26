#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "mixer_output_encoder.h"

/// Opus encoder that produces an Ogg Opus stream.
class OpusOutputEncoder : public MixerOutputEncoder {
 public:
  OpusOutputEncoder();
  ~OpusOutputEncoder() override;

  bool initialize(int sampleRate, int channels) override;
  bool encode(const float *input, size_t samples,
              std::vector<uint8_t> &output) override;
  bool finalize(std::vector<uint8_t> &output) override;
  size_t inputBytesPerSample() const override { return sizeof(float); }

 private:
  bool m_initialized = false;
  int m_sampleRate = 48000;
  int m_channels = 2;
  size_t m_samplesPerFrame = 960;
  uint64_t m_packetCount = 0;
  int64_t m_packetGranulepos = 0;

  std::vector<float> m_inputBuffer;
  std::vector<uint8_t> m_outputAccumulator;

  void *m_encoder = nullptr;

  struct OggState;
  std::unique_ptr<OggState> m_ogg;

  // Opus only supports 8/12/16/24/48 kHz. If the engine runs at a different
  // rate, we resample to 48 kHz with linear interpolation.
  int m_engineSampleRate = 48000;
  double m_resamplerPhase = 0.0;
  std::vector<float> m_resamplerLastFrame;
  std::vector<float> m_resampledBuffer;

#ifndef NO_XIPH_LIBS
  void flushPages(bool force = false);
  bool resampleTo48k(const float *input, size_t samples);
#endif
};

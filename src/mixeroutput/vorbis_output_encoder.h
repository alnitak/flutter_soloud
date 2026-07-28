#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mixer_output_encoder.h"

/// Vorbis encoder that produces an Ogg Vorbis stream.
class VorbisOutputEncoder : public MixerOutputEncoder {
 public:
  VorbisOutputEncoder();
  ~VorbisOutputEncoder() override;

  bool initialize(int sampleRate, int channels) override;
  bool encode(const float *input, size_t samples,
              std::vector<uint8_t> &output) override;
  bool finalize(std::vector<uint8_t> &output) override;
  size_t inputBytesPerSample() const override { return sizeof(float); }

 private:
  bool m_initialized = false;
  int m_sampleRate = 44100;
  int m_channels = 2;

  std::vector<uint8_t> m_outputAccumulator;

#ifndef NO_XIPH_LIBS
  void *m_vi = nullptr;
  void *m_vc = nullptr;
  void *m_vd = nullptr;
  void *m_vb = nullptr;
  void *m_oy = nullptr;
  void *m_os = nullptr;

  void clearState();
  bool writeHeaders();
  bool flushBlocks();
  bool writePacket(void *packet);
#endif
};

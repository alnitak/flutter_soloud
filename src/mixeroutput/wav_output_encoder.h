#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mixer_output_encoder.h"

/// WAV encoder that produces a standard RIFF/WAVE stream with 16-bit PCM.
///
/// Unlike the other compressed encoders, WAV is designed to stream
/// incrementally. The 44-byte header is emitted immediately by [initialize]
/// and the PCM payload is emitted chunk-by-chunk in [encode]. The caller can
/// request an updated header at any time via [writeHeader], which is useful for
/// patching the final file size after capture stops.
class WavOutputEncoder : public MixerOutputEncoder {
 public:
  WavOutputEncoder() = default;
  ~WavOutputEncoder() override = default;

  bool initialize(int sampleRate, int channels) override;
  bool encode(const float *input, size_t samples,
              std::vector<uint8_t> &output) override;
  bool finalize(std::vector<uint8_t> &output) override;
  size_t inputBytesPerSample() const override { return sizeof(float); }

  /// Write the current 44-byte RIFF/WAVE header into [output].
  /// [totalPcmBytes] if 0, uses the accumulated PCM byte count.
  void writeHeader(std::vector<uint8_t> &output, size_t totalPcmBytes = 0);

  /// Total number of PCM bytes emitted so far.
  size_t totalPcmBytes() const { return m_totalSamples * sizeof(int16_t); }

  bool writeCurrentHeader(std::vector<uint8_t> &output) override {
    writeHeader(output, totalPcmBytes());
    return true;
  }

  size_t totalPayloadBytes() const override { return totalPcmBytes(); }

 private:
  bool m_initialized = false;
  int m_sampleRate = 44100;
  int m_channels = 2;
  size_t m_totalSamples = 0;
  std::vector<int16_t> m_inputBuffer;

  void buildHeader(std::vector<uint8_t> &output, size_t pcmDataSize);
};

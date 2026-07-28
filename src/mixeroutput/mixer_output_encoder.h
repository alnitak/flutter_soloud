#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../enums.h"

/// Abstract interface for mixer output encoders.
/// Implementations receive raw interleaved PCM float samples and produce
/// encoded output bytes.
class MixerOutputEncoder {
 public:
  virtual ~MixerOutputEncoder() = default;

  /// Initialize the encoder with the given PCM format.
  /// [sampleRate] input sample rate in Hz.
  /// [channels] input channel count.
  /// Returns true on success.
  virtual bool initialize(int sampleRate, int channels) = 0;

  /// Encode a block of interleaved float PCM samples.
  /// [input] interleaved float samples in [-1.0, 1.0].
  /// [samples] total number of samples (frames * channels).
  /// [output] encoded bytes are appended to this vector.
  /// Returns true on success.
  virtual bool encode(const float *input, size_t samples,
                      std::vector<uint8_t> &output) = 0;

  /// Flush any remaining encoded data and finalize the stream.
  /// [output] encoded bytes are appended to this vector.
  /// Returns true on success.
  virtual bool finalize(std::vector<uint8_t> &output) = 0;

  /// Write the current WAV header into [output]. Only valid for WAV encoders;
  /// other implementations should leave [output] unchanged.
  virtual bool writeCurrentHeader(std::vector<uint8_t> &output) {
    output.clear();
    return false;
  }

  /// Total number of payload bytes emitted so far. Meaningful for WAV only.
  virtual size_t totalPayloadBytes() const { return 0; }

  /// Bytes per sample of the input PCM expected by this encoder.
  virtual size_t inputBytesPerSample() const = 0;

  /// Create an encoder for the given format.
  /// Returns nullptr if the format is unsupported or if Xiph libs are not
  /// available.
  static MixerOutputEncoder *create(MixerOutputFormat format);
};

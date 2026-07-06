#pragma once

#include <cstddef>
#include <cstdint>

#include "../enums.h"

/// Convert interleaved float samples to the requested PCM format.
/// [input] interleaved float samples in the range [-1.0, 1.0].
/// [output] destination buffer large enough for samples * bytesPerSample.
/// [samples] total number of samples (frames * channels).
/// [format] target PCM format.
void convertPcm(const float *input, uint8_t *output, size_t samples,
                MixerOutputFormat format);

/// Bytes per sample for the given PCM format.
/// Returns 0 for compressed formats.
size_t bytesPerSample(MixerOutputFormat format);

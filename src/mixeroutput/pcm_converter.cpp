#include "pcm_converter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void convertPcm(const float *input, uint8_t *output, size_t samples,
                MixerOutputFormat format) {
  switch (format) {
    case MIXER_OUTPUT_PCM_F32LE:
      std::memcpy(output, input, samples * sizeof(float));
      break;

    case MIXER_OUTPUT_PCM_S8: {
      auto *out = reinterpret_cast<int8_t *>(output);
      for (size_t i = 0; i < samples; ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, input[i]));
        out[i] = static_cast<int8_t>(clamped * 127.0f);
      }
      break;
    }

    case MIXER_OUTPUT_PCM_S16LE: {
      auto *out = reinterpret_cast<int16_t *>(output);
      for (size_t i = 0; i < samples; ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, input[i]));
        out[i] = static_cast<int16_t>(clamped * 32767.0f);
      }
      break;
    }

    case MIXER_OUTPUT_PCM_S32LE: {
      auto *out = reinterpret_cast<int32_t *>(output);
      for (size_t i = 0; i < samples; ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, input[i]));
        out[i] = static_cast<int32_t>(clamped * 2147483647.0f);
      }
      break;
    }

    default:
      // Compressed formats are not handled here.
      break;
  }
}

size_t bytesPerSample(MixerOutputFormat format) {
  switch (format) {
    case MIXER_OUTPUT_PCM_F32LE:
      return 4;
    case MIXER_OUTPUT_PCM_S8:
      return 1;
    case MIXER_OUTPUT_PCM_S16LE:
      return 2;
    case MIXER_OUTPUT_PCM_S32LE:
      return 4;
    default:
      return 0;
  }
}

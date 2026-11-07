#include "pcm_converter.h"

#include <algorithm>
#include <cstring>

#include "../simd/simd_pcm.h"

void convertPcm(const float *input, uint8_t *output, size_t samples,
                MixerOutputFormat format) {
  if (samples == 0 || input == nullptr || output == nullptr) {
    return;
  }

  switch (format) {
    case MIXER_OUTPUT_PCM_F32LE:
      std::memcpy(output, input, samples * sizeof(float));
      break;

    case MIXER_OUTPUT_PCM_S8: {
      auto *out = reinterpret_cast<int8_t *>(output);
      size_t done = 0;
      while (done < samples) {
        const unsigned int chunk = static_cast<unsigned int>(
            (samples - done) > 0x40000000u ? 0x40000000u : (samples - done));
        SoLoud::convertFloatsToS8Clamped(input + done, out + done, chunk);
        done += chunk;
      }
      break;
    }

    case MIXER_OUTPUT_PCM_S16LE: {
      if ((reinterpret_cast<uintptr_t>(output) & 1u) == 0u) {
        auto *out = reinterpret_cast<int16_t *>(output);
        size_t done = 0;
        while (done < samples) {
          const unsigned int chunk = static_cast<unsigned int>(
              (samples - done) > 0x40000000u ? 0x40000000u : (samples - done));
          SoLoud::convertFloatsToInt16Clamped(input + done, out + done, chunk);
          done += chunk;
        }
      } else {
        alignas(16) int16_t tmp[256];
        size_t done = 0;
        while (done < samples) {
          const unsigned int chunk = static_cast<unsigned int>(
              std::min<size_t>(256, samples - done));
          SoLoud::convertFloatsToInt16Clamped(input + done, tmp, chunk);
          std::memcpy(output + done * sizeof(int16_t), tmp,
                      chunk * sizeof(int16_t));
          done += chunk;
        }
      }
      break;
    }

    case MIXER_OUTPUT_PCM_S32LE: {
      if ((reinterpret_cast<uintptr_t>(output) & 3u) == 0u) {
        auto *out = reinterpret_cast<int32_t *>(output);
        size_t done = 0;
        while (done < samples) {
          const unsigned int chunk = static_cast<unsigned int>(
              (samples - done) > 0x40000000u ? 0x40000000u : (samples - done));
          SoLoud::convertFloatsToInt32Clamped(input + done, out + done, chunk);
          done += chunk;
        }
      } else {
        alignas(16) int32_t tmp[256];
        size_t done = 0;
        while (done < samples) {
          const unsigned int chunk = static_cast<unsigned int>(
              std::min<size_t>(256, samples - done));
          SoLoud::convertFloatsToInt32Clamped(input + done, tmp, chunk);
          std::memcpy(output + done * sizeof(int32_t), tmp,
                      chunk * sizeof(int32_t));
          done += chunk;
        }
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

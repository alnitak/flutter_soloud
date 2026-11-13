#include "wav_output_encoder.h"

#include <algorithm>
#include <cstring>

#include "../simd/simd_pcm.h"

bool WavOutputEncoder::initialize(int sampleRate, int channels) {
  m_sampleRate = sampleRate;
  m_channels = channels;
  m_totalSamples = 0;
  m_inputBuffer.clear();
  m_initialized = true;

  // Emit the header immediately so the stream starts with a valid-ish WAV
  // header. The size fields will be patched when the caller stops capture.
  return true;
}

bool WavOutputEncoder::encode(const float *input, size_t samples,
                              std::vector<uint8_t> &output) {
  if (!m_initialized || input == nullptr || samples == 0) {
    return false;
  }

  m_totalSamples += samples;
  output.resize(samples * sizeof(int16_t));

  if ((reinterpret_cast<uintptr_t>(output.data()) & 1u) == 0u) {
    auto *dst = reinterpret_cast<int16_t *>(output.data());
    size_t done = 0;
    while (done < samples) {
      const unsigned int chunk = static_cast<unsigned int>(
          (samples - done) > 0x40000000u ? 0x40000000u : (samples - done));
      SoLoud::convertFloatsToInt16Clamped(input + done, dst + done, chunk);
      done += chunk;
    }
  } else {
    m_inputBuffer.resize(samples);
    size_t done = 0;
    while (done < samples) {
      const unsigned int chunk = static_cast<unsigned int>(
          (samples - done) > 0x40000000u ? 0x40000000u : (samples - done));
      SoLoud::convertFloatsToInt16Clamped(input + done,
                                          m_inputBuffer.data() + done, chunk);
      done += chunk;
    }
    std::memcpy(output.data(), m_inputBuffer.data(), output.size());
  }
  return true;
}

bool WavOutputEncoder::finalize(std::vector<uint8_t> &output) {
  if (!m_initialized) {
    return false;
  }

  // Nothing to emit at finalize; the stream already received the header and
  // all PCM chunks. The caller should request the updated header separately.
  output.clear();
  return true;
}

void WavOutputEncoder::writeHeader(std::vector<uint8_t> &output,
                                   size_t totalPcmBytes) {
  const size_t pcmDataSize =
      totalPcmBytes == 0 ? m_totalSamples * sizeof(int16_t) : totalPcmBytes;
  buildHeader(output, pcmDataSize);
}

void WavOutputEncoder::buildHeader(std::vector<uint8_t> &output,
                                   size_t pcmDataSize) {
  const uint32_t byteRate = m_sampleRate * m_channels * sizeof(int16_t);
  const uint32_t dataSize = static_cast<uint32_t>(pcmDataSize);
  const uint32_t chunkSize = 36 + dataSize;
  const uint16_t blockAlign = static_cast<uint16_t>(m_channels * sizeof(int16_t));
  const uint16_t bitsPerSample = 16;
  const uint16_t audioFormat = 1;  // PCM
  const uint32_t subChunk1Size = 16;

  output.resize(44);
  uint8_t *ptr = output.data();

  // RIFF chunk descriptor
  std::memcpy(ptr, "RIFF", 4);
  ptr += 4;
  std::memcpy(ptr, &chunkSize, 4);
  ptr += 4;
  std::memcpy(ptr, "WAVE", 4);
  ptr += 4;

  // fmt sub-chunk
  std::memcpy(ptr, "fmt ", 4);
  ptr += 4;
  std::memcpy(ptr, &subChunk1Size, 4);
  ptr += 4;
  std::memcpy(ptr, &audioFormat, 2);
  ptr += 2;
  std::memcpy(ptr, &m_channels, 2);
  ptr += 2;
  std::memcpy(ptr, &m_sampleRate, 4);
  ptr += 4;
  std::memcpy(ptr, &byteRate, 4);
  ptr += 4;
  std::memcpy(ptr, &blockAlign, 2);
  ptr += 2;
  std::memcpy(ptr, &bitsPerSample, 2);
  ptr += 2;

  // data sub-chunk
  std::memcpy(ptr, "data", 4);
  ptr += 4;
  std::memcpy(ptr, &dataSize, 4);
}

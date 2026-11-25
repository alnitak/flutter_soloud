#include "wav_output_encoder.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "../simd/simd_pcm.h"

bool WavOutputEncoder::initialize(int sampleRate, int channels) {
  m_sampleRate = sampleRate;
  m_channels = channels;
  m_totalSamples = 0;
  m_inputBuffer.clear();
  m_initialized = true;

  // No bytes are emitted here: the caller (MixerOutput) fetches the 44-byte
  // header with writeCurrentHeader() and patches its size fields when the
  // capture stops.
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
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  // WAV PCM samples are little-endian; the conversion wrote host order.
  for (size_t i = 0; i + 1 < output.size(); i += 2) {
    std::swap(output[i], output[i + 1]);
  }
#endif
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

namespace {

// RIFF is little-endian whatever the host byte order.
void putLe16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

void putLe32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

}  // namespace

void WavOutputEncoder::buildHeader(std::vector<uint8_t> &output,
                                   size_t pcmDataSize) {
  const uint32_t byteRate =
      static_cast<uint32_t>(m_sampleRate) * static_cast<uint32_t>(m_channels) *
      static_cast<uint32_t>(sizeof(int16_t));
  // A RIFF chunk size is 32 bits, so a plain WAV file cannot describe more
  // than 4 GiB of audio. Past that (including payloads whose RIFF size
  // 36 + dataSize would overflow) both size fields are set to 0xFFFFFFFF,
  // the value commonly used for "unknown/streaming length": readers such as
  // FFmpeg and SoX then read the data chunk up to the end of the file
  // instead of stopping at a truncated (wrapped) size.
  constexpr uint32_t kUnknownSize = 0xFFFFFFFFu;
  constexpr uint32_t kHeaderTail = 36;  // RIFF chunk size minus data size
  uint32_t dataSize;
  uint32_t chunkSize;
  if (pcmDataSize > static_cast<size_t>(kUnknownSize - kHeaderTail)) {
    dataSize = kUnknownSize;
    chunkSize = kUnknownSize;
  } else {
    dataSize = static_cast<uint32_t>(pcmDataSize);
    chunkSize = kHeaderTail + dataSize;
  }
  const uint16_t blockAlign =
      static_cast<uint16_t>(m_channels * sizeof(int16_t));
  const uint16_t bitsPerSample = 16;
  const uint16_t audioFormat = 1;  // PCM
  const uint32_t subChunk1Size = 16;

  output.resize(44);
  uint8_t *ptr = output.data();

  // RIFF chunk descriptor
  std::memcpy(ptr, "RIFF", 4);
  putLe32(ptr + 4, chunkSize);
  std::memcpy(ptr + 8, "WAVE", 4);

  // fmt sub-chunk
  std::memcpy(ptr + 12, "fmt ", 4);
  putLe32(ptr + 16, subChunk1Size);
  putLe16(ptr + 20, audioFormat);
  putLe16(ptr + 22, static_cast<uint16_t>(m_channels));
  putLe32(ptr + 24, static_cast<uint32_t>(m_sampleRate));
  putLe32(ptr + 28, byteRate);
  putLe16(ptr + 32, blockAlign);
  putLe16(ptr + 34, bitsPerSample);

  // data sub-chunk
  std::memcpy(ptr + 36, "data", 4);
  putLe32(ptr + 40, dataSize);
}

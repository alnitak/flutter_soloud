#include "opus_output_encoder.h"

#include <algorithm>
#include <cstring>
#include <random>

#ifndef NO_XIPH_LIBS
#include <ogg/ogg.h>
#include <opus/opus.h>
#endif

namespace {

// Opus header and comment packets.
constexpr uint8_t kOpusHeadMagic[] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
constexpr uint8_t kOpusTagsMagic[] = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};

void writeLE16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeLE32(uint8_t *dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

}  // namespace

struct OpusOutputEncoder::OggState {
#ifndef NO_XIPH_LIBS
  ogg_stream_state stream;
  opus_int32 serial = 0;
#endif
};

OpusOutputEncoder::OpusOutputEncoder() = default;

OpusOutputEncoder::~OpusOutputEncoder() {
#ifndef NO_XIPH_LIBS
  if (m_encoder != nullptr) {
    opus_encoder_destroy(static_cast<OpusEncoder *>(m_encoder));
    m_encoder = nullptr;
  }
#endif
}

bool OpusOutputEncoder::initialize(int sampleRate, int channels) {
#ifndef NO_XIPH_LIBS
  if (m_initialized) {
    return true;
  }

  if (channels != 1 && channels != 2) {
    return false;
  }

  int application = OPUS_APPLICATION_AUDIO;
  int error = 0;
  OpusEncoder *encoder =
      opus_encoder_create(m_sampleRate, channels, application, &error);
  if (error != OPUS_OK || encoder == nullptr) {
    return false;
  }

  m_engineSampleRate = sampleRate;
  m_channels = channels;
  m_resamplerLastFrame.clear();
  m_resamplerPhase = 0.0;

  // Opus only supports 8/12/16/24/48 kHz. Always encode at 48 kHz and
  // resample the input when necessary.
  m_sampleRate = 48000;
  m_samplesPerFrame = m_sampleRate / 50;  // 20 ms frames.

  m_encoder = encoder;
  m_ogg = std::make_unique<OggState>();

#ifndef NO_XIPH_LIBS
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<opus_int32> dist(1, INT32_MAX);
  m_ogg->serial = dist(gen);
  ogg_stream_init(&m_ogg->stream, m_ogg->serial);

  // Write OpusHead packet.
  uint8_t head[19];
  std::memcpy(head, kOpusHeadMagic, 8);
  head[8] = 1;                        // version
  head[9] = static_cast<uint8_t>(channels);
  writeLE16(head + 10, 0);            // pre-skip
  writeLE32(head + 12, m_engineSampleRate);   // original sample rate
  writeLE16(head + 16, 0);            // output gain
  head[18] = 0;                       // channel mapping family (mono/stereo)

  ogg_packet headPacket;
  headPacket.packet = head;
  headPacket.bytes = sizeof(head);
  headPacket.b_o_s = 1;
  headPacket.e_o_s = 0;
  headPacket.granulepos = 0;
  headPacket.packetno = 0;
  ogg_stream_packetin(&m_ogg->stream, &headPacket);
  flushPages(true);

  // Write OpusTags packet.
  const char vendor[] = "flutter_soloud";
  const size_t vendorLen = sizeof(vendor) - 1;
  std::vector<uint8_t> tags(8 + 4 + vendorLen + 4);
  std::memcpy(tags.data(), kOpusTagsMagic, 8);
  writeLE32(tags.data() + 8, static_cast<uint32_t>(vendorLen));
  std::memcpy(tags.data() + 12, vendor, vendorLen);
  writeLE32(tags.data() + 12 + vendorLen, 0);  // user comment count

  ogg_packet tagsPacket;
  tagsPacket.packet = tags.data();
  tagsPacket.bytes = static_cast<long>(tags.size());
  tagsPacket.b_o_s = 0;
  tagsPacket.e_o_s = 0;
  tagsPacket.granulepos = 0;
  tagsPacket.packetno = 1;
  ogg_stream_packetin(&m_ogg->stream, &tagsPacket);
  flushPages(true);
#endif

  m_initialized = true;
  return true;
#else
  return false;
#endif
}

bool OpusOutputEncoder::encode(const float *input, size_t samples,
                               std::vector<uint8_t> &output) {
#ifndef NO_XIPH_LIBS
  if (!m_initialized || m_encoder == nullptr || input == nullptr ||
      samples == 0) {
    return false;
  }

  if (!resampleTo48k(input, samples)) {
    return false;
  }

  m_inputBuffer.insert(m_inputBuffer.end(), m_resampledBuffer.begin(),
                       m_resampledBuffer.end());

  const size_t frameSamples = m_samplesPerFrame * m_channels;
  std::vector<uint8_t> packet;
  packet.resize(1275);  // Max Opus packet size.

  while (m_inputBuffer.size() >= frameSamples) {
    opus_int32 encodedBytes = opus_encode_float(
        static_cast<OpusEncoder *>(m_encoder), m_inputBuffer.data(),
        static_cast<int>(m_samplesPerFrame), packet.data(),
        static_cast<opus_int32>(packet.size()));

    if (encodedBytes < 0) {
      return false;
    }

    ogg_packet op;
    op.packet = packet.data();
    op.bytes = encodedBytes;
    op.b_o_s = 0;
    op.e_o_s = 0;
    op.granulepos = m_packetGranulepos;
    op.packetno = 2 + m_packetCount;
    ogg_stream_packetin(&m_ogg->stream, &op);
    m_packetCount++;
    m_packetGranulepos += m_samplesPerFrame;

    flushPages();

    m_inputBuffer.erase(m_inputBuffer.begin(),
                        m_inputBuffer.begin() + frameSamples);
  }

  output.swap(m_outputAccumulator);
  m_outputAccumulator.clear();
  return true;
#else
  return false;
#endif
}

bool OpusOutputEncoder::finalize(std::vector<uint8_t> &output) {
#ifndef NO_XIPH_LIBS
  if (!m_initialized || m_encoder == nullptr) {
    return false;
  }

  // Encode any remaining samples with padding if necessary.
  const size_t frameSamples = m_samplesPerFrame * m_channels;
  if (!m_inputBuffer.empty()) {
    std::vector<float> padded(frameSamples, 0.0f);
    std::copy(m_inputBuffer.begin(), m_inputBuffer.end(), padded.begin());

    std::vector<uint8_t> packet;
    packet.resize(1275);
    opus_int32 encodedBytes = opus_encode_float(
        static_cast<OpusEncoder *>(m_encoder), padded.data(),
        static_cast<int>(m_samplesPerFrame), packet.data(),
        static_cast<opus_int32>(packet.size()));

    if (encodedBytes > 0) {
      ogg_packet op;
      op.packet = packet.data();
      op.bytes = encodedBytes;
      op.b_o_s = 0;
      op.e_o_s = 1;
      op.granulepos = m_packetGranulepos;
      op.packetno = 2 + m_packetCount;
      ogg_stream_packetin(&m_ogg->stream, &op);
      m_packetCount++;
      m_packetGranulepos += m_samplesPerFrame;
    }
  }

  ogg_page page;
  while (ogg_stream_flush(&m_ogg->stream, &page) != 0) {
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.header,
                               page.header + page.header_len);
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.body,
                               page.body + page.body_len);
  }

  ogg_stream_clear(&m_ogg->stream);

  opus_encoder_destroy(static_cast<OpusEncoder *>(m_encoder));
  m_encoder = nullptr;
  m_initialized = false;

  output.swap(m_outputAccumulator);
  m_outputAccumulator.clear();
  return true;
#else
  return false;
#endif
}

#ifndef NO_XIPH_LIBS
bool OpusOutputEncoder::resampleTo48k(const float *input, size_t samples) {
  if (m_engineSampleRate == m_sampleRate) {
    m_resampledBuffer.assign(input, input + samples);
    return true;
  }

  const double ratio =
      static_cast<double>(m_engineSampleRate) / static_cast<double>(m_sampleRate);

  // Combine the last frame from the previous chunk with the new input so
  // interpolation is seamless across chunk boundaries.
  std::vector<float> combined;
  combined.reserve(m_resamplerLastFrame.size() + samples);
  if (!m_resamplerLastFrame.empty()) {
    combined.insert(combined.end(), m_resamplerLastFrame.begin(),
                    m_resamplerLastFrame.end());
  }
  combined.insert(combined.end(), input, input + samples);

  const size_t combinedFrames = combined.size() / m_channels;
  if (combinedFrames < 2) {
    m_resamplerLastFrame.assign(input, input + samples);
    m_resampledBuffer.clear();
    return true;
  }

  m_resampledBuffer.clear();
  double phase = m_resamplerPhase;

  while (true) {
    const double inputFrame = phase;
    const size_t i0 = static_cast<size_t>(inputFrame);
    const size_t i1 = i0 + 1;
    if (i1 >= combinedFrames) {
      break;
    }

    const float frac = static_cast<float>(inputFrame - static_cast<double>(i0));
    const float invFrac = 1.0f - frac;
    for (int ch = 0; ch < m_channels; ++ch) {
      const float s0 = combined[i0 * m_channels + ch];
      const float s1 = combined[i1 * m_channels + ch];
      m_resampledBuffer.push_back(s0 * invFrac + s1 * frac);
    }
    phase += ratio;
  }

  const size_t consumedFrames = static_cast<size_t>(phase);
  if (consumedFrames > 0) {
    const size_t start = (consumedFrames - 1) * m_channels;
    m_resamplerLastFrame.assign(combined.begin() + start,
                                combined.begin() + start + m_channels);
  }
  m_resamplerPhase = phase - static_cast<double>(consumedFrames);

  return true;
}

void OpusOutputEncoder::flushPages(bool force) {
  ogg_page page;
  while (force ? ogg_stream_flush(&m_ogg->stream, &page) != 0
               : ogg_stream_pageout(&m_ogg->stream, &page) != 0) {
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.header,
                               page.header + page.header_len);
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.body,
                               page.body + page.body_len);
  }
}
#endif

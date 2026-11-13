#include "vorbis_output_encoder.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "../simd/simd_pcm.h"

#ifndef NO_XIPH_LIBS
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#endif

VorbisOutputEncoder::VorbisOutputEncoder() = default;

VorbisOutputEncoder::~VorbisOutputEncoder() {
#ifndef NO_XIPH_LIBS
  clearState();
#endif
}

#ifndef NO_XIPH_LIBS
void VorbisOutputEncoder::clearState() {
  if (m_vb != nullptr) {
    if (m_vbInited) {
      vorbis_block_clear(static_cast<vorbis_block *>(m_vb));
      m_vbInited = false;
    }
    delete static_cast<vorbis_block *>(m_vb);
    m_vb = nullptr;
  }
  if (m_vd != nullptr) {
    if (m_vdInited) {
      vorbis_dsp_clear(static_cast<vorbis_dsp_state *>(m_vd));
      m_vdInited = false;
    }
    delete static_cast<vorbis_dsp_state *>(m_vd);
    m_vd = nullptr;
  }
  if (m_vc != nullptr) {
    if (m_vcInited) {
      vorbis_comment_clear(static_cast<vorbis_comment *>(m_vc));
      m_vcInited = false;
    }
    delete static_cast<vorbis_comment *>(m_vc);
    m_vc = nullptr;
  }
  if (m_vi != nullptr) {
    if (m_viInited) {
      vorbis_info_clear(static_cast<vorbis_info *>(m_vi));
      m_viInited = false;
    }
    delete static_cast<vorbis_info *>(m_vi);
    m_vi = nullptr;
  }
  if (m_os != nullptr) {
    if (m_osInited) {
      ogg_stream_clear(static_cast<ogg_stream_state *>(m_os));
      m_osInited = false;
    }
    delete static_cast<ogg_stream_state *>(m_os);
    m_os = nullptr;
  }
  if (m_oy != nullptr) {
    if (m_oyInited) {
      ogg_sync_clear(static_cast<ogg_sync_state *>(m_oy));
      m_oyInited = false;
    }
    delete static_cast<ogg_sync_state *>(m_oy);
    m_oy = nullptr;
  }
}

bool VorbisOutputEncoder::writeHeaders() {
  auto *vd = static_cast<vorbis_dsp_state *>(m_vd);
  auto *vc = static_cast<vorbis_comment *>(m_vc);
  auto *os = static_cast<ogg_stream_state *>(m_os);

  ogg_packet op;
  ogg_packet op_comm;
  ogg_packet op_code;

  if (vorbis_analysis_headerout(vd, vc, &op, &op_comm, &op_code) != 0) {
    return false;
  }

  ogg_stream_packetin(os, &op);
  ogg_stream_packetin(os, &op_comm);
  ogg_stream_packetin(os, &op_code);

  ogg_page page;
  while (ogg_stream_flush(os, &page) != 0) {
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.header,
                               page.header + page.header_len);
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.body,
                               page.body + page.body_len);
  }

  return true;
}

bool VorbisOutputEncoder::writePacket(void *packet) {
  auto *os = static_cast<ogg_stream_state *>(m_os);
  ogg_stream_packetin(os, static_cast<ogg_packet *>(packet));

  ogg_page page;
  while (ogg_stream_pageout(os, &page) != 0) {
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.header,
                               page.header + page.header_len);
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.body,
                               page.body + page.body_len);
  }
  return true;
}

bool VorbisOutputEncoder::flushBlocks() {
  auto *vd = static_cast<vorbis_dsp_state *>(m_vd);
  auto *vb = static_cast<vorbis_block *>(m_vb);

  ogg_packet op;
  while (vorbis_analysis_blockout(vd, vb) == 1) {
    vorbis_analysis(vb, &op);
    vorbis_bitrate_addblock(vb);

    while (vorbis_bitrate_flushpacket(vd, &op) != 0) {
      if (!writePacket(&op)) {
        return false;
      }
    }
  }
  return true;
}
#endif

bool VorbisOutputEncoder::initialize(int sampleRate, int channels) {
#ifndef NO_XIPH_LIBS
  if (m_initialized) {
    return true;
  }

  m_sampleRate = sampleRate;
  m_channels = channels;
  m_outputAccumulator.clear();

  m_vi = new vorbis_info();
  auto *vi = static_cast<vorbis_info *>(m_vi);
  vorbis_info_init(vi);
  m_viInited = true;

  // Quality 0.4 gives ~128 kbps stereo.
  if (vorbis_encode_init_vbr(vi, channels, sampleRate, 0.4f) != 0) {
    clearState();
    return false;
  }

  m_vc = new vorbis_comment();
  auto *vc = static_cast<vorbis_comment *>(m_vc);
  vorbis_comment_init(vc);
  m_vcInited = true;
  vorbis_comment_add_tag(vc, const_cast<char *>("ENCODER"),
                         const_cast<char *>("flutter_soloud"));

  m_vd = new vorbis_dsp_state();
  auto *vd = static_cast<vorbis_dsp_state *>(m_vd);
  if (vorbis_analysis_init(vd, vi) != 0) {
    clearState();
    return false;
  }
  m_vdInited = true;

  m_vb = new vorbis_block();
  auto *vb = static_cast<vorbis_block *>(m_vb);
  if (vorbis_block_init(vd, vb) != 0) {
    clearState();
    return false;
  }
  m_vbInited = true;

  m_os = new ogg_stream_state();
  auto *os = static_cast<ogg_stream_state *>(m_os);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(1, INT32_MAX);
  if (ogg_stream_init(os, dist(gen)) != 0) {
    clearState();
    return false;
  }
  m_osInited = true;

  if (!writeHeaders()) {
    clearState();
    return false;
  }

  m_initialized = true;
  return true;
#else
  (void)sampleRate;
  (void)channels;
  return false;
#endif
}

bool VorbisOutputEncoder::encode(const float *input, size_t samples,
                                 std::vector<uint8_t> &output) {
#ifndef NO_XIPH_LIBS
  if (!m_initialized || input == nullptr || samples == 0) {
    return false;
  }

  auto *vd = static_cast<vorbis_dsp_state *>(m_vd);

  const size_t frames = samples / m_channels;
  if (frames == 0) {
    output.clear();
    return true;
  }

  float **buffer = vorbis_analysis_buffer(vd, static_cast<int>(frames));
  if (buffer == nullptr) {
    return false;
  }

  if (m_channels == 1) {
    std::memcpy(buffer[0], input, frames * sizeof(float));
  } else if (m_channels == 2) {
    size_t done = 0;
    while (done < frames) {
      const unsigned int chunk = static_cast<unsigned int>(
          (frames - done) > 0x40000000u ? 0x40000000u : (frames - done));
      SoLoud::deinterleaveStereoFloats(input + done * 2, buffer[0] + done,
                                       buffer[1] + done, chunk);
      done += chunk;
    }
  } else {
    for (size_t frame = 0; frame < frames; ++frame) {
      for (int ch = 0; ch < m_channels; ++ch) {
        buffer[ch][frame] = input[frame * m_channels + ch];
      }
    }
  }

  vorbis_analysis_wrote(vd, static_cast<int>(frames));

  if (!flushBlocks()) {
    return false;
  }

  output.swap(m_outputAccumulator);
  m_outputAccumulator.clear();
  return true;
#else
  (void)input;
  (void)samples;
  (void)output;
  return false;
#endif
}

bool VorbisOutputEncoder::finalize(std::vector<uint8_t> &output) {
#ifndef NO_XIPH_LIBS
  if (!m_initialized) {
    return false;
  }

  auto *vd = static_cast<vorbis_dsp_state *>(m_vd);
  auto *os = static_cast<ogg_stream_state *>(m_os);

  vorbis_analysis_wrote(vd, 0);

  if (!flushBlocks()) {
    return false;
  }

  ogg_page page;
  while (ogg_stream_flush(os, &page) != 0) {
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.header,
                               page.header + page.header_len);
    m_outputAccumulator.insert(m_outputAccumulator.end(), page.body,
                               page.body + page.body_len);
  }

  output.swap(m_outputAccumulator);
  m_outputAccumulator.clear();

  clearState();
  m_initialized = false;
  return true;
#else
  return false;
#endif
}

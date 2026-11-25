// Fixtures for the Ogg Opus and Ogg Vorbis stream decoder tests
// (test/common/opus_stream_decoder_test.cpp, vorbis_stream_decoder_test.cpp):
// a deterministic multi-channel test signal, Ogg Opus and Ogg Vorbis files
// encoded in memory with libopus / libvorbis / libogg, and reference decoders
// that demux the Ogg pages themselves and decode with the same libraries,
// independently of the wrappers under test.
#pragma once

#include <ogg/ogg.h>
#include <opus/opus.h>
#include <opus/opus_multistream.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace oggfx {

using Bytes = std::vector<unsigned char>;

/// [frames] frames of [channels] interleaved channels at [rate]: a different
/// sine per channel, plus a few clicks so that Vorbis switches to short
/// blocks. Values stay within +-0.6.
inline std::vector<float> testSignal(int channels, int rate, size_t frames,
                                     unsigned seed = 1) {
  std::vector<float> s(frames * channels);
  uint32_t lcg = seed * 2654435761u + 1;
  for (size_t i = 0; i < frames; ++i) {
    for (int c = 0; c < channels; ++c) {
      const double f = 220.0 * (c + 1) + 37.0 * seed;
      double v = 0.4 * std::sin(2 * M_PI * f * double(i) / rate);
      // a click every 0.25 s, 2 ms of noise
      if ((i % (size_t)(rate / 4)) < (size_t)(rate / 500)) {
        lcg = lcg * 1664525u + 1013904223u;
        v += 0.2 * (double((lcg >> 8) & 0xffff) / 32768.0 - 1.0);
      }
      s[i * channels + c] = (float)v;
    }
  }
  return s;
}

inline void writePage(Bytes &out, const ogg_page &og) {
  out.insert(out.end(), og.header, og.header + og.header_len);
  out.insert(out.end(), og.body, og.body + og.body_len);
}

inline void putLE16(Bytes &b, uint32_t v) {
  b.push_back(v & 0xff);
  b.push_back((v >> 8) & 0xff);
}
inline void putLE32(Bytes &b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff);
}

// ---------------------------------------------------------------- Opus ----

struct OpusOptions {
  int channels = 2;
  size_t frames = 48000;      ///< input frames at 48 kHz
  int serial = 1234;
  unsigned seed = 1;
  int extraPreSkip = 0;       ///< added to the encoder lookahead in OpusHead
  size_t commentBytes = 0;    ///< length of an extra LONGCOMMENT= value
  bool bigPages = false;      ///< pack up to 255 packets per audio page
  int bitrate = 0;            ///< 0 = libopus default
};

struct OpusFixture {
  Bytes file;
  int channels = 0;
  int preSkip = 0;
  int64_t finalGranule = 0;   ///< granule of the last page (48 kHz)
  std::map<std::string, std::string> comments;
  std::string vendor = "oggfx-test";
};

/// Encodes an Ogg Opus stream (RFC 7845): mapping family 0 for 1-2
/// channels, family 1 (Vorbis order, multistream) for 3-8.
inline bool encodeOpus(const OpusOptions &opt, OpusFixture &fx) {
  const int ch = opt.channels;
  const int family = ch <= 2 ? 0 : 1;
  int streams = 0, coupled = 0;
  unsigned char mapping[8] = {0};
  int err = 0;
  OpusMSEncoder *enc = opus_multistream_surround_encoder_create(
      48000, ch, family, &streams, &coupled, mapping,
      OPUS_APPLICATION_AUDIO, &err);
  if (err != OPUS_OK || !enc) return false;
  if (opt.bitrate > 0)
    opus_multistream_encoder_ctl(enc, OPUS_SET_BITRATE(opt.bitrate));
  opus_int32 lookahead = 0;
  opus_multistream_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));
  const int preSkip = lookahead + opt.extraPreSkip;

  fx = OpusFixture{};
  fx.channels = ch;
  fx.preSkip = preSkip;

  ogg_stream_state os;
  ogg_stream_init(&os, opt.serial);
  ogg_page og;
  ogg_packet op{};

  Bytes head = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, (unsigned char)ch};
  putLE16(head, preSkip);
  putLE32(head, 48000);
  putLE16(head, 0);
  head.push_back(family);
  if (family != 0) {
    head.push_back(streams);
    head.push_back(coupled);
    for (int i = 0; i < ch; ++i) head.push_back(mapping[i]);
  }
  op.packet = head.data();
  op.bytes = (long)head.size();
  op.b_o_s = 1;
  op.packetno = 0;
  ogg_stream_packetin(&os, &op);
  while (ogg_stream_flush(&os, &og)) writePage(fx.file, og);

  fx.comments["TITLE"] = "Opus fixture";
  fx.comments["ARTIST"] = "flutter_soloud tests";
  if (opt.commentBytes)
    fx.comments["LONGCOMMENT"] = std::string(opt.commentBytes, 'x');
  Bytes tags = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};
  putLE32(tags, (uint32_t)fx.vendor.size());
  tags.insert(tags.end(), fx.vendor.begin(), fx.vendor.end());
  putLE32(tags, (uint32_t)fx.comments.size());
  for (auto &kv : fx.comments) {
    const std::string c = kv.first + "=" + kv.second;
    putLE32(tags, (uint32_t)c.size());
    tags.insert(tags.end(), c.begin(), c.end());
  }
  op.packet = tags.data();
  op.bytes = (long)tags.size();
  op.b_o_s = 0;
  op.packetno = 1;
  ogg_stream_packetin(&os, &op);
  while (ogg_stream_flush(&os, &og)) writePage(fx.file, og);

  const std::vector<float> sig = testSignal(ch, 48000, opt.frames, opt.seed);
  const int frameSize = 960;
  std::vector<float> frame(frameSize * ch);
  unsigned char pkt[4000 * 8];
  int64_t packetno = 2;
  // The encoder delays its input by the lookahead: feed enough frames for
  // all the input plus the delay to come out, and pad with silence.
  const size_t needed = opt.frames + (size_t)preSkip;
  for (size_t pos = 0; pos < needed; pos += frameSize) {
    for (int i = 0; i < frameSize; ++i)
      for (int c = 0; c < ch; ++c) {
        const size_t idx = pos + i;
        frame[i * ch + c] = idx < opt.frames ? sig[idx * ch + c] : 0.0f;
      }
    const int n = opus_multistream_encode_float(enc, frame.data(), frameSize,
                                                pkt, sizeof(pkt));
    if (n < 0) {
      opus_multistream_encoder_destroy(enc);
      ogg_stream_clear(&os);
      return false;
    }
    const bool last = pos + frameSize >= needed;
    int64_t granule = (int64_t)(pos + frameSize);
    // The last granule says where the input ends (pre-skip included).
    if (last) granule = (int64_t)opt.frames + preSkip;
    op.packet = pkt;
    op.bytes = n;
    op.b_o_s = 0;
    op.e_o_s = last ? 1 : 0;
    op.granulepos = granule;
    op.packetno = packetno++;
    ogg_stream_packetin(&os, &op);
    if (opt.bigPages) {
      while (ogg_stream_pageout_fill(&os, &og, 1 << 20)) writePage(fx.file, og);
    } else {
      while (ogg_stream_pageout(&os, &og)) writePage(fx.file, og);
    }
    fx.finalGranule = granule;
  }
  while (ogg_stream_flush(&os, &og)) writePage(fx.file, og);
  ogg_stream_clear(&os);
  opus_multistream_encoder_destroy(enc);
  // With an extra pre-skip the decoder drops [extraPreSkip] more samples of
  // the start of the signal; the output still has [frames] frames.
  return true;
}

// -------------------------------------------------------------- Vorbis ----

struct VorbisOptions {
  int channels = 2;
  int rate = 44100;
  size_t frames = 44100;
  int serial = 5678;
  unsigned seed = 1;
  float quality = 0.4f;
  size_t commentBytes = 0;    ///< length of an extra LONGCOMMENT= value
  bool flushHeaders = true;   ///< false: comment/setup headers share pages
  bool bigPages = false;      ///< audio pages of up to 255 segments
};

struct VorbisFixture {
  Bytes file;
  int channels = 0;
  int rate = 0;
  int64_t finalGranule = 0;
  std::map<std::string, std::string> comments;
};

inline bool encodeVorbis(const VorbisOptions &opt, VorbisFixture &fx) {
  fx = VorbisFixture{};
  fx.channels = opt.channels;
  fx.rate = opt.rate;
  vorbis_info vi;
  vorbis_info_init(&vi);
  if (vorbis_encode_init_vbr(&vi, opt.channels, opt.rate, opt.quality) != 0) {
    vorbis_info_clear(&vi);
    return false;
  }
  vorbis_comment vc;
  vorbis_comment_init(&vc);
  fx.comments["TITLE"] = "Vorbis fixture";
  fx.comments["ARTIST"] = "flutter_soloud tests";
  if (opt.commentBytes)
    fx.comments["LONGCOMMENT"] = std::string(opt.commentBytes, 'y');
  for (auto &kv : fx.comments)
    vorbis_comment_add_tag(&vc, kv.first.c_str(), kv.second.c_str());
  vorbis_dsp_state vd;
  vorbis_block vb;
  vorbis_analysis_init(&vd, &vi);
  vorbis_block_init(&vd, &vb);
  ogg_stream_state os;
  ogg_stream_init(&os, opt.serial);
  ogg_page og;
  ogg_packet op;
  ogg_packet hdr, hdrComm, hdrCode;
  vorbis_analysis_headerout(&vd, &vc, &hdr, &hdrComm, &hdrCode);
  ogg_stream_packetin(&os, &hdr);
  while (ogg_stream_flush(&os, &og)) writePage(fx.file, og);
  ogg_stream_packetin(&os, &hdrComm);
  ogg_stream_packetin(&os, &hdrCode);
  if (opt.flushHeaders) {
    while (ogg_stream_flush(&os, &og)) writePage(fx.file, og);
  }

  const std::vector<float> sig =
      testSignal(opt.channels, opt.rate, opt.frames, opt.seed);
  const size_t block = 1024;
  size_t pos = 0;
  bool eos = false;
  while (!eos) {
    if (pos < opt.frames) {
      const size_t n = std::min(block, opt.frames - pos);
      float **buf = vorbis_analysis_buffer(&vd, (int)n);
      for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < opt.channels; ++c)
          buf[c][i] = sig[(pos + i) * opt.channels + c];
      vorbis_analysis_wrote(&vd, (int)n);
      pos += n;
    } else {
      vorbis_analysis_wrote(&vd, 0);
    }
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
      vorbis_analysis(&vb, nullptr);
      vorbis_bitrate_addblock(&vb);
      while (vorbis_bitrate_flushpacket(&vd, &op)) {
        ogg_stream_packetin(&os, &op);
        if (op.granulepos >= 0) fx.finalGranule = op.granulepos;
        while (!opt.bigPages && ogg_stream_pageout(&os, &og)) {
          writePage(fx.file, og);
          if (ogg_page_eos(&og)) eos = true;
        }
      }
    }
    // With [bigPages] all the audio packets are held back and flushed at
    // the end in pages of up to 255 segments.
    if (pos >= opt.frames && !eos && (!opt.bigPages || os.e_o_s)) {
      while (opt.bigPages ? ogg_stream_flush_fill(&os, &og, 1 << 20)
                          : ogg_stream_flush(&os, &og)) {
        writePage(fx.file, og);
        if (ogg_page_eos(&og)) eos = true;
      }
      if (!eos && os.e_o_s) eos = true;
    }
  }
  ogg_stream_clear(&os);
  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
  return true;
}

// ------------------------------------------------------ reference decode ----

/// One logical stream (link) of a decoded file, in its own channel count.
struct RefLink {
  int channels = 0;
  int rate = 0;
  std::vector<float> pcm;
  std::map<std::string, std::string> comments;
  std::string vendor;
};

/// Splits [file] into the packets of each logical stream, in order. Links
/// are sequential (chained) streams; multiplexed streams are not used by
/// the fixtures.
struct DemuxedLink {
  int serial = 0;
  std::vector<std::pair<Bytes, ogg_packet>> packets; // data + metadata
};

inline std::vector<DemuxedLink> demux(const Bytes &file) {
  std::vector<DemuxedLink> links;
  ogg_sync_state oy;
  ogg_sync_init(&oy);
  char *b = ogg_sync_buffer(&oy, (long)file.size());
  std::memcpy(b, file.data(), file.size());
  ogg_sync_wrote(&oy, (long)file.size());
  ogg_page og;
  ogg_stream_state os;
  bool have = false;
  while (ogg_sync_pageout(&oy, &og) == 1) {
    if (!have || ogg_page_serialno(&og) != os.serialno) {
      if (have) ogg_stream_clear(&os);
      ogg_stream_init(&os, ogg_page_serialno(&og));
      have = true;
      links.push_back({});
      links.back().serial = ogg_page_serialno(&og);
    }
    ogg_stream_pagein(&os, &og);
    ogg_packet op;
    while (ogg_stream_packetout(&os, &op) == 1) {
      Bytes data(op.packet, op.packet + op.bytes);
      links.back().packets.push_back({std::move(data), op});
    }
  }
  if (have) ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  for (auto &l : links)
    for (auto &p : l.packets) p.second.packet = p.first.data();
  return links;
}

inline void parseCommentList(const unsigned char *d, size_t n, size_t pos,
                             RefLink &link) {
  auto rd32 = [&](size_t p) {
    return (uint32_t)d[p] | ((uint32_t)d[p + 1] << 8) |
           ((uint32_t)d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24);
  };
  if (pos + 4 > n) return;
  uint32_t vl = rd32(pos);
  pos += 4;
  if (pos + vl > n) return;
  link.vendor.assign((const char *)d + pos, vl);
  pos += vl;
  if (pos + 4 > n) return;
  uint32_t count = rd32(pos);
  pos += 4;
  for (uint32_t i = 0; i < count && pos + 4 <= n; ++i) {
    uint32_t l = rd32(pos);
    pos += 4;
    if (pos + l > n) return;
    std::string c((const char *)d + pos, l);
    pos += l;
    const size_t eq = c.find('=');
    if (eq != std::string::npos) link.comments[c.substr(0, eq)] = c.substr(eq + 1);
  }
}

/// Reference Opus decode at 48 kHz in the stream's own channel count:
/// multistream decoder, pre-skip dropped, the end trimmed to the last
/// granule (RFC 7845, section 4).
inline std::vector<RefLink> referenceOpus(const Bytes &file) {
  std::vector<RefLink> out;
  for (auto &l : demux(file)) {
    RefLink link;
    if (l.packets.size() < 2) continue;
    const Bytes &h = l.packets[0].first;
    if (h.size() < 19 || std::memcmp(h.data(), "OpusHead", 8) != 0) continue;
    const int ch = h[9];
    const int preSkip = h[10] | (h[11] << 8);
    const int family = h[18];
    int streams = 1, coupled = ch > 1 ? 1 : 0;
    unsigned char mapping[255] = {0, 1};
    if (family != 0) {
      streams = h[19];
      coupled = h[20];
      for (int i = 0; i < ch; ++i) mapping[i] = h[21 + i];
    }
    link.channels = ch;
    link.rate = 48000;
    parseCommentList(l.packets[1].first.data(), l.packets[1].first.size(), 8,
                     link);
    int err = 0;
    OpusMSDecoder *dec = opus_multistream_decoder_create(
        48000, ch, streams, coupled, mapping, &err);
    if (!dec) continue;
    std::vector<float> buf(5760 * ch);
    std::vector<float> all;
    int64_t lastGranule = -1;
    for (size_t i = 2; i < l.packets.size(); ++i) {
      const ogg_packet &op = l.packets[i].second;
      const int n = opus_multistream_decode_float(
          dec, op.packet, (opus_int32)op.bytes, buf.data(), 5760, 0);
      if (n > 0) all.insert(all.end(), buf.begin(), buf.begin() + n * ch);
      if (op.granulepos >= 0) lastGranule = op.granulepos;
    }
    opus_multistream_decoder_destroy(dec);
    size_t start = (size_t)preSkip * ch;
    size_t end = all.size();
    // The last granule counts decoded samples, pre-skip included.
    if (lastGranule >= 0 && (size_t)lastGranule * ch < end)
      end = (size_t)lastGranule * ch;
    if (start < end) link.pcm.assign(all.begin() + start, all.begin() + end);
    out.push_back(std::move(link));
  }
  return out;
}

/// Reference Vorbis decode with libvorbis, the library the wrapper uses
/// (bit-exact apart from the wrapper's fades).
inline std::vector<RefLink> referenceVorbis(const Bytes &file) {
  std::vector<RefLink> out;
  for (auto &l : demux(file)) {
    RefLink link;
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    bool ok = l.packets.size() >= 3;
    for (int i = 0; ok && i < 3; ++i)
      ok = vorbis_synthesis_headerin(&vi, &vc, &l.packets[i].second) == 0;
    if (ok) {
      link.channels = vi.channels;
      link.rate = (int)vi.rate;
      if (vc.vendor) link.vendor = vc.vendor;
      for (int i = 0; i < vc.comments; ++i) {
        std::string c(vc.user_comments[i], vc.comment_lengths[i]);
        const size_t eq = c.find('=');
        if (eq != std::string::npos) link.comments[c.substr(0, eq)] = c.substr(eq + 1);
      }
      vorbis_dsp_state vd;
      vorbis_block vb;
      vorbis_synthesis_init(&vd, &vi);
      vorbis_block_init(&vd, &vb);
      for (size_t i = 3; i < l.packets.size(); ++i) {
        if (vorbis_synthesis(&vb, &l.packets[i].second) == 0)
          vorbis_synthesis_blockin(&vd, &vb);
        float **pcm;
        int n;
        while ((n = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
          for (int s = 0; s < n; ++s)
            for (int c = 0; c < vi.channels; ++c)
              link.pcm.push_back(pcm[c][s]);
          vorbis_synthesis_read(&vd, n);
        }
      }
      vorbis_block_clear(&vb);
      vorbis_dsp_clear(&vd);
    }
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    if (ok) out.push_back(std::move(link));
  }
  return out;
}

inline double rms(const std::vector<float> &v, size_t from = 0,
                  size_t to = (size_t)-1) {
  if (to > v.size()) to = v.size();
  if (from >= to) return 0;
  double s = 0;
  for (size_t i = from; i < to; ++i) s += double(v[i]) * v[i];
  return std::sqrt(s / double(to - from));
}

} // namespace oggfx

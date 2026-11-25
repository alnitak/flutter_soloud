// Fixtures and runners for fuzzing the metadata parsers, shared by
// test/common/metadata_parsers_fuzz_test.cpp (fixed seeds) and
// test/common/metadata_libfuzzer_test.cpp (libFuzzer).
//
// Parsers covered:
//   ID3       id3TextToUtf8() (src/audiobuffer/id3_text.h), the ID3v2 frame
//             parser of the AAC decoder (AACDecoderWrapper::parseId3Tags)
//             and of the MP3 decoder (MP3DecoderWrapper::on_meta, reached
//             through StreamDecoder)
//   M4A       parseM4aMetadata() (src/audiobuffer/m4a_metadata.cpp) and
//             parseM4aGaplessMoov() (src/native_stream/m4a_gapless.cpp)
//   ICY       stripIcyMetadataEx() (src/audiobuffer/icy_metadata.h), also
//             through StreamDecoder::setBufferIcyMetaInt()
//   Ogg       the OpusTags parser (OpusDecoderWrapper::getMetadata) and the
//             Vorbis comment header (VorbisDecoderWrapper, libvorbis)
//   FLAC      VORBIS_COMMENT and PICTURE blocks (FlacDecoderWrapper::
//             metadata_callback, libFLAC)
//   ADTS/AC-3 AACDecoderWrapper::parseAacAdtsMetadata/parseAc3Metadata/
//             parseEac3Metadata
#pragma once

#include "audiobuffer/aac_stream_decoder.h"
#include "audiobuffer/flac_stream_decoder.h"
#include "audiobuffer/icy_metadata.h"
#include "audiobuffer/id3_text.h"
#include "audiobuffer/m4a_metadata.h"
#include "audiobuffer/stream_decoder.h"
#include "native_stream/m4a_gapless.h"

#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>
#include <ogg/ogg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mfz {

using Bytes = std::vector<unsigned char>;

inline Bytes readFile(const std::string &path, size_t limit = SIZE_MAX) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  Bytes out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (out.size() > limit) out.resize(limit);
  return out;
}

inline void putBe32(Bytes &out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back((v >> s) & 0xFF);
}
inline void putLe32(Bytes &out, uint32_t v) {
  for (int s = 0; s < 32; s += 8) out.push_back((v >> s) & 0xFF);
}
inline void putText(Bytes &out, const std::string &s) { out.insert(out.end(), s.begin(), s.end()); }

/// True when [s] is well-formed UTF-8 (no overlongs, surrogates or > U+10FFFF).
inline bool validUtf8(const std::string &s) {
  const auto *p = reinterpret_cast<const unsigned char *>(s.data());
  size_t i = 0, n = s.size();
  while (i < n) {
    const unsigned char c = p[i];
    size_t len;
    uint32_t cp;
    if (c < 0x80) { ++i; continue; }
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return false;
    if (i + len > n) return false;
    for (size_t k = 1; k < len; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
        cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      return false;
    i += len;
  }
  return true;
}

// ------------------------------------------------------------------ ID3
struct Id3Frame {
  std::string id;
  Bytes body;
};

/// An ID3v2.[major] tag holding [frames], plus [padding] zero bytes.
inline Bytes id3Tag(uint8_t major, const std::vector<Id3Frame> &frames, size_t padding = 16,
                    bool extendedHeader = false) {
  Bytes body;
  if (extendedHeader) {
    if (major >= 4) body.insert(body.end(), {0, 0, 0, 6, 1, 0});  // synchsafe size 6
    else body.insert(body.end(), {0, 0, 0, 6, 0, 0, 0, 0, 0, 0}); // size 6 + flags/padding
  }
  for (const Id3Frame &f : frames) {
    putText(body, f.id);
    const uint32_t s = static_cast<uint32_t>(f.body.size());
    if (major >= 4) {
      body.insert(body.end(), {static_cast<unsigned char>((s >> 21) & 0x7F),
                               static_cast<unsigned char>((s >> 14) & 0x7F),
                               static_cast<unsigned char>((s >> 7) & 0x7F),
                               static_cast<unsigned char>(s & 0x7F)});
    } else {
      putBe32(body, s);
    }
    body.push_back(0);
    body.push_back(0);
    body.insert(body.end(), f.body.begin(), f.body.end());
  }
  body.resize(body.size() + padding, 0);
  Bytes tag = {'I', 'D', '3', major, 0, static_cast<unsigned char>(extendedHeader ? 0x40 : 0)};
  const uint32_t size = static_cast<uint32_t>(body.size());
  tag.push_back((size >> 21) & 0x7F);
  tag.push_back((size >> 14) & 0x7F);
  tag.push_back((size >> 7) & 0x7F);
  tag.push_back(size & 0x7F);
  tag.insert(tag.end(), body.begin(), body.end());
  return tag;
}

inline Bytes latin1(const std::string &s) {
  Bytes b = {0};
  putText(b, s);
  return b;
}

inline Bytes utf16le(const std::u16string &s, bool bom = true) {
  Bytes b = {1};
  if (bom) b.insert(b.end(), {0xFF, 0xFE});
  for (char16_t c : s) {
    b.push_back(c & 0xFF);
    b.push_back(c >> 8);
  }
  return b;
}

/// COMM: encoding, language, description, NUL (2 NULs for UTF-16), text.
inline Bytes commBody(uint8_t encoding, const Bytes &description, const Bytes &text) {
  Bytes b = {encoding, 'e', 'n', 'g'};
  b.insert(b.end(), description.begin(), description.end());
  b.push_back(0);
  if (encoding == 1 || encoding == 2) b.push_back(0);
  b.insert(b.end(), text.begin(), text.end());
  return b;
}

/// A tag with many frame kinds, in every text encoding.
inline Bytes richId3Tag(uint8_t major) {
  std::vector<Id3Frame> f = {
      {"TIT2", latin1("Caf\xE9 title")},
      {"TPE1", utf16le(u"Artist é\U0001F600")},
      {"TALB", {3, 'A', 'l', 'b', 0xC3, 0xA9}},
      {"TPE2", {2, 0, 'B', 0xD8, 0x3D, 0xDE, 0x00}},
      {"TCON", latin1("(17)Rock")},
      {"TRCK", latin1("3/12")},
      {"TPOS", latin1("1/2")},
      {"TCOM", utf16le(u"Composer")},
      {major >= 4 ? "TDRC" : "TYER", latin1("2024")},
      {"COMM", commBody(0, {}, {'c', 'o', 'm', 'm', 'e', 'n', 't'})},
      {"APIC", Bytes(300, 0x5A)},
      {"TXXX", latin1("desc\0value")},
  };
  return id3Tag(major, f, 32, major == 4);
}

/// Mono MPEG-1 layer III frames (128 kbit/s, 44.1 kHz) of silence-ish data,
/// enough for dr_mp3 to initialize and report the tag.
inline Bytes mp3Frames(size_t count) {
  Bytes frames(417 * count, 0);
  for (size_t i = 0; i < frames.size(); i += 417) {
    frames[i] = 0xFF;
    frames[i + 1] = 0xFB;
    frames[i + 2] = 0x90;
    frames[i + 3] = 0xC0;
  }
  return frames;
}

// ------------------------------------------------------------------ Ogg
/// The packets of the first logical stream of an Ogg file.
inline std::vector<Bytes> oggPackets(const Bytes &file, size_t maxPackets = 64) {
  std::vector<Bytes> packets;
  ogg_sync_state oy;
  ogg_sync_init(&oy);
  char *buf = ogg_sync_buffer(&oy, static_cast<long>(file.size()));
  std::memcpy(buf, file.data(), file.size());
  ogg_sync_wrote(&oy, static_cast<long>(file.size()));
  ogg_stream_state os;
  bool streamInit = false;
  ogg_page og;
  while (packets.size() < maxPackets && ogg_sync_pageout(&oy, &og) == 1) {
    if (!streamInit) {
      ogg_stream_init(&os, ogg_page_serialno(&og));
      streamInit = true;
    }
    if (ogg_page_serialno(&og) != os.serialno) continue;
    ogg_stream_pagein(&os, &og);
    ogg_packet op;
    while (packets.size() < maxPackets && ogg_stream_packetout(&os, &op) == 1)
      packets.emplace_back(op.packet, op.packet + op.bytes);
  }
  if (streamInit) ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  return packets;
}

/// Pages [packets] into an Ogg stream: each of the first [headers] packets
/// on its own page, then the rest with granule positions [granuleStep] apart.
inline Bytes oggStream(const std::vector<Bytes> &packets, size_t headers = 2,
                       int64_t granuleStep = 960, int serial = 0x5eed) {
  ogg_stream_state os;
  ogg_stream_init(&os, serial);
  Bytes out;
  auto drain = [&](bool flush) {
    ogg_page og;
    while (flush ? ogg_stream_flush(&os, &og) : ogg_stream_pageout(&os, &og)) {
      out.insert(out.end(), og.header, og.header + og.header_len);
      out.insert(out.end(), og.body, og.body + og.body_len);
    }
  };
  int64_t granule = 0;
  for (size_t i = 0; i < packets.size(); ++i) {
    ogg_packet op{};
    op.packet = const_cast<unsigned char *>(packets[i].data());
    op.bytes = static_cast<long>(packets[i].size());
    op.b_o_s = i == 0;
    op.e_o_s = i + 1 == packets.size();
    if (i >= headers) granule += granuleStep;
    op.granulepos = i < headers ? 0 : granule;
    op.packetno = static_cast<ogg_int64_t>(i);
    ogg_stream_packetin(&os, &op);
    drain(i < headers);
  }
  drain(true);
  ogg_stream_clear(&os);
  return out;
}

inline Bytes opusTags(const std::string &vendor, const std::vector<std::string> &comments) {
  Bytes t;
  putText(t, "OpusTags");
  putLe32(t, static_cast<uint32_t>(vendor.size()));
  putText(t, vendor);
  putLe32(t, static_cast<uint32_t>(comments.size()));
  for (const std::string &c : comments) {
    putLe32(t, static_cast<uint32_t>(c.size()));
    putText(t, c);
  }
  return t;
}

/// The Vorbis comment header packet (type 3).
inline Bytes vorbisComment(const std::string &vendor, const std::vector<std::string> &comments) {
  Bytes t = {3};
  putText(t, "vorbis");
  putLe32(t, static_cast<uint32_t>(vendor.size()));
  putText(t, vendor);
  putLe32(t, static_cast<uint32_t>(comments.size()));
  for (const std::string &c : comments) {
    putLe32(t, static_cast<uint32_t>(c.size()));
    putText(t, c);
  }
  t.push_back(1);  // framing bit
  return t;
}

// ----------------------------------------------------------------- FLAC
inline FLAC__StreamEncoderWriteStatus flacCollect(const FLAC__StreamEncoder *,
                                                  const FLAC__byte bytes[], size_t length,
                                                  unsigned, unsigned, void *opaque) {
  auto &out = *static_cast<Bytes *>(opaque);
  out.insert(out.end(), bytes, bytes + length);
  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

/// A short FLAC file with VORBIS_COMMENT and PICTURE blocks. [metadataEnd]
/// receives the offset of the first frame.
inline Bytes flacWithPicture(size_t &metadataEnd, bool ogg = false) {
  Bytes out;
  const unsigned frames = 4096;
  std::vector<FLAC__int32> pcm(frames);
  for (unsigned i = 0; i < frames; ++i)
    pcm[i] = static_cast<FLAC__int32>(std::lround(8000 * std::sin(i * 0.05)));
  FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
  if (!enc) return out;
  FLAC__stream_encoder_set_channels(enc, 1);
  FLAC__stream_encoder_set_bits_per_sample(enc, 16);
  FLAC__stream_encoder_set_sample_rate(enc, 44100);
  FLAC__stream_encoder_set_total_samples_estimate(enc, frames);
  FLAC__StreamMetadata *vc = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__StreamMetadata_VorbisComment_Entry entry;
  FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, "TITLE", "Picture test");
  FLAC__metadata_object_vorbiscomment_append_comment(vc, entry, false);
  FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, "ARTIST", "soloud");
  FLAC__metadata_object_vorbiscomment_append_comment(vc, entry, false);
  FLAC__StreamMetadata *pic = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE);
  pic->data.picture.type = FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER;
  FLAC__metadata_object_picture_set_mime_type(pic, const_cast<char *>("image/png"), true);
  FLAC__metadata_object_picture_set_description(
      pic, reinterpret_cast<FLAC__byte *>(const_cast<char *>("cover")), true);
  std::vector<FLAC__byte> image(700);
  for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<FLAC__byte>(i * 7);
  FLAC__metadata_object_picture_set_data(pic, image.data(), static_cast<FLAC__uint32>(image.size()), true);
  pic->data.picture.width = 16;
  pic->data.picture.height = 16;
  pic->data.picture.depth = 24;
  FLAC__StreamMetadata *meta[2] = {vc, pic};
  FLAC__stream_encoder_set_metadata(enc, meta, 2);
  const FLAC__StreamEncoderInitStatus st =
      ogg ? FLAC__stream_encoder_init_ogg_stream(enc, nullptr, flacCollect, nullptr, nullptr,
                                                 nullptr, &out)
          : FLAC__stream_encoder_init_stream(enc, flacCollect, nullptr, nullptr, nullptr, &out);
  bool ok = st == FLAC__STREAM_ENCODER_INIT_STATUS_OK &&
            FLAC__stream_encoder_process_interleaved(enc, pcm.data(), frames);
  if (ok) ok = FLAC__stream_encoder_finish(enc);
  FLAC__stream_encoder_delete(enc);
  FLAC__metadata_object_delete(vc);
  FLAC__metadata_object_delete(pic);
  if (!ok) out.clear();
  metadataEnd = 0;
  if (!ogg && out.size() > 8) {
    size_t p = 4;
    while (p + 4 <= out.size()) {
      const bool last = out[p] & 0x80;
      p += 4 + ((size_t(out[p + 1]) << 16) | (size_t(out[p + 2]) << 8) | out[p + 3]);
      if (last) break;
    }
    metadataEnd = std::min(p, out.size());
  }
  return out;
}

// -------------------------------------------------------------- runners
/// Everything a metadata report carries, touched the way the FFI copy
/// (convertMetadataToFFI) reads it.
inline size_t touch(const AudioMetadata &m) {
  size_t n = 0;
  for (const std::string *s :
       {&m.mp3Metadata.title, &m.mp3Metadata.artist, &m.mp3Metadata.albumArtist,
        &m.mp3Metadata.album, &m.mp3Metadata.date, &m.mp3Metadata.genre,
        &m.mp3Metadata.composer, &m.mp3Metadata.comment, &m.mp3Metadata.track,
        &m.mp3Metadata.disc, &m.mp3Metadata.streamUrl, &m.oggMetadata.vendor,
        &m.aacMetadata.title, &m.aacMetadata.comment, &m.m4aMetadata.title,
        &m.m4aMetadata.artist, &m.m4aMetadata.codec})
    for (char c : *s) n += static_cast<unsigned char>(c);
  for (const auto &kv : m.oggMetadata.comments) n += kv.first.size() + kv.second.size();
  return n;
}

struct StreamRun {
  int reports = 0;
  bool drainEnded = true;
  bool badOutput = false;
  int type = BUFFER_UNKNOWN;  // the wrapper StreamDecoder chose
  AudioMetadata last;
};

/// Feeds [data] to a StreamDecoder in pieces of [chunks] (cycled), as
/// BufferStream does, then drains. [icyMetaInt] > 0 sets ICY stripping.
inline StreamRun runStreamDecoder(const Bytes &data, const std::vector<size_t> &chunks,
                                  int icyMetaInt = 0) {
  StreamRun r;
  StreamDecoder sd;
  if (icyMetaInt > 0) sd.setBufferIcyMetaInt(icyMetaInt);
  sd.setTotalAudioSizeBytes(data.size());
  Bytes pending;
  bool failed = false;
  volatile size_t sink = 0;
  auto cb = [&](const AudioMetadata &m) {
    ++r.reports;
    sink = sink + touch(m);
    r.last = m;
  };
  auto run = [&](Bytes &buf, size_t cap) {
    int sr = 48000, ch = 2;
    auto res = sd.decode(buf, &sr, &ch, cb, cap);
    if (!res.first.empty() && (ch < 1 || ch > 255 || res.first.size() % size_t(ch)))
      r.badOutput = true;
    if (res.second != DecoderError::NoError) failed = true;
  };
  size_t off = 0, k = 0;
  while (off < data.size() && !failed) {
    const size_t want = chunks.empty() ? 4096 : std::max<size_t>(1, chunks[k++ % chunks.size()]);
    const size_t n = std::min(want, data.size() - off);
    pending.insert(pending.end(), data.begin() + off, data.begin() + off + n);
    off += n;
    if (pending.size() > 4096 || sd.hasPendingData()) run(pending, 0);
  }
  sd.setDataEnded();
  if (!failed) {
    run(pending, 4096 * 4);
    int i = 0;
    for (; i < 200 && !failed && sd.hasPendingData(); ++i) {
      Bytes none;
      run(none, 4096 * 4);
    }
    r.drainEnded = failed || !sd.hasPendingData();
  }
  r.type = sd.getWrapperType();
  return r;
}

/// Runs every pure parser on [data] (no decoding); returns false when an
/// output breaks an invariant, with the reason in [why].
inline bool runPureParsers(const Bytes &data, std::string &why) {
  const unsigned char *p = data.empty() ? nullptr : data.data();
  bool ok = true;
  {
    // id3TextToUtf8 needs at least the encoding byte to be readable.
    if (!data.empty()) {
      const std::string s = id3TextToUtf8(p, data.size());
      if ((data[0] == 0 || data[0] == 1 || data[0] == 2) && !validUtf8(s)) {
        ok = false;
        why = "id3TextToUtf8 (encoding " + std::to_string(data[0]) + ") gave invalid UTF-8";
      }
    }
  }
  {
    AacMetadata aac;
    size_t id3Size = 0;
    AACDecoderWrapper::parseId3Tags(p, data.size(), aac, id3Size);
    AacMetadata adts;
    AACDecoderWrapper::parseAacAdtsMetadata(p, data.size(), adts);
    Ac3Metadata ac3;
    AACDecoderWrapper::parseAc3Metadata(p, data.size(), ac3);
    Eac3Metadata eac3;
    AACDecoderWrapper::parseEac3Metadata(p, data.size(), eac3);
  }
  {
    M4aMetadata m4a;
    parseM4aMetadata(p, data.size(), m4a);
    M4aGaplessInfo info;
    parseM4aGaplessMoov(p, data.size(), info);
    uint64_t delay = 0, valid = 0;
    info.resolve(44100, delay, valid);
  }
  return ok;
}

/// Splits an ICY stream built from [audio] and [blocks] (one per [metaInt]
/// bytes) with stripIcyMetadataEx in pieces of [chunks], and compares with
/// what was put in. Returns false with [why] on a difference.
struct IcyCase {
  int metaInt = 16;
  Bytes audio;
  std::vector<Bytes> blocks;  // raw metadata payloads (padded to 16 here)
  std::vector<size_t> chunks;
};

inline Bytes icyStream(const IcyCase &c) {
  Bytes s;
  size_t a = 0, b = 0;
  while (a < c.audio.size()) {
    const size_t n = std::min<size_t>(c.metaInt, c.audio.size() - a);
    s.insert(s.end(), c.audio.begin() + a, c.audio.begin() + a + n);
    a += n;
    if (n < static_cast<size_t>(c.metaInt)) break;
    const Bytes &blk = b < c.blocks.size() ? c.blocks[b] : Bytes{};
    ++b;
    const size_t len16 = std::min<size_t>(255, (blk.size() + 15) / 16);
    s.push_back(static_cast<unsigned char>(len16));
    Bytes padded = blk;
    padded.resize(len16 * 16, 0);
    s.insert(s.end(), padded.begin(), padded.end());
  }
  return s;
}

/// The title and URL updates a correct reader reports for [c].
inline std::vector<std::pair<std::string, std::string>> icyExpected(const IcyCase &c) {
  std::vector<std::pair<std::string, std::string>> out;
  std::string title, url;
  const size_t complete = c.metaInt > 0 ? c.audio.size() / c.metaInt : 0;
  for (size_t b = 0; b < complete; ++b) {
    const Bytes blk = b < c.blocks.size() ? c.blocks[b] : Bytes{};
    const size_t len16 = std::min<size_t>(255, (blk.size() + 15) / 16);
    Bytes padded = blk;
    padded.resize(len16 * 16, 0);
    const std::string meta(padded.begin(), padded.end());
    std::string t = title, u = url;
    size_t pos = meta.find("StreamTitle='");
    if (pos != std::string::npos) {
      const size_t e = meta.find("';", pos + 13);
      if (e != std::string::npos) t = meta.substr(pos + 13, e - pos - 13);
    }
    pos = meta.find("StreamUrl='");
    if (pos != std::string::npos) {
      const size_t e = meta.find("';", pos + 11);
      if (e != std::string::npos) u = meta.substr(pos + 11, e - pos - 11);
    }
    if (t != title || u != url) {
      title = t;
      url = u;
      out.emplace_back(t, u);
    }
  }
  return out;
}

inline bool runIcy(const IcyCase &c, std::string &why) {
  const Bytes stream = icyStream(c);
  IcyStripState st;
  Bytes clean;
  std::vector<std::pair<std::string, std::string>> got;
  size_t off = 0, k = 0;
  while (off < stream.size()) {
    const size_t want = c.chunks.empty() ? stream.size() : std::max<size_t>(1, c.chunks[k++ % c.chunks.size()]);
    const size_t n = std::min(want, stream.size() - off);
    const Bytes piece(stream.begin() + off, stream.begin() + off + n);
    off += n;
    stripIcyMetadataEx(piece, c.metaInt, st, clean,
                       [&](const std::string &t, const std::string &u) { got.emplace_back(t, u); });
  }
  if (clean != c.audio) {
    why = "ICY: " + std::to_string(clean.size()) + " clean bytes, expected " +
          std::to_string(c.audio.size());
    return false;
  }
  if (got != icyExpected(c)) {
    why = "ICY: " + std::to_string(got.size()) + " title/url reports, expected " +
          std::to_string(icyExpected(c).size());
    return false;
  }
  return true;
}

} // namespace mfz

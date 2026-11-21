// Builders for synthetic MP4 / M4A files, shared by the MP4 parser tests
// (test/common/m4a_metadata_test.cpp, m4a_gapless_test.cpp,
// metadata_parsers_fuzz_test.cpp, metadata_libfuzzer_test.cpp).
//
// Each helper returns the bytes of one box (ISO/IEC 14496-12): a 32-bit
// big-endian size, a four-character type, the payload.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mp4fx {

using Bytes = std::vector<unsigned char>;

inline void be16(Bytes &out, uint32_t v) {
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

inline void be24(Bytes &out, uint32_t v) {
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

inline void be32(Bytes &out, uint32_t v) {
  out.push_back((v >> 24) & 0xFF);
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

inline void be64(Bytes &out, uint64_t v) {
  be32(out, static_cast<uint32_t>(v >> 32));
  be32(out, static_cast<uint32_t>(v));
}

inline void text(Bytes &out, const std::string &s) {
  out.insert(out.end(), s.begin(), s.end());
}

inline Bytes cat(Bytes a, const Bytes &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

template <typename... Rest>
inline Bytes cat(Bytes a, const Bytes &b, const Rest &...rest) {
  return cat(cat(std::move(a), b), rest...);
}

/// A box with a 32-bit size.
inline Bytes box(const char *type, const Bytes &payload) {
  Bytes out;
  be32(out, static_cast<uint32_t>(8 + payload.size()));
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

/// A box with the 64-bit "largesize" header (size field 1).
inline Bytes box64(const char *type, const Bytes &payload) {
  Bytes out;
  be32(out, 1);
  out.insert(out.end(), type, type + 4);
  be64(out, 16 + payload.size());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

/// A box whose size field holds [declared] whatever its payload.
inline Bytes rawBox(uint32_t declared, const char *type, const Bytes &payload) {
  Bytes out;
  be32(out, declared);
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

/// A FullBox: version, 24-bit flags, then the payload.
inline Bytes fullBox(const char *type, uint8_t version, uint32_t flags,
                     const Bytes &payload) {
  Bytes p;
  p.push_back(version);
  be24(p, flags);
  return box(type, cat(p, payload));
}

inline Bytes ftyp() {
  Bytes p;
  text(p, "M4A ");
  be32(p, 0);
  text(p, "M4A isommp42");
  return box("ftyp", p);
}

/// An iTunes 'data' atom: type (1 = UTF-8, 0/21 = integer), locale, value.
inline Bytes dataAtom(uint32_t type, const Bytes &value) {
  Bytes p;
  be32(p, type);
  be32(p, 0);
  return box("data", cat(p, value));
}

inline Bytes textItem(const char *fourcc, const std::string &value) {
  Bytes v;
  text(v, value);
  return box(fourcc, dataAtom(1, v));
}

/// trkn / disk: reserved(2), number(2), total(2) [, reserved(2)].
inline Bytes numberPairItem(const char *fourcc, uint16_t number, uint16_t total) {
  Bytes v;
  be16(v, 0);
  be16(v, number);
  be16(v, total);
  be16(v, 0);
  return box(fourcc, dataAtom(0, v));
}

inline Bytes gnreItem(uint16_t id) {
  Bytes v;
  be16(v, id);
  return box("gnre", dataAtom(0, v));
}

/// An iTunes freeform ('----') item: mean, name, data.
inline Bytes freeformItem(const std::string &name, const std::string &value) {
  Bytes mean = {0, 0, 0, 0};
  text(mean, "com.apple.iTunes");
  Bytes nm = {0, 0, 0, 0};
  text(nm, name);
  Bytes v;
  text(v, value);
  return box("----", cat(box("mean", mean), box("name", nm), dataAtom(1, v)));
}

/// udta/meta(FullBox)/hdlr + ilst.
inline Bytes udtaWithIlst(const Bytes &items, bool fullBoxMeta = true) {
  Bytes hdlrPayload = {0, 0, 0, 0};
  text(hdlrPayload, "mdirappl");
  hdlrPayload.resize(hdlrPayload.size() + 9, 0);
  Bytes metaPayload;
  if (fullBoxMeta) be32(metaPayload, 0);
  metaPayload = cat(metaPayload, fullBox("hdlr", 0, 0, hdlrPayload), box("ilst", items));
  return box("udta", box("meta", metaPayload));
}

/// The parameters of an audio track.
struct AudioTrack {
  uint16_t channels = 2;
  uint32_t sampleRate = 44100;
  uint32_t mediaTimescale = 44100;
  uint64_t mediaDuration = 0;   // ticks; 0 = frames * 1024
  bool mdhdV1 = false;
  uint32_t maxBitrate = 160000;
  uint32_t avgBitrate = 128000;
  bool esdsLongLengths = true;  // 0x80 0x80 0x80 nn descriptor lengths
  // Edit list: one empty edit first when [emptyEdit], then (mediaTime,
  // duration). No edts when [hasEdit] is false.
  bool hasEdit = false;
  bool emptyEdit = false;
  bool elstV1 = false;
  int64_t editMediaTime = 2112;
  uint64_t editDuration = 0;
  // The sample table. Frames are put in [chunks] chunks of equal size (the
  // last takes the rest).
  std::vector<uint32_t> frameSizes;
  uint32_t chunks = 1;
  uint32_t sttsDelta = 1024;
  const char *handler = "soun";
};

/// Descriptor length, in the 4-byte form muxers write, or 1 byte.
inline void odLength(Bytes &out, uint32_t length, bool longForm) {
  if (longForm) {
    out.push_back(0x80 | ((length >> 21) & 0x7F));
    out.push_back(0x80 | ((length >> 14) & 0x7F));
    out.push_back(0x80 | ((length >> 7) & 0x7F));
  }
  out.push_back(length & 0x7F);
}

inline Bytes esds(const AudioTrack &t) {
  // DecoderSpecificInfo: AudioSpecificConfig for AAC-LC.
  Bytes dsi = {0x12, 0x10};
  Bytes dcd;
  dcd.push_back(0x40);  // objectTypeIndication: MPEG-4 audio
  dcd.push_back(0x15);  // streamType 5 (audio) << 2 | upstream 0 | 1
  be24(dcd, 0);         // bufferSizeDB
  be32(dcd, t.maxBitrate);
  be32(dcd, t.avgBitrate);
  dcd.push_back(0x05);
  odLength(dcd, static_cast<uint32_t>(dsi.size()), t.esdsLongLengths);
  dcd = cat(dcd, dsi);
  Bytes es;
  be16(es, 1);          // ES_ID
  es.push_back(0);      // flags
  es.push_back(0x04);
  odLength(es, static_cast<uint32_t>(dcd.size()), t.esdsLongLengths);
  es = cat(es, dcd);
  es.push_back(0x06);   // SLConfigDescriptor
  odLength(es, 1, t.esdsLongLengths);
  es.push_back(0x02);
  Bytes p;
  p.push_back(0x03);
  odLength(p, static_cast<uint32_t>(es.size()), t.esdsLongLengths);
  p = cat(p, es);
  return fullBox("esds", 0, 0, p);
}

inline Bytes mp4aEntry(const AudioTrack &t) {
  Bytes p(6, 0);        // reserved
  be16(p, 1);           // data_reference_index
  be32(p, 0);           // version, revision
  be32(p, 0);           // vendor
  be16(p, t.channels);
  be16(p, 16);          // sample size
  be16(p, 0);           // compression id
  be16(p, 0);           // packet size
  be32(p, t.sampleRate << 16);
  return box("mp4a", cat(p, esds(t)));
}

/// The 'stbl' of [t]; [mdatPayloadOffset] is the file offset of the first
/// frame.
inline Bytes stbl(const AudioTrack &t, uint64_t mdatPayloadOffset) {
  Bytes stsdP;
  be32(stsdP, 1);
  const Bytes stsd = fullBox("stsd", 0, 0, cat(stsdP, mp4aEntry(t)));

  const uint32_t frames = static_cast<uint32_t>(t.frameSizes.size());
  Bytes sttsP;
  be32(sttsP, 1);
  be32(sttsP, frames);
  be32(sttsP, t.sttsDelta);
  const Bytes stts = fullBox("stts", 0, 0, sttsP);

  const uint32_t chunks = t.chunks ? t.chunks : 1;
  const uint32_t perChunk = frames / chunks ? frames / chunks : 1;
  Bytes stscP;
  const uint32_t last = frames - perChunk * (chunks - 1);
  if (last == perChunk || chunks == 1) {
    be32(stscP, 1);
    be32(stscP, 1);
    be32(stscP, chunks == 1 ? frames : perChunk);
    be32(stscP, 1);
  } else {
    be32(stscP, 2);
    be32(stscP, 1);
    be32(stscP, perChunk);
    be32(stscP, 1);
    be32(stscP, chunks);
    be32(stscP, last);
    be32(stscP, 1);
  }
  const Bytes stsc = fullBox("stsc", 0, 0, stscP);

  Bytes stszP;
  be32(stszP, 0);
  be32(stszP, frames);
  for (uint32_t s : t.frameSizes) be32(stszP, s);
  const Bytes stsz = fullBox("stsz", 0, 0, stszP);

  Bytes stcoP;
  be32(stcoP, chunks);
  uint64_t offset = mdatPayloadOffset;
  for (uint32_t c = 0; c < chunks; ++c) {
    be32(stcoP, static_cast<uint32_t>(offset));
    const uint32_t first = c * perChunk;
    const uint32_t count = c + 1 == chunks ? frames - first : perChunk;
    for (uint32_t i = 0; i < count && first + i < frames; ++i)
      offset += t.frameSizes[first + i];
  }
  const Bytes stco = fullBox("stco", 0, 0, stcoP);
  return box("stbl", cat(stsd, stts, stsc, stsz, stco));
}

inline Bytes mvhd(uint32_t timescale, uint64_t duration) {
  Bytes p;
  be32(p, 0);  // creation
  be32(p, 0);  // modification
  be32(p, timescale);
  be32(p, static_cast<uint32_t>(duration));
  be32(p, 0x00010000);  // rate
  be16(p, 0x0100);      // volume
  p.resize(p.size() + 10, 0);
  const uint32_t matrix[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000};
  for (uint32_t m : matrix) be32(p, m);
  p.resize(p.size() + 24, 0);
  be32(p, 2);  // next track id
  return fullBox("mvhd", 0, 0, p);
}

inline uint64_t mediaDurationOf(const AudioTrack &t) {
  return t.mediaDuration ? t.mediaDuration
                         : static_cast<uint64_t>(t.frameSizes.size()) * t.sttsDelta;
}

inline Bytes trak(const AudioTrack &t, uint32_t movieTimescale, uint64_t mdatPayloadOffset) {
  Bytes tkhdP(76, 0);
  const Bytes tkhd = fullBox("tkhd", 0, 7, tkhdP);
  Bytes edts;
  if (t.hasEdit) {
    Bytes elstP;
    be32(elstP, t.emptyEdit ? 2 : 1);
    const uint64_t duration = t.editDuration;
    auto entry = [&](uint64_t d, int64_t mediaTime) {
      if (t.elstV1) {
        be64(elstP, d);
        be64(elstP, static_cast<uint64_t>(mediaTime));
      } else {
        be32(elstP, static_cast<uint32_t>(d));
        be32(elstP, static_cast<uint32_t>(mediaTime));
      }
      be32(elstP, 0x00010000);
    };
    if (t.emptyEdit) entry(1000, -1);
    entry(duration, t.editMediaTime);
    edts = box("edts", fullBox("elst", t.elstV1 ? 1 : 0, 0, elstP));
  }
  Bytes mdhdP;
  if (t.mdhdV1) {
    be64(mdhdP, 0);
    be64(mdhdP, 0);
    be32(mdhdP, t.mediaTimescale);
    be64(mdhdP, mediaDurationOf(t));
  } else {
    be32(mdhdP, 0);
    be32(mdhdP, 0);
    be32(mdhdP, t.mediaTimescale);
    be32(mdhdP, static_cast<uint32_t>(mediaDurationOf(t)));
  }
  be16(mdhdP, 0x55C4);  // "und"
  be16(mdhdP, 0);
  const Bytes mdhd = fullBox("mdhd", t.mdhdV1 ? 1 : 0, 0, mdhdP);
  Bytes hdlrP;
  be32(hdlrP, 0);
  text(hdlrP, t.handler);
  hdlrP.resize(hdlrP.size() + 12, 0);
  text(hdlrP, "Sound");
  hdlrP.push_back(0);
  const Bytes hdlr = fullBox("hdlr", 0, 0, hdlrP);
  Bytes smhdP(4, 0);
  const Bytes smhd = fullBox("smhd", 0, 0, smhdP);
  const Bytes minf = box("minf", cat(smhd, stbl(t, mdatPayloadOffset)));
  const Bytes mdia = box("mdia", cat(mdhd, hdlr, minf));
  (void)movieTimescale;
  return box("trak", cat(tkhd, edts, mdia));
}

/// A whole file: ftyp, moov (mvhd, trak, optional udta), mdat with frames
/// whose bytes are (frame index + byte index) & 0xFF.
struct Mp4File {
  Bytes bytes;
  uint64_t mdatPayloadOffset = 0;
  std::vector<uint64_t> frameOffsets;
};

inline Mp4File makeMp4(const AudioTrack &t, const Bytes &udta = {},
                       uint32_t movieTimescale = 1000) {
  Mp4File f;
  const Bytes head = ftyp();
  // The moov size does not depend on the chunk offsets' values, so build it
  // once to measure, then again with the real offset.
  const uint64_t movieDuration =
      static_cast<uint64_t>(mediaDurationOf(t)) * movieTimescale / (t.mediaTimescale ? t.mediaTimescale : 1);
  Bytes moov = box("moov", cat(mvhd(movieTimescale, movieDuration), trak(t, movieTimescale, 0), udta));
  f.mdatPayloadOffset = head.size() + moov.size() + 8;
  moov = box("moov", cat(mvhd(movieTimescale, movieDuration),
                         trak(t, movieTimescale, f.mdatPayloadOffset), udta));
  Bytes mdatP;
  uint64_t off = f.mdatPayloadOffset;
  for (size_t i = 0; i < t.frameSizes.size(); ++i) {
    f.frameOffsets.push_back(off);
    for (uint32_t b = 0; b < t.frameSizes[i]; ++b)
      mdatP.push_back(static_cast<unsigned char>((i + b) & 0xFF));
    off += t.frameSizes[i];
  }
  f.bytes = cat(head, moov, box("mdat", mdatP));
  return f;
}

/// Finds the first box of [type] anywhere in [data] by its type bytes, and
/// returns the offset of its size field, or SIZE_MAX.
inline size_t findBox(const Bytes &data, const char *type, size_t from = 0) {
  for (size_t i = from + 4; i + 4 <= data.size(); ++i)
    if (std::memcmp(data.data() + i, type, 4) == 0) return i - 4;
  return SIZE_MAX;
}

inline void patchBe32(Bytes &data, size_t at, uint32_t v) {
  data[at] = (v >> 24) & 0xFF;
  data[at + 1] = (v >> 16) & 0xFF;
  data[at + 2] = (v >> 8) & 0xFF;
  data[at + 3] = v & 0xFF;
}

inline uint32_t readBe32(const Bytes &data, size_t at) {
  return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) |
         (uint32_t(data[at + 2]) << 8) | uint32_t(data[at + 3]);
}

inline Bytes readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace mp4fx

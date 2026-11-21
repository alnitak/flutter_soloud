// Unit tests for the WAV stream decoder (src/audiobuffer/
// wav_stream_decoder.cpp, WavDecoderWrapper), driven through StreamDecoder
// the way BufferStream does and directly. The WAV files are built in memory
// by the test from known integer / float samples, and the expected output is
// computed from those samples with dr_wav's conversion formulas.
//
// Verifies:
//   formats      PCM 8/16/24/32-bit, IEEE float 32/64-bit and
//                WAVE_FORMAT_EXTENSIBLE (PCM 16, PCM 24, float), 1/2/6
//                channels, decode to the exact expected floats; duration
//   chunks       LIST/INFO, fact and an odd-sized chunk (with its pad byte)
//                before "data" are skipped
//   pieces       the file in pieces of 1, 3, 7, 333, 4096 and 65536 bytes,
//                through StreamDecoder and the wrapper alone, gives the same
//                samples (pieces that end inside a sample frame included)
//   datasize     a "data" chunk of length 0 and one of length 0xFFFFFFFF
//                (a stream whose length is not known): the latter must play
//                everything that arrives, also when it arrives in pieces
//   truncated    input cut in the header drains with no samples; cut in the
//                data (on and off a frame boundary) drains with a prefix of
//                the expected samples
//   bounded      maxOutputSamples gives the same samples as unbounded calls
//   seek         prepareForSeek(target interleaved sample, timeToByteOffset)
//                then the data from that offset: the output starts at the
//                target
//
// Needs no audio device and no assets. The script builds it with the whole
// stream decoder set (so it needs the Xiph libraries too, and skips without).
//
//   ./test/common/wav_stream_decoder_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/stream_decoder.h"
#include "audiobuffer/wav_stream_decoder.h"

#include "../support/stream_decoder_test_harness.h"

#include <cstdint>

using namespace sdh;

enum class Fmt { U8, S16, S24, S32, F32, F64 };

struct WavSpec {
  Fmt fmt = Fmt::S16;
  int channels = 2;
  int rate = 44100;
  size_t frames = 20000;
  bool extensible = false;
  bool extraChunks = false;     ///< LIST, fact, odd-sized chunk before data
  bool sizeOverride = false;    ///< write [dataSize] in the data header
  uint32_t dataSize = 0;
};

struct Wav {
  Bytes file;
  std::vector<float> expected; // interleaved
  size_t dataPos = 0;          // offset of the first data byte
  int bytesPerFrame = 0;
};

static int bitsOf(Fmt f) {
  switch (f) {
  case Fmt::U8: return 8;
  case Fmt::S16: return 16;
  case Fmt::S24: return 24;
  case Fmt::S32: return 32;
  case Fmt::F32: return 32;
  case Fmt::F64: return 64;
  }
  return 0;
}

static const char *nameOf(Fmt f) {
  switch (f) {
  case Fmt::U8: return "u8";
  case Fmt::S16: return "s16";
  case Fmt::S24: return "s24";
  case Fmt::S32: return "s32";
  case Fmt::F32: return "f32";
  case Fmt::F64: return "f64";
  }
  return "?";
}

static void le16(Bytes &b, uint32_t v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff); }
static void le32(Bytes &b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff); }
static void tag(Bytes &b, const char *t) { b.insert(b.end(), t, t + 4); }

static Wav makeWav(const WavSpec &s) {
  Wav w;
  const int bits = bitsOf(s.fmt);
  const bool isFloat = s.fmt == Fmt::F32 || s.fmt == Fmt::F64;
  const int bpf = bits / 8 * s.channels;
  w.bytesPerFrame = bpf;
  Bytes data;
  uint32_t lcg = 12345;
  for (size_t i = 0; i < s.frames; ++i)
    for (int c = 0; c < s.channels; ++c) {
      const double v = 0.7 * std::sin(2 * M_PI * (300.0 + 150 * c) * i / s.rate);
      lcg = lcg * 1664525u + 1013904223u;
      const double noise = ((lcg >> 9) & 0xff) / 2550.0; // stresses the low bits
      const double x = std::max(-1.0, std::min(0.999, v + noise));
      switch (s.fmt) {
      case Fmt::U8: {
        const int u = (int)std::lround(x * 127) + 128;
        data.push_back((unsigned char)u);
        w.expected.push_back((u - 128) * 0.0078125f);
        break;
      }
      case Fmt::S16: {
        const int16_t q = (int16_t)std::lround(x * 32767);
        le16(data, (uint16_t)q);
        w.expected.push_back(q * 0.000030517578125f);
        break;
      }
      case Fmt::S24: {
        const int32_t q = (int32_t)std::lround(x * 8388607);
        data.push_back(q & 0xff);
        data.push_back((q >> 8) & 0xff);
        data.push_back((q >> 16) & 0xff);
        w.expected.push_back((float)(q * 0.00000011920928955078125));
        break;
      }
      case Fmt::S32: {
        const int32_t q = (int32_t)std::llround(x * 2147483647.0);
        le32(data, (uint32_t)q);
        w.expected.push_back((float)(q / 2147483648.0));
        break;
      }
      case Fmt::F32: {
        const float f = (float)x;
        uint32_t u;
        std::memcpy(&u, &f, 4);
        le32(data, u);
        w.expected.push_back(f);
        break;
      }
      case Fmt::F64: {
        uint64_t u;
        std::memcpy(&u, &x, 8);
        le32(data, (uint32_t)u);
        le32(data, (uint32_t)(u >> 32));
        w.expected.push_back((float)x);
        break;
      }
      }
    }

  Bytes body; // everything after "RIFF" size
  tag(body, "WAVE");
  tag(body, "fmt ");
  const uint16_t formatTag = s.extensible ? 0xFFFE : (isFloat ? 3 : 1);
  le32(body, s.extensible ? 40 : (isFloat ? 18 : 16));
  le16(body, formatTag);
  le16(body, s.channels);
  le32(body, s.rate);
  le32(body, s.rate * bpf);
  le16(body, bpf);
  le16(body, bits);
  if (s.extensible) {
    le16(body, 22);
    le16(body, bits);                // valid bits
    le32(body, s.channels == 2 ? 3 : (s.channels == 6 ? 0x3F : 0)); // mask
    // KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT
    const unsigned char guid[16] = {(unsigned char)(isFloat ? 3 : 1), 0, 0, 0,
                                    0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38,
                                    0x9B, 0x71};
    body.insert(body.end(), guid, guid + 16);
  } else if (isFloat) {
    le16(body, 0);
  }
  if (s.extraChunks) {
    tag(body, "fact");
    le32(body, 4);
    le32(body, (uint32_t)s.frames);
    Bytes info;
    tag(info, "INFO");
    tag(info, "INAM");
    le32(info, 6);
    info.insert(info.end(), {'T', 'i', 't', 'l', 'e', 0});
    tag(body, "LIST");
    le32(body, (uint32_t)info.size());
    body.insert(body.end(), info.begin(), info.end());
    tag(body, "odd!");
    le32(body, 5);
    body.insert(body.end(), {1, 2, 3, 4, 5, 0}); // 5 bytes + pad byte
  }
  tag(body, "data");
  le32(body, s.sizeOverride ? s.dataSize : (uint32_t)data.size());
  w.dataPos = 8 + body.size();
  body.insert(body.end(), data.begin(), data.end());

  tag(w.file, "RIFF");
  le32(w.file, s.sizeOverride && s.dataSize == 0xFFFFFFFF ? 0xFFFFFFFF
                                                          : (uint32_t)body.size());
  w.file.insert(w.file.end(), body.begin(), body.end());
  return w;
}

static void testFormats() {
  std::printf("case: formats\n");
  struct C { Fmt fmt; int ch; int rate; bool ext; };
  const C cases[] = {
      {Fmt::U8, 1, 8000, false},   {Fmt::U8, 2, 22050, false},
      {Fmt::S16, 1, 44100, false}, {Fmt::S16, 2, 48000, false},
      {Fmt::S16, 6, 48000, false}, {Fmt::S24, 2, 96000, false},
      {Fmt::S32, 2, 44100, false}, {Fmt::F32, 1, 44100, false},
      {Fmt::F32, 2, 192000, false}, {Fmt::F64, 2, 44100, false},
      {Fmt::S16, 2, 44100, true},  {Fmt::S24, 6, 48000, true},
      {Fmt::F32, 2, 48000, true},
  };
  for (const C &c : cases) {
    WavSpec s;
    s.fmt = c.fmt; s.channels = c.ch; s.rate = c.rate; s.extensible = c.ext;
    Wav w = makeWav(s);
    Out o = decodeStream(w.file, w.file.size());
    const char *n = nameOf(c.fmt);
    CHECK(o.type == BUFFER_WAV && o.error == DecoderError::NoError,
          "%s %dch%s: type %d error %d", n, c.ch, c.ext ? " ext" : "", o.type,
          (int)o.error);
    CHECK(o.ended, "%s %dch%s: not drained", n, c.ch, c.ext ? " ext" : "");
    CHECK(o.sampleRate == c.rate && o.channels == c.ch,
          "%s %dch%s: %d Hz %d ch", n, c.ch, c.ext ? " ext" : "", o.sampleRate,
          o.channels);
    CHECK(o.pcm.size() == w.expected.size(), "%s %dch%s: %zu samples, want %zu",
          n, c.ch, c.ext ? " ext" : "", o.pcm.size(), w.expected.size());
    const long d = firstDiff(o.pcm, w.expected, 0,
                             std::min(o.pcm.size(), w.expected.size()));
    CHECK(d < 0, "%s %dch%s: sample %ld is %f, want %f", n, c.ch,
          c.ext ? " ext" : "", d, d >= 0 ? o.pcm[d] : 0.0,
          d >= 0 ? w.expected[d] : 0.0);
    CHECK(std::fabs(o.duration - double(s.frames) / c.rate) < 1e-9,
          "%s %dch%s: duration %f", n, c.ch, c.ext ? " ext" : "", o.duration);
    CHECK(o.meta.size() == 1 && o.meta[0].type == BUFFER_WAV,
          "%s: %zu metadata callbacks", n, o.meta.size());
  }
}

static void testChunks() {
  std::printf("case: chunks\n");
  for (Fmt f : {Fmt::S16, Fmt::S24, Fmt::F32}) {
    WavSpec s;
    s.fmt = f;
    s.extraChunks = true;
    Wav w = makeWav(s);
    Out o = decodeStream(w.file, w.file.size());
    CHECK(o.pcm.size() == w.expected.size(), "%s: %zu samples, want %zu",
          nameOf(f), o.pcm.size(), w.expected.size());
    const long d = firstDiff(o.pcm, w.expected, 0,
                             std::min(o.pcm.size(), w.expected.size()));
    CHECK(d < 0, "%s: differs at sample %ld", nameOf(f), d);
  }
}

static void testPieces() {
  std::printf("case: pieces\n");
  for (Fmt f : {Fmt::U8, Fmt::S16, Fmt::S24, Fmt::F32}) {
    WavSpec s;
    s.fmt = f;
    s.extraChunks = true;
    s.frames = 30000;
    Wav w = makeWav(s);
    for (size_t chunk : {(size_t)1, (size_t)3, (size_t)7, (size_t)333,
                         (size_t)4096, (size_t)65536}) {
      // StreamDecoder decodes once more than 4 KB are pending: pieces of
      // 4097+ bytes that end anywhere in a frame.
      Out o = decodeStream(w.file, chunk);
      const long d = firstDiff(o.pcm, w.expected, 0,
                               std::min(o.pcm.size(), w.expected.size()));
      // A frame that is only partly buffered must not be read in part:
      // drwav_read_raw would drop the partial frame's bytes.
      CHECK(o.ended && o.pcm.size() == w.expected.size() && d < 0,
            "%s chunk %zu (StreamDecoder): %zu samples (want %zu), "
            "differs at sample %ld",
            nameOf(f), chunk, o.pcm.size(), w.expected.size(), d);
    }
    // The wrapper alone, every piece decoded as it arrives.
    for (size_t chunk : {(size_t)1, (size_t)7, (size_t)4096}) {
      WavDecoderWrapper wr;
      Out o = decodeWrapper(wr, w.file, chunk);
      const long d = firstDiff(o.pcm, w.expected, 0,
                               std::min(o.pcm.size(), w.expected.size()));
      CHECK(o.ended && o.pcm.size() == w.expected.size() && d < 0,
            "%s wrapper chunk %zu: %zu samples (want %zu), differs at "
            "sample %ld",
            nameOf(f), chunk, o.pcm.size(), w.expected.size(), d);
    }
  }
  // Pieces that are whole frames (after a frame-aligned header) work.
  WavSpec s;
  s.fmt = Fmt::S16;
  s.channels = 2;
  Wav w = makeWav(s);
  WavDecoderWrapper wr;
  Out o;
  wr.initializeDecoder(48000, 2);
  Bytes head(w.file.begin(), w.file.begin() + w.dataPos);
  int sr = 0, ch = 0;
  auto r0 = wr.decode(head, &sr, &ch);
  collect(o, r0, sr, ch);
  for (size_t p = w.dataPos; p < w.file.size(); p += 4 * 37) {
    Bytes in(w.file.begin() + p, w.file.begin() + std::min(w.file.size(), p + 4 * 37));
    auto r = wr.decode(in, &sr, &ch);
    collect(o, r, sr, ch);
  }
  wr.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && wr.hasPendingData(); ++i) {
    Bytes none;
    auto r = wr.decode(none, &sr, &ch);
    collect(o, r, sr, ch);
  }
  const long d = firstDiff(o.pcm, w.expected, 0,
                           std::min(o.pcm.size(), w.expected.size()));
  CHECK(o.pcm.size() == w.expected.size() && d < 0,
        "frame-aligned pieces: %zu samples (want %zu), differs at %ld",
        o.pcm.size(), w.expected.size(), d);
}

static void testDataSize() {
  std::printf("case: datasize\n");
  {
    // An empty data chunk and nothing after it.
    WavSpec s;
    s.frames = 0;
    Wav w = makeWav(s);
    Out o = decodeStream(w.file, w.file.size());
    CHECK(o.ended && o.pcm.empty() && o.error == DecoderError::NoError,
          "empty data: ended %d, %zu samples, error %d", o.ended, o.pcm.size(),
          (int)o.error);
  }
  {
    // Length 0 in the header, data after it: no crash, the stream ends.
    WavSpec s;
    s.sizeOverride = true;
    s.dataSize = 0;
    Wav w = makeWav(s);
    Out o = decodeStream(w.file, 4096);
    CHECK(o.ended && o.error == DecoderError::NoError && o.finite,
          "data size 0: ended %d error %d", o.ended, (int)o.error);
    CHECK(o.pcm.size() <= w.expected.size(), "data size 0: %zu samples",
          o.pcm.size());
  }
  {
    // 0xFFFFFFFF: a stream of unknown length.
    WavSpec s;
    s.sizeOverride = true;
    s.dataSize = 0xFFFFFFFF;
    s.frames = 40000;
    Wav w = makeWav(s);
    Out whole = decodeStream(w.file, w.file.size());
    CHECK(whole.ended && whole.pcm.size() == w.expected.size(),
          "0xFFFFFFFF, one piece: %zu samples, want %zu", whole.pcm.size(),
          w.expected.size());
    // dr_wav replaces a 0xFFFFFFFF data size by the number of bytes it can
    // read at init time; a stream that arrives in pieces must not stop after
    // the first piece.
    WavDecoderWrapper wr;
    Out o = decodeWrapper(wr, w.file, 4096);
    CHECK(o.pcm.size() == w.expected.size(),
          "0xFFFFFFFF in 4096-byte pieces: %zu samples, want %zu", o.pcm.size(),
          w.expected.size());
  }
}

static void testTruncated() {
  std::printf("case: truncated\n");
  WavSpec s;
  s.extraChunks = true;
  Wav w = makeWav(s);
  for (size_t cut : {(size_t)20, w.dataPos - 3, w.dataPos}) {
    Bytes f(w.file.begin(), w.file.begin() + cut);
    Out o = decodeStream(f, f.size());
    CHECK(o.ended && o.pcm.empty(), "cut at %zu (header): ended %d, %zu samples",
          cut, o.ended, o.pcm.size());
  }
  for (size_t frames : {(size_t)1000, (size_t)7777}) {
    for (size_t extra : {(size_t)0, (size_t)3}) {
      const size_t cut = w.dataPos + frames * w.bytesPerFrame + extra;
      Bytes f(w.file.begin(), w.file.begin() + cut);
      Out o = decodeStream(f, f.size());
      CHECK(o.ended, "cut at %zu: not drained", cut);
      CHECK(o.pcm.size() == frames * s.channels, "cut at %zu: %zu samples, want %zu",
            cut, o.pcm.size(), frames * s.channels);
      const long d = firstDiff(o.pcm, w.expected, 0,
                               std::min(o.pcm.size(), w.expected.size()));
      CHECK(d < 0, "cut at %zu: differs at sample %ld", cut, d);
    }
  }
}

static void testBounded() {
  std::printf("case: bounded\n");
  WavSpec s;
  s.fmt = Fmt::S24;
  s.channels = 6;
  s.frames = 30000;
  Wav w = makeWav(s);
  for (size_t maxOut : {(size_t)600, (size_t)1000, (size_t)4096 * 4}) {
    WavDecoderWrapper wr;
    Out o = decodeWrapper(wr, w.file, w.file.size(), maxOut);
    bool capped = true;
    for (auto &c : o.calls) capped = capped && c.second <= maxOut;
    CHECK(capped, "max %zu: a call returned more samples", maxOut);
    const long d = firstDiff(o.pcm, w.expected, 0,
                             std::min(o.pcm.size(), w.expected.size()));
    CHECK(o.ended && o.pcm.size() == w.expected.size() && d < 0,
          "max %zu: %zu samples (want %zu), differs at %ld", maxOut,
          o.pcm.size(), w.expected.size(), d);
  }
}

static void testSeek() {
  std::printf("case: seek\n");
  WavSpec s;
  s.fmt = Fmt::S16;
  s.channels = 2;
  s.rate = 8000;
  s.frames = 8000 * 4;
  s.extraChunks = true;
  Wav w = makeWav(s);
  for (size_t piece : {w.file.size(), (size_t)2000}) {
    WavDecoderWrapper wr;
    Out first = decodeWrapper(wr, w.file, 4096);
    const double t = 1.0;
    CHECK(wr.canSeekToTime(t), "cannot seek");
    const uint64_t off = wr.timeToByteOffset(t);
    const size_t frame = (size_t)(t * s.rate);
    CHECK(off == w.dataPos + frame * w.bytesPerFrame, "offset %llu, want %zu",
          (unsigned long long)off, w.dataPos + frame * w.bytesPerFrame);
    // PullBufferStream passes seconds * rate * channels.
    wr.prepareForSeek((uint64_t)frame * s.channels, off);
    Out o;
    for (size_t p = off; p < w.file.size(); p += piece) {
      Bytes in(w.file.begin() + p, w.file.begin() + std::min(w.file.size(), p + piece));
      int sr = 0, ch = 0;
      auto r = wr.decode(in, &sr, &ch);
      collect(o, r, sr, ch);
    }
    wr.setDataEnded();
    for (int i = 0; i < kMaxDrainCalls && wr.hasPendingData(); ++i) {
      Bytes none;
      int sr = 0, ch = 0;
      auto r = wr.decode(none, &sr, &ch);
      collect(o, r, sr, ch);
    }
    const std::vector<float> want(w.expected.begin() + frame * s.channels,
                                  w.expected.end());
    const long d = firstDiff(o.pcm, want, 0, std::min(o.pcm.size(), want.size()));
    // The data fed after the seek already starts at the target, behind the
    // re-prepended header: the wrapper must not skip to the target again.
    CHECK(o.pcm.size() == want.size() && d < 0,
          "pieces of %zu: %zu samples after the seek (want %zu), differs "
          "at %ld",
          piece, o.pcm.size(), want.size(), d);
  }
}

int main(int argc, char **argv) {
  parseArgs(argc, argv);
  testFormats();
  testChunks();
  testPieces();
  testDataSize();
  testTruncated();
  testBounded();
  testSeek();
  return finish("wav_stream_decoder_test");
}

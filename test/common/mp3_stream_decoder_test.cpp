// Unit tests for the MP3 stream decoder (src/audiobuffer/mp3_stream_decoder.cpp,
// MP3DecoderWrapper), driven through StreamDecoder the way BufferStream does
// (the caller keeps undecoded bytes until the decoder consumes them) and
// directly. The reference for every comparison is dr_mp3 decoding the whole
// file from memory (drmp3_init_memory), the decoder the wrapper is built on.
//
// Verifies:
//   id3            files with an ID3v2 tag (a small one, one with the
//                  extended-header flag, a 100 KB one with cover art) are
//                  detected as BUFFER_MP3_WITH_ID3, decode to the reference
//                  samples and report the tag's title through the callback
//   xing           Xing (VBR) and Info (CBR) headers: getDuration() equals
//                  frames * samples-per-frame / rate from the header, for
//                  MPEG-1 and MPEG-2 (576 samples per frame); the header frame
//                  itself is not decoded as audio
//   fragmented     byte-by-byte input (and 7/333/4096-byte pieces) gives the
//                  same samples as the one-shot decode
//   gapless        the LAME encoder delay and padding are trimmed: the output
//                  has exactly the frames the LAME tag says, and a file
//                  encoded at test time from N frames (with lame, if
//                  installed) decodes to exactly N frames
//   truncated      input cut mid-frame drains and ends with a prefix of the
//                  reference
//
// Needs no audio device. Uses the MP3 files in example/assets/audio (path
// given by the script); prints SKIPPED if they are missing. The script builds
// the lame fixture when lame is installed; without it that sub-case is
// skipped.
//
//   ./test/common/mp3_stream_decoder_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/mp3_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"
#include "../support/line_buffered_stdout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static int gFailures = 0;
static int gChecks = 0;
static int gKnownBugs = 0;
static bool gStrict = false;

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      ++gFailures;                                                             \
      std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);           \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

// A check that fails because of a known bug in src/ (see where it is used).
// Reported, but fails the run only with --strict.
#define KNOWN_BUG(cond, ...)                                                   \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      if (gStrict) ++gFailures; else ++gKnownBugs;                             \
      std::printf("  %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",        \
                  __FILE__, __LINE__, #cond);                                  \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

static constexpr int kMaxDrainCalls = 1000;

static bool readFile(const std::string &path, Bytes &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>());
  return !out.empty();
}

struct Ref {
  std::vector<float> pcm;
  int sampleRate = 0, channels = 0;
};

static bool referenceDecode(const Bytes &file, Ref &ref) {
  drmp3 mp3;
  if (!drmp3_init_memory(&mp3, file.data(), file.size(), nullptr))
    return false;
  ref.sampleRate = (int)mp3.sampleRate;
  ref.channels = (int)mp3.channels;
  std::vector<float> buf(4096 * mp3.channels);
  for (;;) {
    drmp3_uint64 n = drmp3_read_pcm_frames_f32(&mp3, 4096, buf.data());
    if (n == 0) break;
    ref.pcm.insert(ref.pcm.end(), buf.begin(), buf.begin() + n * mp3.channels);
  }
  drmp3_uninit(&mp3);
  return true;
}

struct Out {
  std::vector<float> pcm;
  int sampleRate = 0, channels = 0;
  DecoderError error = DecoderError::NoError;
  bool ended = false;
  DetectedType type = BUFFER_UNKNOWN;
  double duration = -1;
  std::vector<AudioMetadata> meta;
};

// BufferStream model: bytes arrive in [chunk]-byte pieces and are appended to
// a pending buffer; StreamDecoder::decode() consumes it (clears it) once it
// has detected the format, and leaves it alone while it waits for more. As in
// BufferStream, the first decode happens once more than 4 KB are pending.
static Out decodeStream(const Bytes &file, size_t chunk) {
  Out o;
  StreamDecoder sd;
  sd.setTotalAudioSizeBytes(file.size());
  Bytes pending;
  auto cb = [&o](const AudioMetadata &m) { o.meta.push_back(m); };
  auto run = [&](Bytes &in) {
    int sr = 44100, ch = 2;
    auto r = sd.decode(in, &sr, &ch, cb);
    if (r.second != DecoderError::NoError && o.error == DecoderError::NoError)
      o.error = r.second;
    if (!r.first.empty()) {
      o.sampleRate = sr;
      o.channels = ch;
    }
    o.pcm.insert(o.pcm.end(), r.first.begin(), r.first.end());
  };
  for (size_t off = 0; off < file.size() && o.error == DecoderError::NoError;
       off += chunk) {
    pending.insert(pending.end(), file.begin() + off,
                   file.begin() + std::min(file.size(), off + chunk));
    // BufferStream::addData (audiobuffer.cpp:592) decodes once it holds more
    // than 4 KB, or whenever the decoder still has data queued.
    if (pending.size() > 4096 || sd.hasPendingData()) run(pending);
  }
  sd.setDataEnded();
  if (o.error == DecoderError::NoError) {
    run(pending); // what is still pending (BufferStream's final addData)
    for (int i = 0; i < kMaxDrainCalls && sd.hasPendingData(); ++i) {
      Bytes none;
      run(none);
    }
  }
  o.ended = !sd.hasPendingData();
  o.type = sd.getWrapperType();
  o.duration = sd.getDuration();
  return o;
}

// The wrapper alone, fed [chunk]-byte pieces from the first byte.
static Out decodeWrapper(const Bytes &file, size_t chunk, DetectedType type) {
  Out o;
  MP3DecoderWrapper w;
  w.detectedType = type;
  w.initializeDecoder(44100, 2);
  for (size_t off = 0; off < file.size(); off += chunk) {
    Bytes in(file.begin() + off,
             file.begin() + std::min(file.size(), off + chunk));
    auto r = w.decode(in, &o.sampleRate, &o.channels);
    o.pcm.insert(o.pcm.end(), r.first.begin(), r.first.end());
  }
  w.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
    Bytes none;
    auto r = w.decode(none, &o.sampleRate, &o.channels);
    o.pcm.insert(o.pcm.end(), r.first.begin(), r.first.end());
  }
  o.ended = !w.hasPendingData();
  o.duration = w.getDuration();
  return o;
}

static bool samePcm(const std::vector<float> &a, const std::vector<float> &b,
                    size_t n, size_t *bad = nullptr) {
  if (a.size() < n || b.size() < n) return false;
  for (size_t i = 0; i < n; ++i)
    if (a[i] != b[i]) {
      if (bad) *bad = i;
      return false;
    }
  return true;
}

// ---- header parsing helpers for the expectations -------------------------

static size_t id3Size(const Bytes &f) {
  if (f.size() < 10 || std::memcmp(f.data(), "ID3", 3) != 0) return 0;
  size_t s = ((f[6] & 0x7f) << 21) | ((f[7] & 0x7f) << 14) |
             ((f[8] & 0x7f) << 7) | (f[9] & 0x7f);
  return 10 + s + ((f[5] & 0x10) ? 10 : 0);
}

// The TIT2 text of an ID3v2.3/2.4 tag with a Latin-1/UTF-8 title, or "".
static std::string id3Title(const Bytes &f) {
  const size_t end = id3Size(f);
  for (size_t p = 10; p + 11 < end; ++p) {
    if (std::memcmp(&f[p], "TIT2", 4) != 0) continue;
    size_t sz = f[3] >= 4 ? ((f[p + 4] & 0x7f) << 21) | ((f[p + 5] & 0x7f) << 14) |
                                ((f[p + 6] & 0x7f) << 7) | (f[p + 7] & 0x7f)
                          : (size_t(f[p + 4]) << 24) | (f[p + 5] << 16) |
                                (f[p + 6] << 8) | f[p + 7];
    if (p + 10 + sz > end || sz < 2) return "";
    const unsigned char enc = f[p + 10];
    if (enc != 0 && enc != 3) return "";
    std::string s(reinterpret_cast<const char *>(&f[p + 11]), sz - 1);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
  }
  return "";
}

struct XingInfo {
  bool found = false;
  bool lame = false;
  uint32_t frames = 0;
  int samplesPerFrame = 0;
  int sampleRate = 0;
  int delay = 0, padding = 0; // from the LAME tag, as dr_mp3 applies them
};

static XingInfo parseXing(const Bytes &f) {
  XingInfo x;
  size_t p = id3Size(f);
  while (p + 4 < f.size() && !(f[p] == 0xff && (f[p + 1] & 0xe0) == 0xe0)) ++p;
  if (p + 4 >= f.size()) return x;
  const int version = (f[p + 1] >> 3) & 3; // 3 = MPEG-1
  const int sri = (f[p + 2] >> 2) & 3;
  const bool mono = ((f[p + 3] >> 6) & 3) == 3;
  static const int r1[3] = {44100, 48000, 32000};
  static const int r2[3] = {22050, 24000, 16000};
  static const int r25[3] = {11025, 12000, 8000};
  if (sri == 3) return x;
  x.sampleRate = version == 3 ? r1[sri] : version == 2 ? r2[sri] : r25[sri];
  x.samplesPerFrame = version == 3 ? 1152 : 576;
  const size_t side = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
  size_t t = p + 4 + side + ((f[p + 1] & 1) ? 0 : 2);
  if (t + 120 > f.size()) return x;
  if (std::memcmp(&f[t], "Xing", 4) && std::memcmp(&f[t], "Info", 4)) return x;
  x.found = true;
  const uint32_t flags = f[t + 7];
  size_t q = t + 8;
  if (flags & 1) {
    x.frames = (uint32_t(f[q]) << 24) | (f[q + 1] << 16) | (f[q + 2] << 8) | f[q + 3];
    q += 4;
  }
  if (flags & 2) q += 4;
  if (flags & 4) q += 100;
  if (flags & 8) q += 4;
  if (q + 24 <= f.size() && f[q] != 0) { // LAME extension
    x.lame = true;
    const size_t d = q + 21;
    x.delay = ((f[d] << 4) | (f[d + 1] >> 4)) + 529;
    x.padding = std::max(0, (((f[d + 1] & 0xf) << 8) | f[d + 2]) - 529);
  }
  return x;
}

// ---------------------------------------------------------------- cases

struct Asset {
  const char *name;
  Bytes file;
  Ref ref;
};

static void testId3(Asset &a) {
  std::printf("case: id3 %s (tag %zu bytes)\n", a.name, id3Size(a.file));
  // 32 KB pieces, like a network stream after BufferStream's 4 KB buffering.
  Out o = decodeStream(a.file, 32 * 1024);
  CHECK(o.error == DecoderError::NoError, "%s: error %d", a.name, (int)o.error);
  CHECK(o.type == BUFFER_MP3_WITH_ID3, "%s: type %d", a.name, (int)o.type);
  CHECK(o.ended, "%s: did not drain", a.name);
  CHECK(o.sampleRate == a.ref.sampleRate && o.channels == a.ref.channels,
        "%s: format %d/%d, want %d/%d", a.name, o.sampleRate, o.channels,
        a.ref.sampleRate, a.ref.channels);
  size_t bad = 0;
  CHECK(o.pcm.size() == a.ref.pcm.size(), "%s: %zu samples, want %zu", a.name,
        o.pcm.size(), a.ref.pcm.size());
  CHECK(samePcm(o.pcm, a.ref.pcm, std::min(o.pcm.size(), a.ref.pcm.size()),
                &bad),
        "%s: mismatch at %zu", a.name, bad);
  const std::string title = id3Title(a.file);
  if (!title.empty()) {
    bool seen = false;
    for (auto &m : o.meta) seen = seen || m.mp3Metadata.title == title;
    // Regression for S6: a tag with an extended header (flag 0x40, such as
    // IveSeenThings.mp3's 12-byte one) must still report its frames.
    CHECK(seen, "%s: title '%s' not reported%s", a.name, title.c_str(),
          (a.file[5] & 0x40) ? " (extended header)" : "");
  }
  bool rate = false;
  for (auto &m : o.meta)
    rate = rate || (m.mp3Metadata.sampleRate == a.ref.sampleRate &&
                    m.mp3Metadata.channels == a.ref.channels);
  CHECK(rate, "%s: no metadata with the stream format", a.name);
}

// A tag larger than the first deliveries (100 KB of cover art, fed in 32 KB
// pieces): StreamDecoder must wait for the rest of the tag and then decode
// the stream like a small-tag file.
//
// Regression for S4: while the tag was incomplete, detectAudioFormat() used
// to skip the ID3 branch and probe the cover-art bytes as raw audio (the AC-3
// probe matched inside the JPEG). Any MP3 whose ID3 tag is larger than what
// BufferStream holds at its first decode (> 4 KB) was affected.
static void testLargeId3(Asset &a) {
  std::printf("case: id3 tag larger than the first deliveries (%s, %zu)\n",
              a.name, id3Size(a.file));
  Out o = decodeStream(a.file, 32 * 1024);
  CHECK(o.ended, "did not drain");
  CHECK(o.error == DecoderError::NoError && o.type == BUFFER_MP3_WITH_ID3,
        "error %d, detected type %d", (int)o.error, (int)o.type);
  CHECK(o.pcm.size() == a.ref.pcm.size() &&
            samePcm(o.pcm, a.ref.pcm, o.pcm.size()),
        "%zu samples, want %zu", o.pcm.size(), a.ref.pcm.size());

  // The same file in one piece (tag complete at detection) works.
  std::printf("case: id3 large tag delivered whole (%s)\n", a.name);
  Out w = decodeStream(a.file, a.file.size());
  CHECK(w.error == DecoderError::NoError && w.type == BUFFER_MP3_WITH_ID3,
        "error %d, type %d", (int)w.error, (int)w.type);
  CHECK(w.ended, "did not drain");
  size_t bad = 0;
  CHECK(w.pcm.size() == a.ref.pcm.size(), "%zu samples, want %zu",
        w.pcm.size(), a.ref.pcm.size());
  CHECK(samePcm(w.pcm, a.ref.pcm, std::min(w.pcm.size(), a.ref.pcm.size()),
                &bad),
        "mismatch at %zu", bad);
}

// A first delivery shorter than one MPEG frame must wait for more data.
// Regression for S7: without an ID3 tag, a buffer that starts with a valid
// MPEG frame header but does not yet hold the whole frame used to be
// reported as BUFFER_UNKNOWN (FormatNotSupported).
static void testShortFirstDelivery(Asset &a) {
  std::printf("case: first delivery shorter than a frame (%s)\n", a.name);
  StreamDecoder sd;
  Bytes first(a.file.begin(), a.file.begin() + 100);
  int sr = 44100, ch = 2;
  auto r = sd.decode(first, &sr, &ch, nullptr);
  CHECK(r.second == DecoderError::NoError && first.size() == 100,
        "100 bytes: error %d", (int)r.second);
}

static void testXing(Asset &a) {
  XingInfo x = parseXing(a.file);
  if (!x.found || x.frames == 0) return;
  std::printf("case: xing/info %s (%u frames x %d @ %d Hz)\n", a.name,
              x.frames, x.samplesPerFrame, x.sampleRate);
  Out o = decodeStream(a.file, a.file.size());
  const double want = double(x.frames) * x.samplesPerFrame / x.sampleRate;
  CHECK(std::fabs(o.duration - want) < 1e-6, "%s: duration %f, header says %f",
        a.name, o.duration, want);
  // The Xing frame carries no audio: at most frames-1 MPEG frames of output.
  const size_t outFrames = o.pcm.size() / std::max(1, o.channels);
  CHECK(outFrames <= size_t(x.frames) * x.samplesPerFrame, "%s: %zu frames",
        a.name, outFrames);
}

static void testFragmented(Asset &a) {
  for (size_t chunk : {(size_t)1, (size_t)7, (size_t)333, (size_t)4096}) {
    std::printf("case: fragmented %s, %zu-byte pieces\n", a.name, chunk);
    Out o = decodeStream(a.file, chunk);
    CHECK(o.error == DecoderError::NoError, "error %d", (int)o.error);
    CHECK(o.ended, "did not drain");
    size_t bad = 0;
    CHECK(o.pcm.size() == a.ref.pcm.size(), "%zu samples, one-shot %zu",
          o.pcm.size(), a.ref.pcm.size());
    CHECK(samePcm(o.pcm, a.ref.pcm, std::min(o.pcm.size(), a.ref.pcm.size()),
                  &bad),
          "mismatch at %zu", bad);
  }

  // The wrapper on its own, byte by byte from the first byte.
  //
  // Regression for S5: a failed lazy drmp3_init() (not a whole frame yet)
  // used to keep the bytes it had read, so fed in pieces smaller than a
  // frame the decoder never initialized and, after setDataEnded(),
  // hasPendingData() stayed true forever. dr_mp3 also skips the bytes of a
  // frame that is not complete and stops reading for good once a read finds
  // no data: the wrapper must hand it whole frames only.
  const DetectedType t =
      id3Size(a.file) ? BUFFER_MP3_WITH_ID3 : BUFFER_MP3_STREAM;
  for (size_t chunk : {(size_t)1, (size_t)333}) {
    std::printf("case: wrapper alone, %s in %zu-byte pieces\n", a.name, chunk);
    Out w = decodeWrapper(a.file, chunk, t);
    CHECK(w.ended, "did not drain");
    CHECK(w.pcm.size() == a.ref.pcm.size() &&
              samePcm(w.pcm, a.ref.pcm, w.pcm.size()),
          "%zu samples, one-shot %zu", w.pcm.size(), a.ref.pcm.size());
  }
}

static void testGaplessAsset(Asset &a) {
  XingInfo x = parseXing(a.file);
  if (!x.lame || x.frames == 0) return;
  std::printf("case: gapless %s (delay %d, padding %d)\n", a.name, x.delay,
              x.padding);
  Out o = decodeStream(a.file, 4096);
  const size_t total = size_t(x.frames) * x.samplesPerFrame;
  const size_t want = total - x.delay - x.padding;
  const size_t got = o.pcm.size() / std::max(1, o.channels);
  CHECK(got == want, "%s: %zu frames, LAME tag says %zu (= %zu - %d - %d)",
        a.name, got, want, total, x.delay, x.padding);
}

static void testGaplessEncoded(const char *path, size_t frames) {
  Bytes file;
  if (!readFile(path, file)) {
    std::printf("case: gapless lame fixture SKIPPED: cannot read %s\n", path);
    return;
  }
  std::printf("case: gapless lame fixture (%zu frames encoded)\n", frames);
  Out o = decodeStream(file, 4096);
  CHECK(o.error == DecoderError::NoError && o.ended, "decode/drain");
  const size_t got = o.pcm.size() / std::max(1, o.channels);
  CHECK(got == frames, "decoded %zu frames, encoded %zu", got, frames);
}

static void testTruncated(Asset &a) {
  std::printf("case: truncated %s\n", a.name);
  const size_t start = id3Size(a.file);
  for (size_t cut : {start + 5000, start + (a.file.size() - start) / 2 + 17,
                     a.file.size() - 3}) {
    Bytes part(a.file.begin(), a.file.begin() + cut);
    Out o = decodeStream(part, 4096);
    CHECK(o.error == DecoderError::NoError, "cut %zu: error %d", cut,
          (int)o.error);
    CHECK(o.ended, "cut %zu: did not drain", cut);
    CHECK(o.pcm.size() <= a.ref.pcm.size(), "cut %zu: too much output", cut);
    // dr_mp3 decodes frame by frame, and the bit reservoir makes a frame
    // depend on the previous ones only: the output is a prefix of the full
    // decode, except that gapless padding is not trimmed (the stream ended
    // early) and the frame cut in half is dropped.
    size_t bad = 0;
    CHECK(samePcm(o.pcm, a.ref.pcm, o.pcm.size(), &bad),
          "cut %zu: not a prefix (at %zu of %zu)", cut, bad, o.pcm.size());
  }
}

int main(int argc, char **argv) {
  lineBufferedStdout();
  // --write-raw <path> <frames>: write the PCM the script encodes with lame
  // (stereo, 16-bit little-endian, 44.1 kHz, deterministic).
  if (argc == 4 && std::strcmp(argv[1], "--write-raw") == 0) {
    const size_t frames = std::strtoul(argv[3], nullptr, 10);
    FILE *f = std::fopen(argv[2], "wb");
    if (!f) return 1;
    for (size_t i = 0; i < frames; ++i) {
      const int16_t l = (int16_t)std::lround(
          12000 * std::sin(2 * M_PI * 440.0 * double(i) / 44100.0));
      const int16_t r = (int16_t)std::lround(
          8000 * std::sin(2 * M_PI * 660.0 * double(i) / 44100.0));
      const unsigned char b[4] = {(unsigned char)(l & 0xff),
                                  (unsigned char)((l >> 8) & 0xff),
                                  (unsigned char)(r & 0xff),
                                  (unsigned char)((r >> 8) & 0xff)};
      std::fwrite(b, 1, 4, f);
    }
    return std::fclose(f) == 0 ? 0 : 1;
  }
  std::vector<const char *> pos;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--strict") == 0)
      gStrict = true;
    else
      pos.push_back(argv[i]);
  }
  if (pos.empty()) {
    std::printf("usage: %s <assets dir> [lame.mp3 frames] [--strict]\n",
                argv[0]);
    return 2;
  }
  const std::string dir = pos[0];
  std::vector<Asset> assets = {{"sample-MP3.mp3", {}, {}},
                               {"IveSeenThings.mp3", {}, {}},
                               {"explosion.mp3", {}, {}},
                               {"test_mpeg2_24khz_mono.mp3", {}, {}},
                               {"8_bit_mentality.mp3", {}, {}}};
  for (auto &a : assets) {
    if (!readFile(dir + "/" + a.name, a.file)) {
      std::printf("SKIPPED: %s/%s not found\n", dir.c_str(), a.name);
      return 0;
    }
    if (!referenceDecode(a.file, a.ref)) {
      std::printf("FAIL: dr_mp3 cannot decode %s\n", a.name);
      return 1;
    }
  }
  Asset &sample = assets[0], &ive = assets[1], &explosion = assets[2],
        &mpeg2 = assets[3], &big = assets[4];

  testId3(sample);
  testId3(ive);
  testId3(mpeg2);
  testLargeId3(big);
  testShortFirstDelivery(explosion);
  for (auto &a : assets) testXing(a);
  testFragmented(explosion);
  testFragmented(ive);
  testGaplessAsset(explosion);
  testGaplessAsset(ive);
  if (pos.size() >= 3)
    testGaplessEncoded(pos[1], std::strtoul(pos[2], nullptr, 10));
  else
    std::printf("case: gapless lame fixture SKIPPED: lame not installed\n");
  testTruncated(sample);
  testTruncated(explosion);

  std::printf("%d checks, %d failures, %d known bugs\n", gChecks, gFailures,
              gKnownBugs);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

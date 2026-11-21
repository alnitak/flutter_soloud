// Unit tests for the Ogg FLAC stream decoder
// (src/audiobuffer/ogg_flac_stream_decoder.cpp, OggFlacDecoderWrapper), driven
// directly and through StreamDecoder (format detection of the Ogg FLAC
// mapping header).
//
// The fixture is encoded in memory with libFLAC's Ogg encoder at test time
// (stereo, 16 bit, 44.1 kHz, a deterministic signal plus Vorbis comments).
// FLAC is lossless, so output is compared sample-for-sample. If the installed
// libFLAC was built without Ogg support the test prints SKIPPED.
//
// Verifies:
//   detect          StreamDecoder detects BUFFER_OGG_FLAC and decodes it;
//                   a deferred setDataEnded() reaches the wrapper
//   one-shot        whole file in one call: bit-exact, sample rate, channels,
//                   duration, Vorbis comments and STREAMINFO via the callback,
//                   the drain loop ends
//   partial         the file in pieces of 1, 7, 100, 999, 4096 bytes (pages
//                   and packets split anywhere) decodes as the one-shot does
//   max-output      maxOutputSamples bounds the samples of a call, with the
//                   end signalled first as BufferStream::setDataIsEnded does
//   truncated       data ending mid-page/mid-packet drains to a bit-exact
//                   prefix and ends; data ending inside the header pages
//                   ends without audio; garbage after "OggS" does not hang
//   seek-estimate   canSeekToTime/timeToByteOffset/getDuration
//
// Needs no audio device and no assets. Needs libFLAC with Ogg support and
// libogg (brew install flac libogg opus libvorbis; apt install libflac-dev
// libogg-dev libopus-dev libvorbis-dev).
//
//   ./test/common/ogg_flac_stream_decoder_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/ogg_flac_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"
#include "../support/line_buffered_stdout.h"

#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

static constexpr unsigned kRate = 44100;
static constexpr unsigned kChannels = 2;
static constexpr unsigned kBlock = 1152;
// Bound on the decode() calls of a drain loop after the data has ended.
static constexpr int kMaxDrainCalls = 1000;

struct Fixture {
  Bytes file;
  std::vector<FLAC__int32> pcm;
  size_t frames = 0;
};

static FLAC__StreamEncoderWriteStatus collect(const FLAC__StreamEncoder *,
                                              const FLAC__byte bytes[],
                                              size_t length, unsigned,
                                              unsigned, void *opaque) {
  auto &out = *static_cast<Bytes *>(opaque);
  out.insert(out.end(), bytes, bytes + length);
  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

// Returns false with [unsupported] set when libFLAC has no Ogg support.
static bool encodeOggFlac(size_t frames, Fixture &fx, bool &unsupported) {
  unsupported = false;
  fx.frames = frames;
  fx.pcm.resize(frames * kChannels);
  for (size_t i = 0; i < frames; ++i) {
    fx.pcm[i * 2] = static_cast<FLAC__int32>(
        std::lround(15000.0 * std::sin(2 * M_PI * 330.0 * i / kRate)));
    fx.pcm[i * 2 + 1] =
        static_cast<FLAC__int32>(
            std::lround(7000.0 * std::sin(2 * M_PI * 2000.0 * i / kRate))) +
        static_cast<int>((static_cast<uint32_t>(i) * 2654435761u) >> 27);
  }
  FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
  if (!enc) return false;
  FLAC__stream_encoder_set_channels(enc, kChannels);
  FLAC__stream_encoder_set_bits_per_sample(enc, 16);
  FLAC__stream_encoder_set_sample_rate(enc, kRate);
  FLAC__stream_encoder_set_blocksize(enc, kBlock);
  FLAC__stream_encoder_set_ogg_serial_number(enc, 0x50F0);
  FLAC__stream_encoder_set_total_samples_estimate(enc, frames);

  FLAC__StreamMetadata *vc =
      FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__StreamMetadata_VorbisComment_Entry entry;
  FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
      &entry, "TITLE", "ogg flac stream test");
  FLAC__metadata_object_vorbiscomment_append_comment(vc, entry, false);
  FLAC__StreamMetadata *meta[1] = {vc};
  FLAC__stream_encoder_set_metadata(enc, meta, 1);

  const FLAC__StreamEncoderInitStatus st = FLAC__stream_encoder_init_ogg_stream(
      enc, nullptr, collect, nullptr, nullptr, nullptr, &fx.file);
  bool ok = st == FLAC__STREAM_ENCODER_INIT_STATUS_OK;
  if (st == FLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER)
    unsupported = true;
  ok = ok && FLAC__stream_encoder_process_interleaved(
                 enc, fx.pcm.data(), static_cast<unsigned>(frames));
  if (ok) ok = FLAC__stream_encoder_finish(enc);
  FLAC__stream_encoder_delete(enc);
  FLAC__metadata_object_delete(vc);
  return ok;
}

static bool matches(const std::vector<float> &got,
                    const std::vector<FLAC__int32> &ref, size_t count,
                    size_t *firstBad = nullptr) {
  if (count > ref.size() || count > got.size()) return false;
  for (size_t i = 0; i < count; ++i)
    if (got[i] != static_cast<float>(ref[i]) / 32768.0f) {
      if (firstBad) *firstBad = i;
      return false;
    }
  return true;
}

// Offsets just past each complete Ogg page.
static std::vector<size_t> pageEnds(const Bytes &f) {
  std::vector<size_t> ends;
  size_t pos = 0;
  while (pos + 27 <= f.size() && std::memcmp(&f[pos], "OggS", 4) == 0) {
    size_t segs = f[pos + 26];
    if (pos + 27 + segs > f.size()) break;
    size_t body = 0;
    for (size_t i = 0; i < segs; ++i) body += f[pos + 27 + i];
    pos += 27 + segs + body;
    if (pos > f.size()) break;
    ends.push_back(pos);
  }
  return ends;
}

struct Result {
  std::vector<float> pcm;
  int sampleRate = 0, channels = 0;
  bool error = false;
  bool ended = false;
  size_t maxPerCall = 0;
  std::vector<AudioMetadata> meta;
};

static Result decodeWrapper(const Bytes &data, size_t chunk,
                            size_t maxOut = 0) {
  Result r;
  OggFlacDecoderWrapper w;
  w.detectedType = BUFFER_OGG_FLAC;
  w.setTrackChangeCallback(
      [&r](const AudioMetadata &m) { r.meta.push_back(m); });
  if (!w.initializeDecoder(kRate, kChannels)) {
    r.error = true;
    return r;
  }
  auto run = [&](Bytes &in) {
    auto o = w.decode(in, &r.sampleRate, &r.channels, maxOut);
    if (o.second != DecoderError::NoError) r.error = true;
    r.maxPerCall = std::max(r.maxPerCall, o.first.size());
    r.pcm.insert(r.pcm.end(), o.first.begin(), o.first.end());
  };
  for (size_t off = 0; off < data.size(); off += chunk) {
    Bytes in(data.begin() + off,
             data.begin() + std::min(data.size(), off + chunk));
    run(in);
  }
  w.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
    Bytes none;
    run(none);
  }
  r.ended = !w.hasPendingData();
  return r;
}

static bool hasTitle(const std::vector<AudioMetadata> &meta) {
  for (auto &m : meta)
    if (m.type == BUFFER_OGG_FLAC && m.oggMetadata.comments.count("TITLE") &&
        m.oggMetadata.comments.at("TITLE") == "ogg flac stream test")
      return true;
  return false;
}

static void testDetect(const Fixture &fx) {
  std::printf("case: detect\n");
  StreamDecoder sd;
  sd.setDataEnded(); // before the wrapper exists: must be deferred
  Bytes in = fx.file;
  int sr = kRate, ch = kChannels;
  std::vector<AudioMetadata> meta;
  auto o = sd.decode(in, &sr, &ch,
                     [&meta](const AudioMetadata &m) { meta.push_back(m); });
  CHECK(o.second == DecoderError::NoError, "error %d", (int)o.second);
  CHECK(sd.getWrapperType() == BUFFER_OGG_FLAC, "type %d",
        (int)sd.getWrapperType());
  std::vector<float> pcm = o.first;
  for (int i = 0; i < kMaxDrainCalls && sd.hasPendingData(); ++i) {
    Bytes none;
    auto d = sd.decode(none, &sr, &ch, nullptr);
    pcm.insert(pcm.end(), d.first.begin(), d.first.end());
  }
  CHECK(!sd.hasPendingData(), "did not drain");
  CHECK(sr == (int)kRate && ch == (int)kChannels, "format %d/%d", sr, ch);
  CHECK(pcm.size() == fx.pcm.size() && matches(pcm, fx.pcm, pcm.size()),
        "%zu samples, want %zu", pcm.size(), fx.pcm.size());
  CHECK(hasTitle(meta), "comments not reported");

  // Part of the first page: not enough to detect, so StreamDecoder must ask
  // for more (NoError, nothing consumed), not reject the stream. Regression
  // for S7: a buffer that starts with "OggS" but holds fewer than 28 bytes
  // used to be reported as BUFFER_UNKNOWN (FormatNotSupported).
  for (size_t n : {(size_t)4, (size_t)20, (size_t)27, (size_t)28, (size_t)40}) {
    StreamDecoder sd2;
    Bytes head(fx.file.begin(), fx.file.begin() + n);
    auto o2 = sd2.decode(head, &sr, &ch, nullptr);
    const bool waits = o2.second == DecoderError::NoError &&
                       head.size() == n &&
                       sd2.getWrapperType() == BUFFER_UNKNOWN;
    CHECK(waits, "%zu bytes of the first page: error %d", n, (int)o2.second);
  }
  // Data that is not Ogg after the magic is still rejected.
  {
    StreamDecoder sd3;
    // A complete page (one 10-byte segment) of no known codec, then bytes
    // that are no page.
    Bytes junk = {'O', 'g', 'g', 'S'};
    junk.resize(26, 0);
    junk.push_back(1);  // segment count
    junk.push_back(10); // segment table
    junk.resize(junk.size() + 10, 0x55);
    junk.resize(junk.size() + 100, 0x33);
    auto o3 = sd3.decode(junk, &sr, &ch, nullptr);
    CHECK(o3.second == DecoderError::FormatNotSupported,
          "garbage after OggS: error %d", (int)o3.second);
  }
}

static void testOneShot(const Fixture &fx) {
  std::printf("case: one-shot\n");
  Result r = decodeWrapper(fx.file, fx.file.size());
  CHECK(!r.error, "decode error");
  CHECK(r.ended, "drain did not end");
  CHECK(r.sampleRate == (int)kRate && r.channels == (int)kChannels,
        "format %d/%d", r.sampleRate, r.channels);
  size_t bad = 0;
  CHECK(r.pcm.size() == fx.pcm.size(), "%zu samples, want %zu", r.pcm.size(),
        fx.pcm.size());
  CHECK(matches(r.pcm, fx.pcm, std::min(r.pcm.size(), fx.pcm.size()), &bad),
        "mismatch at %zu", bad);
  CHECK(hasTitle(r.meta), "comments not reported");
  bool info = false;
  for (auto &m : r.meta)
    info = info || (m.oggMetadata.flacInfo.sample_rate == kRate &&
                    m.oggMetadata.flacInfo.channels == kChannels &&
                    m.oggMetadata.flacInfo.bits_per_sample == 16 &&
                    m.oggMetadata.flacInfo.total_samples == fx.frames);
  CHECK(info, "STREAMINFO not in reported metadata");
}

// Partial input must decode exactly as the one-shot decode does. Pages and
// packets are split anywhere; the wrapper hands libFLAC whole packets, and
// only once all the metadata blocks are there.
static void testPartial(const Fixture &fx, const std::vector<float> &oneShot) {
  for (size_t chunk : {1u, 7u, 100u, 999u, 4096u}) {
    std::printf("case: partial input, %zu-byte pieces\n", chunk);
    Result r = decodeWrapper(fx.file, chunk);
    CHECK(!r.error, "chunk %zu: error", chunk);
    CHECK(r.ended, "chunk %zu: hasPendingData() still true after %d "
                       "drain calls", chunk, kMaxDrainCalls);
    CHECK(r.pcm.size() == oneShot.size(),
              "chunk %zu: %zu samples, one-shot %zu", chunk, r.pcm.size(),
              oneShot.size());
    size_t bad = 0;
    CHECK(matches(r.pcm, fx.pcm, std::min(r.pcm.size(), fx.pcm.size()),
                      &bad),
              "chunk %zu: mismatch at %zu", chunk, bad);
    CHECK(hasTitle(r.meta), "chunk %zu: comments lost", chunk);
  }

  std::printf("case: partial input, page by page\n");
  std::vector<size_t> ends = pageEnds(fx.file);
  CHECK(ends.size() > 3 && ends.back() == fx.file.size(), "%zu pages",
        ends.size());
  OggFlacDecoderWrapper w;
  w.initializeDecoder(kRate, kChannels);
  int sr = 0, ch = 0;
  std::vector<float> pcm;
  size_t prev = 0;
  for (size_t e : ends) {
    Bytes page(fx.file.begin() + prev, fx.file.begin() + e);
    prev = e;
    auto o = w.decode(page, &sr, &ch);
    pcm.insert(pcm.end(), o.first.begin(), o.first.end());
  }
  w.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
    Bytes none;
    auto o = w.decode(none, &sr, &ch);
    pcm.insert(pcm.end(), o.first.begin(), o.first.end());
  }
  CHECK(!w.hasPendingData(), "page by page: did not drain");
  CHECK(pcm.size() == fx.pcm.size() && matches(pcm, fx.pcm, pcm.size()),
            "page by page: %zu samples, want %zu", pcm.size(), fx.pcm.size());
}

// Mirrors BufferStream::setDataIsEnded(): the end of the data is signalled
// first, then the remaining bytes are decoded with a cap (4096 * 4 samples in
// audiobuffer.cpp) and drained with the same cap until hasPendingData() is
// false. Also with the data fed before the end is signalled.
//
// Regression for S3: when the cap stopped the loop after libFLAC had read the
// last bytes into its bit reader but before it had decoded the final frame,
// the wrapper reported the stream drained and the final frame was lost.
static void testMaxOutput(const Fixture &fx) {
  for (size_t maxOut : {(size_t)4096 * 4, (size_t)2 * kBlock * kChannels}) {
    for (bool endFirst : {true, false}) {
      std::printf("case: max-output %zu, end signalled %s\n", maxOut,
                  endFirst ? "first" : "after the data");
      OggFlacDecoderWrapper w;
      w.detectedType = BUFFER_OGG_FLAC;
      w.initializeDecoder(kRate, kChannels);
      int sr = 0, ch = 0;
      if (endFirst) w.setDataEnded();
      Bytes in = fx.file;
      auto o = w.decode(in, &sr, &ch, maxOut);
      std::vector<float> pcm = o.first;
      size_t maxPerCall = o.first.size();
      if (!endFirst) w.setDataEnded();
      for (int i = 0; i < 10 * kMaxDrainCalls && w.hasPendingData(); ++i) {
        Bytes none;
        auto d = w.decode(none, &sr, &ch, maxOut);
        maxPerCall = std::max(maxPerCall, d.first.size());
        pcm.insert(pcm.end(), d.first.begin(), d.first.end());
      }
      CHECK(!w.hasPendingData(), "did not drain");
      CHECK(maxPerCall <= maxOut + kBlock * kChannels, "a call returned %zu",
            maxPerCall);
      CHECK(pcm.size() == fx.pcm.size(), "%zu samples, want %zu",
            pcm.size(), fx.pcm.size());
      CHECK(matches(pcm, fx.pcm,  std::min(pcm.size(), fx.pcm.size())),
            "output is not a prefix");
    }
  }
}

static void testTruncated(const Fixture &fx) {
  std::printf("case: truncated\n");
  std::vector<size_t> ends = pageEnds(fx.file);
  // ends[0] = BOS page (mapping header + STREAMINFO); header pages follow.
  for (size_t cut : {ends[0] + 30, fx.file.size() / 3, fx.file.size() / 2 + 1,
                     fx.file.size() - 1}) {
    Bytes part(fx.file.begin(), fx.file.begin() + cut);
    Result r = decodeWrapper(part, part.size());
    CHECK(r.ended, "cut %zu: did not end", cut);
    CHECK(r.pcm.size() <= fx.pcm.size(), "cut %zu: too much output", cut);
    size_t bad = 0;
    CHECK(matches(r.pcm, fx.pcm, r.pcm.size(), &bad),
          "cut %zu: output is not a prefix (at %zu)", cut, bad);
  }
  for (size_t cut : {(size_t)5, (size_t)30, ends[0] - 1, ends[0]}) {
    Bytes part(fx.file.begin(), fx.file.begin() + cut);
    Result r = decodeWrapper(part, part.size());
    CHECK(r.ended, "header cut %zu: did not end", cut);
    CHECK(r.pcm.empty(), "header cut %zu: produced audio", cut);
  }

  std::printf("case: garbage after the first page\n");
  Bytes junk(fx.file.begin(), fx.file.begin() + ends[0]);
  uint32_t x = 0x1234567u;
  for (int i = 0; i < 20000; ++i) {
    x = x * 1664525u + 1013904223u;
    junk.push_back(static_cast<unsigned char>(x >> 24));
  }
  Result r = decodeWrapper(junk, 1000);
  CHECK(r.ended, "garbage: did not end");
  CHECK(r.pcm.empty(), "garbage produced %zu samples", r.pcm.size());
}

static void testSeekEstimate(const Fixture &fx) {
  std::printf("case: seek-estimate\n");
  OggFlacDecoderWrapper w;
  w.initializeDecoder(kRate, kChannels);
  CHECK(w.getDuration() < 0 && !w.canSeekToTime(0.1) &&
            w.timeToByteOffset(0.1) == 0,
        "nothing known before data");
  static_cast<IDecoderWrapper &>(w).setTotalAudioSizeBytes(fx.file.size());
  int sr = 0, ch = 0;
  Bytes in(fx.file.begin(), fx.file.begin() + fx.file.size() / 2);
  w.decode(in, &sr, &ch);
  const double total = double(fx.frames) / kRate;
  CHECK(std::fabs(w.getDuration() - total) < 1e-9, "duration %f vs %f",
        w.getDuration(), total);
  CHECK(w.canSeekToTime(total / 2) && !w.canSeekToTime(0.0),
        "canSeekToTime");
  const uint64_t off = w.timeToByteOffset(total / 2);
  CHECK(off == static_cast<uint64_t>(0.5 * fx.file.size()),
        "offset %llu for half of %zu", (unsigned long long)off,
        fx.file.size());
}

int main(int argc, char **argv) {
  lineBufferedStdout();
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;

  Fixture fx;
  bool unsupported = false;
  if (!encodeOggFlac(kRate / 2, fx, unsupported)) {
    if (unsupported) {
      std::printf("SKIPPED: libFLAC was built without Ogg support\n");
      return 0;
    }
    std::printf("FAIL: libFLAC Ogg encoder failed\n");
    return 1;
  }
  std::printf("fixture: %zu bytes, %zu frames\n", fx.file.size(), fx.frames);

  testDetect(fx);
  testOneShot(fx);
  Result one = decodeWrapper(fx.file, fx.file.size());
  testPartial(fx, one.pcm);
  testMaxOutput(fx);
  testTruncated(fx);
  testSeekEstimate(fx);

  std::printf("%d checks, %d failures, %d known bugs\n", gChecks, gFailures,
              gKnownBugs);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

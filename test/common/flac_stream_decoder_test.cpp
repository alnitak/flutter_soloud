// Unit tests for the native FLAC stream decoder
// (src/audiobuffer/flac_stream_decoder.cpp, FlacDecoderWrapper), driven both
// directly and through StreamDecoder the way BufferStream/PullBufferStream do.
//
// The fixture is encoded in memory with libFLAC at test time (stereo, 16 bit,
// 44.1 kHz, a deterministic two-tone signal plus a VORBIS_COMMENT block), so
// the decoded output can be compared sample-for-sample with the input: FLAC
// is lossless.
//
// Verifies:
//   one-shot        the whole file in one call decodes bit-exactly; sample
//                   rate, channels, duration and the Vorbis comments are
//                   reported; the drain loop ends (hasPendingData() false)
//   stream-decoder  the same through StreamDecoder (format detection,
//                   deferred setDataEnded before the wrapper exists)
//   pieces          the metadata fed byte by byte (partial metadata blocks)
//                   then the frames; and the whole file in 512/1000/4099/
//                   16384-byte pieces (partial frames) must decode to the
//                   same samples as the one-shot decode (regression for
//                   S1/S2, see testPieces)
//   max-output      maxOutputSamples bounds each call (to one frame of
//                   overshoot), with the end of data signalled first as
//                   BufferStream::setDataIsEnded does, and the total is
//                   unchanged
//   seek            canSeekToTime/timeToByteOffset (a linear estimate), then
//                   prepareForSeek() and data from a mid-frame offset before
//                   the target yield exactly the original samples from the
//                   target sample on
//   truncated       data ending mid-frame drains to a bit-exact prefix and
//                   ends; data ending inside the metadata returns CodecError
//                   and ends; no data at all ends
//
// Needs no audio device and no assets. Needs libFLAC (brew install flac;
// apt install libflac-dev) and libogg/libopus/libvorbis for linking the
// stream decoder set.
//
//   ./test/common/flac_stream_decoder_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/flac_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"
#include "../support/line_buffered_stdout.h"

#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static int gFailures = 0;
static int gChecks = 0;
static int gKnownBugs = 0;
static bool gStrict = false; // --strict: known bugs count as failures

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

// A check that fails because of a known bug in src/ (see the comment where it
// is used). It is reported but does not fail the run unless --strict is given.
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
// Bound on the decode() calls of a drain loop after the data has ended: a
// decoder that never stops reporting pending data shows up as a failure.
static constexpr int kMaxDrainCalls = 1000;

struct Fixture {
  Bytes file;
  std::vector<FLAC__int32> pcm; // interleaved
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

static bool encodeFlac(size_t frames, Fixture &fx) {
  fx.frames = frames;
  fx.pcm.resize(frames * kChannels);
  for (size_t i = 0; i < frames; ++i) {
    fx.pcm[i * 2] = static_cast<FLAC__int32>(
        std::lround(12000.0 * std::sin(2 * M_PI * 440.0 * i / kRate)));
    fx.pcm[i * 2 + 1] = static_cast<FLAC__int32>(
        std::lround(9000.0 * std::sin(2 * M_PI * 1234.5 * i / kRate)) +
        static_cast<int>((static_cast<uint32_t>(i) * 2654435761u) >> 28)); // a little noise
  }
  FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
  if (!enc) return false;
  FLAC__stream_encoder_set_channels(enc, kChannels);
  FLAC__stream_encoder_set_bits_per_sample(enc, 16);
  FLAC__stream_encoder_set_sample_rate(enc, kRate);
  FLAC__stream_encoder_set_blocksize(enc, kBlock);
  FLAC__stream_encoder_set_total_samples_estimate(enc, frames);

  FLAC__StreamMetadata *vc =
      FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__StreamMetadata_VorbisComment_Entry entry;
  FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
      &entry, "TITLE", "flac stream test");
  FLAC__metadata_object_vorbiscomment_append_comment(vc, entry, false);
  FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
      &entry, "ARTIST", "soloud");
  FLAC__metadata_object_vorbiscomment_append_comment(vc, entry, false);
  FLAC__StreamMetadata *meta[1] = {vc};
  FLAC__stream_encoder_set_metadata(enc, meta, 1);

  bool ok = FLAC__stream_encoder_init_stream(enc, collect, nullptr, nullptr,
                                             nullptr, &fx.file) ==
            FLAC__STREAM_ENCODER_INIT_STATUS_OK;
  ok = ok && FLAC__stream_encoder_process_interleaved(
                 enc, fx.pcm.data(), static_cast<unsigned>(frames));
  if (!ok)
    std::printf("FLAC encoder state: %s\n",
                FLAC__stream_encoder_get_resolved_state_string(enc));
  ok = FLAC__stream_encoder_finish(enc) && ok;
  FLAC__stream_encoder_delete(enc);
  FLAC__metadata_object_delete(vc);
  return ok;
}

static size_t headerLength(const Bytes &f) {
  size_t pos = 4;
  while (pos + 4 <= f.size()) {
    bool last = f[pos] & 0x80;
    size_t len = (size_t(f[pos + 1]) << 16) | (size_t(f[pos + 2]) << 8) |
                 f[pos + 3];
    pos += 4 + len;
    if (last) return pos;
  }
  return 0;
}

struct Result {
  std::vector<float> pcm;
  int sampleRate = 0;
  int channels = 0;
  bool error = false;
  DecoderError firstError = DecoderError::NoError;
  bool ended = false; // hasPendingData() went false after setDataEnded
  int calls = 0;
};

// Feed [data] to a fresh FlacDecoderWrapper in [chunk]-byte pieces, then end
// the data and drain.
static Result decodeWrapper(const Bytes &data, size_t chunk,
                            size_t maxOut = 0,
                            std::vector<AudioMetadata> *meta = nullptr,
                            size_t *maxPerCall = nullptr) {
  Result r;
  FlacDecoderWrapper w;
  w.detectedType = BUFFER_FLAC;
  if (meta)
    w.setTrackChangeCallback(
        [meta](const AudioMetadata &m) { meta->push_back(m); });
  if (!w.initializeDecoder(kRate, kChannels)) {
    r.error = true;
    return r;
  }
  auto run = [&](Bytes &in) {
    auto out = w.decode(in, &r.sampleRate, &r.channels, maxOut);
    ++r.calls;
    if (out.second != DecoderError::NoError && !r.error) {
      r.error = true;
      r.firstError = out.second;
    }
    if (maxPerCall && out.first.size() > *maxPerCall)
      *maxPerCall = out.first.size();
    r.pcm.insert(r.pcm.end(), out.first.begin(), out.first.end());
  };
  for (size_t off = 0; off < data.size(); off += chunk) {
    Bytes in(data.begin() + off,
             data.begin() + std::min(data.size(), off + chunk));
    run(in);
    // Like BufferStream: keep decoding while the decoder has queued data.
    for (int i = 0; maxOut > 0 && i < kMaxDrainCalls && w.hasPendingData(); ++i) {
      Bytes none;
      size_t before = r.pcm.size();
      run(none);
      if (r.pcm.size() == before) break;
    }
  }
  w.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
    Bytes none;
    run(none);
  }
  r.ended = !w.hasPendingData();
  return r;
}

static bool matches(const std::vector<float> &got,
                    const std::vector<FLAC__int32> &ref, size_t refStart,
                    size_t count, size_t *firstBad = nullptr) {
  if (refStart + count > ref.size() || count > got.size()) return false;
  for (size_t i = 0; i < count; ++i) {
    if (got[i] != static_cast<float>(ref[refStart + i]) / 32768.0f) {
      if (firstBad) *firstBad = i;
      return false;
    }
  }
  return true;
}

static void testOneShot(const Fixture &fx) {
  std::printf("case: one-shot\n");
  std::vector<AudioMetadata> meta;
  Result r = decodeWrapper(fx.file, fx.file.size(), 0, &meta);
  CHECK(!r.error, "decode error %d", (int)r.firstError);
  CHECK(r.ended, "drain loop did not end after %d calls", r.calls);
  CHECK(r.sampleRate == (int)kRate && r.channels == (int)kChannels,
        "format %d Hz %d ch", r.sampleRate, r.channels);
  CHECK(r.pcm.size() == fx.pcm.size(), "samples %zu, expected %zu",
        r.pcm.size(), fx.pcm.size());
  size_t bad = 0;
  CHECK(matches(r.pcm, fx.pcm, 0, fx.pcm.size(), &bad),
        "not bit-exact at %zu", bad);
  bool title = false;
  for (auto &m : meta)
    if (m.oggMetadata.comments.count("TITLE") &&
        m.oggMetadata.comments.at("TITLE") == "flac stream test" &&
        m.oggMetadata.comments.count("ARTIST") &&
        m.oggMetadata.flacInfo.sample_rate == kRate &&
        m.oggMetadata.flacInfo.channels == kChannels &&
        m.oggMetadata.flacInfo.total_samples == fx.frames)
      title = true;
  CHECK(title, "metadata callback with comments and STREAMINFO (%zu calls)",
        meta.size());

  FlacDecoderWrapper w;
  w.initializeDecoder(kRate, kChannels);
  Bytes in = fx.file;
  int sr = 0, ch = 0;
  w.decode(in, &sr, &ch);
  CHECK(in.empty(), "decode consumes the input buffer");
  const double dur = double(fx.frames) / kRate;
  CHECK(std::fabs(w.getDuration() - dur) < 1e-9, "duration %f vs %f",
        w.getDuration(), dur);
}

static void testStreamDecoder(const Fixture &fx) {
  std::printf("case: stream-decoder\n");
  // Small file, data ended before the first decode: StreamDecoder must
  // forward the deferred end-of-stream to the wrapper it creates.
  StreamDecoder sd;
  sd.setDataEnded();
  sd.setTotalAudioSizeBytes(fx.file.size());
  Bytes in = fx.file;
  int sr = kRate, ch = kChannels;
  std::vector<float> pcm;
  auto out = sd.decode(in, &sr, &ch, nullptr);
  CHECK(out.second == DecoderError::NoError, "error %d", (int)out.second);
  CHECK(sd.getWrapperType() == BUFFER_FLAC, "type %d", sd.getWrapperType());
  pcm = out.first;
  int calls = 0;
  while (sd.hasPendingData() && calls++ < kMaxDrainCalls) {
    Bytes none;
    auto o = sd.decode(none, &sr, &ch, nullptr);
    pcm.insert(pcm.end(), o.first.begin(), o.first.end());
  }
  CHECK(!sd.hasPendingData(), "stream decoder did not drain");
  CHECK(pcm.size() == fx.pcm.size() && matches(pcm, fx.pcm, 0, pcm.size()),
        "%zu samples", pcm.size());
  CHECK(std::fabs(sd.getDuration() - double(fx.frames) / kRate) < 1e-9,
        "duration %f", sd.getDuration());

  // Too little data to detect: NoError, nothing consumed.
  StreamDecoder sd2;
  Bytes three(fx.file.begin(), fx.file.begin() + 3);
  auto o2 = sd2.decode(three, &sr, &ch, nullptr);
  CHECK(o2.second == DecoderError::NoError && o2.first.empty() &&
            three.size() == 3,
        "short input must wait");
}

// Chunked delivery must decode to exactly the samples of a one-shot decode.
// Regression for S1/S2: libFLAC's read can abort while it searches for a frame
// sync (process_single() then returns true with the state ABORTED), and a
// frame can span two decode() calls while its head sits in libFLAC's bit
// reader. The wrapper must roll back to the end of the last decoded frame
// and resume there, and the drain must end.
static void testPieces(const Fixture &fx) {
  // Byte-by-byte over the metadata (the wrapper must wait for the whole
  // header), then the frames in one piece. The first delivery carries the
  // 4-byte magic (as StreamDecoder guarantees: it detects the format on it)
  // or a single byte (regression for S2: a first delivery of 1-3 bytes must
  // wait instead of reaching libFLAC).
  for (size_t firstLen : {(size_t)4, (size_t)1}) {
    std::printf("case: header byte by byte, first delivery %zu bytes\n",
                firstLen);
    const size_t hdr = headerLength(fx.file);
    FlacDecoderWrapper w;
    std::vector<AudioMetadata> meta;
    w.setTrackChangeCallback(
        [&meta](const AudioMetadata &m) { meta.push_back(m); });
    w.initializeDecoder(kRate, kChannels);
    int sr = 0, ch = 0;
    std::vector<float> pcm;
    bool ok = true;
    {
      Bytes first(fx.file.begin(), fx.file.begin() + firstLen);
      auto o = w.decode(first, &sr, &ch);
      ok = o.second == DecoderError::NoError && o.first.empty();
    }
    for (size_t i = firstLen; i < hdr; ++i) {
      Bytes one{fx.file[i]};
      auto o = w.decode(one, &sr, &ch);
      ok = ok && o.second == DecoderError::NoError && o.first.empty();
    }
    CHECK(ok, "partial header must wait without output or error");
    Bytes rest(fx.file.begin() + hdr, fx.file.end());
    auto o = w.decode(rest, &sr, &ch);
    pcm = o.first;
    w.setDataEnded();
    for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
      Bytes none;
      auto d = w.decode(none, &sr, &ch);
      pcm.insert(pcm.end(), d.first.begin(), d.first.end());
    }
    CHECK(!w.hasPendingData(), "did not drain");
    CHECK(sr == (int)kRate && ch == (int)kChannels, "format %d/%d", sr, ch);
    CHECK(pcm.size() == fx.pcm.size() && matches(pcm, fx.pcm, 0, pcm.size()),
          "%zu samples, want %zu", pcm.size(), fx.pcm.size());
    bool comments = false;
    for (auto &m : meta)
      comments = comments || m.oggMetadata.comments.count("TITLE");
    CHECK(comments, "comments lost");
  }

  for (size_t chunk : {1u, 7u, 100u, 512u, 1000u, 4099u, 16384u}) {
    std::printf("case: pieces of %zu bytes\n", chunk);
    std::vector<AudioMetadata> meta;
    Result r = decodeWrapper(fx.file, chunk, 0, &meta);
    CHECK(!r.error, "error %d", (int)r.firstError);
    CHECK(r.ended, "chunk %zu: hasPendingData() still true after %d "
                   "drain calls", chunk, kMaxDrainCalls);
    CHECK(r.pcm.size() == fx.pcm.size(),
          "chunk %zu: %zu samples, want %zu", chunk, r.pcm.size(),
          fx.pcm.size());
    if (r.pcm.size() == fx.pcm.size())
      CHECK(matches(r.pcm, fx.pcm, 0, r.pcm.size()), "chunk %zu: mismatch",
            chunk);
  }
}

// Mirrors BufferStream::setDataIsEnded(): the end of the data is signalled
// first, then the remaining bytes are decoded with a cap (4096 * 4 samples in
// audiobuffer.cpp) and drained with the same cap until hasPendingData() is
// false. Also with the data fed before the end is signalled. (The Ogg FLAC
// wrapper loses its final frame here: see ogg_flac_stream_decoder_test.cpp.)
static void testMaxOutput(const Fixture &fx) {
  for (size_t maxOut : {(size_t)4096 * 4, (size_t)2 * kBlock * kChannels}) {
    for (bool endFirst : {true, false}) {
      std::printf("case: max-output %zu, end signalled %s\n", maxOut,
                  endFirst ? "first" : "after the data");
      FlacDecoderWrapper w;
      w.detectedType = BUFFER_FLAC;
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
      CHECK(matches(pcm, fx.pcm, 0, std::min(pcm.size(), fx.pcm.size())),
            "output is not a prefix");
    }
  }
}

static void testSeek(const Fixture &fx, size_t chunk) {
  std::printf("case: seek, data in pieces of %zu bytes\n", chunk);
  const size_t hdr = headerLength(fx.file);
  CHECK(hdr > 0, "header length");
  // Let the wrapper see the header and the first part of the stream.
  FlacDecoderWrapper w;
  w.detectedType = BUFFER_FLAC;
  w.initializeDecoder(kRate, kChannels);
  // Private in FlacDecoderWrapper, public in the interface StreamDecoder uses.
  static_cast<IDecoderWrapper &>(w).setTotalAudioSizeBytes(fx.file.size());
  int sr = 0, ch = 0;
  Bytes first(fx.file.begin(), fx.file.begin() + fx.file.size() / 4);
  w.decode(first, &sr, &ch);
  const double total = double(fx.frames) / kRate;
  CHECK(w.canSeekToTime(total / 2), "canSeekToTime(mid)");
  CHECK(!w.canSeekToTime(0.0), "canSeekToTime(0) is false");
  uint64_t off = w.timeToByteOffset(total / 2);
  CHECK(off > hdr && off < fx.file.size(), "offset %llu of %zu",
        (unsigned long long)off, fx.file.size());
  CHECK(w.timeToByteOffset(0.0) == 0, "offset of 0 s");

  CHECK(w.timeToByteOffset(total / 4) < off && off < w.timeToByteOffset(total * 0.9),
        "offsets grow with time");

  // timeToByteOffset() is a linear estimate (by design), so the frame that
  // holds the target can start before the estimated offset. Feed from a
  // point safely before it, mid-frame: the wrapper must trim to the next
  // frame sync and skip to exactly the target sample.
  const uint64_t targetFrame = fx.frames / 2 + 123;
  const size_t feedFrom = std::max(hdr + 1, static_cast<size_t>(off) - 3000);
  w.prepareForSeek(targetFrame * kChannels, feedFrom);
  Bytes rest(fx.file.begin() + feedFrom, fx.file.end());
  std::vector<float> pcm;
  bool err = false;
  for (size_t at = 0; at < rest.size(); at += chunk) {
    Bytes in(rest.begin() + at, rest.begin() + std::min(rest.size(), at + chunk));
    auto o = w.decode(in, &sr, &ch);
    err = err || o.second != DecoderError::NoError;
    pcm.insert(pcm.end(), o.first.begin(), o.first.end());
  }
  CHECK(!err, "error after seek");
  w.setDataEnded();
  int calls = 0;
  while (w.hasPendingData() && calls++ < kMaxDrainCalls) {
    Bytes none;
    auto o = w.decode(none, &sr, &ch);
    pcm.insert(pcm.end(), o.first.begin(), o.first.end());
  }
  CHECK(!w.hasPendingData(), "did not drain after seek");
  const size_t want = (fx.frames - targetFrame) * kChannels;
  size_t bad = 0;
  // Everything from the target on must come out, starting exactly at it.
  CHECK(pcm.size() == want, "after seek %zu samples, want %zu", pcm.size(),
        want);
  CHECK(matches(pcm, fx.pcm, targetFrame * kChannels,
                std::min(pcm.size(), want), &bad),
        "post-seek mismatch at %zu", bad);
}

static void testTruncated(const Fixture &fx) {
  std::printf("case: truncated mid-frame\n");
  const size_t hdr = headerLength(fx.file);
  for (size_t cut : {hdr + 1, hdr + 777, fx.file.size() / 2,
                     fx.file.size() - 5}) {
    Bytes part(fx.file.begin(), fx.file.begin() + cut);
    Result r = decodeWrapper(part, part.size());
    CHECK(r.ended, "cut %zu: did not drain (%d calls)", cut, r.calls);
    CHECK(r.pcm.size() <= fx.pcm.size(), "cut %zu: too many samples", cut);
    size_t bad = 0;
    CHECK(matches(r.pcm, fx.pcm, 0, r.pcm.size(), &bad),
          "cut %zu: output is not a prefix (bad at %zu)", cut, bad);
    if (cut == fx.file.size() - 5)
      CHECK(r.pcm.size() + kBlock * kChannels >= fx.pcm.size(),
            "cut %zu: lost more than the last frame (%zu)", cut, r.pcm.size());
  }

  std::printf("case: truncated inside metadata\n");
  for (size_t cut : {(size_t)4, (size_t)10, (size_t)42, hdr - 1}) {
    Bytes part(fx.file.begin(), fx.file.begin() + cut);
    Result r = decodeWrapper(part, part.size());
    CHECK(r.ended, "cut %zu: did not end", cut);
    CHECK(r.pcm.empty(), "cut %zu: produced audio", cut);
    CHECK(r.error && r.firstError == DecoderError::CodecError,
          "cut %zu: expected CodecError, got %d", cut, (int)r.firstError);
  }

  std::printf("case: no data\n");
  Result r = decodeWrapper(Bytes{}, 1);
  CHECK(r.ended && r.pcm.empty(), "empty stream must end");
}

int main(int argc, char **argv) {
  lineBufferedStdout();
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
  Fixture fx;
  if (!encodeFlac(kRate / 2, fx)) { // 0.5 s
    std::printf("SKIPPED: libFLAC encoder failed\n");
    return 0;
  }
  std::printf("fixture: %zu bytes, %zu frames\n", fx.file.size(), fx.frames);
  testOneShot(fx);
  testStreamDecoder(fx);
  testPieces(fx);
  testMaxOutput(fx);
  testSeek(fx, fx.file.size());
  testSeek(fx, 700);
  testTruncated(fx);
  std::printf("%d checks, %d failures, %d known bugs\n", gChecks, gFailures,
              gKnownBugs);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

// Unit tests for the Ogg Opus stream decoder (src/audiobuffer/
// opus_stream_decoder.cpp, OpusDecoderWrapper), driven through StreamDecoder
// the way BufferStream does and directly. Every fixture is encoded in memory
// at test time with libopus + libogg (test/support/ogg_stream_fixtures.h);
// the reference is an independent demux + opus_multistream decode of the
// same file with the pre-skip dropped and the end trimmed to the last
// granule (RFC 7845).
//
// Verifies:
//   basic        a stereo file decodes to the reference (all but the 5 ms
//                end fade), 48 kHz, 2 channels, OpusTags vendor and comments
//                reported once, duration from the last granule
//   chunks       pieces of 1, 7, 333, 4096 and 65536 bytes give the same
//                samples as one piece
//   preskip      a pre-skip longer than three packets is dropped exactly
//   comments     a 200 KB comment (OpusTags across many pages) reaches the
//                callback intact, fed 7 bytes at a time
//   channels     1-8 channels: 1-2 match the reference; 3-8 (mapping family
//                1, multistream packets) decode to the right length and to
//                the reference downmixed to stereo with the RFC 7845
//                matrices (the wrapper outputs at most 2 channels)
//   chained      two concatenated streams (different serials) decode one
//                after the other and report their tags twice; streams with
//                different channel counts are never mixed in one decode()
//                call
//   truncated    input cut mid-page drains and is a prefix of the full decode
//   bounded      maxOutputSamples (BufferStream's 4096*4 drain cap) gives
//                the same samples as an unbounded decode
//   rate24k      a 24 kHz engine decodes at 24 kHz with the exact length
//   seek         prepareForSeek with the target in interleaved samples (as
//                PullBufferStream passes it) decodes from the target to the
//                end of the file; a target before the resume page decodes
//                from that page to the end
//
// Needs no audio device and no assets. Needs libopus, libogg (and the other
// Xiph libraries the stream decoder set links); the script skips without.
//
//   ./test/common/opus_stream_decoder_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/opus_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"

#include "../support/ogg_stream_fixtures.h"
#include "../support/stream_decoder_test_harness.h"

using namespace sdh;
using oggfx::OpusFixture;
using oggfx::OpusOptions;

static const size_t kFade = 240; // 5 ms at 48 kHz, frames

// RFC 7845 section 5.1.1.5 stereo downmix of 3-8 channels in Vorbis order.
static const float kDownmix[6][8][2] = {
    {{0.5858f, 0.0f}, {0.4142f, 0.4142f}, {0.0f, 0.5858f}},
    {{0.4226f, 0.0f}, {0.0f, 0.4226f}, {0.366f, 0.2114f}, {0.2114f, 0.366f}},
    {{0.651f, 0.0f}, {0.46f, 0.46f}, {0.0f, 0.651f}, {0.5636f, 0.3254f},
     {0.3254f, 0.5636f}},
    {{0.529f, 0.0f}, {0.3741f, 0.3741f}, {0.0f, 0.529f}, {0.4582f, 0.2645f},
     {0.2645f, 0.4582f}, {0.3741f, 0.3741f}},
    {{0.4553f, 0.0f}, {0.322f, 0.322f}, {0.0f, 0.4553f}, {0.3943f, 0.2277f},
     {0.2277f, 0.3943f}, {0.2788f, 0.2788f}, {0.322f, 0.322f}},
    {{0.3886f, 0.0f}, {0.2748f, 0.2748f}, {0.0f, 0.3886f}, {0.3366f, 0.1943f},
     {0.1943f, 0.3366f}, {0.3366f, 0.1943f}, {0.1943f, 0.3366f},
     {0.2748f, 0.2748f}},
};

static bool makeOpus(const OpusOptions &o, OpusFixture &fx) {
  if (!oggfx::encodeOpus(o, fx)) {
    std::printf("  FAIL: cannot encode the Opus fixture (%d ch)\n", o.channels);
    ++gFailures;
    return false;
  }
  return true;
}

static void testBasic() {
  std::printf("case: basic\n");
  OpusFixture fx;
  if (!makeOpus({}, fx)) return;
  auto ref = oggfx::referenceOpus(fx.file);
  CHECK(ref.size() == 1 && ref[0].pcm.size() == 48000 * 2,
        "reference has %zu samples", ref.empty() ? 0 : ref[0].pcm.size());
  if (ref.empty()) return;
  Out o = decodeStream(fx.file, fx.file.size());
  CHECK(o.type == BUFFER_OGG_OPUS, "type %d", o.type);
  CHECK(o.error == DecoderError::NoError, "error %d", (int)o.error);
  CHECK(o.ended, "decoder still has pending data");
  CHECK(o.sampleRate == 48000 && o.channels == 2, "%d Hz %d ch", o.sampleRate,
        o.channels);
  CHECK(o.pcm.size() == ref[0].pcm.size(), "%zu samples, reference %zu",
        o.pcm.size(), ref[0].pcm.size());
  const size_t cmp = std::min(o.pcm.size(), ref[0].pcm.size());
  const long d = firstDiff(o.pcm, ref[0].pcm, 0,
                           cmp > kFade * 2 ? cmp - kFade * 2 : 0, 1e-6f);
  CHECK(d < 0, "differs from the reference at sample %ld", d);
  CHECK(o.finite, "non-finite samples");
  CHECK(o.meta.size() == 1, "%zu metadata callbacks", o.meta.size());
  if (!o.meta.empty()) {
    CHECK(o.meta[0].type == BUFFER_OGG_OPUS, "metadata type %d", o.meta[0].type);
    CHECK(o.meta[0].oggMetadata.vendor == fx.vendor, "vendor '%s'",
          o.meta[0].oggMetadata.vendor.c_str());
    const char *t = metaComment(o.meta, 0, "TITLE");
    CHECK(t && std::string(t) == "Opus fixture", "TITLE '%s'", t ? t : "(none)");
    CHECK(o.meta[0].oggMetadata.opusInfo.pre_skip == fx.preSkip,
          "pre_skip %d, header %d", o.meta[0].oggMetadata.opusInfo.pre_skip,
          fx.preSkip);
  }
  CHECK(std::fabs(o.duration - 1.0) < 1.0 / 48000 + 1e-9, "duration %f",
        o.duration);
}

static void testChunks() {
  std::printf("case: chunks\n");
  OpusOptions opt;
  opt.frames = 30000;
  OpusFixture fx;
  if (!makeOpus(opt, fx)) return;
  Out whole = decodeStream(fx.file, fx.file.size());
  CHECK(whole.pcm.size() == opt.frames * 2, "%zu samples", whole.pcm.size());
  for (size_t chunk : {(size_t)1, (size_t)7, (size_t)333, (size_t)4096,
                       (size_t)65536}) {
    Out o = decodeStream(fx.file, chunk);
    CHECK(o.ended, "chunk %zu: not drained", chunk);
    CHECK(o.pcm.size() == whole.pcm.size(), "chunk %zu: %zu samples, want %zu",
          chunk, o.pcm.size(), whole.pcm.size());
    const long d = firstDiff(o.pcm, whole.pcm, 0,
                             std::min(o.pcm.size(), whole.pcm.size()));
    CHECK(d < 0, "chunk %zu: differs at sample %ld", chunk, d);
    CHECK(o.meta.size() == 1, "chunk %zu: %zu metadata callbacks", chunk,
          o.meta.size());
  }
}

static void testPreSkip() {
  std::printf("case: preskip\n");
  OpusOptions opt;
  opt.extraPreSkip = 3000; // more than three 960-sample packets
  OpusFixture fx;
  if (!makeOpus(opt, fx)) return;
  auto ref = oggfx::referenceOpus(fx.file);
  if (ref.empty()) { CHECK(false, "no reference"); return; }
  for (size_t chunk : {fx.file.size(), (size_t)97}) {
    Out o = decodeStream(fx.file, chunk);
    CHECK(o.pcm.size() == opt.frames * 2, "chunk %zu: %zu samples, want %zu",
          chunk, o.pcm.size(), opt.frames * 2);
    const size_t cmp = std::min(o.pcm.size(), ref[0].pcm.size());
    const long d = firstDiff(o.pcm, ref[0].pcm, 0,
                             cmp > kFade * 2 ? cmp - kFade * 2 : 0, 1e-6f);
    CHECK(d < 0, "chunk %zu: differs from the reference at sample %ld", chunk, d);
  }
}

static void testLongComment() {
  std::printf("case: comments\n");
  OpusOptions opt;
  opt.frames = 9600;
  opt.commentBytes = 200000;
  OpusFixture fx;
  if (!makeOpus(opt, fx)) return;
  Out o = decodeStream(fx.file, 7);
  CHECK(o.type == BUFFER_OGG_OPUS, "type %d", o.type);
  CHECK(o.meta.size() == 1, "%zu metadata callbacks", o.meta.size());
  const char *c = metaComment(o.meta, 0, "LONGCOMMENT");
  CHECK(c && std::strlen(c) == opt.commentBytes, "LONGCOMMENT has %zu bytes",
        c ? std::strlen(c) : 0);
  CHECK(o.pcm.size() == opt.frames * 2, "%zu samples", o.pcm.size());
}

static void testChannels() {
  std::printf("case: channels\n");
  for (int ch = 1; ch <= 8; ++ch) {
    OpusOptions opt;
    opt.channels = ch;
    opt.frames = 24000;
    OpusFixture fx;
    if (!makeOpus(opt, fx)) continue;
    auto ref = oggfx::referenceOpus(fx.file);
    if (ref.empty() || ref[0].pcm.size() != opt.frames * ch) {
      CHECK(false, "%d ch: reference decode failed", ch);
      continue;
    }
    Out o = decodeStream(fx.file, 4096);
    CHECK(o.type == BUFFER_OGG_OPUS && o.ended, "%d ch: type %d ended %d", ch,
          o.type, o.ended);
    const int outCh = std::min(ch, 2);
    CHECK(o.channels == outCh || o.pcm.empty(), "%d ch: reported %d channels",
          ch, o.channels);
    if (ch <= 2) {
      CHECK(o.pcm.size() == ref[0].pcm.size(), "%d ch: %zu samples, want %zu",
            ch, o.pcm.size(), ref[0].pcm.size());
      const size_t cmp = std::min(o.pcm.size(), ref[0].pcm.size());
      const long d = firstDiff(o.pcm, ref[0].pcm, 0,
                               cmp > kFade * ch ? cmp - kFade * ch : 0, 1e-6f);
      CHECK(d < 0, "%d ch: differs from the reference at sample %ld", ch, d);
    } else {
      // Multistream packets (one self-delimited Opus stream per channel
      // pair) need the multistream decoder; the output is the stereo
      // downmix. Compare both channels with the downmixed reference, away
      // from the end fade.
      const size_t frames = o.pcm.size() / outCh;
      double err = 0, pow = 0;
      const size_t n = std::min(frames, opt.frames);
      for (size_t i = 0; i + kFade < n; ++i)
        for (int k = 0; k < 2; ++k) {
          double want = 0;
          for (int c = 0; c < ch; ++c)
            want += ref[0].pcm[i * ch + c] * kDownmix[ch - 3][c][k];
          const double d = o.pcm[i * outCh + k] - want;
          err += d * d;
          pow += want * want;
        }
      const double rel = pow > 0 ? std::sqrt(err / pow) : 1.0;
      CHECK(frames == opt.frames && rel < 1e-4,
            "%d ch: %zu frames (want %zu), error %.3g of the downmixed "
            "reference",
            ch, frames, opt.frames, rel);
    }
  }
}

static void testChained() {
  std::printf("case: chained\n");
  OpusOptions a, b;
  a.frames = 24000; a.serial = 11; a.seed = 1;
  b.frames = 36000; b.serial = 22; b.seed = 2;
  OpusFixture fa, fb;
  if (!makeOpus(a, fa) || !makeOpus(b, fb)) return;
  oggfx::Bytes file = fa.file;
  file.insert(file.end(), fb.file.begin(), fb.file.end());
  auto ref = oggfx::referenceOpus(file);
  CHECK(ref.size() == 2, "reference has %zu links", ref.size());
  if (ref.size() != 2) return;
  std::vector<float> want = ref[0].pcm;
  want.insert(want.end(), ref[1].pcm.begin(), ref[1].pcm.end());
  for (size_t chunk : {file.size(), (size_t)4096, (size_t)1}) {
    Out o = decodeStream(file, chunk);
    CHECK(o.ended, "chunk %zu: not drained", chunk);
    CHECK(o.pcm.size() == want.size(), "chunk %zu: %zu samples, want %zu",
          chunk, o.pcm.size(), want.size());
    CHECK(o.meta.size() == 2, "chunk %zu: %zu metadata callbacks", chunk,
          o.meta.size());
    // Compare outside the 5 ms fades at the end of each link. The decoder
    // is reset at the link boundary, as in the reference.
    const size_t endA = ref[0].pcm.size();
    const size_t n = std::min(o.pcm.size(), want.size());
    long d = firstDiff(o.pcm, want, 0, std::min(n, endA - kFade * 2), 1e-6f);
    if (d < 0 && n > endA)
      d = firstDiff(o.pcm, want, endA, n > kFade * 2 ? n - kFade * 2 : 0, 1e-6f);
    CHECK(d < 0, "chunk %zu: differs from the reference at sample %ld", chunk,
          d);
  }

  // A stereo stream followed by a mono one, fed at once. A decode() call
  // returns one channel count: it must not return samples of both links.
  OpusOptions m = b;
  m.channels = 1;
  OpusFixture fm;
  if (!makeOpus(m, fm)) return;
  oggfx::Bytes mixed = fa.file;
  mixed.insert(mixed.end(), fm.file.begin(), fm.file.end());
  Out o = decodeStream(mixed, mixed.size());
  size_t frames = 0;
  for (auto &c : o.calls) frames += c.second / (size_t)std::max(1, c.first);
  CHECK(frames == a.frames + m.frames,
        "%zu frames over %zu calls (want %zu + %zu): a call mixes the "
        "stereo and mono links under one channel count",
        frames, o.calls.size(), a.frames, m.frames);
}

static void testTruncated() {
  std::printf("case: truncated\n");
  OpusOptions opt;
  opt.frames = 48000;
  OpusFixture fx;
  if (!makeOpus(opt, fx)) return;
  Out full = decodeStream(fx.file, fx.file.size());
  for (double frac : {0.6, 0.31, 0.97}) {
    oggfx::Bytes cut(fx.file.begin(), fx.file.begin() + (size_t)(fx.file.size() * frac));
    Out o = decodeStream(cut, 1000);
    CHECK(o.ended, "cut %.2f: not drained", frac);
    CHECK(o.error == DecoderError::NoError, "cut %.2f: error %d", frac,
          (int)o.error);
    CHECK(!o.pcm.empty() && o.pcm.size() < full.pcm.size(),
          "cut %.2f: %zu samples of %zu", frac, o.pcm.size(), full.pcm.size());
    const size_t n = std::min(o.pcm.size(),
                              full.pcm.size() > kFade * 2 ? full.pcm.size() - kFade * 2 : 0);
    const long d = firstDiff(o.pcm, full.pcm, 0, n);
    CHECK(d < 0, "cut %.2f: not a prefix of the full decode (sample %ld)", frac, d);
  }
}

static void testBounded() {
  std::printf("case: bounded\n");
  OpusOptions opt;
  opt.frames = 96000;
  opt.bigPages = true; // the last page holds more than 4096*4 samples
  OpusFixture fx;
  if (!makeOpus(opt, fx)) return;
  OpusDecoderWrapper w1, w2;
  Out whole = decodeWrapper(w1, fx.file, fx.file.size());
  Out bounded = decodeWrapper(w2, fx.file, fx.file.size(), 4096 * 4);
  CHECK(whole.pcm.size() == opt.frames * 2, "unbounded: %zu samples",
        whole.pcm.size());
  CHECK(bounded.pcm.size() == whole.pcm.size(), "bounded: %zu samples, want %zu",
        bounded.pcm.size(), whole.pcm.size());
  // The 5 ms end fade must land at the end of the stream, also when the
  // decode() call that read the EOS page stopped at maxOutputSamples with
  // packets of the page still queued.
  const long d = firstDiff(bounded.pcm, whole.pcm, 0,
                           std::min(bounded.pcm.size(), whole.pcm.size()));
  CHECK(d < 0,
        "bounded decode differs from the unbounded one at sample %ld of %zu "
        "(end fade applied mid-stream)",
        d, whole.pcm.size());
}

static void testRate24k() {
  std::printf("case: rate24k\n");
  OpusFixture fx;
  if (!makeOpus({}, fx)) return;
  Out o = decodeStream(fx.file, 4096, 24000, 2);
  CHECK(o.sampleRate == 24000, "decoded at %d Hz", o.sampleRate);
  CHECK(o.pcm.size() == 24000 * 2, "%zu samples, want %d", o.pcm.size(),
        24000 * 2);
  CHECK(std::fabs(o.duration - 1.0) < 1.0 / 24000 + 1e-9, "duration %f",
        o.duration);
}

// Decodes [file] from [off] after prepareForSeek(target, off) on a wrapper
// that has already decoded the whole file once (so the EOS granule is known).
static Out decodeAfterSeek(const OpusFixture &fx, double seconds,
                           uint64_t target, uint64_t *offOut) {
  OpusDecoderWrapper w;
  Out first = decodeWrapper(w, fx.file, 4096);
  Out o;
  CHECK(w.canSeekToTime(seconds), "cannot seek to %.2f s after a full decode",
        seconds);
  const uint64_t off = w.timeToByteOffset(seconds);
  *offOut = off;
  CHECK(off > 0 && off < fx.file.size(), "offset %llu of %zu",
        (unsigned long long)off, fx.file.size());
  if (off == 0 || off >= fx.file.size()) return o;
  w.prepareForSeek(target, off);
  for (size_t p = off; p < fx.file.size(); p += 4096) {
    Bytes in(fx.file.begin() + p,
             fx.file.begin() + std::min<size_t>(fx.file.size(), p + 4096));
    int sr = 0, ch = 0;
    auto r = w.decode(in, &sr, &ch);
    collect(o, r, sr, ch);
  }
  w.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
    Bytes none;
    int sr = 0, ch = 0;
    auto r = w.decode(none, &sr, &ch);
    collect(o, r, sr, ch);
  }
  return o;
}

// True when [o] ends with the reference's last 0.2 s (before the end fade).
static bool endsLikeReference(const Out &o, const std::vector<float> &rp) {
  const size_t tail = 9600 * 2;
  if (o.pcm.size() <= tail + kFade * 2 || rp.size() <= tail + kFade * 2)
    return false;
  const size_t oe = o.pcm.size() - kFade * 2, re = rp.size() - kFade * 2;
  for (size_t i = 1; i <= tail; ++i)
    if (std::fabs(o.pcm[oe - i] - rp[re - i]) > 2e-3f) return false;
  return true;
}

static void testSeek() {
  std::printf("case: seek\n");
  OpusOptions opt;
  opt.frames = 48000 * 3;
  OpusFixture fx;
  if (!makeOpus(opt, fx)) return;
  auto ref = oggfx::referenceOpus(fx.file);
  if (ref.empty()) { CHECK(false, "no reference"); return; }
  const double t = 1.5;
  uint64_t off = 0;
  // The index maps a granule to the start of the page before the one
  // holding it (OggSeekIndex pairs a page's start byte with its end
  // granule), so decoding resumes up to a page before the target. The
  // wrapper must count from where it resumes, or the EOS trim cuts the end
  // of the file.
  //
  // A target before the resume page (here half the time, as a target in
  // frames reads in interleaved samples): the output runs from that page
  // to the end of the file.
  Out early = decodeAfterSeek(fx, t, (uint64_t)(t * 48000), &off);
  CHECK(!early.pcm.empty(), "early target: nothing decoded after the seek");
  CHECK(endsLikeReference(early, ref[0].pcm),
        "early target: the output (%zu samples) does not end with the "
        "file's last samples", early.pcm.size());
  // PullBufferStream passes the target in interleaved samples, seconds *
  // rate * channels: the samples before the target are dropped, and the
  // output is the file from the target to its end.
  Out byInterleaved = decodeAfterSeek(fx, t, (uint64_t)(t * 48000) * 2, &off);
  const size_t want = (opt.frames - (size_t)(t * 48000)) * 2;
  CHECK(byInterleaved.pcm.size() == want,
        "target in interleaved samples: %zu samples after the seek, want %zu",
        byInterleaved.pcm.size(), want);
  CHECK(endsLikeReference(byInterleaved, ref[0].pcm),
        "target in interleaved samples: the output does not end with the "
        "file's last samples");
  // Away from the decoder restart, the output is the reference from the
  // target.
  const size_t from = (size_t)(t * 48000) * 2;
  long d = -1;
  for (size_t i = 9600; i < std::min(byInterleaved.pcm.size(), want) - kFade * 2;
       ++i)
    if (std::fabs(byInterleaved.pcm[i] - ref[0].pcm[from + i]) > 2e-3f) {
      d = (long)i;
      break;
    }
  CHECK(d < 0, "target in interleaved samples: differs from the reference "
        "at the target at sample %ld", d);
}

int main(int argc, char **argv) {
  parseArgs(argc, argv);
  testBasic();
  testChunks();
  testPreSkip();
  testLongComment();
  testChannels();
  testChained();
  testTruncated();
  testBounded();
  testRate24k();
  testSeek();
  return finish("opus_stream_decoder_test");
}

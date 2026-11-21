// Unit tests for the Ogg Vorbis stream decoder (src/audiobuffer/
// vorbis_stream_decoder.cpp, VorbisDecoderWrapper), driven through
// StreamDecoder the way BufferStream does and directly. Every fixture is
// encoded in memory at test time with libvorbisenc + libogg
// (test/support/ogg_stream_fixtures.h); the reference is an independent
// demux + libvorbis decode of the same file. The wrapper fades the first
// 2 ms in and the last 5 ms out; comparisons leave those out.
//
// Verifies:
//   basic        a stereo file decodes to the reference, at its rate and
//                channel count, with vendor and comments reported once and
//                the duration of the last granule
//   headers      the three headers (identification, comment, setup) split
//                across pieces of 1, 7, 333, 4096 and 65536 bytes, with
//                the comment and setup headers on their own pages or sharing
//                pages, give the same samples as one piece
//   comments     a 300 KB comment reaches the callback intact
//   formats      1-8 channels, 8 to 96 kHz, short and long blocks (the
//                signal has clicks every 0.25 s)
//   chained      two concatenated streams decode one after the other and
//                report their tags twice, fed in pieces and in one piece;
//                streams with different channel counts are never mixed in
//                one decode() call
//   truncated    input cut mid-page drains and is a prefix of the full decode
//   bounded      maxOutputSamples (BufferStream's 4096*4 drain cap) gives
//                the same samples as an unbounded decode
//
// Needs no audio device and no assets. Needs libvorbis, libvorbisenc,
// libogg (and the other Xiph libraries the stream decoder set links); the
// script skips without.
//
//   ./test/common/vorbis_stream_decoder_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/stream_decoder.h"
#include "audiobuffer/vorbis_stream_decoder.h"

#include "../support/ogg_stream_fixtures.h"
#include "../support/stream_decoder_test_harness.h"

using namespace sdh;
using oggfx::VorbisFixture;
using oggfx::VorbisOptions;

static bool makeVorbis(const VorbisOptions &o, VorbisFixture &fx) {
  if (!oggfx::encodeVorbis(o, fx)) {
    std::printf("  FAIL: cannot encode the Vorbis fixture (%d ch, %d Hz)\n",
                o.channels, o.rate);
    ++gFailures;
    return false;
  }
  return true;
}

static size_t fadeInFloats(int rate, int ch) { return (size_t)(rate * 0.002) * ch; }
static size_t fadeOutFloats(int rate, int ch) { return (size_t)(rate * 0.005) * ch; }

// [o] equals [ref] outside the fade-in at its start and the fade-out at
// its end. Returns the first differing sample, or -1.
static long diffOutsideFades(const std::vector<float> &o,
                             const std::vector<float> &ref, int rate, int ch) {
  const size_t n = std::min(o.size(), ref.size());
  const size_t from = fadeInFloats(rate, ch), out = fadeOutFloats(rate, ch);
  if (n <= from + out) return n ? 0 : -1;
  return firstDiff(o, ref, from, n - out);
}

static void testBasic() {
  std::printf("case: basic\n");
  VorbisFixture fx;
  if (!makeVorbis({}, fx)) return;
  auto ref = oggfx::referenceVorbis(fx.file);
  CHECK(ref.size() == 1, "reference has %zu links", ref.size());
  if (ref.empty()) return;
  CHECK(ref[0].pcm.size() == (size_t)fx.finalGranule * 2,
        "reference has %zu samples, last granule %lld", ref[0].pcm.size(),
        (long long)fx.finalGranule);
  Out o = decodeStream(fx.file, fx.file.size(), 44100, 2);
  CHECK(o.type == BUFFER_OGG_VORBIS, "type %d", o.type);
  CHECK(o.error == DecoderError::NoError, "error %d", (int)o.error);
  CHECK(o.ended, "decoder still has pending data");
  CHECK(o.sampleRate == 44100 && o.channels == 2, "%d Hz %d ch", o.sampleRate,
        o.channels);
  CHECK(o.pcm.size() == ref[0].pcm.size(), "%zu samples, reference %zu",
        o.pcm.size(), ref[0].pcm.size());
  const long d = diffOutsideFades(o.pcm, ref[0].pcm, 44100, 2);
  CHECK(d < 0, "differs from the reference at sample %ld", d);
  CHECK(o.finite, "non-finite samples");
  CHECK(o.meta.size() == 1, "%zu metadata callbacks", o.meta.size());
  if (!o.meta.empty()) {
    CHECK(o.meta[0].type == BUFFER_OGG_VORBIS, "metadata type %d", o.meta[0].type);
    CHECK(o.meta[0].oggMetadata.vendor == ref[0].vendor, "vendor '%s'",
          o.meta[0].oggMetadata.vendor.c_str());
    const char *t = metaComment(o.meta, 0, "TITLE");
    CHECK(t && std::string(t) == "Vorbis fixture", "TITLE '%s'", t ? t : "(none)");
    CHECK(o.meta[0].oggMetadata.vorbisInfo.rate == 44100 &&
              o.meta[0].oggMetadata.vorbisInfo.channels == 2,
          "vorbisInfo %ld Hz %d ch", o.meta[0].oggMetadata.vorbisInfo.rate,
          o.meta[0].oggMetadata.vorbisInfo.channels);
  }
  CHECK(std::fabs(o.duration - double(fx.finalGranule) / 44100) < 1e-9,
        "duration %f", o.duration);
}

static void testHeaders() {
  std::printf("case: headers\n");
  for (bool flush : {true, false}) {
    VorbisOptions opt;
    opt.frames = 30000;
    opt.flushHeaders = flush;
    VorbisFixture fx;
    if (!makeVorbis(opt, fx)) return;
    Out whole = decodeStream(fx.file, fx.file.size(), 44100, 2);
    CHECK(whole.pcm.size() == (size_t)fx.finalGranule * 2,
          "flush %d: %zu samples, last granule %lld", flush, whole.pcm.size(),
          (long long)fx.finalGranule);
    for (size_t chunk : {(size_t)1, (size_t)7, (size_t)333, (size_t)4096,
                         (size_t)65536}) {
      Out o = decodeStream(fx.file, chunk, 44100, 2);
      CHECK(o.ended, "flush %d chunk %zu: not drained", flush, chunk);
      CHECK(o.pcm.size() == whole.pcm.size(),
            "flush %d chunk %zu: %zu samples, want %zu", flush, chunk,
            o.pcm.size(), whole.pcm.size());
      const long d = firstDiff(o.pcm, whole.pcm, 0,
                               std::min(o.pcm.size(), whole.pcm.size()));
      CHECK(d < 0, "flush %d chunk %zu: differs at sample %ld", flush, chunk, d);
      CHECK(o.meta.size() == 1, "flush %d chunk %zu: %zu metadata callbacks",
            flush, chunk, o.meta.size());
    }
    // The wrapper alone, from the first byte, one byte at a time.
    VorbisDecoderWrapper w;
    Out o = decodeWrapper(w, fx.file, 1, 0, 44100, 2);
    CHECK(o.pcm.size() == whole.pcm.size(),
          "flush %d wrapper 1-byte: %zu samples, want %zu", flush, o.pcm.size(),
          whole.pcm.size());
  }
}

static void testLongComment() {
  std::printf("case: comments\n");
  VorbisOptions opt;
  opt.frames = 8000;
  opt.commentBytes = 300000;
  VorbisFixture fx;
  if (!makeVorbis(opt, fx)) return;
  Out o = decodeStream(fx.file, 7, 44100, 2);
  CHECK(o.type == BUFFER_OGG_VORBIS, "type %d", o.type);
  CHECK(o.meta.size() == 1, "%zu metadata callbacks", o.meta.size());
  const char *c = metaComment(o.meta, 0, "LONGCOMMENT");
  CHECK(c && std::strlen(c) == opt.commentBytes, "LONGCOMMENT has %zu bytes",
        c ? std::strlen(c) : 0);
  CHECK(o.pcm.size() == (size_t)fx.finalGranule * 2, "%zu samples",
        o.pcm.size());
}

static void testFormats() {
  std::printf("case: formats\n");
  struct F { int ch, rate; };
  const F formats[] = {{1, 44100}, {2, 44100}, {3, 44100}, {4, 44100},
                       {5, 44100}, {6, 44100}, {7, 44100}, {8, 44100},
                       {1, 8000},  {2, 22050}, {2, 48000}, {2, 96000}};
  for (const F &f : formats) {
    VorbisOptions opt;
    opt.channels = f.ch;
    opt.rate = f.rate;
    opt.frames = (size_t)f.rate; // 1 s: 4 clicks, so short blocks too
    VorbisFixture fx;
    if (!makeVorbis(opt, fx)) continue;
    auto ref = oggfx::referenceVorbis(fx.file);
    if (ref.empty()) { CHECK(false, "%d ch %d Hz: no reference", f.ch, f.rate); continue; }
    Out o = decodeStream(fx.file, 4096, 48000, 2);
    CHECK(o.ended && o.type == BUFFER_OGG_VORBIS, "%d ch %d Hz: ended %d type %d",
          f.ch, f.rate, o.ended, o.type);
    CHECK(o.sampleRate == f.rate && o.channels == f.ch,
          "%d ch %d Hz: reported %d Hz %d ch", f.ch, f.rate, o.sampleRate,
          o.channels);
    CHECK(o.pcm.size() == ref[0].pcm.size(), "%d ch %d Hz: %zu samples, want %zu",
          f.ch, f.rate, o.pcm.size(), ref[0].pcm.size());
    const long d = diffOutsideFades(o.pcm, ref[0].pcm, f.rate, f.ch);
    CHECK(d < 0, "%d ch %d Hz: differs from the reference at sample %ld", f.ch,
          f.rate, d);
  }
}

static void testChained() {
  std::printf("case: chained\n");
  VorbisOptions a, b;
  a.frames = 30000; a.serial = 11; a.seed = 1;
  b.frames = 50000; b.serial = 22; b.seed = 2;
  VorbisFixture fa, fb;
  if (!makeVorbis(a, fa) || !makeVorbis(b, fb)) return;
  oggfx::Bytes file = fa.file;
  file.insert(file.end(), fb.file.begin(), fb.file.end());
  auto ref = oggfx::referenceVorbis(file);
  CHECK(ref.size() == 2, "reference has %zu links", ref.size());
  if (ref.size() != 2) return;
  const size_t lenA = ref[0].pcm.size(), lenB = ref[1].pcm.size();
  for (size_t chunk : {(size_t)4096, (size_t)1}) {
    Out o = decodeStream(file, chunk, 44100, 2);
    CHECK(o.ended, "chunk %zu: not drained", chunk);
    CHECK(o.pcm.size() == lenA + lenB, "chunk %zu: %zu samples, want %zu",
          chunk, o.pcm.size(), lenA + lenB);
    CHECK(o.meta.size() == 2, "chunk %zu: %zu metadata callbacks", chunk,
          o.meta.size());
    if (o.pcm.size() == lenA + lenB) {
      std::vector<float> oa(o.pcm.begin(), o.pcm.begin() + lenA);
      std::vector<float> ob(o.pcm.begin() + lenA, o.pcm.end());
      const long da = diffOutsideFades(oa, ref[0].pcm, 44100, 2);
      const long db = diffOutsideFades(ob, ref[1].pcm, 44100, 2);
      CHECK(da < 0 && db < 0, "chunk %zu: link 1 differs at %ld, link 2 at %ld",
            chunk, da, db);
    }
  }

  // In one piece: the trim to the EOS granule must compare each link's
  // granule, which counts from that link's start, with that link's samples
  // only, not cut the output of both links to the length of the second.
  Out one = decodeStream(file, file.size(), 44100, 2);
  CHECK(one.pcm.size() == lenA + lenB,
        "one piece: %zu samples, want %zu + %zu (cut to the last link's "
        "granule)", one.pcm.size(), lenA, lenB);

  // A stereo stream followed by a mono one. The first piece ends where the
  // mono stream's audio starts: the call that decodes it returns the rest of
  // the stereo link and parses the mono link's headers. It must report the
  // stereo link's channel count (and all its samples).
  VorbisOptions m = b;
  m.channels = 1;
  VorbisFixture fm;
  if (!makeVorbis(m, fm)) return;
  oggfx::Bytes mixed = fa.file;
  mixed.insert(mixed.end(), fm.file.begin(), fm.file.end());
  size_t headersEnd = 0; // first page with a granule > 0 in the mono stream
  for (size_t p = 0; p + 27 <= fm.file.size();) {
    const unsigned char *h = fm.file.data() + p;
    int64_t g = 0;
    for (int i = 0; i < 8; ++i) g |= (int64_t)h[6 + i] << (8 * i);
    if (g > 0) { headersEnd = p; break; }
    size_t body = 0;
    for (int i = 0; i < h[26]; ++i) body += h[27 + i];
    p += 27 + h[26] + body;
  }
  CHECK(headersEnd > 0, "mixed: no audio page in the mono stream");
  VorbisDecoderWrapper w;
  w.initializeDecoder(44100, 2);
  Out o;
  Bytes p1(mixed.begin(), mixed.begin() + fa.file.size() + headersEnd);
  Bytes p2(mixed.begin() + fa.file.size() + headersEnd, mixed.end());
  int sr = 0, ch = 0;
  auto r1 = w.decode(p1, &sr, &ch);
  collect(o, r1, sr, ch);
  const size_t lenStereo = (size_t)fa.finalGranule * 2;
  // Not the channel count of the link whose headers were parsed last, nor
  // the call's samples trimmed with that link's channel count.
  CHECK(o.calls.size() == 1 && o.calls[0].first == 2 &&
            o.calls[0].second == lenStereo,
        "the call ending the stereo link returned %zu samples labelled %d "
        "channel(s); want %zu samples, 2 channels",
        o.calls.empty() ? 0 : o.calls[0].second,
        o.calls.empty() ? 0 : o.calls[0].first, lenStereo);
  auto r2 = w.decode(p2, &sr, &ch);
  CHECK(ch == 1 && r2.first.size() > 0, "mono link: %zu samples, %d ch",
        r2.first.size(), ch);
}

static void testTruncated() {
  std::printf("case: truncated\n");
  VorbisOptions opt;
  opt.frames = 44100 * 5;
  VorbisFixture fx;
  if (!makeVorbis(opt, fx)) return;
  Out full = decodeStream(fx.file, fx.file.size(), 44100, 2);
  for (double frac : {0.6, 0.33, 0.97}) {
    oggfx::Bytes cut(fx.file.begin(), fx.file.begin() + (size_t)(fx.file.size() * frac));
    Out o = decodeStream(cut, 1000, 44100, 2);
    CHECK(o.ended, "cut %.2f: not drained", frac);
    CHECK(o.error == DecoderError::NoError, "cut %.2f: error %d", frac,
          (int)o.error);
    CHECK(!o.pcm.empty() && o.pcm.size() < full.pcm.size(),
          "cut %.2f: %zu samples of %zu", frac, o.pcm.size(), full.pcm.size());
    const size_t n = std::min(o.pcm.size(), full.pcm.size() -
                                                fadeOutFloats(44100, 2));
    const long d = firstDiff(o.pcm, full.pcm, 0, n);
    CHECK(d < 0, "cut %.2f: not a prefix of the full decode (sample %ld)", frac, d);
  }
}

static void testBounded() {
  std::printf("case: bounded\n");
  VorbisOptions opt;
  opt.frames = 88200;
  opt.bigPages = true; // the last page holds more than 4096*4 samples
  VorbisFixture fx;
  if (!makeVorbis(opt, fx)) return;
  VorbisDecoderWrapper w1, w2;
  Out whole = decodeWrapper(w1, fx.file, fx.file.size(), 0, 44100, 2);
  Out bounded = decodeWrapper(w2, fx.file, fx.file.size(), 4096 * 4, 44100, 2);
  CHECK(whole.pcm.size() == (size_t)fx.finalGranule * 2,
        "unbounded: %zu samples", whole.pcm.size());
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

int main(int argc, char **argv) {
  parseArgs(argc, argv);
  testBasic();
  testHeaders();
  testLongComment();
  testFormats();
  testChained();
  testTruncated();
  testBounded();
  return finish("vorbis_stream_decoder_test");
}

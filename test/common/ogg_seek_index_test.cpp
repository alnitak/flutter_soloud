// Unit tests for the Ogg granule -> byte index (src/audiobuffer/ogg_seek_index.h,
// OggSeekIndex) that the Opus and Vorbis stream decoders use to map a seek
// time to a byte offset, and for its use by OpusDecoderWrapper
// (canSeekToTime / timeToByteOffset).
//
// Verifies:
//   real-pages   pages made by libogg, read with ogg_sync_pageseek the way
//                the decoders do (skipped garbage counted by the caller):
//                every entry's offset is the start of a page ("OggS") whose
//                granule is the entry's; pages with granule -1 (a packet
//                continued on the next page) are not indexed; repeated
//                granules are indexed once
//   interpolate  for a target inside the index, byteOffsetFor() returns an
//                offset between the start of the previous indexed page and
//                the start of the page that completes the target (never
//                after it), and is monotonic in the target
//   covers       covers() at the edges: first granule - 1, first, last, last
//                + 1; empty index; byteOffsetFor() is 0 when not covered
//   clear        clear(offset) then reuse: entries start again, offsets
//                counted from the given offset
//   out-of-order granules that go backwards (hand-made pages): no crash or
//                UB (UBSan), the result stays within the indexed bytes
//   zero-size    a page of size 0 changes no entry
//   chained      two links with different serials: the index holds the raw
//                per-link granules. Through OpusDecoderWrapper, a time in the
//                second link must map to a byte offset in the second link at
//                that link's local time
//   decoder      a single-link Opus file decoded by OpusDecoderWrapper:
//                timeToByteOffset() lands at or before the page that holds
//                the target, and canSeekToTime() is false past the end
//
// Needs no audio device and no assets. Needs the Xiph libraries (the
// decoder cases encode Opus in memory); without them it is skipped.
//
//   ./test/common/ogg_seek_index_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/ogg_seek_index.h"
#include "audiobuffer/opus_stream_decoder.h"

#include "../support/ogg_stream_fixtures.h"
#include "../support/stream_decoder_test_harness.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using sdh::Bytes;

struct PageInfo {
  size_t offset, size;
  int64_t granule;
  uint32_t serial;
};

// Lists the pages of [data] by scanning for valid headers (no CRC check;
// the test's own data).
static std::vector<PageInfo> listPages(const Bytes &data) {
  std::vector<PageInfo> pages;
  size_t p = 0;
  while (p + 27 <= data.size()) {
    if (std::memcmp(&data[p], "OggS", 4) != 0) { ++p; continue; }
    const int nseg = data[p + 26];
    if (p + 27 + nseg > data.size()) break;
    size_t body = 0;
    for (int i = 0; i < nseg; ++i) body += data[p + 27 + i];
    const size_t size = 27 + nseg + body;
    if (p + size > data.size()) break;
    int64_t g = 0;
    for (int i = 7; i >= 0; --i) g = (g << 8) | data[p + 6 + i];
    uint32_t serial = data[p + 14] | (data[p + 15] << 8) | (data[p + 16] << 16) |
                      (uint32_t(data[p + 17]) << 24);
    pages.push_back({p, size, g, serial});
    p += size;
  }
  return pages;
}

// Feeds [data] to an index in [chunk]-byte pieces with ogg_sync_pageseek,
// as OpusDecoderWrapper::decode and VorbisDecoderWrapper::decode do.
static void indexBytes(OggSeekIndex &idx, const Bytes &data, size_t chunk) {
  ogg_sync_state oy;
  ogg_sync_init(&oy);
  for (size_t off = 0; off < data.size(); off += chunk) {
    const size_t n = std::min(chunk, data.size() - off);
    char *buf = ogg_sync_buffer(&oy, static_cast<long>(n));
    std::memcpy(buf, &data[off], n);
    ogg_sync_wrote(&oy, static_cast<long>(n));
    ogg_page og;
    for (;;) {
      const long ret = ogg_sync_pageseek(&oy, &og);
      if (ret == 0) break;
      if (ret < 0) {
        idx.bytesConsumed += static_cast<uint64_t>(-ret);
        continue;
      }
      idx.trackPage(ret, og);
    }
  }
  ogg_sync_clear(&oy);
}

// A plain Ogg stream (no codec): packets of random size with increasing
// granules, some packets spanning several pages (granule -1 pages), a few
// packets with the same granule. Garbage bytes between some pages.
static Bytes makeStream(uint32_t serial, int64_t firstGranule, int packets,
                        unsigned seed, bool garbage, int64_t step = 960) {
  std::mt19937 rng(seed);
  ogg_stream_state os;
  ogg_stream_init(&os, static_cast<int>(serial));
  Bytes out;
  auto flushPages = [&](bool force) {
    ogg_page og;
    while (force ? ogg_stream_flush(&os, &og) : ogg_stream_pageout(&os, &og)) {
      oggfx::writePage(out, og);
      if (garbage && rng() % 4 == 0)
        for (int i = 0, n = 1 + rng() % 40; i < n; ++i)
          out.push_back(static_cast<unsigned char>('A' + rng() % 20));
    }
  };
  int64_t granule = firstGranule;
  for (int i = 0; i < packets; ++i) {
    const size_t size = rng() % 25 == 0 ? 70000 + rng() % 30000 : 20 + rng() % 400;
    Bytes data(size);
    for (auto &b : data) b = static_cast<unsigned char>(rng());
    if (rng() % 7 != 0) granule += step; // sometimes repeat the granule
    ogg_packet op{};
    op.packet = data.data();
    op.bytes = static_cast<long>(size);
    op.b_o_s = i == 0;
    op.e_o_s = i == packets - 1;
    op.granulepos = granule;
    op.packetno = i;
    ogg_stream_packetin(&os, &op);
    flushPages(i == packets - 1 || rng() % 5 == 0);
  }
  ogg_stream_clear(&os);
  return out;
}

static void testRealPages() {
  std::printf("real-pages\n");
  const Bytes data = makeStream(77, 0, 400, 1, true);
  const auto pages = listPages(data);
  int negPages = 0;
  for (const auto &p : pages) negPages += p.granule < 0;
  CHECK(negPages > 0, "test mistake: no granule -1 pages");
  for (size_t chunk : {size_t(1), size_t(7), size_t(4096), data.size()}) {
    OggSeekIndex idx;
    indexBytes(idx, data, chunk);
    // Garbage after the last page is skipped, except for bytes that could
    // still start a capture pattern.
    const size_t end = pages.back().offset + pages.back().size;
    CHECK(idx.bytesConsumed >= end && idx.bytesConsumed <= data.size(),
          "chunk %zu: bytesConsumed %llu, last page ends at %zu of %zu", chunk,
          (unsigned long long)idx.bytesConsumed, end, data.size());
    // Expected entries: each page with granule >= 0 whose granule differs
    // from the previous indexed one.
    std::vector<OggSeekIndex::Entry> expect;
    for (const auto &p : pages)
      if (p.granule >= 0 && (expect.empty() || expect.back().granule != p.granule))
        expect.push_back({p.granule, p.offset});
    bool same = idx.entries.size() == expect.size();
    for (size_t i = 0; same && i < expect.size(); ++i)
      same = idx.entries[i].granule == expect[i].granule &&
             idx.entries[i].byteOffset == expect[i].byteOffset;
    CHECK(same, "chunk %zu: %zu entries, expected %zu (or offsets differ)", chunk,
          idx.entries.size(), expect.size());
    for (const auto &e : idx.entries)
      if (e.byteOffset + 4 > data.size() || std::memcmp(&data[e.byteOffset], "OggS", 4) != 0) {
        CHECK(false, "chunk %zu: entry offset %llu is not a page start", chunk,
              (unsigned long long)e.byteOffset);
        break;
      }
  }
}

static void testInterpolate() {
  std::printf("interpolate\n");
  const Bytes data = makeStream(5, 0, 300, 2, false);
  const auto pages = listPages(data);
  OggSeekIndex idx;
  indexBytes(idx, data, 4096);
  CHECK(!idx.entries.empty(), "no entries");
  if (idx.entries.empty()) return;
  uint64_t prev = 0;
  int bad = 0, nonMono = 0;
  for (int64_t t = idx.entries.front().granule; t <= idx.entries.back().granule; t += 37) {
    const uint64_t off = idx.byteOffsetFor(t);
    // The page that completes sample t: first page with granule >= t. The
    // lower bound: the first page of the last run of pages with the highest
    // granule below t (a run of equal granules is indexed by its first page).
    size_t holder = 0, before = 0;
    int64_t beforeGranule = -1;
    bool found = false;
    for (size_t i = 0; i < pages.size(); ++i) {
      if (pages[i].granule >= t) { holder = pages[i].offset; found = true; break; }
      if (pages[i].granule >= 0 && pages[i].granule != beforeGranule) {
        before = pages[i].offset;
        beforeGranule = pages[i].granule;
      }
    }
    if (!found || off > holder || off < before) {
      if (++bad <= 3)
        CHECK(false, "target %lld: offset %llu not in [%zu, %zu]", (long long)t,
              (unsigned long long)off, before, holder);
    }
    if (off < prev) ++nonMono;
    prev = off;
  }
  CHECK(bad == 0, "%d targets out of range", bad);
  CHECK(nonMono == 0, "%d non-monotonic steps", nonMono);
  // An exact entry granule maps to that page's start (or earlier).
  for (size_t i = 0; i < idx.entries.size(); ++i)
    CHECK(idx.byteOffsetFor(idx.entries[i].granule) <= idx.entries[i].byteOffset,
          "entry %zu: offset after its page", i);
}

static void testCovers() {
  std::printf("covers\n");
  OggSeekIndex empty;
  CHECK(!empty.covers(0) && !empty.covers(-1) && !empty.covers(INT64_MAX), "empty covers");
  CHECK(empty.byteOffsetFor(0) == 0, "empty byteOffsetFor");
  const Bytes data = makeStream(9, 48000, 100, 3, false);
  OggSeekIndex idx;
  indexBytes(idx, data, 333);
  const int64_t first = idx.entries.front().granule, last = idx.entries.back().granule;
  int64_t firstPage = -1;
  for (const auto &p : listPages(data))
    if (p.granule >= 0) { firstPage = p.granule; break; }
  CHECK(first == firstPage, "first granule %lld, first page %lld", (long long)first,
        (long long)firstPage);
  CHECK(!idx.covers(first - 1), "first - 1 covered");
  CHECK(idx.covers(first), "first not covered");
  CHECK(idx.covers(last), "last not covered");
  CHECK(!idx.covers(last + 1), "last + 1 covered");
  CHECK(idx.byteOffsetFor(first - 1) == 0, "first - 1: %llu", (unsigned long long)idx.byteOffsetFor(first - 1));
  CHECK(idx.byteOffsetFor(last + 1) == 0, "last + 1");
  CHECK(idx.byteOffsetFor(first) == idx.entries.front().byteOffset, "first offset");
  CHECK(idx.byteOffsetFor(last) <= idx.entries.back().byteOffset, "last offset");
  // One entry only.
  OggSeekIndex one;
  one.entries.push_back({1000, 50});
  CHECK(one.covers(1000) && !one.covers(999) && !one.covers(1001), "single entry covers");
  CHECK(one.byteOffsetFor(1000) == 50, "single entry offset %llu", (unsigned long long)one.byteOffsetFor(1000));
}

static void testClear() {
  std::printf("clear\n");
  const Bytes data = makeStream(3, 0, 120, 4, false);
  OggSeekIndex idx;
  indexBytes(idx, data, 4096);
  const auto firstPass = idx.entries;
  idx.clear();
  CHECK(idx.entries.empty() && idx.bytesConsumed == 0, "clear()");
  CHECK(!idx.covers(firstPass.front().granule), "covers after clear");
  idx.clear(100000);
  indexBytes(idx, data, 4096);
  bool same = idx.entries.size() == firstPass.size();
  for (size_t i = 0; same && i < firstPass.size(); ++i)
    same = idx.entries[i].granule == firstPass[i].granule &&
           idx.entries[i].byteOffset == firstPass[i].byteOffset + 100000;
  CHECK(same, "reuse after clear(100000): offsets not shifted by the seek offset");
  CHECK(idx.bytesConsumed == 100000 + data.size(), "bytesConsumed %llu",
        (unsigned long long)idx.bytesConsumed);
}

// A hand-made ogg_page: only header_len/body_len and the granule field are
// read by trackPage().
struct FakePage {
  unsigned char header[27] = {'O', 'g', 'g', 'S'};
  unsigned char body[1] = {0};
  ogg_page page{};
  FakePage(int64_t granule, long headerLen, long bodyLen) {
    for (int i = 0; i < 8; ++i) header[6 + i] = static_cast<unsigned char>(uint64_t(granule) >> (8 * i));
    page.header = header;
    page.header_len = headerLen;
    page.body = body;
    page.body_len = bodyLen;
  }
};

static void testOutOfOrder() {
  std::printf("out-of-order, zero-size\n");
  OggSeekIndex idx;
  const int64_t granules[] = {100, 500, 200, 600, 50, 700, 700, 650, -1, 800};
  for (int64_t g : granules) {
    FakePage fp(g, 27, 973);
    idx.trackPage(1000, fp.page);
  }
  CHECK(idx.entries.size() == 8, "%zu entries (repeated 700 and -1 skipped)", idx.entries.size());
  for (int64_t t = 0; t <= 900; t += 10) {
    const uint64_t off = idx.byteOffsetFor(t);
    if (idx.covers(t))
      CHECK(off <= idx.entries.back().byteOffset, "target %lld offset %llu past the last entry",
            (long long)t, (unsigned long long)off);
    else
      CHECK(off == 0, "target %lld not covered but offset %llu", (long long)t, (unsigned long long)off);
  }
  // Zero-size page: counted bytes only.
  OggSeekIndex z;
  FakePage fp(1234, 0, 0);
  z.trackPage(0, fp.page);
  CHECK(z.entries.empty() && z.bytesConsumed == 0, "zero-size page indexed");
  // A granule equal to INT64_MAX and huge byte offsets.
  OggSeekIndex big;
  big.clear(uint64_t(1) << 40);
  FakePage a(0, 27, 100), b(INT64_MAX, 27, 100);
  big.trackPage(127, a.page);
  big.trackPage(127, b.page);
  const uint64_t mid = big.byteOffsetFor(INT64_MAX / 2);
  CHECK(mid >= (uint64_t(1) << 40) && mid <= (uint64_t(1) << 40) + 127, "huge granules: %llu",
        (unsigned long long)mid);
}

// The chained case at the index level: entries hold per-link granules.
static void testChainedIndex() {
  std::printf("chained (index)\n");
  const Bytes l1 = makeStream(1, 0, 60, 5, false);
  const Bytes l2 = makeStream(2, 0, 180, 6, false);
  Bytes data = l1;
  data.insert(data.end(), l2.begin(), l2.end());
  OggSeekIndex idx;
  indexBytes(idx, data, 4096);
  bool backwards = false;
  for (size_t i = 1; i < idx.entries.size(); ++i)
    backwards |= idx.entries[i].granule < idx.entries[i - 1].granule;
  CHECK(backwards, "test mistake: granules never go back at the link boundary");
  CHECK(idx.bytesConsumed == data.size(), "bytesConsumed");
}

// --- Through OpusDecoderWrapper ------------------------------------------

static bool encode(size_t frames, int serial, oggfx::OpusFixture &fx) {
  oggfx::OpusOptions o;
  o.channels = 2;
  o.frames = frames;
  o.serial = serial;
  o.seed = serial;
  return oggfx::encodeOpus(o, fx);
}

// Start of the page of [link] that completes local granule [g], or -1.
static long pageHolding(const Bytes &link, int64_t g) {
  for (const auto &p : listPages(link))
    if (p.granule >= g) return static_cast<long>(p.offset);
  return -1;
}

static void testDecoderSingle() {
  std::printf("decoder (single link)\n");
  oggfx::OpusFixture fx;
  if (!encode(48000 * 3, 11, fx)) { CHECK(false, "encode failed"); return; }
  OpusDecoderWrapper w;
  sdh::Out o = sdh::decodeWrapper(w, fx.file, 4096);
  CHECK(o.error == DecoderError::NoError, "decode error %d", (int)o.error);
  for (double t : {0.25, 1.0, 1.5, 2.9}) {
    const int64_t g = static_cast<int64_t>(t * 48000) + fx.preSkip;
    const long holder = pageHolding(fx.file, g);
    CHECK(w.canSeekToTime(t), "t=%.2f not seekable", t);
    const uint64_t off = w.timeToByteOffset(t);
    CHECK(holder >= 0 && off <= static_cast<uint64_t>(holder) && off > 0,
          "t=%.2f: offset %llu, page holding the target at %ld", t, (unsigned long long)off, holder);
  }
  CHECK(!w.canSeekToTime(3.5), "3.5 s of a 3 s file seekable");
}

static void testDecoderChained() {
  std::printf("chained (decoder)\n");
  oggfx::OpusFixture a, b;
  if (!encode(48000, 21, a) || !encode(48000 * 3, 22, b)) { CHECK(false, "encode failed"); return; }
  Bytes file = a.file;
  file.insert(file.end(), b.file.begin(), b.file.end());
  OpusDecoderWrapper w;
  sdh::Out o = sdh::decodeWrapper(w, file, 4096);
  CHECK(o.error == DecoderError::NoError, "decode error %d", (int)o.error);
  CHECK(o.pcm.size() >= 2 * 48000 * 4 - 2 * 4800, "test mistake: chained file decoded to %zu samples",
        o.pcm.size());
  // A time inside the first link maps inside the first link.
  {
    const uint64_t off = w.timeToByteOffset(0.5);
    const long holder = pageHolding(a.file, static_cast<int64_t>(0.5 * 48000) + a.preSkip);
    CHECK(holder >= 0 && off <= static_cast<uint64_t>(holder), "t=0.5: offset %llu, holder %ld",
          (unsigned long long)off, holder);
  }
  // 1.5 s from the start is 0.5 s into the second link. The index stores
  // each link's own granules; the wrapper must index the time from the
  // start of the file, or it finds the second link's page at granule 72000,
  // i.e. 1.5 s into that link (2.5 s from the start).
  const double t = 1.5;
  const uint64_t off = w.timeToByteOffset(t);
  const bool seekable = w.canSeekToTime(t);
  const long local = pageHolding(b.file, static_cast<int64_t>(0.5 * 48000) + b.preSkip);
  const uint64_t lo = a.file.size();
  const uint64_t hi = a.file.size() + static_cast<uint64_t>(local);
  CHECK(seekable && off >= lo && off <= hi,
        "t=1.5 s maps to byte %llu; the page holding it is at %llu (link 2 starts at %llu)",
        (unsigned long long)off, (unsigned long long)hi, (unsigned long long)lo);
}

int main(int argc, char **argv) {
  sdh::parseArgs(argc, argv);
  testRealPages();
  testInterpolate();
  testCovers();
  testClear();
  testOutOfOrder();
  testChainedIndex();
  testDecoderSingle();
  testDecoderChained();
  return sdh::finish("ogg_seek_index_test");
}

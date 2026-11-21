// Robustness tests of the gapless-info parser of the native streams
// (src/native_stream/m4a_gapless.cpp: parseM4aGapless(), parseM4aGaplessFd(),
// parseM4aGaplessMoov(), M4aGaplessInfo::resolve()), on synthetic files.
//
// Verifies:
//   valid          a file built here (ftyp, moov/trak/.../stbl, mdat): media
//                  and movie timescales, duration, edit list (v0 and v1, with
//                  an empty edit first), iTunSMPB in MP4 and QuickTime meta,
//                  the first audio track after a video one, resolve()
//   sizes          box size 0 (to the end of the file) and 1 (64-bit), an
//                  mdat or moov that claims more bytes than the file has, a
//                  trailing box cut short, sizes smaller than a box header
//   nesting        62 to 200 nested containers
//   tables         stsz/stts/stsc/stco that disagree or are missing
//   truncated      every prefix of a file, and of its moov payload: no read
//                  past the data (run with --asan)
//
// Needs no audio device and no assets.
//
//   ./test/common/m4a_gapless_test.sh [--asan]

#include "native_stream/m4a_gapless.h"

#include "../support/mp4_fixtures.h"
#include "../support/temp_dir.h"
#include "../support/line_buffered_stdout.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace mp4fx;

static int gFailures = 0;
static int gChecks = 0;

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

static AudioTrack track(size_t frames = 20, uint32_t chunks = 3) {
  AudioTrack t;
  for (size_t i = 0; i < frames; ++i) t.frameSizes.push_back(static_cast<uint32_t>(200 + (i * 37) % 150));
  t.chunks = chunks;
  return t;
}

// ------------------------------------------------------------ m4a_gapless
static bool gaplessFromFile(const Bytes &file, M4aGaplessInfo &info, bool useFd = false) {
  static std::string dir;
  if (dir.empty()) dir = makeTestTempDir("m4a_gapless_test");
  const std::string path = dir + "/file.m4a";
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  if (!file.empty()) std::fwrite(file.data(), 1, file.size(), f);
  std::fclose(f);
#if !defined(_WIN32)
  if (useFd) {
    const int fd = open(path.c_str(), O_RDONLY);
    const bool ok = parseM4aGaplessFd(fd, info);
    if (fd >= 0) close(fd);
    return ok;
  }
#else
  (void)useFd;
#endif
  return parseM4aGapless(path, info);
}

// The payload of the moov box of [file].
static Bytes moovPayload(const Bytes &file) {
  const size_t at = findBox(file, "moov");
  if (at == SIZE_MAX) return {};
  const uint32_t size = readBe32(file, at);
  return Bytes(file.begin() + at + 8, file.begin() + at + size);
}

static void testValidGapless() {
  std::printf("valid: m4a_gapless reads timescales, edit list and iTunSMPB\n");
  {
    AudioTrack t = track();
    t.hasEdit = true;
    t.editMediaTime = 2112;
    t.editDuration = 400;  // movie ticks (1000/s)
    const Mp4File f = makeMp4(t);
    for (bool fd : {false, true}) {
      M4aGaplessInfo info;
      CHECK(gaplessFromFile(f.bytes, info, fd), "%s: not parsed", fd ? "fd" : "path");
      CHECK(info.hasAudioTrack && info.mediaTimescale == 44100 &&
                info.mediaDuration == 20 * 1024 && info.movieTimescale == 1000,
            "track: %d, timescale %u, duration %llu, movie timescale %u", info.hasAudioTrack,
            info.mediaTimescale, (unsigned long long)info.mediaDuration, info.movieTimescale);
      CHECK(info.hasEdit && info.editMediaTime == 2112 && info.editDuration == 400,
            "edit: %d, media time %lld, duration %llu", info.hasEdit,
            (long long)info.editMediaTime, (unsigned long long)info.editDuration);
      uint64_t delay = 0, valid = 0;
      CHECK(info.resolve(44100, delay, valid) && delay == 2112 && valid == 17640,
            "resolve: delay %llu, valid %llu", (unsigned long long)delay,
            (unsigned long long)valid);
    }
  }
  {
    // Version 1 elst and mdhd, with an empty edit first.
    AudioTrack t = track();
    t.hasEdit = true;
    t.emptyEdit = true;
    t.elstV1 = true;
    t.mdhdV1 = true;
    t.editMediaTime = 1024;
    t.editDuration = 0;
    t.mediaTimescale = 22050;
    t.mediaDuration = 5000000000ull;  // needs 64 bits
    const Bytes moov = moovPayload(makeMp4(t).bytes);
    M4aGaplessInfo info;
    CHECK(parseM4aGaplessMoov(moov.data(), moov.size(), info), "v1: not parsed");
    CHECK(info.mediaDuration == 5000000000ull && info.mediaTimescale == 22050,
          "v1: duration %llu, timescale %u", (unsigned long long)info.mediaDuration,
          info.mediaTimescale);
    CHECK(info.hasEdit && info.editMediaTime == 1024 && info.editDuration == 0,
          "v1: the empty edit was not skipped (media time %lld)", (long long)info.editMediaTime);
    uint64_t delay = 0, valid = 0;
    info.resolve(44100, delay, valid);
    CHECK(delay == 2048 && valid == 10000000000ull - 2048, "v1 resolve: delay %llu valid %llu",
          (unsigned long long)delay, (unsigned long long)valid);
  }
  {
    // iTunSMPB, no edit list; QuickTime meta (no FullBox header).
    for (bool full : {true, false}) {
      const Bytes udta = udtaWithIlst(
          freeformItem("iTunSMPB", " 00000000 00000840 000001CA 0000000000004E20"), full);
      const Bytes moov = moovPayload(makeMp4(track(), udta).bytes);
      M4aGaplessInfo info;
      parseM4aGaplessMoov(moov.data(), moov.size(), info);
      CHECK(info.hasSmpb && info.smpbDelay == 0x840 && info.smpbPadding == 0x1CA &&
                info.smpbLength == 0x4E20,
            "iTunSMPB (%s meta): has %d, delay %u, padding %u, length %llu",
            full ? "MP4" : "QuickTime", info.hasSmpb, info.smpbDelay, info.smpbPadding,
            (unsigned long long)info.smpbLength);
    }
  }
  {
    // A video track first: the first audio track is the one used.
    AudioTrack v = track(5, 1);
    v.handler = "vide";
    v.mediaTimescale = 90000;
    AudioTrack a = track(20, 2);
    Bytes moov = cat(mvhd(600, 1000), trak(v, 600, 0), trak(a, 600, 0));
    M4aGaplessInfo info;
    CHECK(parseM4aGaplessMoov(moov.data(), moov.size(), info) && info.mediaTimescale == 44100,
          "video first: timescale %u", info.mediaTimescale);
  }
  {
    // No audio track: hdlr 'vide' only.
    AudioTrack v = track();
    v.handler = "vide";
    const Bytes moov = moovPayload(makeMp4(v).bytes);
    M4aGaplessInfo info;
    CHECK(!parseM4aGaplessMoov(moov.data(), moov.size(), info), "no audio track accepted");
    uint64_t delay = 1, valid = 1;
    CHECK(!info.resolve(44100, delay, valid) && delay == 0 && valid == 0, "resolve without track");
  }
  {
    // Not an MP4 at all, and an empty file.
    M4aGaplessInfo info;
    Bytes junk(100, 'x');
    CHECK(!gaplessFromFile(junk, info), "junk accepted");
    CHECK(!gaplessFromFile(Bytes{}, info), "empty file accepted");
    CHECK(!parseM4aGapless(std::string("/nonexistent/dir/x.m4a"), info), "missing file");
  }
}

// ------------------------------------------------------------------ sizes
static void testSizes() {
  std::printf("sizes: box size 0 and 1, boxes past the end of the file\n");
  const AudioTrack t = track();
  const Mp4File f = makeMp4(t);
  const size_t mdatAt = findBox(f.bytes, "mdat", f.mdatPayloadOffset - 16);
  CHECK(mdatAt == f.mdatPayloadOffset - 8, "test fixture: mdat at %zu", mdatAt);
  M4aGaplessInfo info;
  {
    // mdat with size 0: to the end of the file.
    Bytes b = f.bytes;
    patchBe32(b, mdatAt, 0);
    CHECK(gaplessFromFile(b, info), "mdat size 0 rejected");
  }
  {
    // mdat with a 64-bit size.
    Bytes mdatPayload(f.bytes.begin() + f.mdatPayloadOffset, f.bytes.end());
    Bytes b(f.bytes.begin(), f.bytes.begin() + (f.mdatPayloadOffset - 8));
    b = cat(b, box64("mdat", mdatPayload));
    CHECK(gaplessFromFile(b, info), "64-bit mdat rejected");
  }
  {
    // mdat claims twice the bytes it has: the file was cut short.
    Bytes b = f.bytes;
    patchBe32(b, mdatAt, static_cast<uint32_t>(2 * (b.size() - mdatAt)));
    CHECK(!gaplessFromFile(b, info), "a cut mdat must be rejected");
    patchBe32(b, mdatAt, 0xFFFFFFFFu);
    gaplessFromFile(b, info);  // must not crash
  }
  {
    // A trailing box cut short after a whole moov and mdat (an appended tag).
    const Bytes b = cat(f.bytes, rawBox(4000, "free", Bytes(10, 0)));
    CHECK(gaplessFromFile(b, info), "trailing partial box rejected");
    const Bytes c = cat(f.bytes, Bytes{1, 2, 3});
    CHECK(gaplessFromFile(c, info), "3 trailing bytes rejected");
  }
  {
    // moov claims more than the file.
    Bytes b = f.bytes;
    const size_t moovAt = findBox(b, "moov");
    patchBe32(b, moovAt, 0x7FFFFFF0u);
    CHECK(!gaplessFromFile(b, info), "moov past the end accepted");
    // A 64-bit moov size of UINT64_MAX.
    Bytes c(b.begin(), b.begin() + moovAt);
    be32(c, 1);
    text(c, "moov");
    be64(c, ~0ull);
    c.insert(c.end(), b.begin() + moovAt + 8, b.end());
    CHECK(!gaplessFromFile(c, info), "moov of 2^64-1 bytes accepted");
  }
  {
    // Box sizes 2..7 (smaller than a header) inside the moov.
    for (uint32_t s = 2; s < 8; ++s) {
      Bytes b = f.bytes;
      patchBe32(b, findBox(b, "stsz"), s);
      const Bytes moov = moovPayload(b);
      M4aGaplessInfo i2;
      parseM4aGaplessMoov(moov.data(), moov.size(), i2);
    }
    CHECK(true, "sizes 2..7");
  }
}

// ---------------------------------------------------------------- nesting
static Bytes nestedBoxes(const char *type, size_t levels, const Bytes &inner) {
  Bytes b = inner;
  for (size_t i = 0; i < levels; ++i) b = box(type, b);
  return b;
}

static void testNesting() {
  std::printf("nesting: deep containers\n");
  for (size_t levels : {62u, 63u, 64u, 200u}) {
    for (const char *type : {"trak", "udta", "meta"}) {
      const Bytes moov = nestedBoxes(type, levels, box("free", Bytes(4, 0)));
      M4aGaplessInfo info;
      CHECK(!parseM4aGaplessMoov(moov.data(), moov.size(), info),
            "%zu nested %s: accepted a moov without an audio track", levels, type);
    }
  }
  {
    // The real moov followed by 40 levels of udta still parses.
    const Mp4File f = makeMp4(track());
    const Bytes moov = cat(moovPayload(f.bytes), nestedBoxes("udta", 40, Bytes(8, 0)));
    M4aGaplessInfo info;
    CHECK(parseM4aGaplessMoov(moov.data(), moov.size(), info) && info.mediaTimescale == 44100,
          "track then 40 nested udta: timescale %u", info.mediaTimescale);
  }
}

// ----------------------------------------------------------------- tables
// Replaces the payload (after version/flags) of the first [type] full box
// in the moov of [f], fixing the sizes of every enclosing box.
static Bytes replaceFullBox(const Bytes &file, const char *type, const Bytes &newPayload) {
  const size_t at = findBox(file, type);
  if (at == SIZE_MAX) return file;
  const uint32_t oldSize = readBe32(file, at);
  Bytes nb;
  be32(nb, static_cast<uint32_t>(12 + newPayload.size()));
  nb.insert(nb.end(), file.begin() + at + 4, file.begin() + at + 12);
  nb = cat(nb, newPayload);
  Bytes out(file.begin(), file.begin() + at);
  out = cat(out, nb);
  out.insert(out.end(), file.begin() + at + oldSize, file.end());
  const int64_t delta = static_cast<int64_t>(nb.size()) - oldSize;
  // Fix the containers that enclose [at].
  for (const char *c : {"moov", "trak", "mdia", "minf", "stbl"}) {
    const size_t ca = findBox(out, c);
    if (ca != SIZE_MAX && ca < at)
      patchBe32(out, ca, static_cast<uint32_t>(readBe32(out, ca) + delta));
  }
  return out;
}

static Bytes removeBox(const Bytes &file, const char *type) {
  const size_t at = findBox(file, type);
  if (at == SIZE_MAX) return file;
  const uint32_t size = readBe32(file, at);
  Bytes out(file.begin(), file.begin() + at);
  out.insert(out.end(), file.begin() + at + size, file.end());
  for (const char *c : {"moov", "trak", "mdia", "minf", "stbl"}) {
    const size_t ca = findBox(out, c);
    if (ca != SIZE_MAX && ca < at) patchBe32(out, ca, readBe32(out, ca) - size);
  }
  return out;
}

static Bytes words(std::initializer_list<uint32_t> v) {
  Bytes b;
  for (uint32_t x : v) be32(b, x);
  return b;
}

static void testTables() {
  std::printf("tables: stsz/stts/stsc/stco that disagree\n");
  const Mp4File f = makeMp4(track(20, 4));
  struct Case {
    const char *what;
    Bytes file;
    bool top = false;  // the moov is not the one built by makeMp4
  };
  std::vector<Case> cases;
  {
    // 25 sizes, 20 timestamps.
    Bytes stsz = words({0, 25});
    for (int i = 0; i < 25; ++i) be32(stsz, 100);
    cases.push_back({"stsz > stts", replaceFullBox(f.bytes, "stsz", stsz)});
  }
  cases.push_back({"stsc wants 10 per chunk, 4 chunks",
                   replaceFullBox(f.bytes, "stsc", words({1, 1, 10, 1}))});
  cases.push_back({"stsc first_chunk 0", replaceFullBox(f.bytes, "stsc", words({2, 0, 5, 1, 0, 3, 1}))});
  cases.push_back({"stsc going backwards",
                   replaceFullBox(f.bytes, "stsc", words({3, 3, 5, 1, 1, 4, 1, 2, 2, 1}))});
  cases.push_back({"stsc of 0 entries", replaceFullBox(f.bytes, "stsc", words({0}))});
  cases.push_back({"no stsc", removeBox(f.bytes, "stsc")});
  cases.push_back({"no stco", removeBox(f.bytes, "stco")});
  cases.push_back({"no stts", removeBox(f.bytes, "stts")});
  cases.push_back({"no stsz", removeBox(f.bytes, "stsz")});
  cases.push_back({"stco of 0 chunks", replaceFullBox(f.bytes, "stco", words({0}))});
  cases.push_back({"stco offsets past the file",
                   replaceFullBox(f.bytes, "stco", words({4, 0xFFFFFF00u, 0xFFFFFFFFu, 7, 1}))});
  cases.push_back({"stsz count 2^30 in 8 bytes", replaceFullBox(f.bytes, "stsz", words({0, 0x40000000u}))});
  cases.push_back({"stsz constant size, count 1e6", replaceFullBox(f.bytes, "stsz", words({500, 1000000}))});
  cases.push_back({"stsz count 1e5 in 8 bytes", replaceFullBox(f.bytes, "stsz", words({0, 100000}))});
  cases.push_back({"stsc count 2^28", replaceFullBox(f.bytes, "stsc", words({0x10000000u, 1, 1, 1}))});
  cases.push_back({"stco count 2^29", replaceFullBox(f.bytes, "stco", words({0x20000000u, 0}))});
  cases.push_back({"stts count 2^30", replaceFullBox(f.bytes, "stts", words({0x40000000u, 1, 1}))});
  cases.push_back({"stts samples 2^32-1",
                   replaceFullBox(f.bytes, "stts", words({2, 10, 1024, 0xFFFFFFFFu, 1}))});
  {
    // stz2 with each field size, and an invalid one.
    for (uint32_t bits : {4u, 8u, 16u, 3u}) {
      Bytes p = words({bits, 20});
      p.resize(p.size() + 40, 0x33);
      Bytes b = replaceFullBox(f.bytes, "stsz", p);
      std::memcpy(b.data() + findBox(b, "stsz") + 4, "stz2", 4);
      cases.push_back({bits == 3 ? "stz2 field size 3" : "stz2", b});
    }
  }
  {
    // stsz and stco twice.
    const size_t at = findBox(f.bytes, "stsz");
    const uint32_t size = readBe32(f.bytes, at);
    Bytes dup(f.bytes.begin() + at, f.bytes.begin() + at + size);
    Bytes b(f.bytes.begin(), f.bytes.begin() + at);
    b = cat(b, dup, dup);
    b.insert(b.end(), f.bytes.begin() + at + size, f.bytes.end());
    for (const char *c : {"moov", "trak", "mdia", "minf", "stbl"}) {
      const size_t ca = findBox(b, c);
      patchBe32(b, ca, readBe32(b, ca) + size);
    }
    cases.push_back({"stsz twice", b});
  }
  {
    // stsz and stco at the top level, after the trak.
    const Bytes stray = cat(fullBox("stsz", 0, 0, words({0, 3, 1, 2, 3})),
                            fullBox("stco", 0, 0, words({0x10000000u, 0})),
                            fullBox("stsc", 0, 0, words({1, 1, 0xFFFFFFFFu, 1})));
    cases.push_back({"top-level tables after the trak", cat(f.bytes, stray), false});
    cases.push_back({"top-level tables, no trak", cat(ftyp(), stray), true});
  }
  for (const Case &c : cases) {
    // m4a_gapless reads no sample table: whatever they say, the timescales
    // and the duration come from mdhd.
    const Bytes moov = moovPayload(c.file);
    M4aGaplessInfo info;
    const bool ok = parseM4aGaplessMoov(moov.data(), moov.size(), info);
    CHECK(c.top || (ok && info.mediaTimescale == 44100), "%s: parsed %d, timescale %u", c.what,
          ok, info.mediaTimescale);
  }
  CHECK(true, "%zu table cases ran", cases.size());
}

// -------------------------------------------------------------- truncated
static void testTruncated() {
  std::printf("truncated: every prefix of the file and of its moov\n");
  AudioTrack t = track(12, 3);
  t.hasEdit = true;
  const Bytes udta = udtaWithIlst(cat(textItem("\xa9nam", "T"),
                                      freeformItem("iTunSMPB", " 0 840 1CA 4E20")));
  const Mp4File f = makeMp4(t, udta);
  for (size_t n = 0; n <= f.bytes.size(); ++n) {
    if (n % 7 != 0 && n + 64 <= f.bytes.size()) continue;
    const Bytes prefix(f.bytes.begin(), f.bytes.begin() + n);
    M4aGaplessInfo info;
    const bool ok = gaplessFromFile(prefix, info);
    CHECK(!ok || n == f.bytes.size(), "accepted a %zu-byte prefix of %zu", n, f.bytes.size());
  }
  const Bytes moov = moovPayload(f.bytes);
  for (size_t n = 0; n <= moov.size(); ++n) {
    const Bytes prefix(moov.begin(), moov.begin() + n);
    M4aGaplessInfo info;
    parseM4aGaplessMoov(prefix.empty() ? nullptr : prefix.data(), n, info);
  }
}

int main() {
  lineBufferedStdout();
  testValidGapless();
  testSizes();
  testNesting();
  testTables();
  testTruncated();
  std::printf("%d checks, %d failures\n", gChecks, gFailures);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

// Unit tests for the M4A / MP4 metadata box parser
// (src/audiobuffer/m4a_metadata.cpp, parseM4aMetadata()), which reads the
// first bytes of a network stream for StreamDecoder (BUFFER_MP4) and of a
// file for the Linux OS decoder. Platform independent: the parser is plain
// C++. The M4A cases of test/macos/metadata_parsers_test.cpp moved here.
//
// Verifies:
//   valid          every iTunes item the parser maps (title, artist, album
//                  artist, album, date, genre and numeric 'gnre', composer,
//                  comment, track and disc numbers), and the audio sample
//                  entry (codec, channels, sample rate)
//   esds-bitrate   the average bitrate of the esds DecoderConfigDescriptor,
//                  with 4-byte and 1-byte descriptor lengths
//   assets         the M4A/MP4 files in example/assets/audio: sample rate and
//                  channels from the file name / stsd, codec, and bitrate
//                  against the esds parsed here independently
//   containers     a 'meta' box without the FullBox header (QuickTime), a
//                  'moov' with a 64-bit size and with size 0 (to the end)
//   nested         4 MiB of nested 'moov' boxes, parsed on a thread with a
//                  1 MiB stack (the iOS main thread and Android's default);
//                  the depth limit: 30 levels parse, 40 do not
//   wrap           a box size that wraps `offset + size` on a 32-bit size_t
//                  (can only fail there, e.g. the --wasm32 build of
//                  test/macos/metadata_parsers_test.sh), and an extended
//                  (64-bit) size of UINT64_MAX
//   truncated      every prefix of a file with metadata: no read past the
//                  data (run with --asan) and never a partial string
//
// Needs no audio device. The assets case uses example/assets/audio (path
// given by the script) and is skipped without it.
//
//   ./test/common/m4a_metadata_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/m4a_metadata.h"

#include "../support/mp4_fixtures.h"
#include "../support/line_buffered_stdout.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

using namespace mp4fx;

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

static Bytes m4aWithTitle(const char *title) {
  Bytes data = {0, 0, 0, 1, 0, 0, 0, 0};
  text(data, title);
  Bytes meta = {0, 0, 0, 0};
  meta = cat(meta, box("ilst", box("\xa9nam", box("data", data))));
  return cat(ftyp(), box("moov", box("udta", box("meta", meta))));
}

static AudioTrack defaultTrack() {
  AudioTrack t;
  t.frameSizes = {300, 310, 290, 305, 320, 280};
  return t;
}

static Bytes allItems() {
  return cat(textItem("\xa9nam", "The Title"), textItem("\xa9" "ART", "The Artist"),
             textItem("aART", "The Album Artist"), textItem("\xa9" "alb", "The Album"),
             textItem("\xa9" "day", "2024-05-06"), textItem("\xa9wrt", "The Composer"),
             textItem("\xa9" "cmt", "A comment"), numberPairItem("trkn", 3, 12),
             numberPairItem("disk", 1, 2), gnreItem(18) /* Rock */,
             freeformItem("iTunSMPB", " 00000000 00000840 00000000 0000000000001000"));
}

// ------------------------------------------------------------------ valid
static void testValid() {
  std::printf("valid: every mapped item and the sample entry\n");
  {
    Bytes file = m4aWithTitle("Box Title");
    M4aMetadata meta;
    CHECK(parseM4aMetadata(file.data(), file.size(), meta), "no metadata");
    CHECK(meta.title == "Box Title", "title '%s'", meta.title.c_str());
  }
  const Mp4File f = makeMp4(defaultTrack(), udtaWithIlst(allItems()));
  M4aMetadata m;
  CHECK(parseM4aMetadata(f.bytes.data(), f.bytes.size(), m), "full file: not found");
  CHECK(m.title == "The Title", "title '%s'", m.title.c_str());
  CHECK(m.artist == "The Artist", "artist '%s'", m.artist.c_str());
  CHECK(m.albumArtist == "The Album Artist", "album artist '%s'", m.albumArtist.c_str());
  CHECK(m.album == "The Album", "album '%s'", m.album.c_str());
  CHECK(m.date == "2024-05-06", "date '%s'", m.date.c_str());
  CHECK(m.composer == "The Composer", "composer '%s'", m.composer.c_str());
  CHECK(m.comment == "A comment", "comment '%s'", m.comment.c_str());
  CHECK(m.track == "3/12", "track '%s'", m.track.c_str());
  CHECK(m.disc == "1/2", "disc '%s'", m.disc.c_str());
  CHECK(m.genre == "Rock", "genre '%s' (gnre 18)", m.genre.c_str());
  CHECK(m.codec == "mp4a", "codec '%s'", m.codec.c_str());
  CHECK(m.channels == 2, "channels %d", m.channels);
  CHECK(m.sampleRate == 44100, "sample rate %d", m.sampleRate);

  // Text genre wins over a numeric one; numeric ids out of range are ignored;
  // a track number without total; UTF-8 passes through.
  {
    const Bytes items = cat(textItem("\xa9gen", "Synthwave"), gnreItem(18),
                            numberPairItem("trkn", 7, 0),
                            textItem("\xa9nam", "Caf\xC3\xA9 \xF0\x9F\x8E\xB5"));
    const Mp4File g = makeMp4(defaultTrack(), udtaWithIlst(items));
    M4aMetadata mm;
    parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
    CHECK(mm.genre == "Synthwave", "genre '%s'", mm.genre.c_str());
    CHECK(mm.track == "7", "track '%s'", mm.track.c_str());
    CHECK(mm.title == "Caf\xC3\xA9 \xF0\x9F\x8E\xB5", "UTF-8 title changed");
  }
  for (uint16_t id : {uint16_t(0), uint16_t(200), uint16_t(0xFFFF)}) {
    const Mp4File g = makeMp4(defaultTrack(), udtaWithIlst(gnreItem(id)));
    M4aMetadata mm;
    parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
    CHECK(mm.genre.empty(), "gnre %u gave genre '%s'", id, mm.genre.c_str());
  }
  {
    const Mp4File g = makeMp4(defaultTrack(), udtaWithIlst(gnreItem(1)));
    M4aMetadata mm;
    parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
    CHECK(mm.genre == "Blues", "gnre 1 gave genre '%s'", mm.genre.c_str());
  }
  // A 'data' atom of integer type in a text item leaves the field empty.
  {
    Bytes v;
    text(v, "nope");
    const Mp4File g = makeMp4(defaultTrack(), udtaWithIlst(box("\xa9nam", dataAtom(21, v))));
    M4aMetadata mm;
    parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
    CHECK(mm.title.empty(), "integer data atom read as title '%s'", mm.title.c_str());
  }
  // Mono 8 kHz and 48 kHz sample entries (the 16.16 field holds up to
  // 65535 Hz).
  for (uint32_t rate : {8000u, 48000u}) {
    AudioTrack t = defaultTrack();
    t.channels = 1;
    t.sampleRate = rate;
    const Mp4File g = makeMp4(t);
    M4aMetadata mm;
    CHECK(parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm), "%u Hz: not found", rate);
    CHECK(mm.channels == 1 && mm.sampleRate == static_cast<int>(rate),
          "%u Hz: channels %d, rate %d", rate, mm.channels, mm.sampleRate);
  }
}

// ------------------------------------------------------------ esds bitrate
// The DecoderConfigDescriptor is: tag 0x04, length, objectTypeIndication (1),
// streamType (1), bufferSizeDB (3), maxBitrate (4), avgBitrate (4):
// avgBitrate is at offset 9 after the length.
static void testEsdsBitrate() {
  std::printf("esds-bitrate: avgBitrate of the DecoderConfigDescriptor\n");
  for (bool longLengths : {true, false}) {
    AudioTrack t = defaultTrack();
    t.esdsLongLengths = longLengths;
    t.maxBitrate = 160000;
    t.avgBitrate = 128000;
    const Mp4File g = makeMp4(t);
    M4aMetadata mm;
    parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
    CHECK(mm.bitrate == 128000,
          "bitrate %d, expected avgBitrate 128000 (%s descriptor lengths)",
          mm.bitrate, longLengths ? "4-byte" : "1-byte");
  }
}

// ----------------------------------------------------------------- assets
// The esds avgBitrate, parsed by walking the descriptors (not by scanning).
static int esdsAvgBitrate(const Bytes &file) {
  const size_t at = findBox(file, "esds");
  if (at == SIZE_MAX) return -1;
  size_t p = at + 12;  // size, type, version/flags
  const size_t end = at + readBe32(file, at);
  auto readLen = [&](size_t &q) {
    uint32_t len = 0;
    for (int i = 0; i < 4 && q < end; ++i) {
      const unsigned char b = file[q++];
      len = (len << 7) | (b & 0x7F);
      if (!(b & 0x80)) break;
    }
    return len;
  };
  if (p >= end || file[p++] != 0x03) return -1;
  readLen(p);
  const unsigned char flags = file[p + 2];
  p += 3;
  if (flags & 0x80) p += 2;
  if (flags & 0x40) p += 1 + file[p];
  if (flags & 0x20) p += 2;
  if (p >= end || file[p++] != 0x04) return -1;
  readLen(p);
  if (p + 13 > end) return -1;
  return static_cast<int>(readBe32(file, p + 9));
}

static void testAssets(const std::string &dir) {
  std::printf("assets: M4A/MP4 files in %s\n", dir.c_str());
  struct Asset {
    const char *name;
    int rate;
    int channels;
  };
  const Asset assets[] = {{"aac_lc_mono_44100.m4a", 44100, 1},
                          {"aac_lc_stereo_48000.m4a", 48000, 2},
                          {"sample-AAC.m4a", 44100, 2},
                          {"sample-AAC.mp4", 44100, 2}};
  int found = 0;
  for (const Asset &a : assets) {
    const Bytes file = readFile(dir + "/" + a.name);
    if (file.empty()) {
      std::printf("  %s missing, skipped\n", a.name);
      continue;
    }
    ++found;
    M4aMetadata m;
    CHECK(parseM4aMetadata(file.data(), file.size(), m), "%s: not found", a.name);
    CHECK(m.sampleRate == a.rate, "%s: rate %d, expected %d", a.name, m.sampleRate, a.rate);
    CHECK(m.channels == a.channels, "%s: channels %d, expected %d", a.name, m.channels,
          a.channels);
    CHECK(m.codec == "mp4a", "%s: codec '%s'", a.name, m.codec.c_str());
    const int avg = esdsAvgBitrate(file);
    CHECK(avg > 0, "%s: test could not parse the esds", a.name);
    if (avg > 0)
      CHECK(m.bitrate == avg, "%s: bitrate %d, esds avgBitrate %d", a.name,
            m.bitrate, avg);
  }
  if (!found) std::printf("SKIPPED (assets case): no M4A assets in %s\n", dir.c_str());
}

// ------------------------------------------------------------- containers
static void testContainers() {
  std::printf("containers: QuickTime meta, 64-bit and to-the-end sizes\n");
  const Bytes items = textItem("\xa9nam", "QT");
  {
    const Mp4File g = makeMp4(defaultTrack(), udtaWithIlst(items, false));
    M4aMetadata mm;
    parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
    CHECK(mm.title == "QT", "meta without FullBox header: title '%s'", mm.title.c_str());
  }
  {
    const Bytes file = cat(ftyp(), box64("moov", udtaWithIlst(items)));
    M4aMetadata mm;
    parseM4aMetadata(file.data(), file.size(), mm);
    CHECK(mm.title == "QT", "64-bit moov: title '%s'", mm.title.c_str());
  }
  {
    // A 'free' box with a 64-bit size before moov.
    const Bytes file = cat(ftyp(), box64("free", Bytes(40, 0)), box("moov", udtaWithIlst(items)));
    M4aMetadata mm;
    parseM4aMetadata(file.data(), file.size(), mm);
    CHECK(mm.title == "QT", "after a 64-bit free: title '%s'", mm.title.c_str());
  }
  {
    // A moov of size 0 runs to the end of the data.
    const Bytes file = cat(ftyp(), rawBox(0, "moov", udtaWithIlst(items)));
    M4aMetadata mm;
    parseM4aMetadata(file.data(), file.size(), mm);
    CHECK(mm.title == "QT", "moov of size 0: title '%s'", mm.title.c_str());
  }
  {
    // Boxes of sizes 2..7 (invalid) stop the parse, without reading on.
    for (uint32_t s = 2; s < 8; ++s) {
      const Bytes file = cat(ftyp(), rawBox(s, "moov", udtaWithIlst(items)));
      M4aMetadata mm;
      CHECK(!parseM4aMetadata(file.data(), file.size(), mm), "box of size %u parsed", s);
    }
  }
  {
    M4aMetadata mm;
    CHECK(!parseM4aMetadata(nullptr, 100, mm), "null data");
    const Bytes tiny = {0, 0, 0, 8, 'm', 'o', 'o'};
    CHECK(!parseM4aMetadata(tiny.data(), tiny.size(), mm), "7 bytes");
  }
}

// ----------------------------------------------------------------- nested
struct ParseJob {
  const Bytes *data;
  bool found;
  M4aMetadata meta;
};

static void parseJob(ParseJob *job) {
  job->found = parseM4aMetadata(job->data->data(), job->data->size(), job->meta);
}

#if defined(_WIN32)
static DWORD WINAPI parseThread(LPVOID arg) {
  parseJob(static_cast<ParseJob *>(arg));
  return 0;
}
#else
static void *parseThread(void *arg) {
  parseJob(static_cast<ParseJob *>(arg));
  return nullptr;
}
#endif

// Runs the parse on a thread with a [stack]-byte stack.
static bool parseOnSmallStack(ParseJob &job, size_t stack) {
#if defined(_WIN32)
  HANDLE thread = CreateThread(nullptr, stack, parseThread, &job,
                               STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!thread) return false;
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
  return true;
#else
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, stack);
  pthread_t thread;
  const bool started = pthread_create(&thread, &attr, parseThread, &job) == 0;
  if (started) pthread_join(thread, nullptr);
  pthread_attr_destroy(&attr);
  return started;
#endif
}

static Bytes nestedMoov(size_t levels, const Bytes &inner) {
  Bytes b = inner;
  for (size_t i = 0; i < levels; ++i) b = box("moov", b);
  return cat(ftyp(), b);
}

static void testNested() {
  std::printf("nested: 4 MiB of nested boxes on a 1 MiB stack; depth limit\n");
  // ftyp, then a chain of 'moov' boxes, each the only child of the one
  // before it: one recursive call per 8 bytes of input.
  const size_t levels = (4u << 20) / 8;
  Bytes file = ftyp();
  const size_t base = file.size();
  file.resize(base + levels * 8);
  for (size_t i = 0; i < levels; ++i) {
    patchBe32(file, base + i * 8, static_cast<uint32_t>((levels - i) * 8));
    std::memcpy(file.data() + base + i * 8 + 4, "moov", 4);
  }
  ParseJob job{&file, true, {}};
  CHECK(parseOnSmallStack(job, 1u << 20), "thread start failed");
  CHECK(!job.found, "a file of empty containers has no metadata");

  // udta/meta/ilst under N moov levels: the parser bounds the depth at 32.
  const Bytes inner = udtaWithIlst(textItem("\xa9nam", "Deep"));
  {
    const Bytes f = nestedMoov(28, inner);
    M4aMetadata mm;
    parseM4aMetadata(f.data(), f.size(), mm);
    CHECK(mm.title == "Deep", "28 levels: title '%s'", mm.title.c_str());
  }
  {
    const Bytes f = nestedMoov(40, inner);
    M4aMetadata mm;
    parseM4aMetadata(f.data(), f.size(), mm);
    CHECK(mm.title.empty(), "40 levels (past the bound): title '%s'", mm.title.c_str());
  }
}

// ------------------------------------------------------------------- wrap
static void testWrap() {
  std::printf("wrap: box sizes that wrap the offset\n");
  {
    // ftyp, then a 'moov' that declares 2^32 - 16 bytes. With a 32-bit
    // size_t, 16 + (2^32 - 16) wraps to 0 and passes a naive bound check;
    // inside it a 1 GiB 'free' box moves the read offset far past the data.
    Bytes file = ftyp();
    be32(file, 0xFFFFFFF0u);
    text(file, "moov");
    be32(file, 0x40000000u);
    text(file, "free");
    file.resize(file.size() + 32, 0);
    M4aMetadata meta;
    CHECK(!parseM4aMetadata(file.data(), file.size(), meta),
          "a box larger than the data must stop the parse");
  }
  {
    // An extended-size box at offset 8 whose 64-bit size would wrap its end.
    const Bytes file = {0, 0, 0, 8, 'f', 'r', 'e', 'e', 0, 0, 0, 1, 'm', 'o', 'o', 'v',
                        255, 255, 255, 255, 255, 255, 255, 255};
    M4aMetadata meta;
    CHECK(!parseM4aMetadata(file.data(), file.size(), meta),
          "an extended box larger than the data must stop the parse");
  }
  {
    // Item and 'data' sizes past their parent, 0xFFFFFFFF and 8.
    for (uint32_t bad : {0xFFFFFFFFu, 0x7FFFFFFFu, 9u, 8u}) {
      Bytes item = textItem("\xa9nam", "Item");
      patchBe32(item, 8, bad);  // the 'data' atom's size
      const Mp4File g = makeMp4(defaultTrack(), udtaWithIlst(item));
      M4aMetadata mm;
      parseM4aMetadata(g.bytes.data(), g.bytes.size(), mm);
      CHECK(mm.title.empty() || mm.title == "Item", "data size %08x: title '%s'", bad,
            mm.title.c_str());
    }
  }
}

// -------------------------------------------------------------- truncated
// Copies every prefix into a buffer of exactly that size, so ASan sees any
// read past it. Strings are whole or missing, never cut.
static void testTruncated() {
  std::printf("truncated: every prefix of a file with metadata\n");
  const Mp4File f = makeMp4(defaultTrack(), udtaWithIlst(allItems()));
  int partial = 0;
  for (size_t n = 0; n <= f.bytes.size(); ++n) {
    std::vector<unsigned char> prefix(f.bytes.begin(), f.bytes.begin() + n);
    M4aMetadata m;
    parseM4aMetadata(prefix.empty() ? nullptr : prefix.data(), n, m);
    const bool ok = (m.title.empty() || m.title == "The Title") &&
                    (m.artist.empty() || m.artist == "The Artist") &&
                    (m.album.empty() || m.album == "The Album") &&
                    (m.comment.empty() || m.comment == "A comment") &&
                    (m.track.empty() || m.track == "3/12");
    if (!ok && partial++ < 3)
      CHECK(ok, "prefix %zu: partial string (title '%s', artist '%s')", n, m.title.c_str(),
            m.artist.c_str());
  }
  CHECK(partial == 0, "%d prefixes gave partial strings", partial);
}

int main(int argc, char **argv) {
  lineBufferedStdout();
  std::string assets;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--strict") == 0)
      gStrict = true;
    else
      assets = argv[i];
  }
  testValid();
  testEsdsBitrate();
  if (!assets.empty())
    testAssets(assets);
  else
    std::printf("assets: no assets directory given, skipped\n");
  testContainers();
  testNested();
  testWrap();
  testTruncated();
  std::printf("%d checks, %d failures, %d known bugs\n", gChecks, gFailures, gKnownBugs);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

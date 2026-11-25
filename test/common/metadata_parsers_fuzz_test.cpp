// Fixed-seed fuzz tests for the metadata parsers (the audio decoders are
// fuzzed by stream_decoders_fuzz_test.cpp): ID3 text and tags, M4A boxes and
// gapless info, ICY metadata, Ogg comments (OpusTags, Vorbis comment
// header), FLAC VORBIS_COMMENT and PICTURE blocks (native FLAC and Ogg
// FLAC). See test/support/metadata_fuzz_inputs.h for the parsers reached.
//
// Groups (each input is built from the seed, so a failure names the input
// that reproduces it):
//   valid     well-formed tags parse to what was written, before any fuzzing:
//             ID3 text frames in every encoding, COMM comments in Latin-1
//             and UTF-16 (converted to UTF-8), OpusTags, Vorbis and FLAC
//             comments, ICY titles
//   id3text   id3TextToUtf8() on random frames: for encodings 0, 1 and 2 the
//             output must be valid UTF-8
//   id3tag    ID3v2.3/2.4 tags with many frames, corrupted (sizes, flags,
//             encodings, truncation) or random behind "ID3": the AAC
//             parser, and the MP3 decoder through StreamDecoder
//   m4a       synthetic and bundled M4A files with corrupted moov boxes:
//             parseM4aMetadata, parseM4aGaplessMoov and resolve()
//   icy       random ICY streams (interval 1..64, blocks of 0..255*16
//             bytes, titles with and without terminator) split at random:
//             the audio comes out unchanged and the title/URL reports match
//             a reference reader; ICY + MP3 through StreamDecoder
//   ogg       the bundled Opus and Vorbis files with their comment packet
//             replaced by a corrupted one (re-paged, so CRCs stay valid)
//   flac      a FLAC file with VORBIS_COMMENT and PICTURE blocks, and the
//             same in Ogg FLAC, with corrupted metadata; a STREAMINFO with
//             more channels than the frames (checked on its own too)
//   random    random bytes, alone and behind each format's magic, through
//             every pure parser and StreamDecoder
//
// On POSIX each input runs in a child process with a time limit, so a crash,
// a sanitizer report (with --asan) or a hang is reported for that input; on
// Windows inputs run in-process. Every StreamDecoder run must also end its
// drain within 200 calls and give a sample count that is a multiple of the
// channel count.
//
// Needs no audio device. Uses example/assets/audio (path given by the
// script) for the Opus, Vorbis, MP3 and M4A seeds; groups whose seeds are
// missing are skipped. Needs the Xiph libraries.
//
//   ./test/common/metadata_parsers_fuzz_test.sh [--asan] [--strict]
//       [--seed N] [--iterations N] [--only GROUP:N]
//
// --strict makes the checks marked KNOWN BUG fail the run. --only runs one
// input in-process (for a debugger).

#include "../support/metadata_fuzz_inputs.h"
#include "../support/mp4_fixtures.h"
#include "../support/line_buffered_stdout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace mfz;
using mp4fx::cat;

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

// ---------------------------------------------------------------- seeds
struct Seeds {
  Bytes mp3;                       // MP3 frames (no tag)
  std::vector<Bytes> opusPackets;  // first packets of sample-OPUS.opus
  std::vector<Bytes> vorbisPackets;
  std::vector<Bytes> m4aFiles;     // synthetic + bundled
  Bytes flac;
  size_t flacMetadataEnd = 0;
  std::vector<Bytes> oggFlacPackets;
};
static Seeds gSeeds;

static void loadSeeds(const std::string &dir) {
  gSeeds.mp3 = readFile(dir + "/test_mpeg2_24khz_mono.mp3", 24 * 1024);
  if (gSeeds.mp3.empty()) gSeeds.mp3 = mp3Frames(40);
  gSeeds.opusPackets = oggPackets(readFile(dir + "/sample-OPUS.opus", 256 * 1024), 40);
  gSeeds.vorbisPackets = oggPackets(readFile(dir + "/sample-vorbis.ogg", 256 * 1024), 40);
  {
    mp4fx::AudioTrack t;
    t.frameSizes = {300, 310, 290, 305, 320, 280, 200, 250};
    t.hasEdit = true;
    using namespace mp4fx;
    const Bytes items = cat(textItem("\xa9nam", "Title"), textItem("\xa9" "ART", "Artist"),
                            numberPairItem("trkn", 3, 12), numberPairItem("disk", 1, 2),
                            gnreItem(18), textItem("\xa9" "cmt", "Comment"),
                            freeformItem("iTunSMPB", " 00000000 00000840 000001CA 0000000000001000"));
    gSeeds.m4aFiles.push_back(makeMp4(t, udtaWithIlst(items)).bytes);
  }
  for (const char *name : {"aac_lc_mono_44100.m4a", "aac_lc_stereo_48000.m4a", "sample-AAC.m4a"}) {
    Bytes f = readFile(dir + "/" + name);
    if (!f.empty()) gSeeds.m4aFiles.push_back(f);
  }
  gSeeds.flac = flacWithPicture(gSeeds.flacMetadataEnd);
  size_t unused = 0;
  gSeeds.oggFlacPackets = oggPackets(flacWithPicture(unused, true), 64);
}

// --------------------------------------------------------------- mutate
struct Rng {
  std::mt19937 g;
  explicit Rng(uint32_t s) : g(s) {}
  size_t operator()(size_t n) { return n ? size_t(g() % n) : 0; }
  unsigned char byte() { return static_cast<unsigned char>(g()); }
};

static const uint32_t kInteresting[] = {0, 1, 2, 7, 8, 9, 15, 16, 0x7F, 0x80, 0xFF, 0x100,
                                        0x7FFF, 0xFFFF, 0x7FFFFFFF, 0x80000000u,
                                        0xFFFFFFF0u, 0xFFFFFFFFu};

/// Random edits in [from, to) of [b]: byte changes, bit flips, interesting
/// 32-bit values (both byte orders), insertions, deletions; maybe truncation.
static void mutate(Rng &rnd, Bytes &b, size_t from = 0, size_t to = SIZE_MAX) {
  to = std::min(to, b.size());
  if (from >= to) return;
  const size_t edits = 1 + rnd(12);
  for (size_t e = 0; e < edits && from < to && to <= b.size(); ++e) {
    const size_t p = from + rnd(to - from);
    switch (rnd(7)) {
    case 0: b[p] = rnd.byte(); break;
    case 1: b[p] ^= static_cast<unsigned char>(1u << rnd(8)); break;
    case 2:
    case 3: {
      const uint32_t v = kInteresting[rnd(sizeof(kInteresting) / sizeof(kInteresting[0]))];
      const bool be = rnd(2);
      for (int i = 0; i < 4 && p + i < b.size(); ++i)
        b[p + i] = static_cast<unsigned char>(be ? v >> (24 - 8 * i) : v >> (8 * i));
      break;
    }
    case 4: b.insert(b.begin() + p, 1 + rnd(16), rnd.byte()); to = std::min(b.size(), to + 1); break;
    case 5: {
      const size_t n = std::min<size_t>(1 + rnd(16), b.size() - p);
      b.erase(b.begin() + p, b.begin() + p + n);
      to = std::min(to, b.size());
      break;
    }
    default: b[p] = rnd(2) ? 0x00 : 0xFF; break;
    }
  }
  if (rnd(4) == 0) b.resize(rnd(b.size() + 1));
}

static std::vector<size_t> randomChunks(Rng &rnd) {
  std::vector<size_t> c;
  const int mode = static_cast<int>(rnd(3));
  for (int i = 0; i < 8; ++i)
    c.push_back(mode == 0 ? 1 + rnd(64) : mode == 1 ? 1 + rnd(1500) : 512 + rnd(16384));
  return c;
}

static Bytes randomBytes(Rng &rnd, size_t maxLen) {
  Bytes b(rnd(maxLen + 1));
  for (auto &x : b) x = rnd.byte();
  return b;
}

// ---------------------------------------------------------------- groups
// Each returns "" when the input passed, or what went wrong.
static std::string checkStream(const StreamRun &r, const char *what) {
  if (!r.drainEnded) return std::string(what) + ": drain never ended";
  if (r.badOutput) return std::string(what) + ": sample count not a multiple of the channels";
  return "";
}

static std::string runId3Text(uint32_t seed) {
  Rng rnd(seed);
  Bytes b;
  const int kind = static_cast<int>(rnd(3));
  if (kind == 0) {
    b = randomBytes(rnd, 64);
  } else {
    // A UTF-16 body of random code units, biased to surrogates and NULs.
    b.push_back(static_cast<unsigned char>(1 + rnd(2)));
    if (rnd(2)) b.insert(b.end(), {static_cast<unsigned char>(rnd(2) ? 0xFF : 0xFE),
                                   static_cast<unsigned char>(rnd(2) ? 0xFE : 0xFF)});
    const size_t n = rnd(24);
    for (size_t i = 0; i < n; ++i) {
      static const uint16_t units[] = {0, 0x41, 0xE9, 0x20AC, 0xD800, 0xDBFF, 0xDC00, 0xDFFF,
                                       0xD83D, 0xDE00, 0xFFFD, 0xFEFF, 0xFFFE};
      const uint16_t u = rnd(3) ? units[rnd(sizeof(units) / 2)] : static_cast<uint16_t>(rnd(0x10000));
      b.push_back(u >> 8);
      b.push_back(u & 0xFF);
    }
    if (rnd(4) == 0) b.push_back(rnd.byte());  // odd length
    if (kind == 2 && !b.empty()) b[0] = rnd(2) ? 0 : 1;
  }
  std::string why;
  // An exact-size copy, so ASan sees a read past the end.
  Bytes exact(b);
  if (!runPureParsers(exact, why)) return why;
  return "";
}

static std::string runId3Tag(uint32_t seed) {
  Rng rnd(seed);
  Bytes tag;
  if (rnd(5) == 0) {
    tag = {'I', 'D', '3', static_cast<unsigned char>(2 + rnd(3)), 0, rnd.byte()};
    const Bytes tail = randomBytes(rnd, 2000);
    tag.insert(tag.end(), tail.begin(), tail.end());
  } else {
    tag = richId3Tag(rnd(2) ? 3 : 4);
    mutate(rnd, tag, rnd(3) ? 10 : 3);
  }
  std::string why;
  if (!runPureParsers(tag, why)) return why;
  const Bytes stream = rnd(3) ? cat(tag, gSeeds.mp3) : tag;
  return checkStream(runStreamDecoder(stream, randomChunks(rnd)), "MP3 with ID3");
}

static Bytes moovPayloadOf(const Bytes &file) {
  const size_t at = mp4fx::findBox(file, "moov");
  if (at == SIZE_MAX || at + 8 > file.size()) return {};
  const size_t end = std::min<size_t>(file.size(), at + mp4fx::readBe32(file, at));
  return Bytes(file.begin() + at + 8, file.begin() + std::max(end, at + 8));
}

static std::string runM4a(uint32_t seed) {
  Rng rnd(seed);
  if (gSeeds.m4aFiles.empty()) return "";
  Bytes f = gSeeds.m4aFiles[rnd(gSeeds.m4aFiles.size())];
  const size_t moovAt = mp4fx::findBox(f, "moov");
  if (moovAt != SIZE_MAX && rnd(4)) {
    const size_t end = std::min<size_t>(f.size(), moovAt + mp4fx::readBe32(f, moovAt));
    mutate(rnd, f, moovAt, end);
  } else {
    mutate(rnd, f);
  }
  std::string why;
  if (!runPureParsers(f, why)) return why;
  Bytes moov = moovPayloadOf(f);
  if (!runPureParsers(moov, why)) return why;
  return "";
}

static Bytes randomIcyBlock(Rng &rnd) {
  Bytes b;
  switch (rnd(6)) {
  case 0: break;  // empty block (length byte 0)
  case 1: putText(b, "StreamTitle='Song " + std::to_string(rnd(4)) + "';"); break;
  case 2: putText(b, "StreamTitle='Song " + std::to_string(rnd(4)) + "';StreamUrl='http://x/" +
                         std::to_string(rnd(3)) + "';"); break;
  case 3: putText(b, "StreamTitle='no terminator " + std::to_string(rnd(100))); break;
  case 4: {
    const size_t n = rnd(255 * 16);
    for (size_t i = 0; i < n; ++i) b.push_back(static_cast<unsigned char>('a' + rnd(26)));
    break;
  }
  default: {
    b = randomBytes(rnd, 64);
    for (auto &c : b)
      if (c == '\'') c = '"';
    break;
  }
  }
  return b;
}

static std::string runIcyGroup(uint32_t seed) {
  Rng rnd(seed);
  IcyCase c;
  c.metaInt = static_cast<int>(1 + rnd(64));
  c.audio = randomBytes(rnd, 3000);
  const size_t blocks = c.audio.size() / c.metaInt + 1;
  for (size_t i = 0; i < blocks; ++i) c.blocks.push_back(randomIcyBlock(rnd));
  c.chunks = randomChunks(rnd);
  std::string why;
  if (!runIcy(c, why)) return why;
  // ICY over MP3 through StreamDecoder (detection strips a copy too).
  IcyCase m;
  m.metaInt = static_cast<int>(256 + rnd(8192));
  m.audio = gSeeds.mp3;
  for (size_t i = 0; i < m.audio.size() / m.metaInt + 1; ++i) m.blocks.push_back(randomIcyBlock(rnd));
  Bytes stream = icyStream(m);
  if (rnd(3) == 0) mutate(rnd, stream);
  return checkStream(runStreamDecoder(stream, randomChunks(rnd), m.metaInt), "ICY MP3");
}

static std::string runOgg(uint32_t seed) {
  Rng rnd(seed);
  const bool opus = rnd(2) == 0;
  std::vector<Bytes> packets = opus ? gSeeds.opusPackets : gSeeds.vorbisPackets;
  if (packets.size() < 3) return "";
  Bytes &comment = packets[1];
  switch (rnd(3)) {
  case 0: {
    // A structured comment packet with random lengths and counts.
    std::vector<std::string> comments;
    const size_t n = rnd(6);
    for (size_t i = 0; i < n; ++i)
      comments.push_back(rnd(4) ? "KEY" + std::to_string(i) + "=value" : std::string(rnd(300), 'x'));
    comment = opus ? opusTags(std::string(rnd(40), 'v'), comments)
                   : vorbisComment(std::string(rnd(40), 'v'), comments);
    mutate(rnd, comment, opus ? 8 : 7);
    break;
  }
  case 1: mutate(rnd, comment, opus ? 8 : 7); break;
  default: comment.resize(rnd(comment.size() + 1)); break;
  }
  const size_t headers = opus ? 2 : 3;
  Bytes stream = oggStream(packets, headers, opus ? 960 : 1024);
  return checkStream(runStreamDecoder(stream, randomChunks(rnd)), opus ? "Opus" : "Vorbis");
}

// The native FLAC fixture with its metadata blocks corrupted (STREAMINFO
// included now and then).
static Bytes nativeFlacInput(Rng &rnd) {
  Bytes f = gSeeds.flac;
  mutate(rnd, f, rnd(4) ? 42 : 4, gSeeds.flacMetadataEnd);
  return f;
}

static std::string runFlac(uint32_t seed) {
  Rng rnd(seed);
  if (rnd(3) == 0 && gSeeds.oggFlacPackets.size() > 3) {
    // Ogg FLAC: packet 0 is the mapping header with STREAMINFO, then one
    // packet per metadata block, then audio.
    std::vector<Bytes> packets = gSeeds.oggFlacPackets;
    const size_t which = 1 + rnd(2);
    mutate(rnd, packets[which], 4);
    Bytes stream = oggStream(packets, 3, 4096);
    return checkStream(runStreamDecoder(stream, randomChunks(rnd)), "Ogg FLAC");
  }
  return checkStream(runStreamDecoder(nativeFlacInput(rnd), randomChunks(rnd)), "FLAC");
}

static std::string runRandom(uint32_t seed) {
  Rng rnd(seed);
  static const char *magics[] = {"", "ID3\x03", "ID3\x04", "OggS", "fLaC", "\xff\xfb\x90\xc0",
                                 "\xff\xf1\x50\x80", "\x0b\x77"};
  const char *m = magics[rnd(sizeof(magics) / sizeof(magics[0]))];
  Bytes b(m, m + std::strlen(m));
  const Bytes tail = randomBytes(rnd, 8000);
  b.insert(b.end(), tail.begin(), tail.end());
  std::string why;
  if (!runPureParsers(b, why)) return why;
  return checkStream(runStreamDecoder(b, randomChunks(rnd)), "random");
}

struct Group {
  const char *name;
  std::function<std::string(uint32_t)> run;
  // For a failed input: the ID of the known bug it hits, or "".
  std::function<std::string(uint32_t)> known;
};
static std::string noKnownBug(uint32_t) { return ""; }
static const Group kGroups[] = {
    {"id3text", runId3Text, noKnownBug}, {"id3tag", runId3Tag, noKnownBug},
    {"m4a", runM4a, noKnownBug},         {"icy", runIcyGroup, noKnownBug},
    {"ogg", runOgg, noKnownBug},         {"flac", runFlac, noKnownBug},
    {"random", runRandom, noKnownBug},
};

// ----------------------------------------------------------------- valid
static void testValid() {
  std::printf("valid: well-formed metadata parses to what was written\n");
  // ID3 text frames, every encoding, through the AAC parser and the MP3 decoder.
  const Bytes tag = richId3Tag(3);
  AacMetadata aac;
  size_t id3Size = 0;
  CHECK(AACDecoderWrapper::parseId3Tags(tag.data(), tag.size(), aac, id3Size), "AAC ID3");
  const StreamRun mp3 = runStreamDecoder(cat(tag, mp3Frames(8)), {4096});
  CHECK(mp3.reports > 0, "MP3 with ID3: no metadata report");
  struct Field {
    const char *what;
    const std::string &aac, &mp3;
    const char *expected;
  };
  const Mp3Metadata &m = mp3.last.mp3Metadata;
  const Field fields[] = {
      {"title (Latin-1)", aac.title, m.title, "Caf\xC3\xA9 title"},
      {"artist (UTF-16 BOM)", aac.artist, m.artist, "Artist \xC3\xA9\xF0\x9F\x98\x80"},
      {"album (UTF-8)", aac.album, m.album, "Alb\xC3\xA9"},
      {"album artist (UTF-16BE)", aac.albumArtist, m.albumArtist, "B\xF0\x9F\x98\x80"},
      {"track", aac.track, m.track, "3/12"},
      {"comment (Latin-1, ASCII)", aac.comment, m.comment, "comment"},
  };
  for (const Field &f : fields) {
    CHECK(f.aac == f.expected, "AAC %s: '%s'", f.what, f.aac.c_str());
    CHECK(f.mp3 == f.expected, "MP3 %s: '%s'", f.what, f.mp3.c_str());
  }
  // COMM text in Latin-1 and UTF-16 must come out as UTF-8, like the text
  // frames.
  struct Comm {
    const char *what;
    Bytes body;
  };
  const Comm comms[] = {
      {"Latin-1", commBody(0, {'d'}, {'C', 'a', 'f', 0xE9})},
      {"UTF-16 BOM", commBody(1, {0xFF, 0xFE, 'd', 0}, {0xFF, 0xFE, 'C', 0, 'a', 0, 'f', 0, 0xE9, 0})},
  };
  for (const Comm &c : comms) {
    const Bytes t = id3Tag(3, {{"COMM", c.body}});
    AacMetadata a;
    size_t s = 0;
    AACDecoderWrapper::parseId3Tags(t.data(), t.size(), a, s);
    const StreamRun r = runStreamDecoder(cat(t, mp3Frames(8)), {4096});
    CHECK(a.comment == "Caf\xC3\xA9", "AAC COMM %s: comment %zu bytes, valid UTF-8 %d",
          c.what, a.comment.size(), validUtf8(a.comment));
    CHECK(r.last.mp3Metadata.comment == "Caf\xC3\xA9",
          "MP3 COMM %s: comment %zu bytes, valid UTF-8 %d", c.what,
          r.last.mp3Metadata.comment.size(), validUtf8(r.last.mp3Metadata.comment));
  }
  // OpusTags.
  if (gSeeds.opusPackets.size() > 3) {
    std::vector<Bytes> p = gSeeds.opusPackets;
    p[1] = opusTags("vendor x", {"TITLE=Opus title", "ARTIST=Someone"});
    const StreamRun r = runStreamDecoder(oggStream(p, 2), {4096});
    CHECK(r.reports > 0 && r.last.oggMetadata.vendor == "vendor x" &&
              r.last.oggMetadata.comments.count("TITLE") &&
              r.last.oggMetadata.comments.at("TITLE") == "Opus title",
          "Opus comments: %d reports, vendor '%s'", r.reports, r.last.oggMetadata.vendor.c_str());
  } else {
    std::printf("  Opus seed missing, skipped\n");
  }
  // Vorbis comment header.
  if (gSeeds.vorbisPackets.size() > 3) {
    std::vector<Bytes> p = gSeeds.vorbisPackets;
    p[1] = vorbisComment("vorbis vendor", {"TITLE=Vorbis title"});
    const StreamRun r = runStreamDecoder(oggStream(p, 3, 1024), {4096});
    bool found = false;
    for (const auto &kv : r.last.oggMetadata.comments)
      found = found || kv.second == "Vorbis title";
    CHECK(r.reports > 0 && found, "Vorbis comments: %d reports, %zu comments", r.reports,
          r.last.oggMetadata.comments.size());
  } else {
    std::printf("  Vorbis seed missing, skipped\n");
  }
  // FLAC with a picture: comments still reported, all samples decode.
  {
    const StreamRun r = runStreamDecoder(gSeeds.flac, {4096});
    CHECK(r.reports > 0 && r.last.oggMetadata.comments.count("TITLE") &&
              r.last.oggMetadata.comments.at("TITLE") == "Picture test",
          "FLAC + PICTURE: %d reports", r.reports);
  }
  // ICY: one title per change, split anywhere.
  {
    IcyCase c;
    c.metaInt = 8;
    c.audio.resize(40, 0x11);
    auto t = [](const char *s) { Bytes b; putText(b, s); return b; };
    c.blocks = {t("StreamTitle='A';"), {}, t("StreamTitle='A';"), t("StreamTitle='B';StreamUrl='u';"),
                t("StreamTitle='no end")};
    for (size_t chunk : {1u, 3u, 17u, 1000u}) {
      c.chunks = {chunk};
      std::string why;
      CHECK(runIcy(c, why), "ICY split by %zu: %s", chunk, why.c_str());
    }
    CHECK(icyExpected(c).size() == 2, "ICY reference: %zu reports", icyExpected(c).size());
  }
}

// ---------------------------------------------------------------- driver
#if !defined(_WIN32)
// Runs [fn] in a child with a time limit; returns "" or the problem.
static std::string inChild(const std::function<std::string()> &fn) {
  std::fflush(stdout);
#if defined(__SANITIZE_THREAD__)
#define FUZZ_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define FUZZ_TSAN 1
#endif
#endif
#if defined(FUZZ_TSAN)
  // fork() from a process with threads under ThreadSanitizer can deadlock the
  // child (and the parent waits for it): run in-process; TSan reports the races.
  return fn();
#endif
  int fds[2];
  if (pipe(fds) != 0) return fn();
  const pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    alarm(60);
    const std::string r = fn();
    (void)!write(fds[1], r.data(), r.size());
    close(fds[1]);
    _exit(0);
  }
  close(fds[1]);
  std::string msg;
  char buf[512];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof(buf))) > 0) msg.append(buf, static_cast<size_t>(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFSIGNALED(status))
    return WTERMSIG(status) == SIGALRM ? "hang (60 s)" : "crash (signal " + std::to_string(WTERMSIG(status)) + ")";
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return "exit status " + std::to_string(WEXITSTATUS(status)) + " (sanitizer report?)";
  return msg;
}
#endif

// The native FLAC wrapper outputs the STREAMINFO channel count: a frame with
// fewer channels must not make it read libFLAC's channel buffers past the
// frame's (a NULL dereference when STREAMINFO says stereo and the frames are
// mono).
static void testFlacChannels() {
  std::printf("flac-channels: STREAMINFO says 2 channels, the frames have 1\n");
  Bytes f = gSeeds.flac;
  if (f.size() <= 20) return;
  f[20] = (f[20] & ~0x0E) | (1 << 1);  // channels - 1 = 1
#if defined(_WIN32)
  std::printf("  skipped on Windows (needs fork)\n");
#else
  const std::string r = inChild([&] { return checkStream(runStreamDecoder(f, {4096}), "FLAC"); });
  CHECK(r.empty(), "mono frames under a stereo STREAMINFO: %s", r.c_str());
  // The same mismatch in Ogg FLAC is handled.
  std::vector<Bytes> packets = gSeeds.oggFlacPackets;
  if (packets.size() > 3 && packets[0].size() > 29) {
    // Mapping header (13 bytes) + "fLaC" + block header: STREAMINFO at 17.
    Bytes &h = packets[0];
    h[29] = (h[29] & ~0x0E) | (1 << 1);
    const std::string o = inChild([&] {
      return checkStream(runStreamDecoder(oggStream(packets, 3, 4096), {4096}), "Ogg FLAC");
    });
    CHECK(o.empty(), "Ogg FLAC, same mismatch: %s", o.c_str());
  }
#endif
}

int main(int argc, char **argv) {
  lineBufferedStdout();
  std::string assets;
  uint32_t seed = 4242;
  int iterations = 150;
  std::string only;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--strict")) gStrict = true;
    else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    else if (!std::strcmp(argv[i], "--iterations") && i + 1 < argc) iterations = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (argv[i][0] != '-') assets = argv[i];
    else {
      std::printf("usage: %s <assets dir> [--strict] [--seed N] [--iterations N] [--only GROUP:N]\n", argv[0]);
      return 2;
    }
  }
  loadSeeds(assets.empty() ? "example/assets/audio" : assets);
  if (gSeeds.flac.empty()) {
    std::printf("SKIPPED: libFLAC encoder failed\n");
    return 0;
  }

  if (!only.empty()) {
    const size_t colon = only.find(':');
    const std::string g = only.substr(0, colon);
    const uint32_t n = colon == std::string::npos ? 0 : static_cast<uint32_t>(std::strtoul(only.c_str() + colon + 1, nullptr, 10));
    for (const Group &gr : kGroups)
      if (g == gr.name) {
        const std::string r = gr.run(n);
        std::printf("%s:%u: %s\n", gr.name, n, r.empty() ? "ok" : r.c_str());
        return r.empty() ? 0 : 1;
      }
    std::printf("unknown group %s\n", g.c_str());
    return 2;
  }

  testValid();
  testFlacChannels();
  std::printf("seed %u, %d inputs per group\n", seed, iterations);
  for (size_t gi = 0; gi < sizeof(kGroups) / sizeof(kGroups[0]); ++gi) {
    const Group &g = kGroups[gi];
    int failed = 0, known = 0;
    for (int i = 0; i < iterations; ++i) {
      const uint32_t inputSeed = seed * 1000003u + static_cast<uint32_t>(gi) * 7919u + static_cast<uint32_t>(i);
#if defined(_WIN32)
      const std::string r = g.run(inputSeed);
#else
      const std::string r = inChild([&] { return g.run(inputSeed); });
#endif
      ++gChecks;
      if (!r.empty()) {
        const std::string bug = g.known(inputSeed);
        if (!bug.empty() && !gStrict) {
          ++gKnownBugs;
          ++known;
          std::printf("  KNOWN BUG %s: %s input %u: %s (rerun: --only %s:%u)\n", bug.c_str(),
                      g.name, inputSeed, r.c_str(), g.name, inputSeed);
          continue;
        }
        ++gFailures;
        if (++failed <= 5)
          std::printf("  FAIL %s%s input %u: %s (rerun: --only %s:%u)\n",
                      bug.empty() ? "" : (bug + " ").c_str(), g.name, inputSeed, r.c_str(),
                      g.name, inputSeed);
      }
    }
    std::printf("%-8s %d inputs, %d failed, %d known bugs\n", g.name, iterations, failed, known);
  }
  std::printf("%d checks, %d failures, %d known bugs\n", gChecks, gFailures, gKnownBugs);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

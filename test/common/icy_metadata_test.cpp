// Unit tests for the ICY (Shoutcast) metadata stripper
// (src/audiobuffer/icy_metadata.h: stripIcyMetadataEx, used by the MP3 and
// AAC stream decoders, and stripIcyMetadata, used by the FLAC and Ogg FLAC
// ones). A stream is built here from known audio bytes and metadata blocks;
// the stripper must give back exactly the audio, and report each change of
// title / URL once, however the stream is split into chunks.
//
// Verifies:
//   basic        one block per interval, fed in one chunk: audio intact,
//                one callback per title
//   split        the same stream in chunks of 1, 2, 3, 7, 16, 17 and 333
//                bytes and in random chunks: identical output, so a block
//                (its length byte, its body) split anywhere is put back
//                together
//   metaint      metaint 0 and negative: nothing is consumed or reported
//                (the decoders call the stripper only with metaint > 0);
//                metaint 1 (a length byte after every audio byte)
//   zero-length  a block of length 0 (no metadata this interval): no
//                callback, audio continuous
//   padding      a body padded with NULs to its 16-byte multiple
//   beyond-data  a block whose length goes past the end of the data: the
//                rest is held as metadata, nothing leaks into the audio, and
//                the block completes with the next chunk
//   unterminated StreamTitle=' without the closing "';": the title is not
//                changed and no callback is made; the same for StreamUrl
//   once         the same title in consecutive blocks reports once; A, B, A
//                reports three times; an empty title after a title reports
//                the empty title
//   url          stripIcyMetadataEx reports a URL-only change with the same
//                title; stripIcyMetadata (title only) must not report the
//                unchanged title again
//   append       cleanOut is appended to, not replaced
//   max-block    a block of the largest size (255 * 16 = 4080 bytes)
//
// Needs no audio device and no assets.
//
//   ./test/common/icy_metadata_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "audiobuffer/icy_metadata.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
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
#define KNOWN_BUG(id, cond, ...)                                               \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      if (gStrict) ++gFailures; else ++gKnownBugs;                             \
      std::printf("  %s %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG", id,  \
                  __FILE__, __LINE__, #cond);                                  \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

struct Meta {
  std::string title, url;
  bool operator==(const Meta &o) const { return title == o.title && url == o.url; }
};

// One interval of the stream: [audio] bytes (metaint of them, except maybe
// the last interval), then a metadata block with body [body] (padded with
// NULs to a multiple of 16) unless [noBlock].
struct Interval {
  Bytes audio;
  std::string body;
  bool noBlock = false;
};

static Bytes buildStream(const std::vector<Interval> &intervals) {
  Bytes s;
  for (const Interval &iv : intervals) {
    s.insert(s.end(), iv.audio.begin(), iv.audio.end());
    if (iv.noBlock) continue;
    const size_t blocks = (iv.body.size() + 15) / 16;
    s.push_back(static_cast<unsigned char>(blocks));
    s.insert(s.end(), iv.body.begin(), iv.body.end());
    s.insert(s.end(), blocks * 16 - iv.body.size(), 0);
  }
  return s;
}

static Bytes audioBytes(size_t n, unsigned seed) {
  Bytes b(n);
  std::mt19937 rng(seed);
  for (auto &x : b) x = static_cast<unsigned char>(rng());
  return b;
}

static std::string block(const std::string &title, const std::string &url = "") {
  std::string s = "StreamTitle='" + title + "';";
  if (!url.empty()) s += "StreamUrl='" + url + "';";
  return s;
}

struct Result {
  Bytes audio;
  std::vector<Meta> metas;     // stripIcyMetadataEx callbacks
  IcyStripState state;
};

// Feeds [stream] in chunks whose sizes cycle through [sizes] (0 = all).
static Result run(const Bytes &stream, int metaint, const std::vector<size_t> &sizes) {
  Result r;
  size_t pos = 0, k = 0;
  while (pos < stream.size()) {
    size_t n = sizes.empty() || sizes[k % sizes.size()] == 0 ? stream.size()
                                                              : sizes[k % sizes.size()];
    ++k;
    n = std::min(n, stream.size() - pos);
    const Bytes chunk(stream.begin() + pos, stream.begin() + pos + n);
    pos += n;
    stripIcyMetadataEx(chunk, metaint, r.state, r.audio,
                       [&](const std::string &t, const std::string &u) {
                         r.metas.push_back({t, u});
                       });
  }
  return r;
}

static Result runRandom(const Bytes &stream, int metaint, unsigned seed) {
  std::mt19937 rng(seed);
  std::vector<size_t> sizes;
  size_t total = 0;
  while (total < stream.size()) {
    const size_t n = 1 + rng() % (rng() % 4 == 0 ? 3 : 200);
    sizes.push_back(n);
    total += n;
  }
  return run(stream, metaint, sizes);
}

static std::string metasStr(const std::vector<Meta> &m) {
  std::string s;
  for (const Meta &x : m) s += "[" + x.title + "|" + x.url + "]";
  return s.empty() ? "(none)" : s;
}

static void expectResult(const char *label, const Result &r, const Bytes &audio,
                         const std::vector<Meta> &metas) {
  CHECK(r.audio == audio, "%s: audio %zu bytes, expected %zu%s", label,
        r.audio.size(), audio.size(),
        r.audio.size() == audio.size() ? " (content differs)" : "");
  CHECK(r.metas == metas, "%s: callbacks %s, expected %s", label,
        metasStr(r.metas).c_str(), metasStr(metas).c_str());
}

static Bytes concatAudio(const std::vector<Interval> &ivs) {
  Bytes a;
  for (const Interval &iv : ivs) a.insert(a.end(), iv.audio.begin(), iv.audio.end());
  return a;
}

static std::vector<Interval> sampleIntervals(int metaint) {
  std::vector<Interval> ivs;
  const char *titles[] = {"Artist - Song", "Artist - Song", "Other - Tune", "",
                          "Caf\xC3\xA9 - \xE2\x82\xAC"};
  for (int i = 0; i < 5; ++i)
    ivs.push_back({audioBytes(metaint, 100 + i), block(titles[i])});
  ivs.push_back({audioBytes(metaint / 2, 200), "", true}); // stream ends mid-interval
  return ivs;
}

static const std::vector<Meta> kSampleMetas = {
    {"Artist - Song", ""}, {"Other - Tune", ""}, {"", ""},
    {"Caf\xC3\xA9 - \xE2\x82\xAC", ""}};

static void testBasic() {
  std::printf("basic\n");
  const int metaint = 64;
  const auto ivs = sampleIntervals(metaint);
  expectResult("one chunk", run(buildStream(ivs), metaint, {0}), concatAudio(ivs), kSampleMetas);
}

static void testSplit() {
  std::printf("split\n");
  for (int metaint : {16, 64, 1000}) {
    const auto ivs = sampleIntervals(metaint);
    const Bytes stream = buildStream(ivs);
    for (size_t n : {1, 2, 3, 7, 16, 17, 333}) {
      char label[64];
      std::snprintf(label, sizeof label, "metaint %d, %zu-byte chunks", metaint, n);
      expectResult(label, run(stream, metaint, {n}), concatAudio(ivs), kSampleMetas);
    }
    for (unsigned seed = 1; seed <= 20; ++seed) {
      char label[64];
      std::snprintf(label, sizeof label, "metaint %d, random chunks %u", metaint, seed);
      expectResult(label, runRandom(stream, metaint, seed), concatAudio(ivs), kSampleMetas);
    }
  }
  // Every split point of the first block: length byte, body start, body end.
  const int metaint = 32;
  const auto ivs = sampleIntervals(metaint);
  const Bytes stream = buildStream(ivs);
  for (size_t cut = 1; cut < 100; ++cut) {
    Result r;
    for (int part = 0; part < 2; ++part) {
      const Bytes chunk = part == 0 ? Bytes(stream.begin(), stream.begin() + cut)
                                    : Bytes(stream.begin() + cut, stream.end());
      stripIcyMetadataEx(chunk, metaint, r.state, r.audio,
                         [&](const std::string &t, const std::string &u) {
                           r.metas.push_back({t, u});
                         });
    }
    char label[48];
    std::snprintf(label, sizeof label, "cut at %zu", cut);
    expectResult(label, r, concatAudio(ivs), kSampleMetas);
  }
}

static void testMetaint() {
  std::printf("metaint\n");
  const Bytes stream = buildStream(sampleIntervals(16));
  for (int metaint : {0, -1, -16000}) {
    Result r;
    r.audio = {1, 2, 3};
    stripIcyMetadataEx(stream, metaint, r.state, r.audio,
                       [&](const std::string &t, const std::string &u) {
                         r.metas.push_back({t, u});
                       });
    CHECK(r.audio == Bytes({1, 2, 3}), "metaint %d: cleanOut changed (%zu bytes)",
          metaint, r.audio.size());
    CHECK(r.metas.empty(), "metaint %d: callback made", metaint);
    CHECK(r.state.audioBytesCount == 0 && r.state.icyMetaSize == 0,
          "metaint %d: state changed", metaint);
  }
  // An empty chunk changes nothing.
  Result r;
  stripIcyMetadataEx(Bytes{}, 16, r.state, r.audio, [&](const std::string &, const std::string &) {
    r.metas.push_back({});
  });
  CHECK(r.audio.empty() && r.metas.empty() && r.state.audioBytesCount == 0, "empty chunk");
  // metaint 1: a (zero) length byte after every audio byte.
  std::vector<Interval> ivs;
  for (int i = 0; i < 50; ++i) ivs.push_back({audioBytes(1, i), ""});
  ivs[10].body = block("one");
  ivs.push_back({audioBytes(1, 99), "", true});
  for (size_t n : {1, 2, 5, 0}) {
    char label[32];
    std::snprintf(label, sizeof label, "metaint 1, chunks %zu", n);
    expectResult(label, run(buildStream(ivs), 1, {n}), concatAudio(ivs), {{"one", ""}});
  }
}

static void testZeroLengthAndPadding() {
  std::printf("zero-length, padding\n");
  const int metaint = 20;
  std::vector<Interval> ivs = {
      {audioBytes(metaint, 1), ""},                 // length byte 0
      {audioBytes(metaint, 2), block("T1")},
      {audioBytes(metaint, 3), ""},                 // 0 again: title kept
      {audioBytes(metaint, 4), ""},
      {audioBytes(metaint, 5), block("T2") + std::string(5, '\0') + "junk"},
      {audioBytes(7, 6), "", true}};
  for (size_t n : {1, 3, 0}) {
    char label[32];
    std::snprintf(label, sizeof label, "chunks %zu", n);
    expectResult(label, run(buildStream(ivs), metaint, {n}), concatAudio(ivs),
                 {{"T1", ""}, {"T2", ""}});
  }
}

static void testBeyondData() {
  std::printf("beyond-data\n");
  const int metaint = 16;
  const Bytes audio = audioBytes(metaint, 7);
  const std::string body = block("Late title");
  Bytes stream = audio;
  stream.push_back(static_cast<unsigned char>((body.size() + 15) / 16));
  const size_t bodyLen = ((body.size() + 15) / 16) * 16;
  Bytes fullBody(body.begin(), body.end());
  fullBody.resize(bodyLen, 0);
  // First chunk: audio, length byte and 5 bytes of the body.
  Bytes first = stream;
  first.insert(first.end(), fullBody.begin(), fullBody.begin() + 5);
  Result r;
  auto cb = [&](const std::string &t, const std::string &u) { r.metas.push_back({t, u}); };
  stripIcyMetadataEx(first, metaint, r.state, r.audio, cb);
  CHECK(r.audio == audio, "audio before the block: %zu bytes", r.audio.size());
  CHECK(r.metas.empty(), "callback before the block is complete");
  CHECK(r.state.icyMetaSize == static_cast<int>(bodyLen - 5),
        "remaining metadata %d, expected %zu", r.state.icyMetaSize, bodyLen - 5);
  // Second chunk: the rest of the body, then more audio.
  const Bytes more = audioBytes(10, 8);
  Bytes second(fullBody.begin() + 5, fullBody.end());
  second.insert(second.end(), more.begin(), more.end());
  stripIcyMetadataEx(second, metaint, r.state, r.audio, cb);
  Bytes expectAudio = audio;
  expectAudio.insert(expectAudio.end(), more.begin(), more.end());
  CHECK(r.audio == expectAudio, "audio after the block: %zu bytes", r.audio.size());
  CHECK(r.metas == std::vector<Meta>({{"Late title", ""}}), "callbacks %s",
        metasStr(r.metas).c_str());

  // The largest length byte (255 * 16 bytes) when the data ends after it:
  // all of it stays pending, no audio is produced.
  Result r2;
  Bytes s2 = audio;
  s2.push_back(255);
  s2.insert(s2.end(), 100, 'x');
  stripIcyMetadataEx(s2, metaint, r2.state, r2.audio,
                     [&](const std::string &t, const std::string &u) { r2.metas.push_back({t, u}); });
  CHECK(r2.audio == audio, "255 block: audio %zu bytes", r2.audio.size());
  CHECK(r2.state.icyMetaSize == 255 * 16 - 100, "255 block: pending %d", r2.state.icyMetaSize);
  CHECK(r2.metas.empty(), "255 block: callback");
}

static void testMaxBlock() {
  std::printf("max-block\n");
  const int metaint = 100;
  std::string body = block("Long", "http://example.invalid/") ;
  body += std::string(255 * 16 - body.size(), ' ');
  std::vector<Interval> ivs = {{audioBytes(metaint, 1), body},
                               {audioBytes(metaint, 2), "", true}};
  const Bytes stream = buildStream(ivs);
  CHECK(stream[metaint] == 255, "test mistake: length byte %d", stream[metaint]);
  for (size_t n : {1, 4096, 0})
    expectResult("max block", run(stream, metaint, {n}), concatAudio(ivs),
                 {{"Long", "http://example.invalid/"}});
}

static void testUnterminated() {
  std::printf("unterminated\n");
  const int metaint = 16;
  std::vector<Interval> ivs = {
      {audioBytes(metaint, 1), block("First")},
      {audioBytes(metaint, 2), "StreamTitle='No end"},
      {audioBytes(metaint, 3), "StreamTitle='Quote only'"},
      {audioBytes(metaint, 4), "StreamTitle=Second';"},       // no opening quote
      {audioBytes(metaint, 5), block("First") + "StreamUrl='http://x"},
      {audioBytes(4, 6), "", true}};
  expectResult("unterminated", run(buildStream(ivs), metaint, {3}), concatAudio(ivs),
               {{"First", ""}});
}

static void testOnce() {
  std::printf("once\n");
  const int metaint = 16;
  std::vector<Interval> ivs = {
      {audioBytes(metaint, 1), block("A")}, {audioBytes(metaint, 2), block("A")},
      {audioBytes(metaint, 3), block("A")}, {audioBytes(metaint, 4), block("B")},
      {audioBytes(metaint, 5), block("A")}, {audioBytes(metaint, 6), block("")},
      {audioBytes(metaint, 7), block("")},  {audioBytes(metaint, 8), "", true}};
  expectResult("once", run(buildStream(ivs), metaint, {5}), concatAudio(ivs),
               {{"A", ""}, {"B", ""}, {"A", ""}, {"", ""}});
  // The first block with an empty title: nothing changed from the initial
  // (empty) state, so no callback.
  std::vector<Interval> ivs2 = {{audioBytes(metaint, 1), block("")},
                                {audioBytes(metaint, 2), "", true}};
  expectResult("empty first", run(buildStream(ivs2), metaint, {0}), concatAudio(ivs2), {});
}

static void testUrl() {
  std::printf("url\n");
  const int metaint = 16;
  std::vector<Interval> ivs = {
      {audioBytes(metaint, 1), block("Song", "http://a/")},
      {audioBytes(metaint, 2), block("Song", "http://b/")},   // URL only
      {audioBytes(metaint, 3), block("Song", "http://b/")},
      {audioBytes(metaint, 4), "StreamUrl='http://c/';"},     // no title: kept
      {audioBytes(metaint, 5), block("Next", "http://c/")},
      {audioBytes(metaint, 6), "", true}};
  const Bytes stream = buildStream(ivs);
  expectResult("Ex", run(stream, metaint, {0}), concatAudio(ivs),
               {{"Song", "http://a/"}, {"Song", "http://b/"}, {"Song", "http://c/"},
                {"Next", "http://c/"}});

  // stripIcyMetadata reports titles only: "invoked with the new title
  // whenever the StreamTitle metadata changes" (icy_metadata.h:84). The FLAC
  // decoders turn each call into an onTrackChange event.
  IcyStripState state;
  Bytes audio;
  std::vector<std::string> titles;
  stripIcyMetadata(stream, metaint, state, audio,
                   [&](const std::string &t) { titles.push_back(t); });
  CHECK(audio == concatAudio(ivs), "title-only: audio differs");
  std::string got;
  for (const auto &t : titles) got += "[" + t + "]";
  // A block that changes only StreamUrl must not call onStreamTitle again
  // with the unchanged title (the FLAC / Ogg FLAC decoders would send a
  // duplicate track-change event).
  CHECK(titles == std::vector<std::string>({"Song", "Next"}),
        "title-only callbacks %s, expected [Song][Next]", got.c_str());
}

static void testAppend() {
  std::printf("append\n");
  const int metaint = 8;
  std::vector<Interval> ivs = {{audioBytes(metaint, 1), block("X")},
                               {audioBytes(3, 2), "", true}};
  Result r;
  r.audio = {9, 9};
  stripIcyMetadataEx(buildStream(ivs), metaint, r.state, r.audio,
                     [&](const std::string &t, const std::string &u) { r.metas.push_back({t, u}); });
  Bytes expect = {9, 9};
  const Bytes a = concatAudio(ivs);
  expect.insert(expect.end(), a.begin(), a.end());
  CHECK(r.audio == expect, "cleanOut not appended to (%zu bytes)", r.audio.size());
}

// Random streams against the model: random metaint, random bodies (with
// quotes, NULs, partial keys), random splits.
static void testRandomModel() {
  std::printf("random model\n");
  std::mt19937 rng(777);
  int bad = 0;
  for (int iter = 0; iter < 300; ++iter) {
    const int metaint = 1 + rng() % 300;
    std::vector<Interval> ivs;
    std::vector<Meta> metas;
    Meta cur;
    const int n = 1 + rng() % 12;
    for (int i = 0; i < n; ++i) {
      Interval iv{audioBytes(metaint, rng()), ""};
      const unsigned kind = rng() % 5;
      Meta next = cur;
      if (kind == 1 || kind == 2) {
        next.title = "t" + std::to_string(rng() % 3);
        iv.body = block(next.title);
      } else if (kind == 3) {
        next.title = "t" + std::to_string(rng() % 3);
        next.url = "u" + std::to_string(rng() % 2);
        iv.body = block(next.title, next.url);
      } else if (kind == 4) {
        iv.body = std::string("StreamTitle='") + std::string(rng() % 40, '\0');
      }
      if (!(next == cur)) metas.push_back(next);
      cur = next;
      ivs.push_back(iv);
    }
    ivs.push_back({audioBytes(rng() % metaint, 1), "", true});
    const Result r = runRandom(buildStream(ivs), metaint, rng());
    if (!(r.audio == concatAudio(ivs)) || !(r.metas == metas)) {
      if (++bad <= 3)
        CHECK(false, "iteration %d metaint %d: audio %s, callbacks %s expected %s", iter,
              metaint, r.audio == concatAudio(ivs) ? "ok" : "differs",
              metasStr(r.metas).c_str(), metasStr(metas).c_str());
    }
  }
  CHECK(bad == 0, "%d of 300 random streams differ", bad);
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
  testBasic();
  testSplit();
  testMetaint();
  testZeroLengthAndPadding();
  testBeyondData();
  testMaxBlock();
  testUnterminated();
  testOnce();
  testUrl();
  testAppend();
  testRandomModel();
  if (gFailures) {
    std::printf("FAILED: %d of %d checks\n", gFailures, gChecks);
    return 1;
  }
  std::printf("OK: %d checks passed%s\n", gChecks,
              gKnownBugs ? (" (" + std::to_string(gKnownBugs) + " KNOWN BUG)").c_str() : "");
  return 0;
}

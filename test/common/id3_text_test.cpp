// Unit tests for id3TextToUtf8() (src/audiobuffer/id3_text.h), which turns
// the body of an ID3v2 text frame (encoding byte + text) into the UTF-8 the
// metadata strings cross the native boundary in. Used by the MP3 and AAC
// stream decoders for TIT2/TPE1/TALB/... frames.
//
// Verifies:
//   latin1     encoding 0: ASCII passes through, bytes 0x80-0xFF become
//              two-byte sequences, trailing NULs are dropped
//   utf16      encoding 1 with a little-endian and a big-endian BOM, and
//              without a BOM (little-endian assumed); encoding 2 (UTF-16BE)
//              with and without a BOM; BMP, 3-byte and surrogate-pair
//              (4-byte) characters
//   surrogates an unpaired high surrogate (followed by a normal unit, by
//              another high surrogate, at the end of the text) and a lone
//              low surrogate each become U+FFFD; the next unit is kept
//   odd        an odd number of UTF-16 bytes: the trailing byte is ignored,
//              no read past the frame (run with --asan)
//   empty      frame size 0 and 1, encoding byte only, BOM only, NUL only
//   terminator a NUL in the middle: UTF-16 stops there; Latin-1 and UTF-8
//              keep the bytes after it (only trailing NULs are dropped), as
//              the header documents
//   utf8       encoding 3 passes through unchanged; an unknown encoding
//              byte is treated like encoding 3
//   valid      for encodings 0-2, every output is valid UTF-8 for random
//              input (a fixed-seed sweep), and is never longer than the
//              bound implied by the input size
//
// Needs no audio device and no assets.
//
//   ./test/common/id3_text_test.sh [--asan|--wasm32]

#include "audiobuffer/id3_text.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

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

static std::string hex(const std::string &s) {
  std::string out;
  char buf[4];
  for (unsigned char c : s) {
    std::snprintf(buf, sizeof buf, "%02X ", c);
    out += buf;
  }
  return out.empty() ? "(empty)" : out;
}

// Converts [frame] in a heap buffer of exactly its size, so ASan sees any
// read past the frame.
static std::string conv(const Bytes &frame) {
  if (frame.empty()) return id3TextToUtf8(nullptr, 0);
  unsigned char *p = new unsigned char[frame.size()];
  std::memcpy(p, frame.data(), frame.size());
  std::string s = id3TextToUtf8(p, frame.size());
  delete[] p;
  return s;
}

static Bytes frame(uint8_t enc, const Bytes &text) {
  Bytes f{enc};
  f.insert(f.end(), text.begin(), text.end());
  return f;
}

static Bytes utf16(const std::vector<uint16_t> &units, bool little) {
  Bytes b;
  for (uint16_t u : units) {
    if (little) { b.push_back(u & 0xFF); b.push_back(u >> 8); }
    else { b.push_back(u >> 8); b.push_back(u & 0xFF); }
  }
  return b;
}

static Bytes cat(Bytes a, const Bytes &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

#define EXPECT_CONV(frameExpr, expectedStr)                                    \
  do {                                                                         \
    const std::string got_ = conv(frameExpr);                                  \
    const std::string exp_ = (expectedStr);                                    \
    CHECK(got_ == exp_, "got %s expected %s", hex(got_).c_str(),               \
          hex(exp_).c_str());                                                  \
  } while (0)

static const std::string kFFFD = "\xEF\xBF\xBD";

// Strict UTF-8 validation (no overlongs, no surrogates, max U+10FFFF).
static bool validUtf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = s[i];
    int n;
    uint32_t cp;
    if (c < 0x80) { ++i; continue; }
    else if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
    else return false;
    for (int k = 1; k <= n; ++k) {
      if (i + k >= s.size()) return false;
      const unsigned char cc = s[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000))
      return false;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += n + 1;
  }
  return true;
}

static void testLatin1() {
  std::printf("latin1\n");
  EXPECT_CONV(frame(0, {'H', 'i', '!'}), "Hi!");
  EXPECT_CONV(frame(0, {'C', 'a', 'f', 0xE9}), "Caf\xC3\xA9");
  EXPECT_CONV(frame(0, {0x80, 0xA0, 0xFF}), "\xC2\x80\xC2\xA0\xC3\xBF");
  EXPECT_CONV(frame(0, {'a', 'b', 0, 0, 0}), "ab");
  EXPECT_CONV(frame(0, {0}), "");
  // Every byte value round-trips through a UTF-8 decoder.
  Bytes all;
  for (int b = 1; b < 256; ++b) all.push_back(static_cast<unsigned char>(b));
  const std::string s = conv(frame(0, all));
  CHECK(validUtf8(s), "all Latin-1 bytes: invalid UTF-8");
  CHECK(s.size() == 127 + 2 * 128, "all Latin-1 bytes: %zu output bytes", s.size());
}

static void testUtf16() {
  std::printf("utf16\n");
  // "Aé€😀" = U+0041 U+00E9 U+20AC U+1F600 (D83D DE00)
  const std::vector<uint16_t> units = {0x0041, 0x00E9, 0x20AC, 0xD83D, 0xDE00};
  const std::string expect = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
  EXPECT_CONV(frame(1, cat({0xFF, 0xFE}, utf16(units, true))), expect);
  EXPECT_CONV(frame(1, cat({0xFE, 0xFF}, utf16(units, false))), expect);
  EXPECT_CONV(frame(1, utf16(units, true)), expect); // no BOM: little-endian
  EXPECT_CONV(frame(2, utf16(units, false)), expect);
  EXPECT_CONV(frame(2, cat({0xFE, 0xFF}, utf16(units, false))), expect);
  // A BOM in encoding 2 is honored as well (lenient reading).
  EXPECT_CONV(frame(2, cat({0xFF, 0xFE}, utf16(units, true))), expect);
  // A trailing NUL terminator (2 bytes) and garbage after it.
  EXPECT_CONV(frame(1, cat(cat({0xFF, 0xFE}, utf16(units, true)), {0, 0})), expect);
  // Highest code point U+10FFFF = DBFF DFFF, lowest supplementary U+10000.
  EXPECT_CONV(frame(2, utf16({0xDBFF, 0xDFFF}, false)), "\xF4\x8F\xBF\xBF");
  EXPECT_CONV(frame(2, utf16({0xD800, 0xDC00}, false)), "\xF0\x90\x80\x80");
  // U+FEFF after the first BOM is a character (ZWNBSP), not a BOM.
  EXPECT_CONV(frame(1, cat({0xFF, 0xFE}, utf16({0xFEFF, 'x'}, true))), "\xEF\xBB\xBFx");
  // 0x07FF and 0x0800: the 2/3-byte boundary.
  EXPECT_CONV(frame(2, utf16({0x07FF, 0x0800}, false)), "\xDF\xBF\xE0\xA0\x80");
}

static void testSurrogates() {
  std::printf("surrogates\n");
  EXPECT_CONV(frame(2, utf16({0xD83D, 'A'}, false)), kFFFD + "A");
  EXPECT_CONV(frame(2, utf16({0xD83D, 0xD83D, 0xDE00}, false)), kFFFD + "\xF0\x9F\x98\x80");
  EXPECT_CONV(frame(2, utf16({'A', 0xD83D}, false)), "A" + kFFFD);
  EXPECT_CONV(frame(2, utf16({0xDE00, 'B'}, false)), kFFFD + "B");
  EXPECT_CONV(frame(1, cat({0xFF, 0xFE}, utf16({0xDE00, 0xD83D}, true))), kFFFD + kFFFD);
  // High surrogate then NUL: U+FFFD, then the text ends.
  EXPECT_CONV(frame(2, utf16({0xD83D, 0, 'C'}, false)), kFFFD);
  // High surrogate whose low half is cut by an odd length.
  EXPECT_CONV(frame(2, cat(utf16({0xD83D}, false), {0xDE})), kFFFD);
}

static void testOdd() {
  std::printf("odd\n");
  EXPECT_CONV(frame(2, {0x00, 'A', 0x00}), "A");
  EXPECT_CONV(frame(1, {0xFF, 0xFE, 'A', 0x00, 'B'}), "A");
  EXPECT_CONV(frame(1, {0xFF}), "");            // half a BOM
  EXPECT_CONV(frame(1, {0xFF, 0xFE, 'A'}), ""); // BOM + half a unit
  EXPECT_CONV(frame(2, {'A'}), "");
}

static void testEmpty() {
  std::printf("empty\n");
  CHECK(id3TextToUtf8(nullptr, 0).empty(), "size 0");
  for (uint8_t enc = 0; enc < 5; ++enc) {
    EXPECT_CONV(Bytes{enc}, "");
    EXPECT_CONV(frame(enc, {0}), "");
    EXPECT_CONV(frame(enc, {0, 0}), "");
  }
  EXPECT_CONV(frame(1, {0xFF, 0xFE}), "");
  EXPECT_CONV(frame(1, {0xFE, 0xFF}), "");
  EXPECT_CONV(frame(2, {0xFE, 0xFF, 0, 0}), "");
}

static void testTerminator() {
  std::printf("terminator\n");
  EXPECT_CONV(frame(1, cat({0xFF, 0xFE}, utf16({'a', 0, 'b'}, true))), "a");
  EXPECT_CONV(frame(2, utf16({'a', 0, 'b'}, false)), "a");
  // A NUL in the middle of a UTF-16 unit is not a terminator: 0x0100.
  EXPECT_CONV(frame(2, {0x01, 0x00, 'z' >> 8, 'z'}), "\xC4\x80z");
  // Latin-1 and UTF-8 drop trailing NULs only (ID3v2.4 multi-value frames
  // separate values with NUL; the bytes after it are kept).
  EXPECT_CONV(frame(0, {'a', 0, 'b', 0}), std::string("a\0b", 3));
  EXPECT_CONV(frame(3, {'a', 0, 'b', 0, 0}), std::string("a\0b", 3));
}

static void testUtf8() {
  std::printf("utf8\n");
  EXPECT_CONV(frame(3, {'C', 'a', 'f', 0xC3, 0xA9}), "Caf\xC3\xA9");
  EXPECT_CONV(frame(3, {0xF0, 0x9F, 0x98, 0x80, 0}), "\xF0\x9F\x98\x80");
  // A UTF-8 BOM is kept (passed through as is).
  EXPECT_CONV(frame(3, {0xEF, 0xBB, 0xBF, 'x'}), "\xEF\xBB\xBFx");
  // Unknown encodings pass through like encoding 3.
  EXPECT_CONV(frame(4, {'x', 'y'}), "xy");
  EXPECT_CONV(frame(0xFF, {'x', 0}), "x");
}

static void testValid() {
  std::printf("valid (random sweep)\n");
  // The validator itself.
  CHECK(validUtf8("a\xC3\xA9\xF0\x9F\x98\x80"), "validator rejects valid text");
  CHECK(!validUtf8("\xC3"), "validator accepts a cut sequence");
  CHECK(!validUtf8("\xE9t\xE9"), "validator accepts raw Latin-1");
  CHECK(!validUtf8("\xED\xA0\xBD"), "validator accepts an encoded surrogate");
  CHECK(!validUtf8("\xC0\x80"), "validator accepts an overlong NUL");
  std::mt19937 rng(20261120);
  int invalid = 0, tooLong = 0;
  for (int iter = 0; iter < 20000; ++iter) {
    const uint8_t enc = static_cast<uint8_t>(rng() % 3);
    const size_t len = rng() % 64;
    Bytes text(len);
    for (auto &b : text) {
      // Bias towards surrogate halves and NULs so they show up often.
      const unsigned r = rng() % 8;
      b = r == 0 ? 0xD8 : r == 1 ? 0xDC : r == 2 ? 0x00 : static_cast<unsigned char>(rng());
    }
    const std::string s = conv(frame(enc, text));
    if (!validUtf8(s)) {
      if (++invalid <= 3)
        CHECK(false, "enc %d len %zu: invalid UTF-8 %s", enc, len, hex(s).c_str());
    }
    // Latin-1: at most 2 bytes per input byte; UTF-16: at most 3 bytes per
    // unit (U+FFFD or a BMP char) and 4 per surrogate pair (2 units).
    const size_t bound = enc == 0 ? 2 * len : 3 * (len / 2);
    if (s.size() > bound) {
      if (++tooLong <= 3)
        CHECK(false, "enc %d len %zu: %zu output bytes > %zu", enc, len, s.size(), bound);
    }
  }
  CHECK(invalid == 0, "%d invalid outputs", invalid);
  CHECK(tooLong == 0, "%d outputs over the bound", tooLong);
}

int main() {
  testLatin1();
  testUtf16();
  testSurrogates();
  testOdd();
  testEmpty();
  testTerminator();
  testUtf8();
  testValid();
  if (gFailures) {
    std::printf("FAILED: %d of %d checks\n", gFailures, gChecks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", gChecks);
  return 0;
}

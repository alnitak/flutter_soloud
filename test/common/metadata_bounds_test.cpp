// Unit tests for metadataHasBytes() (src/audiobuffer/metadata_bounds.h), the
// bounds check every metadata parser (ID3, M4A, Vorbis/Opus comments, FLAC
// pictures) uses before reading a length taken from the media data.
//
// Verifies:
//   edges          offset and length at 0 and at size, one past them, and
//                  size 0
//   wide-lengths   lengths of UINT32_MAX, UINT32_MAX + 1, SIZE_MAX and
//                  UINT64_MAX, which a parser can read from a 32-bit or 64-bit
//                  field, are rejected whatever the offset
//   wrap           the cases where `offset + length` wraps the size_t (at 32
//                  bits on the web and armeabi-v7a, at 64 bits elsewhere) and
//                  would pass a naive `offset + length <= size` check: the
//                  naive check is computed alongside to show the case is real
//   offset-past    an offset beyond size is rejected even with length 0
//   exhaustive     every (size, offset, length) up to 40 against the
//                  arithmetic answer
//
// Needs no audio device and no assets.
//
//   ./test/common/metadata_bounds_test.sh [--asan|--wasm32|--m32]
//
// The wrap cases are meaningful at both widths: build with --wasm32 (em++,
// 32-bit size_t) or --m32 (where the host compiler supports it) to run them
// with a 32-bit size_t.

#include "audiobuffer/metadata_bounds.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

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

static constexpr size_t kSizeMax = std::numeric_limits<size_t>::max();
static constexpr uint64_t kU32Max = 0xFFFFFFFFull;
static constexpr uint64_t kU64Max = std::numeric_limits<uint64_t>::max();

// The answer in exact arithmetic (no type can overflow: offset and length
// are compared separately against size).
static bool expected(size_t size, size_t offset, uint64_t length) {
  if (offset > size) return false;
  return length <= static_cast<uint64_t>(size) - static_cast<uint64_t>(offset);
}

// The check metadataHasBytes replaces: `offset + length <= size`, computed
// in size_t as the parsers used to.
static bool naive(size_t size, size_t offset, uint64_t length) {
  return offset + static_cast<size_t>(length) <= size;
}

static void testEdges() {
  std::printf("edges\n");
  CHECK(metadataHasBytes(0, 0, 0), "empty read of empty data");
  CHECK(!metadataHasBytes(0, 0, 1), "1 byte of empty data");
  CHECK(!metadataHasBytes(0, 1, 0), "offset 1 of empty data");
  CHECK(metadataHasBytes(10, 0, 10), "whole data");
  CHECK(!metadataHasBytes(10, 0, 11), "one past the whole data");
  CHECK(metadataHasBytes(10, 10, 0), "empty read at the end");
  CHECK(!metadataHasBytes(10, 10, 1), "1 byte at the end");
  CHECK(metadataHasBytes(10, 9, 1), "last byte");
  CHECK(!metadataHasBytes(10, 9, 2), "last byte + 1");
  CHECK(!metadataHasBytes(10, 11, 0), "offset past the end, length 0");
  CHECK(metadataHasBytes(kSizeMax, 0, kSizeMax), "SIZE_MAX of SIZE_MAX");
  CHECK(metadataHasBytes(kSizeMax, kSizeMax, 0), "empty read at SIZE_MAX");
  CHECK(!metadataHasBytes(kSizeMax, kSizeMax, 1), "1 byte at SIZE_MAX");
  CHECK(metadataHasBytes(kSizeMax, 1, kSizeMax - 1), "SIZE_MAX-1 from 1");
  CHECK(!metadataHasBytes(kSizeMax, 1, kSizeMax), "SIZE_MAX from 1");
}

static void testWideLengths() {
  std::printf("wide-lengths\n");
  const size_t sizes[] = {0, 1, 100, 0xFFFF, static_cast<size_t>(0x7FFFFFFF)};
  const uint64_t lengths[] = {kU32Max + 1, kU64Max, kU64Max - 1,
                              static_cast<uint64_t>(kSizeMax) + (sizeof(size_t) < 8 ? 1 : 0)};
  for (size_t size : sizes) {
    for (size_t offset : {size_t(0), size / 2, size}) {
      for (uint64_t length : lengths) {
        if (static_cast<uint64_t>(size) >= length) continue;
        CHECK(!metadataHasBytes(size, offset, length),
              "size %zu offset %zu length %llu accepted", size, offset,
              (unsigned long long)length);
      }
      if (size < kU32Max)
        CHECK(!metadataHasBytes(size, offset, kU32Max),
              "size %zu offset %zu length UINT32_MAX accepted", size, offset);
    }
  }
  // A length of UINT32_MAX fits exactly in data of that size (64-bit only).
  if (sizeof(size_t) >= 8) {
    const size_t big = static_cast<size_t>(kU32Max);
    CHECK(metadataHasBytes(big, 0, kU32Max), "UINT32_MAX of UINT32_MAX");
    CHECK(!metadataHasBytes(big, 1, kU32Max), "UINT32_MAX from 1");
  }
}

static void testWrap() {
  std::printf("wrap (size_t is %zu bits)\n", sizeof(size_t) * 8);
  struct Case { size_t size, offset; uint64_t length; };
  // Each case wraps `offset + length` in size_t, so the naive check passes.
  const Case cases[] = {
      {100, 10, static_cast<uint64_t>(kSizeMax)},           // 10 + SIZE_MAX = 9
      {100, 1, static_cast<uint64_t>(kSizeMax)},            // = 0
      {100, 50, static_cast<uint64_t>(kSizeMax - 20)},      // = 29
      {100, 100, static_cast<uint64_t>(kSizeMax) - 99},     // = 0
      {4096, 8, static_cast<uint64_t>(kSizeMax) - 7},       // 0, an MP4 box size - header
  };
  for (const Case &c : cases) {
    CHECK(naive(c.size, c.offset, c.length),
          "test mistake: size %zu offset %zu length %llu does not wrap",
          c.size, c.offset, (unsigned long long)c.length);
    CHECK(!metadataHasBytes(c.size, c.offset, c.length),
          "wrapping size %zu offset %zu length %llu accepted", c.size,
          c.offset, (unsigned long long)c.length);
  }
  // A 32-bit length field (ID3 frame size, MP4 box size, Vorbis comment
  // length) read as UINT32_MAX: on a 32-bit size_t it wraps.
  if (sizeof(size_t) == 4) {
    CHECK(naive(64, 4, kU32Max), "test mistake: 4 + UINT32_MAX should wrap");
  }
  CHECK(!metadataHasBytes(64, 4, kU32Max), "4 + UINT32_MAX accepted");
  CHECK(!metadataHasBytes(64, 4, kU32Max - 3), "4 + UINT32_MAX-3 accepted");
  // A length beyond size_t's range must not be truncated to a small value.
  if (sizeof(size_t) == 4) {
    CHECK(!metadataHasBytes(64, 0, (uint64_t(1) << 32) + 8),
          "2^32 + 8 truncated to 8 and accepted");
  }
  CHECK(!metadataHasBytes(64, 0, (uint64_t(1) << 63) + 8), "2^63 + 8 accepted");
}

static void testExhaustive() {
  std::printf("exhaustive\n");
  int mismatches = 0;
  for (size_t size = 0; size <= 40; ++size)
    for (size_t offset = 0; offset <= 41; ++offset)
      for (uint64_t length = 0; length <= 41; ++length)
        if (metadataHasBytes(size, offset, length) != expected(size, offset, length)) {
          if (++mismatches <= 5)
            CHECK(false, "size %zu offset %zu length %llu", size, offset,
                  (unsigned long long)length);
        }
  CHECK(mismatches == 0, "%d mismatches", mismatches);
  // Near the top of size_t.
  for (size_t d = 0; d < 4; ++d)
    for (size_t o = 0; o < 4; ++o)
      for (uint64_t l = 0; l < 6; ++l) {
        const size_t size = kSizeMax - d;
        const size_t offset = kSizeMax - o;
        const uint64_t length = l;
        CHECK(metadataHasBytes(size, offset, length) == expected(size, offset, length),
              "near SIZE_MAX: size -%zu offset -%zu length %llu", d, o,
              (unsigned long long)l);
      }
}

int main() {
  testEdges();
  testWideLengths();
  testWrap();
  testExhaustive();
  if (gFailures) {
    std::printf("FAILED: %d of %d checks\n", gFailures, gChecks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", gChecks);
  return 0;
}

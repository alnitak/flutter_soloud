// Unit tests for SoLoud::CircularFloatBuffer (src/audiobuffer/circular_float_buffer.cpp),
// the float ring the realtime/PCM stream path sits on.
//
// Verifies:
//   - capacity 0: every operation is a no-op that returns 0
//   - capacity 1: one sample at a time, alternating write/read
//   - writes larger than the capacity are truncated to the free space, and a
//     write into a full buffer returns 0 without touching the data
//   - wrap-around: data written across the physical end reads back in order,
//     also when the buffer is exactly full (write offset == read offset)
//   - read()/advanceRead() clamp to what is available
//   - "peek" semantics: read() followed by rewindRead() restores the same
//     samples, limited to what has been written before the ring has lapped
//     and to the whole free gap after it has
//   - readPlanar(): deinterleaves stereo across the wrap boundary, including
//     the frame straddling the end of an odd-sized ring
//   - clear() and setCapacity() reset the bookkeeping
//   - a randomized single-threaded model check against std::deque
//
// Needs no audio device and no assets.
//
//   ./test/common/circular_float_buffer_test.sh [--asan]

#include "audiobuffer/circular_float_buffer.h"
#include "../support/line_buffered_stdout.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <random>
#include <vector>

using SoLoud::CircularFloatBuffer;

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

static std::vector<float> ramp(size_t n, float start = 0.0f) {
  std::vector<float> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = start + static_cast<float>(i);
  return v;
}

static void testCapacityZero() {
  std::printf("case: capacity 0\n");
  CircularFloatBuffer b; // default-constructed: capacity 0
  float in[4] = {1, 2, 3, 4};
  float out[4] = {9, 9, 9, 9};
  CHECK(b.capacity() == 0, "capacity %zu", b.capacity());
  CHECK(b.available() == 0 && b.freeSpace() == 0 && b.empty(), "counters");
  CHECK(b.write(in, 4) == 0, "write");
  CHECK(b.read(out, 4) == 0, "read");
  CHECK(out[0] == 9, "read must not touch output");
  CHECK(b.readPlanar(out, 2, 2, 2) == 0, "readPlanar");
  b.advanceRead(3);
  CHECK(b.rewindRead(3) == 0, "rewindRead");
  CHECK(b.available() == 0, "available after no-ops");

  CircularFloatBuffer e(0); // explicit capacity 0
  CHECK(e.write(in, 1) == 0 && e.read(out, 1) == 0, "explicit 0");
}

static void testCapacityOne() {
  std::printf("case: capacity 1\n");
  CircularFloatBuffer b(1);
  float in[3] = {10, 20, 30};
  float out[3] = {0, 0, 0};
  CHECK(b.write(in, 3) == 1, "only one sample fits");
  CHECK(b.available() == 1 && b.freeSpace() == 0, "full");
  CHECK(b.write(in + 1, 1) == 0, "write into full buffer");
  CHECK(b.read(out, 3) == 1 && out[0] == 10, "read %f", out[0]);
  CHECK(b.empty(), "empty after read");
  for (int i = 0; i < 5; ++i) {
    float v = static_cast<float>(100 + i);
    CHECK(b.write(&v, 1) == 1, "write #%d", i);
    float r = -1;
    CHECK(b.read(&r, 1) == 1 && r == v, "read #%d got %f", i, r);
  }
  // Rewind after lapping restores the single slot.
  CHECK(b.rewindRead(5) == 1, "rewind on capacity 1");
  float r = -1;
  CHECK(b.read(&r, 1) == 1 && r == 104.0f, "rewound sample %f", r);
}

static void testOversizedWrite() {
  std::printf("case: write larger than capacity\n");
  CircularFloatBuffer b(8);
  auto data = ramp(20);
  CHECK(b.write(data.data(), data.size()) == 8, "truncated to capacity");
  CHECK(b.available() == 8 && b.freeSpace() == 0, "full");
  std::vector<float> out(20, -1.0f);
  CHECK(b.read(out.data(), out.size()) == 8, "read all");
  bool ok = true;
  for (int i = 0; i < 8; ++i) ok = ok && out[i] == static_cast<float>(i);
  CHECK(ok, "first 8 samples kept in order");
  CHECK(out[8] == -1.0f, "no write past what was read");

  // Partly full: the write is truncated to the free space.
  b.clear();
  CHECK(b.write(data.data(), 5) == 5, "5 in");
  CHECK(b.write(data.data() + 5, 10) == 3, "only 3 more fit");
  CHECK(b.write(data.data(), 0) == 0, "zero-length write");
}

static void testWrapAround() {
  std::printf("case: wrap-around\n");
  CircularFloatBuffer b(8);
  auto a = ramp(6, 0.0f);
  auto c = ramp(6, 100.0f);
  std::vector<float> out(16, -1.0f);
  CHECK(b.write(a.data(), 6) == 6, "first write");
  CHECK(b.read(out.data(), 4) == 4, "first read");
  // Read offset 4, write offset 6: 6 free, split 2 at the end + 4 at start.
  CHECK(b.write(c.data(), 6) == 6, "wrapping write");
  CHECK(b.available() == 8 && b.freeSpace() == 0, "full after wrap");
  // Full with write == read: the read must still take everything in order.
  CHECK(b.read(out.data(), 16) == 8, "wrapping read");
  const float expect[8] = {4, 5, 100, 101, 102, 103, 104, 105};
  bool ok = true;
  for (int i = 0; i < 8; ++i) ok = ok && out[i] == expect[i];
  CHECK(ok, "order across the wrap: %g %g %g %g %g %g %g %g", out[0], out[1],
        out[2], out[3], out[4], out[5], out[6], out[7]);
  CHECK(b.empty(), "empty");

  // Many laps with odd sizes.
  std::deque<float> model;
  float next = 0;
  for (int lap = 0; lap < 50; ++lap) {
    std::vector<float> w(3);
    for (auto &x : w) x = next++;
    size_t n = b.write(w.data(), w.size());
    for (size_t i = 0; i < n; ++i) model.push_back(w[i]);
    float r[5];
    size_t m = b.read(r, 5);
    for (size_t i = 0; i < m; ++i) {
      if (model.empty() || model.front() != r[i]) ok = false;
      if (!model.empty()) model.pop_front();
    }
  }
  CHECK(ok, "50 laps stay in order");
}

static void testAdvanceRead() {
  std::printf("case: advanceRead clamps\n");
  CircularFloatBuffer b(8);
  auto a = ramp(5);
  b.write(a.data(), 5);
  b.advanceRead(2);
  CHECK(b.available() == 3, "available %zu", b.available());
  float r = -1;
  CHECK(b.read(&r, 1) == 1 && r == 2.0f, "next sample %f", r);
  b.advanceRead(100);
  CHECK(b.available() == 0, "advance past end clamps, available %zu",
        b.available());
  CHECK(b.write(a.data(), 5) == 5, "writable again");
}

static void testPeekViaRewind() {
  std::printf("case: read + rewindRead (peek)\n");
  CircularFloatBuffer b(8);
  auto a = ramp(6);
  b.write(a.data(), 6);
  float out[6];
  CHECK(b.read(out, 4) == 4, "read 4");
  // Not lapped yet: at most the 4 samples behind the read pointer.
  CHECK(b.rewindRead(10) == 4, "rewind limited to read offset");
  float again[6];
  CHECK(b.read(again, 6) == 6, "read again");
  bool ok = true;
  for (int i = 0; i < 6; ++i) ok = ok && again[i] == static_cast<float>(i);
  CHECK(ok, "rewound data equals original");

  // Before lapping the never-written tail must not be exposed.
  CircularFloatBuffer f(8);
  f.write(a.data(), 2);
  f.read(out, 2);
  CHECK(f.rewindRead(8) == 2, "only the 2 written samples");

  // After lapping, the whole free gap is valid.
  CircularFloatBuffer g(8);
  auto big = ramp(8);
  g.write(big.data(), 8);
  g.read(out, 6);
  auto more = ramp(3, 50.0f);
  g.write(more.data(), 3); // write offset 3, read offset 6, size 5
  float r8[8];
  g.read(r8, 5); // read offset 3, size 0
  CHECK(g.rewindRead(100) == 8, "lapped ring rewinds over the free gap");
  CHECK(g.read(r8, 8) == 8, "read restored");
  const float expect[8] = {3, 4, 5, 6, 7, 50, 51, 52};
  ok = true;
  for (int i = 0; i < 8; ++i) ok = ok && r8[i] == expect[i];
  CHECK(ok, "restored order after lap: %g %g %g %g %g %g %g %g", r8[0], r8[1],
        r8[2], r8[3], r8[4], r8[5], r8[6], r8[7]);

  // Rewind cannot reach data that has been overwritten.
  CircularFloatBuffer h(4);
  auto four = ramp(4);
  h.write(four.data(), 4);
  h.read(out, 2);
  h.write(four.data(), 2); // full again
  CHECK(h.rewindRead(3) == 0, "full buffer has nothing to rewind");
  CHECK(h.rewindRead(0) == 0, "zero rewind");
}

static void testReadPlanar() {
  std::printf("case: readPlanar across the wrap\n");
  // Even capacity, stereo frames wrapping cleanly.
  CircularFloatBuffer b(8);
  float lr[8] = {0, 100, 1, 101, 2, 102, 3, 103};
  b.write(lr, 6);
  float tmp[4];
  b.read(tmp, 4); // read offset 4
  float lr2[4] = {4, 104, 5, 105};
  b.write(lr2, 4); // ring holds [2,102] + [4,104,5,105], wrapping at index 8
  float planar[2 * 4];
  for (float &x : planar) x = -1;
  size_t frames = b.readPlanar(planar, 4, 4, 2);
  CHECK(frames == 3, "frames %zu", frames);
  const float l[3] = {2, 4, 5}, r[3] = {102, 104, 105};
  bool ok = true;
  for (int i = 0; i < 3; ++i) ok = ok && planar[i] == l[i] && planar[4 + i] == r[i];
  CHECK(ok, "L %g %g %g R %g %g %g", planar[0], planar[1], planar[2], planar[4],
        planar[5], planar[6]);

  // Odd capacity: a stereo frame straddles the physical end.
  CircularFloatBuffer o(7);
  float s[6] = {0, 10, 1, 11, 2, 12};
  o.write(s, 6);
  float junk[6];
  o.read(junk, 6); // read offset 6
  float s2[6] = {3, 13, 4, 14, 5, 15};
  CHECK(o.write(s2, 6) == 6, "odd ring write");
  float p[2 * 3];
  for (float &x : p) x = -1;
  frames = o.readPlanar(p, 3, 3, 2);
  CHECK(frames == 3, "odd ring frames %zu", frames);
  ok = p[0] == 3 && p[1] == 4 && p[2] == 5 && p[3] == 13 && p[4] == 14 &&
       p[5] == 15;
  CHECK(ok, "odd ring L %g %g %g R %g %g %g", p[0], p[1], p[2], p[3], p[4], p[5]);
  CHECK(o.empty(), "odd ring empty");

  // A partial frame is left in the buffer.
  CircularFloatBuffer q(8);
  float three[3] = {1, 2, 3};
  q.write(three, 3);
  float pp[4];
  CHECK(q.readPlanar(pp, 2, 2, 2) == 1, "one full frame");
  CHECK(q.available() == 1, "partial frame kept");
  CHECK(q.readPlanar(nullptr, 2, 2, 2) == 0, "null destination");
  CHECK(q.readPlanar(pp, 2, 2, 0) == 0, "zero channels");
}

static void testClearAndResize() {
  std::printf("case: clear and setCapacity\n");
  CircularFloatBuffer b(4);
  auto a = ramp(4);
  b.write(a.data(), 4);
  b.clear();
  CHECK(b.empty() && b.freeSpace() == 4, "clear empties");
  CHECK(b.rewindRead(4) == 0, "clear forgets the lap");
  b.write(a.data(), 3);
  b.setCapacity(16);
  CHECK(b.capacity() == 16 && b.empty(), "setCapacity resets");
  auto big = ramp(16);
  CHECK(b.write(big.data(), 16) == 16, "new capacity usable");
  b.setCapacity(0);
  CHECK(b.write(big.data(), 1) == 0, "resized to 0");
}

static void testRandomModel() {
  std::printf("case: randomized model check\n");
  std::mt19937 rng(12345);
  bool ok = true;
  for (size_t cap : {1u, 2u, 3u, 7u, 64u, 1000u}) {
    CircularFloatBuffer b(cap);
    std::deque<float> model;
    float next = 0;
    for (int step = 0; step < 5000 && ok; ++step) {
      int op = rng() % 3;
      size_t n = rng() % (cap * 2 + 2);
      if (op == 0) {
        std::vector<float> w(n);
        for (auto &x : w) x = next++;
        size_t got = b.write(w.data(), n);
        size_t expect = std::min(n, cap - model.size());
        if (got != expect) ok = false;
        for (size_t i = 0; i < got; ++i) model.push_back(w[i]);
      } else if (op == 1) {
        std::vector<float> r(n + 1);
        size_t got = b.read(r.data(), n);
        if (got != std::min(n, model.size())) ok = false;
        for (size_t i = 0; i < got && ok; ++i) {
          if (r[i] != model.front()) ok = false;
          model.pop_front();
        }
      } else {
        size_t k = std::min(n, model.size());
        b.advanceRead(n);
        for (size_t i = 0; i < k; ++i) model.pop_front();
      }
      if (b.available() != model.size()) ok = false;
      if (b.freeSpace() != cap - model.size()) ok = false;
    }
    CHECK(ok, "model mismatch at capacity %zu", cap);
  }
}

int main() {
  lineBufferedStdout();
  testCapacityZero();
  testCapacityOne();
  testOversizedWrite();
  testWrapAround();
  testAdvanceRead();
  testPeekViaRewind();
  testReadPlanar();
  testClearAndResize();
  testRandomModel();
  std::printf("%d checks, %d failures\n", gChecks, gFailures);
  if (gFailures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

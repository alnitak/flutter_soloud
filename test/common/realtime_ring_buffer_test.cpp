// Standalone tests for SoLoud::RealtimeRingBuffer
// (src/audiobuffer/realtime_ring_buffer.h), the SPSC planar ring behind
// RealtimeStream.
//
//   - init(): power-of-two capacity, history reserved, 0 channels -> 1;
//   - null / zero arguments and an uninitialized ring return 0;
//   - single thread, 1..8 channels: interleaved float, planar float and
//     interleaved int16 pushes in odd chunk sizes, popped in other odd sizes
//     across many wraps of the capacity: every frame comes back once, in
//     order, with every channel intact;
//   - full ring: push returns only what fits, then 0;
//   - history window: after the ring is refilled to the limit, the last
//     historyFrames() frames before the read cursor are still re-readable
//     with setReadFrame(); setReadFrame() clamps to the write cursor and to
//     the raw capacity;
//   - one producer thread and one consumer thread with numbered frames, 1..8
//     channels: nothing lost, duplicated or reordered (run with --tsan);
//   - reset() from the consumer while the producer keeps pushing: the frames
//     read afterwards are still strictly increasing and never torn.
//
//   ./test/common/realtime_ring_buffer_test.sh [--asan|--tsan] [--strict]

#include "audiobuffer/realtime_ring_buffer.h"

#include "../support/robustness_checks.h"

#include <random>
#include <thread>
#include <vector>

using SoLoud::RealtimeRingBuffer;

namespace {

// Frame f, channel c is encoded as f * 8 + c (exact in float up to 2^24).
constexpr uint64_t kModulo = (1u << 21);
inline float encode(uint64_t frame, unsigned ch) {
  return static_cast<float>((frame % kModulo) * 8 + ch);
}
// int16 variant: small values, exact after the /32768 scaling.
inline int16_t encode16(uint64_t frame, unsigned ch) {
  return static_cast<int16_t>(((frame % 4000) * 8 + ch) - 16000);
}
inline float decode16(int16_t v) { return static_cast<float>(v) / 32768.0f; }

void testInit() {
  std::printf("init\n");
  RealtimeRingBuffer rb;
  CHECK(rb.availableToRead() == 0, "fresh ring has nothing to read");
  CHECK(rb.availableToWrite() == 0, "uninitialized ring accepts nothing");
  float buf[16] = {};
  int16_t s16[16] = {};
  CHECK(rb.pushInterleavedFloat32(buf, 4) == 0, "push into uninit ring");
  CHECK(rb.pushPlanarFloat32(buf, 4) == 0, "planar push into uninit ring");
  CHECK(rb.pushInterleavedInt16(s16, 4) == 0, "int16 push into uninit ring");
  CHECK(rb.popPlanar(buf, 4, 4) == 0, "pop from uninit ring");

  rb.init(0, 10, 0);
  CHECK(rb.channels() == 1, "0 channels becomes 1 (got %u)", rb.channels());
  CHECK(rb.rawCapacityFrames() == 256, "minimum capacity 256 (got %zu)",
        rb.rawCapacityFrames());

  rb.init(2, 1000, 4096);
  CHECK(rb.rawCapacityFrames() == 8192, "1000+4096 -> 8192 (got %zu)",
        rb.rawCapacityFrames());
  CHECK(rb.capacityFrames() == 8192 - 4096, "usable = raw - history (%zu)",
        rb.capacityFrames());
  CHECK(rb.historyFrames() == 4096, "history kept");
  CHECK(rb.availableToWrite() == rb.capacityFrames(), "empty ring writable");

  CHECK(rb.pushInterleavedFloat32(nullptr, 4) == 0, "null push");
  CHECK(rb.pushPlanarFloat32(nullptr, 4) == 0, "null planar push");
  CHECK(rb.pushInterleavedInt16(nullptr, 4) == 0, "null int16 push");
  CHECK(rb.pushInterleavedFloat32(buf, 0) == 0, "zero-frame push");
  CHECK(rb.popPlanar(nullptr, 4, 4) == 0, "null pop");
  CHECK(rb.popPlanar(buf, 0, 4) == 0, "zero-frame pop");
  CHECK(rb.popPlanar(buf, 4, 4) == 0, "pop from empty ring");
  CHECK(rb.getWriteFrame() == 0 && rb.getReadFrame() == 0,
        "no cursor moved");
}

// Push frames [next, next + n) in the chosen format. Returns frames written.
size_t pushFrames(RealtimeRingBuffer &rb, unsigned ch, uint64_t next, size_t n,
                  int format, std::vector<float> &scratch,
                  std::vector<int16_t> &scratch16) {
  if (format == 0) {
    scratch.resize(n * ch);
    for (size_t i = 0; i < n; ++i)
      for (unsigned c = 0; c < ch; ++c)
        scratch[i * ch + c] = encode(next + i, c);
    return rb.pushInterleavedFloat32(scratch.data(), n);
  }
  if (format == 1) {
    scratch.resize(n * ch);
    for (unsigned c = 0; c < ch; ++c)
      for (size_t i = 0; i < n; ++i) scratch[c * n + i] = encode(next + i, c);
    return rb.pushPlanarFloat32(scratch.data(), n);
  }
  scratch16.resize(n * ch);
  for (size_t i = 0; i < n; ++i)
    for (unsigned c = 0; c < ch; ++c)
      scratch16[i * ch + c] = encode16(next + i, c);
  return rb.pushInterleavedInt16(scratch16.data(), n);
}

bool frameMatches(const float *buf, size_t stride, size_t i, unsigned ch,
                  uint64_t frame, int format) {
  for (unsigned c = 0; c < ch; ++c) {
    const float want = format == 2 ? decode16(encode16(frame, c))
                                   : encode(frame, c);
    if (buf[c * stride + i] != want) return false;
  }
  return true;
}

void testSingleThreadWrap() {
  std::printf("single thread, 1..8 channels, all push formats, wraps\n");
  for (int format = 0; format < 3; ++format) {
    for (unsigned ch = 1; ch <= 8; ++ch) {
      RealtimeRingBuffer rb;
      rb.init(ch, 300, 64); // raw 512, usable 448
      const size_t usable = rb.capacityFrames();
      std::vector<float> scratch;
      std::vector<int16_t> scratch16;
      std::vector<float> out(64 * ch);
      uint64_t written = 0, read = 0;
      int mismatches = 0;
      bool overfull = false;
      const uint64_t target = rb.rawCapacityFrames() * 9 + 17;
      size_t pushSize = 1, popSize = 1;
      while (read < target) {
        size_t want = std::min<size_t>(pushSize, 61);
        const size_t room = rb.availableToWrite();
        const size_t got =
            pushFrames(rb, ch, written, want, format, scratch, scratch16);
        if (got != std::min(want, room)) overfull = true;
        written += got;
        if (rb.availableToRead() > usable) overfull = true;
        pushSize = (pushSize * 7 + 3) % 61 + 1;

        size_t n = std::min<size_t>(popSize, 64);
        const size_t popped = rb.popPlanar(out.data(), n, 64);
        for (size_t i = 0; i < popped; ++i)
          if (!frameMatches(out.data(), 64, i, ch, read + i, format))
            ++mismatches;
        read += popped;
        popSize = (popSize * 5 + 1) % 53 + 1;
      }
      CHECK(mismatches == 0, "format %d ch %u: %d frames wrong", format, ch,
            mismatches);
      CHECK(!overfull, "format %d ch %u: push exceeded the usable capacity",
            format, ch);
      CHECK(rb.getReadFrame() == read && rb.getWriteFrame() == written,
            "format %d ch %u: cursors", format, ch);
    }
  }
}

void testFull() {
  std::printf("full ring\n");
  RealtimeRingBuffer rb;
  rb.init(2, 200, 56); // raw 256, usable 200
  const size_t usable = rb.capacityFrames();
  std::vector<float> big(1000 * 2, 0.5f);
  CHECK(rb.pushInterleavedFloat32(big.data(), 1000) == usable,
        "first push fills to %zu", usable);
  CHECK(rb.availableToWrite() == 0, "nothing writable when full");
  CHECK(rb.pushInterleavedFloat32(big.data(), 1) == 0, "push into full ring");
  CHECK(rb.pushPlanarFloat32(big.data(), 1) == 0, "planar push into full");
  std::vector<float> out(16 * 2);
  CHECK(rb.popPlanar(out.data(), 16, 16) == 16, "pop 16");
  CHECK(rb.availableToWrite() == 16, "16 writable again");
  CHECK(rb.pushInterleavedFloat32(big.data(), 100) == 16, "push fits 16");
}

void testHistory() {
  std::printf("history window\n");
  RealtimeRingBuffer rb;
  const unsigned ch = 3;
  rb.init(ch, 400, 112); // raw 512, usable 400
  const size_t usable = rb.capacityFrames();
  const size_t history = rb.historyFrames();
  std::vector<float> scratch;
  std::vector<int16_t> scratch16;
  std::vector<float> out(512 * ch);
  uint64_t written = 0, read = 0;
  // Advance a few wraps first so the history window crosses the wrap point.
  for (int i = 0; i < 7; ++i) {
    written += pushFrames(rb, ch, written, 333, 0, scratch, scratch16);
    read += rb.popPlanar(out.data(), 333, 512);
  }
  // Refill to the limit: the producer may now have overwritten everything
  // except [read - history, read).
  written += pushFrames(rb, ch, written, usable, 0, scratch, scratch16);
  CHECK(rb.availableToWrite() == 0, "refilled to the limit");

  rb.setReadFrame(read - history);
  CHECK(rb.getReadFrame() == read - history, "rewound by history");
  const size_t got = rb.popPlanar(out.data(), history + 50, 512);
  CHECK(got == history + 50, "re-read history + 50 frames (got %zu)", got);
  int bad = 0;
  for (size_t i = 0; i < got; ++i)
    if (!frameMatches(out.data(), 512, i, ch, read - history + i, 0)) ++bad;
  CHECK(bad == 0, "%d history frames corrupted", bad);

  // Clamping.
  rb.setReadFrame(written + 1000);
  CHECK(rb.getReadFrame() == written, "setReadFrame clamps to write cursor");
  rb.setReadFrame(0);
  CHECK(rb.getReadFrame() == written - rb.rawCapacityFrames(),
        "setReadFrame clamps to raw capacity (%llu vs %llu)",
        (unsigned long long)rb.getReadFrame(),
        (unsigned long long)(written - rb.rawCapacityFrames()));
  CHECK(rb.availableToWrite() == 0, "nothing writable after a deep rewind");
  // Reading the full raw capacity back still yields consecutive frames
  // (the slot at the write cursor has not been overwritten yet).
  const size_t deep = rb.popPlanar(out.data(), 512, 512);
  bad = 0;
  for (size_t i = 0; i < deep; ++i)
    if (!frameMatches(out.data(), 512, i, ch,
                      written - rb.rawCapacityFrames() + i, 0))
      ++bad;
  CHECK(deep == rb.rawCapacityFrames() && bad == 0,
        "deep rewind re-read %zu frames, %d bad", deep, bad);

  rb.reset();
  CHECK(rb.availableToRead() == 0, "reset empties the ring");
  CHECK(rb.availableToWrite() == usable, "reset frees all space");
}

void testThreaded(unsigned ch, int format, uint64_t total) {
  RealtimeRingBuffer rb;
  rb.init(ch, 1000, 256);
  std::atomic<bool> producerDone{false};
  std::thread producer([&] {
    std::vector<float> scratch;
    std::vector<int16_t> scratch16;
    std::mt19937 rng(ch * 31 + format);
    uint64_t next = 0;
    while (next < total) {
      size_t n = std::min<uint64_t>(rng() % 97 + 1, total - next);
      size_t got = pushFrames(rb, ch, next, n, format, scratch, scratch16);
      next += got;
      if (got == 0) std::this_thread::yield();
    }
    producerDone.store(true);
  });

  std::vector<float> out(128 * ch);
  std::mt19937 rng(ch * 17 + format);
  uint64_t read = 0;
  int bad = 0;
  int spins = 0;
  while (read < total) {
    size_t n = rng() % 128 + 1;
    size_t got = rb.popPlanar(out.data(), n, 128);
    for (size_t i = 0; i < got; ++i)
      if (!frameMatches(out.data(), 128, i, ch, read + i, format)) ++bad;
    read += got;
    if (got == 0) {
      if (producerDone.load() && rb.availableToRead() == 0) break;
      if (++spins % 64 == 0) std::this_thread::yield();
    }
  }
  producer.join();
  CHECK(read == total, "ch %u format %d: read %llu of %llu", ch, format,
        (unsigned long long)read, (unsigned long long)total);
  CHECK(bad == 0, "ch %u format %d: %d frames lost/duplicated/reordered/torn",
        ch, format, bad);
}

void testThreadedAll() {
  std::printf("one producer + one consumer thread, 1..8 channels\n");
  for (unsigned ch = 1; ch <= 8; ++ch)
    for (int format = 0; format < 3; ++format)
      testThreaded(ch, format, 60000);
}

// The consumer calls reset() now and then (the read cursor is the consumer's
// side) while the producer keeps pushing. After every reset the frames read
// must still be strictly increasing, complete, and never torn.
void testResetDuringUse() {
  std::printf("reset() from the consumer while the producer pushes\n");
  for (unsigned ch : {1u, 2u, 5u}) {
    RealtimeRingBuffer rb;
    rb.init(ch, 500, 128);
    const uint64_t total = 80000;
    std::atomic<bool> done{false};
    std::thread producer([&] {
      std::vector<float> scratch;
      std::vector<int16_t> scratch16;
      uint64_t next = 0;
      unsigned n = 1;
      while (next < total) {
        size_t got = pushFrames(rb, ch, next, n, 0, scratch, scratch16);
        next += got;
        n = n % 67 + 1;
        if (got == 0) std::this_thread::yield();
      }
      done.store(true);
    });
    std::vector<float> out(64 * ch);
    int64_t last = -1;
    int bad = 0, resets = 0, loops = 0;
    while (!done.load() || rb.availableToRead() > 0) {
      if (++loops % 50 == 0) {
        rb.reset();
        ++resets;
        continue;
      }
      size_t got = rb.popPlanar(out.data(), 64, 64);
      for (size_t i = 0; i < got; ++i) {
        const float v0 = out[i];
        const uint64_t f = static_cast<uint64_t>(v0) / 8;
        if (static_cast<int64_t>(f) <= last) ++bad;
        if (!frameMatches(out.data(), 64, i, ch, f, 0)) ++bad;
        last = static_cast<int64_t>(f);
      }
      if (got == 0) std::this_thread::yield();
    }
    producer.join();
    CHECK(bad == 0, "ch %u: %d frames out of order or torn after %d resets",
          ch, bad, resets);
    CHECK(rb.availableToRead() == 0, "drained");
  }
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  testInit();
  testSingleThreadWrap();
  testFull();
  testHistory();
  testThreadedAll();
  testResetDuringUse();
  return robustness::finish("realtime_ring_buffer_test");
}

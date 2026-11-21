// Direct tests of the pull stream (src/audiobuffer/pull_buffer_stream.cpp,
// PullBufferStream), on raw PCM so every expected sample is known exactly.
//
// The stream is created through the exports Dart uses (setPullBufferStream,
// addPullBufferDataStream, resetPullBufferStream) on a real engine with the
// null backend, but it is never played: the test makes its own voice instance
// (createInstance()) and calls getAudio(), seek() and hasEnded() itself, with
// SoLoud's audio mutex held as the mixer does. The data callback only records
// the requested offsets; the test answers them, so the whole run is
// deterministic.
//
// Verifies:
//   short        every request answered with fewer bytes than a block, in
//                pieces that split frames (1, 3, 7, 1001, ... bytes), for
//                f32le, s16le, s32le and s8: the output equals the source
//   empty        an empty delivery returns noError and changes nothing; a
//                stream left waiting for data recovers with a reset
//   tail         the declared size ends mid-frame: the whole frames play and
//                then the voice ends
//   seek-end     a seek past the end (and to exactly the end) ends the voice
//   seek-frame   an out-of-buffer seek requests a frame-aligned offset and an
//                in-buffer seek keeps the channel order, also when the target
//                time falls between two frames
//   reset        a reset mid-stream asks for offset 0 again and the stream
//                plays the source from the start
//   threads      a feeder answers requests, another thread resets and the
//                test reads and seeks, all at once (no crash, no sanitizer
//                report); then a new stream plays exactly
//
// Needs no audio device and no assets (null backend, PCM made in memory).
//
//   ./test/common/pull_buffer_stream_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "bindings.h"
#include "player.h"
#include "audiobuffer/pull_buffer_stream.h"
#include "enums.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

// The engine's Player, defined in bindings.cpp.
extern "C" std::unique_ptr<Player> &player;

namespace {
constexpr int64_t kEngineId = 4021;

int gFailures = 0;
int gChecks = 0;
int gKnownBugs = 0;
bool gStrict = false;

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

std::mutex gReqMutex;
std::deque<uint64_t> gRequests;

void onMoreData(uint64_t offset) {
  std::lock_guard<std::mutex> lock(gReqMutex);
  gRequests.push_back(offset);
}

bool popRequest(uint64_t &offset) {
  std::lock_guard<std::mutex> lock(gReqMutex);
  if (gRequests.empty()) return false;
  offset = gRequests.front();
  gRequests.pop_front();
  return true;
}

void clearRequests() {
  std::lock_guard<std::mutex> lock(gReqMutex);
  gRequests.clear();
}

void onVoiceEnded(unsigned int *h) { std::free(h); }
void onFileLoaded(enum PlayerErrors *, char *, unsigned int *, uint64_t *) {}
void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

struct AudioLock {
  AudioLock() { player->soloud.lockAudioMutex_internal(); }
  ~AudioLock() { player->soloud.unlockAudioMutex_internal(); }
};

/// The source: interleaved frames where the left channel is positive and the
/// right one negative, both changing every frame, so a lost byte, a channel
/// swap or a skipped frame changes the output.
struct Source {
  BufferType type;
  unsigned int rate;
  unsigned int channels;
  unsigned int bytesPerSample;
  std::vector<unsigned char> bytes;
  std::vector<float> expected; // what the stream must decode, interleaved
};

Source makeSource(BufferType type, unsigned int rate, unsigned int channels,
                  size_t frames) {
  Source s{type, rate, channels, 4, {}, {}};
  if (type == BufferType::PCM_S16LE) s.bytesPerSample = 2;
  if (type == BufferType::PCM_S8) s.bytesPerSample = 1;
  for (size_t f = 0; f < frames; ++f) {
    for (unsigned int c = 0; c < channels; ++c) {
      // 0.1 .. 0.9, positive on channel 0, negative on the others.
      const int step = static_cast<int>((f * 7 + c * 3) % 100);
      const double mag = 0.1 + 0.008 * step;
      const double v = c == 0 ? mag : -mag;
      switch (type) {
      case BufferType::PCM_F32LE: {
        const float x = static_cast<float>(v);
        unsigned char b[4];
        std::memcpy(b, &x, 4);
        s.bytes.insert(s.bytes.end(), b, b + 4);
        s.expected.push_back(x);
        break;
      }
      case BufferType::PCM_S16LE: {
        const int16_t x = static_cast<int16_t>(std::lround(v * 32767));
        s.bytes.push_back(static_cast<unsigned char>(x & 0xFF));
        s.bytes.push_back(static_cast<unsigned char>((x >> 8) & 0xFF));
        s.expected.push_back(static_cast<float>(x) / 32768.0f);
        break;
      }
      case BufferType::PCM_S32LE: {
        const int32_t x = static_cast<int32_t>(std::lround(v * 2147483647.0));
        for (int i = 0; i < 4; ++i)
          s.bytes.push_back(static_cast<unsigned char>((x >> (8 * i)) & 0xFF));
        s.expected.push_back(static_cast<float>(x) * (1.0f / 2147483648.0f));
        break;
      }
      case BufferType::PCM_S8: {
        const int8_t x = static_cast<int8_t>(std::lround(v * 127));
        s.bytes.push_back(static_cast<unsigned char>(x));
        s.expected.push_back(static_cast<float>(x) / 128.0f);
        break;
      }
      default:
        break;
      }
    }
  }
  return s;
}

/// One stream and the test's own voice on it.
struct Stream {
  unsigned int hash = 0;
  SoLoud::PullBufferStream *stream = nullptr;
  SoLoud::AudioSourceInstance *voice = nullptr;
  const Source *src = nullptr;
  uint64_t declaredSize = 0;

  bool open(const Source &s, double bufferSeconds, uint64_t size = 0) {
    clearRequests();
    src = &s;
    declaredSize = size ? size : s.bytes.size();
    const unsigned int bufferBytes = static_cast<unsigned int>(
        bufferSeconds * s.rate * s.channels * sizeof(float));
    const PlayerErrors err = setPullBufferStream(
        &hash, bufferBytes, 0.8, s.rate, s.channels, static_cast<int>(s.type),
        declaredSize, nullptr, nullptr, onMoreData, nullptr);
    CHECK(err == PlayerErrors::noError, "setPullBufferStream: %d", (int)err);
    if (err != PlayerErrors::noError) return false;
    ActiveSound *sound = player->findByHash(hash);
    CHECK(sound != nullptr, "the stream's sound should exist");
    if (!sound) return false;
    stream = static_cast<SoLoud::PullBufferStream *>(sound->sound.get());
    voice = stream->createInstance();
    voice->init(*stream, 0);
    return true;
  }

  void close() {
    {
      AudioLock lock;
      delete voice;
      voice = nullptr;
    }
    disposeSound(hash);
    hash = 0;
    clearRequests();
  }

  /// Answers one request with [len] bytes (fewer at the end of the data).
  bool answer(uint64_t offset, size_t len) {
    const uint64_t avail = std::min<uint64_t>(declaredSize, src->bytes.size());
    if (offset >= avail) return false;
    len = static_cast<size_t>(std::min<uint64_t>(len, avail - offset));
    const PlayerErrors err = addPullBufferDataStream(
        hash, src->bytes.data() + offset, static_cast<unsigned int>(len),
        offset);
    CHECK(err == PlayerErrors::noError, "addPullBufferDataStream(%llu, %zu): %d",
          (unsigned long long)offset, len, (int)err);
    return true;
  }

  /// Reads [frames] frames from the voice, as the mixer would, and appends
  /// them interleaved to [out]. Returns the frames the voice produced.
  unsigned int read(unsigned int frames, std::vector<float> *out) {
    const unsigned int ch = src->channels;
    std::vector<float> planar(static_cast<size_t>(frames) * ch);
    unsigned int got = 0;
    {
      AudioLock lock;
      got = voice->getAudio(planar.data(), frames, frames);
    }
    if (out) {
      for (unsigned int f = 0; f < got; ++f)
        for (unsigned int c = 0; c < ch; ++c)
          out->push_back(planar[c * frames + f]);
    }
    return got;
  }

  bool ended() {
    AudioLock lock;
    return voice->hasEnded();
  }

  void seek(double t) {
    AudioLock lock;
    voice->seek(t, nullptr, 0);
  }
};

/// Plays [s] to the end, answering every request with the next size in
/// [sizes] (cycling). Returns what the voice played.
std::vector<float> playAll(Stream &st, const std::vector<size_t> &sizes,
                           unsigned int block = 256, int maxIter = 200000) {
  std::vector<float> out;
  size_t next = 0;
  for (int i = 0; i < maxIter && !st.ended(); ++i) {
    uint64_t offset;
    while (popRequest(offset)) {
      st.answer(offset, sizes[next % sizes.size()]);
      ++next;
    }
    st.read(block, &out);
  }
  return out;
}

const char *typeName(BufferType t) {
  switch (t) {
  case BufferType::PCM_F32LE: return "f32le";
  case BufferType::PCM_S16LE: return "s16le";
  case BufferType::PCM_S32LE: return "s32le";
  case BufferType::PCM_S8: return "s8";
  default: return "?";
  }
}

/// Index of the first sample where [a] and [b] differ, or -1.
long firstDiff(const std::vector<float> &a, const std::vector<float> &b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i)
    if (a[i] != b[i]) return static_cast<long>(i);
  return a.size() == b.size() ? -1 : static_cast<long>(n);
}

void testShortDeliveries() {
  std::printf("short deliveries that split frames\n");
  const BufferType types[] = {BufferType::PCM_F32LE, BufferType::PCM_S16LE,
                              BufferType::PCM_S32LE, BufferType::PCM_S8};
  const std::vector<size_t> sizes = {1, 3, 7, 1001, 333, 4099, 2, 65537, 5};
  for (BufferType t : types) {
    for (unsigned int ch : {1u, 2u, 6u}) {
      const Source s = makeSource(t, 22050, ch, 22050);
      Stream st;
      if (!st.open(s, 0.3)) continue;
      const std::vector<float> out = playAll(st, sizes);
      const long d = firstDiff(out, s.expected);
      CHECK(d < 0, "%s %u ch: %zu samples played of %zu, first difference at "
                   "%ld", typeName(t), ch, out.size(), s.expected.size(), d);
      CHECK(st.ended(), "%s %u ch: the voice should end", typeName(t), ch);
      st.close();
    }
  }
}

void testEmptyDelivery() {
  std::printf("empty deliveries\n");
  const Source s = makeSource(BufferType::PCM_F32LE, 22050, 2, 22050);
  Stream st;
  if (!st.open(s, 0.3)) return;
  uint64_t offset = 0;
  CHECK(popRequest(offset) && offset == 0,
        "the stream should ask for offset 0 first, got %llu",
        (unsigned long long)offset);
  // The request is answered with nothing (Dart drops empty chunks; the
  // native export accepts them).
  PlayerErrors err = addPullBufferDataStream(0x7FFFFFF0u, s.bytes.data(), 4, 0);
  CHECK(err == PlayerErrors::soundHashNotFound,
        "a delivery to an unknown hash should be refused, got %d", (int)err);
  err = addPullBufferDataStream(st.hash, s.bytes.data(), 0, 0);
  CHECK(err == PlayerErrors::noError, "an empty delivery: %d", (int)err);
  std::vector<float> out;
  for (int i = 0; i < 20; ++i) st.read(256, &out);
  CHECK(out.empty(), "nothing should play before any data, got %zu samples",
        out.size());
  CHECK(!st.ended(), "the voice should not end while it waits for data");
  // The stream stays waiting for the answer to its first request. A reset is
  // how the app restarts it: it must ask for offset 0 again.
  clearRequests();
  err = resetPullBufferStream(st.hash);
  CHECK(err == PlayerErrors::noError, "resetPullBufferStream: %d", (int)err);
  CHECK(popRequest(offset) && offset == 0,
        "after a reset the stream should ask for offset 0, got %llu",
        (unsigned long long)offset);
  st.answer(0, 4096);
  out = playAll(st, {4096});
  const long d = firstDiff(out, s.expected);
  CHECK(d < 0, "after the reset the source should play exactly: %zu of %zu "
               "samples, first difference at %ld", out.size(),
        s.expected.size(), d);
  st.close();
}

void testTailMidFrame() {
  std::printf("declared size ending mid-frame\n");
  for (BufferType t : {BufferType::PCM_F32LE, BufferType::PCM_S16LE}) {
    const Source s = makeSource(t, 22050, 2, 11025);
    const size_t frameBytes = 2 * s.bytesPerSample;
    // The data stops 1 byte into a frame; the declared size matches it.
    Source cut = s;
    cut.bytes.resize(s.bytes.size() - frameBytes + 1);
    cut.expected.resize(s.expected.size() - 2);
    Stream st;
    if (!st.open(cut, 0.3)) continue;
    const std::vector<float> out = playAll(st, {4000, 777});
    const long d = firstDiff(out, cut.expected);
    CHECK(d < 0, "%s: %zu of %zu samples, first difference at %ld",
          typeName(t), out.size(), cut.expected.size(), d);
    CHECK(st.ended(), "%s: the voice should end after the last whole frame",
          typeName(t));
    st.close();
  }
}

void testSeekPastEnd() {
  std::printf("seek past the end\n");
  const Source s = makeSource(BufferType::PCM_F32LE, 22050, 2, 22050 * 2);
  const double length = 2.0;
  for (double target : {length + 5.0, length}) {
    Stream st;
    if (!st.open(s, 0.3)) continue;
    // Play a little first, so the buffer holds the start.
    std::vector<float> out;
    for (int i = 0; i < 20; ++i) {
      uint64_t offset;
      while (popRequest(offset)) st.answer(offset, 8192);
      st.read(256, &out);
    }
    st.seek(target);
    bool ended = false;
    for (int i = 0; i < 2000 && !ended; ++i) {
      uint64_t offset;
      while (popRequest(offset)) st.answer(offset, 8192);
      st.read(256, nullptr);
      ended = st.ended();
    }
    // The seek clamps to the length and maps it to offset == audioSizeBytes,
    // where no data can be requested: the voice must end.
    CHECK(ended, "after seek(%.1f) on a %.1f s stream the voice never ends",
          target, length);
    st.close();
  }
}

void testSeekFrameAlignment() {
  std::printf("seeks to a time between two frames\n");
  // 16384 Hz makes the times exact in binary: 2001 / 32768 s is frame 1000.5.
  const unsigned int rate = 16384;
  const double between = 2001.0 / 32768.0 + 1.0; // frame 17384.5
  const Source s = makeSource(BufferType::PCM_F32LE, rate, 2, rate * 3);
  const uint64_t frameBytes = 8;

  // Out of the buffer: the requested offset must start a frame.
  {
    Stream st;
    if (st.open(s, 0.25)) {
      std::vector<float> out;
      for (int i = 0; i < 20; ++i) {
        uint64_t offset;
        while (popRequest(offset)) st.answer(offset, 4096);
        st.read(256, &out);
      }
      clearRequests();
      st.seek(between);
      uint64_t requested = UINT64_MAX;
      popRequest(requested);
      // The decode frames the data from the requested byte, so the offset
      // must start a frame.
      CHECK(requested != UINT64_MAX && requested % frameBytes == 0,
            "seek(%.9f) requested byte %llu, not a multiple of the "
            "%llu-byte frame", between, (unsigned long long)requested,
            (unsigned long long)frameBytes);
      if (requested != UINT64_MAX) {
        st.answer(requested, 8192);
        out.clear();
        for (int i = 0; i < 10 && out.empty(); ++i) {
          uint64_t offset;
          while (popRequest(offset)) st.answer(offset, 8192);
          st.read(64, &out);
        }
        const bool sane = out.size() >= 2 && out[0] > 0.0f && out[0] < 1.0f &&
                          out[1] < 0.0f && out[1] > -1.0f;
        CHECK(sane, "after the seek the first frame is (%g, %g); "
                    "the left channel should be in (0, 1), the right in "
                    "(-1, 0)", out.size() > 0 ? out[0] : 0.0,
              out.size() > 1 ? out[1] : 0.0);
      }
      st.close();
    }
  }

  // In the buffer: the channel order must survive the seek.
  {
    Stream st;
    if (st.open(s, 2.5)) {
      std::vector<float> out;
      // The whole file in one answer: 2.5 s go to the buffer.
      uint64_t offset;
      while (popRequest(offset)) st.answer(offset, s.bytes.size());
      st.read(256, &out); // start the voice
      double start = 0, end = 0;
      getPullBufferTimeRange(st.hash, &start, &end);
      CHECK(start <= between && between < end,
            "%.3f s should be buffered, buffered [%g, %g]", between, start,
            end);
      // The reference: a time on a frame boundary (frame 16384).
      st.seek(1.0);
      out.clear();
      st.read(64, &out);
      const std::vector<float> want(s.expected.begin() + 16384 * 2,
                                    s.expected.begin() + 16384 * 2 + 128);
      CHECK(firstDiff(out, want) < 0,
            "after an in-buffer seek(1.0) the output should be the source "
            "from frame 16384 (first difference at %ld)",
            firstDiff(out, want));
      clearRequests();
      st.seek(between);
      CHECK(!popRequest(offset), "an in-buffer seek should request nothing, "
                                 "requested %llu", (unsigned long long)offset);
      out.clear();
      st.read(64, &out);
      const bool sane = out.size() >= 2 && out[0] > 0.0f && out[1] < 0.0f;
      // A time between two frames must move the read pointer to a frame
      // start, or every later frame takes its channels from two
      // neighbouring frames (left and right swapped in stereo).
      CHECK(sane, "after an in-buffer seek(%.9f) the first frame is (%g, %g); "
                  "the left channel should be positive, the right negative",
            between, out.size() > 0 ? out[0] : 0.0,
            out.size() > 1 ? out[1] : 0.0);
      st.close();
    }
  }

  // A time on a frame boundary must work in both paths (the reference for
  // the two cases above).
  {
    Stream st;
    if (st.open(s, 0.25)) {
      std::vector<float> out;
      for (int i = 0; i < 20; ++i) {
        uint64_t offset;
        while (popRequest(offset)) st.answer(offset, 4096);
        st.read(256, &out);
      }
      clearRequests();
      const double t = 1.5; // frame 24576
      st.seek(t);
      uint64_t requested = UINT64_MAX;
      popRequest(requested);
      CHECK(requested == 24576 * frameBytes,
            "seek(1.5) should request byte %llu, got %llu",
            (unsigned long long)(24576 * frameBytes),
            (unsigned long long)requested);
      if (requested != UINT64_MAX) st.answer(requested, 8192);
      out.clear();
      for (int i = 0; i < 10 && out.size() < 128; ++i) {
        uint64_t offset;
        while (popRequest(offset)) st.answer(offset, 8192);
        st.read(64, &out);
      }
      const std::vector<float> want(s.expected.begin() + 24576 * 2,
                                    s.expected.begin() + 24576 * 2 + 128);
      out.resize(std::min<size_t>(out.size(), 128));
      CHECK(firstDiff(out, want) < 0,
            "after seek(1.5) the output should be the source from frame "
            "24576 (%zu samples, first difference at %ld)", out.size(),
            firstDiff(out, want));
      st.close();
    }
  }
}

void testResetAndReuse() {
  std::printf("reset mid-stream and reuse\n");
  const Source s = makeSource(BufferType::PCM_S16LE, 22050, 2, 22050);
  Stream st;
  if (!st.open(s, 0.3)) return;
  std::vector<float> out;
  for (int i = 0; i < 40; ++i) {
    uint64_t offset;
    while (popRequest(offset)) st.answer(offset, 3000);
    st.read(256, &out);
  }
  CHECK(!out.empty(), "the stream should have played something before the "
                      "reset");
  for (int pass = 0; pass < 2; ++pass) {
    clearRequests();
    {
      const PlayerErrors err = resetPullBufferStream(st.hash);
      CHECK(err == PlayerErrors::noError, "reset: %d", (int)err);
    }
    uint64_t offset = UINT64_MAX;
    CHECK(popRequest(offset) && offset == 0,
          "pass %d: after a reset the first request should be 0, got %llu",
          pass, (unsigned long long)offset);
    double start = -1, end = -1;
    getPullBufferTimeRange(st.hash, &start, &end);
    CHECK(start == 0.0 && end == 0.0,
          "pass %d: a reset stream should have an empty range, got [%g, %g]",
          pass, start, end);
    st.answer(0, 3000);
    // The voice keeps its own position, but the data plays from the start.
    out = playAll(st, {3000});
    const long d = firstDiff(out, s.expected);
    CHECK(d < 0, "pass %d: after the reset %zu of %zu samples, first "
                 "difference at %ld", pass, out.size(), s.expected.size(), d);
    CHECK(st.ended(), "pass %d: the voice should end", pass);
    // A delivery after the end is refused.
    const PlayerErrors late =
        addPullBufferDataStream(st.hash, s.bytes.data(), 16, 0);
    CHECK(late == PlayerErrors::streamEndedAlready,
          "pass %d: a delivery after the end should be refused, got %d", pass,
          (int)late);
  }
  st.close();
}

void testThreads() {
  std::printf("delivery, reset, reads and seeks on three threads\n");
  const Source s = makeSource(BufferType::PCM_F32LE, 22050, 2, 22050 * 2);
  Stream st;
  if (!st.open(s, 0.3)) return;
  std::atomic<bool> stop{false};
  std::thread feeder([&] {
    std::mt19937 rng(12345);
    while (!stop.load()) {
      uint64_t offset;
      if (!popRequest(offset)) {
        std::this_thread::yield();
        continue;
      }
      const size_t len = 1 + rng() % 9000;
      if (offset < s.bytes.size()) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(len, s.bytes.size() - offset));
        addPullBufferDataStream(st.hash, s.bytes.data() + offset,
                                static_cast<unsigned int>(n), offset);
      }
      std::this_thread::yield();
    }
  });
  std::thread resetter([&] {
    while (!stop.load()) {
      resetPullBufferStream(st.hash);
      double a, b;
      getPullBufferTimeRange(st.hash, &a, &b);
      std::this_thread::sleep_for(std::chrono::microseconds(700));
    }
  });
  std::mt19937 rng(777);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
  while (std::chrono::steady_clock::now() < deadline) {
    // A reset can land between a request and its answer, so an answer can
    // arrive for the stream before the reset: what plays here is not
    // checked, only that nothing crashes (run it with --asan).
    st.read(1 + rng() % 512, nullptr);
    if (rng() % 50 == 0) {
      // Frame-exact times only.
      st.seek((rng() % (2 * 22050)) / 22050.0);
    }
  }
  stop.store(true);
  feeder.join();
  resetter.join();
  st.close();

  // Then a clean stream must still be exact.
  Stream again;
  if (!again.open(s, 0.3)) return;
  const std::vector<float> clean = playAll(again, {9000});
  CHECK(firstDiff(clean, s.expected) < 0,
        "after the stress a new stream should play exactly (%zu of %zu)",
        clean.size(), s.expected.size());
  again.close();
}
} // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
  setAudioDeviceIdleTimeout(600000);
  prepareEngineInit(kEngineId);
  if (initEngine(-1, 44100, 2048, 2, 1, 0, 0) != PlayerErrors::noError) {
    std::printf("SKIPPED: the engine did not start on the null backend\n");
    return 0;
  }
  setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);

  testShortDeliveries();
  testEmptyDelivery();
  testTailMidFrame();
  testSeekPastEnd();
  testSeekFrameAlignment();
  testResetAndReuse();
  testThreads();

  dispose();
  std::printf("\n%d checks, %d failures, %d known bugs\n", gChecks, gFailures,
              gKnownBugs);
  return gFailures ? 1 : 0;
}

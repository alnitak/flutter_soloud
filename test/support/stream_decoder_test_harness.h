// Shared harness of the Opus, Vorbis and WAV stream decoder tests
// (test/common/{opus,vorbis,wav}_stream_decoder_test.cpp): CHECK and
// KNOWN_BUG macros, and drivers that feed a file to StreamDecoder (the way
// BufferStream does) or to a wrapper directly, in pieces of a given size.
#pragma once

#include "audiobuffer/stream_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sdh {

using Bytes = std::vector<unsigned char>;

inline int gFailures = 0;
inline int gChecks = 0;
inline int gKnownBugs = 0;
inline bool gStrict = false;

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    ++sdh::gChecks;                                                            \
    if (!(cond)) {                                                             \
      ++sdh::gFailures;                                                        \
      std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);           \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

// A check that fails because of a known bug in src/ (see where it is used).
// Reported, but fails the run only with --strict.
#define KNOWN_BUG(id, cond, ...)                                               \
  do {                                                                         \
    ++sdh::gChecks;                                                            \
    if (!(cond)) {                                                             \
      if (sdh::gStrict) ++sdh::gFailures; else ++sdh::gKnownBugs;              \
      std::printf("  %s %s [%s:%d] %s: ",                                      \
                  sdh::gStrict ? "FAIL" : "KNOWN BUG", id, __FILE__,           \
                  __LINE__, #cond);                                            \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

inline constexpr int kMaxDrainCalls = 2000;

struct Out {
  std::vector<float> pcm;
  int sampleRate = 0, channels = 0;
  DecoderError error = DecoderError::NoError;
  bool ended = false;
  DetectedType type = BUFFER_UNKNOWN;
  double duration = -1;
  std::vector<AudioMetadata> meta;
  /// (channels, samples) of every decode() call that returned samples.
  std::vector<std::pair<int, size_t>> calls;
  bool finite = true;
};

inline void collect(Out &o, std::pair<std::vector<float>, DecoderError> &r,
                    int sr, int ch) {
  if (r.second != DecoderError::NoError && o.error == DecoderError::NoError)
    o.error = r.second;
  if (!r.first.empty()) {
    o.sampleRate = sr;
    o.channels = ch;
    o.calls.push_back({ch, r.first.size()});
    for (float f : r.first)
      if (!std::isfinite(f)) o.finite = false;
  }
  o.pcm.insert(o.pcm.end(), r.first.begin(), r.first.end());
}

/// BufferStream model: bytes arrive in [chunk]-byte pieces and are appended
/// to a pending buffer; StreamDecoder::decode() consumes it once it has
/// detected the format. The first decode happens once more than 4 KB are
/// pending (or always, with [eager]), then whenever the decoder still has
/// data queued. [maxOut] caps the samples of each call (BufferStream caps the
/// end-of-data drain at 4096 * 4).
inline Out decodeStream(const Bytes &file, size_t chunk, int engineRate = 48000,
                        int engineChannels = 2, size_t maxOut = 0,
                        bool eager = false) {
  Out o;
  StreamDecoder sd;
  sd.setTotalAudioSizeBytes(file.size());
  Bytes pending;
  auto cb = [&o](const AudioMetadata &m) { o.meta.push_back(m); };
  auto run = [&](Bytes &in) {
    int sr = engineRate, ch = engineChannels;
    auto r = sd.decode(in, &sr, &ch, cb, maxOut);
    collect(o, r, sr, ch);
  };
  for (size_t off = 0; off < file.size() && o.error == DecoderError::NoError;
       off += chunk) {
    pending.insert(pending.end(), file.begin() + off,
                   file.begin() + std::min(file.size(), off + chunk));
    if (eager || pending.size() > 4096 || sd.hasPendingData()) run(pending);
  }
  sd.setDataEnded();
  if (o.error == DecoderError::NoError) {
    run(pending);
    for (int i = 0; i < kMaxDrainCalls && sd.hasPendingData(); ++i) {
      Bytes none;
      run(none);
    }
  }
  o.ended = !sd.hasPendingData();
  o.type = sd.getWrapperType();
  o.duration = sd.getDuration();
  return o;
}

/// A wrapper alone, fed [chunk]-byte pieces from the first byte; every call
/// capped at [maxOut] samples (0 = no cap).
template <typename W>
inline Out decodeWrapper(W &w, const Bytes &file, size_t chunk,
                         size_t maxOut = 0, int engineRate = 48000,
                         int engineChannels = 2) {
  Out o;
  w.setTrackChangeCallback([&o](const AudioMetadata &m) { o.meta.push_back(m); });
  w.initializeDecoder(engineRate, engineChannels);
  for (size_t off = 0; off < file.size(); off += chunk) {
    Bytes in(file.begin() + off,
             file.begin() + std::min(file.size(), off + chunk));
    int sr = 0, ch = 0;
    auto r = w.decode(in, &sr, &ch, maxOut);
    collect(o, r, sr, ch);
    // A bounded call may leave decoded data queued: keep reading it before
    // more input arrives, as BufferStream does.
    for (int i = 0; maxOut && i < kMaxDrainCalls && w.hasPendingData(); ++i) {
      Bytes none;
      auto r2 = w.decode(none, &sr, &ch, maxOut);
      if (r2.first.empty()) break;
      collect(o, r2, sr, ch);
    }
  }
  w.setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && w.hasPendingData(); ++i) {
    Bytes none;
    int sr = 0, ch = 0;
    auto r = w.decode(none, &sr, &ch, maxOut);
    collect(o, r, sr, ch);
  }
  o.ended = !w.hasPendingData();
  o.duration = w.getDuration();
  return o;
}

/// Index of the first sample where [a] and [b] differ by more than [eps]
/// within [from, to), or -1.
inline long firstDiff(const std::vector<float> &a, const std::vector<float> &b,
                      size_t from, size_t to, float eps = 0.0f) {
  if (to > a.size() || to > b.size()) return (long)std::min(a.size(), b.size());
  for (size_t i = from; i < to; ++i)
    if (std::fabs(a[i] - b[i]) > eps) return (long)i;
  return -1;
}

inline const char *metaComment(const std::vector<AudioMetadata> &meta,
                               size_t i, const std::string &key) {
  if (i >= meta.size()) return nullptr;
  auto it = meta[i].oggMetadata.comments.find(key);
  return it == meta[i].oggMetadata.comments.end() ? nullptr : it->second.c_str();
}

inline void parseArgs(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
}

inline int finish(const char *name) {
  std::printf("%s: %d checks, %d failures, %d known bugs\n", name, gChecks,
              gFailures, gKnownBugs);
  std::printf(gFailures ? "FAILED\n" : "PASSED\n");
  return gFailures ? 1 : 0;
}

} // namespace sdh

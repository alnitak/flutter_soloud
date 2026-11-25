// Robustness (fuzz-style) tests for every stream decoder in src/audiobuffer/:
// StreamDecoder's format detection and the MP3, WAV, Ogg Opus, Ogg Vorbis,
// Ogg FLAC, native FLAC, AAC and AC-3 wrappers it creates, plus each wrapper
// driven directly. Inputs are random bytes, random bytes behind each format's
// magic, and real files (example/assets/audio plus FLAC and Ogg FLAC encoded
// in memory) that are truncated at random points and/or corrupted with random
// byte changes, delivered in random chunk sizes the way BufferStream delivers
// them (first decode once > 4 KB are pending, then whenever the decoder has
// data queued; the end-of-data drain is capped at 4096 * 4 samples a call).
//
// Each input runs in a child process with a time limit, so a crash, an abort
// (ASan/UBSan report) or a hang inside a decode() call is reported as a
// failure of that input with the seed that reproduces it. Every result must
// also be clean: a DecoderError enum value, a channel count and sample rate in
// range whenever samples come out, a sample count that is a multiple of the
// channel count, and finite samples.
//
// A drain that never ends (hasPendingData() still true after 200 decode()
// calls once the data has ended) is a failure. For the platform AAC / AC-3
// decoders it is counted and reported per detected format as KNOWN BUG and
// fails the run only with --strict (none is known at the moment; the old
// corrupted-AC-3 case on Apple, --seed 777 --only 300069, now drains).
// (Regressions for S1 and S5: FLAC/Ogg FLAC left ABORTED by the decode loop,
// and MP3/WAV wrappers fed data they can never initialize from, used to
// never drain.)
//
// Deterministic: the seed is fixed (override with --seed N), and
// --only N runs input N alone in-process (for a debugger). --iterations N
// sets the number of inputs per group (default 40).
//
// Needs no audio device. Uses example/assets/audio (path given by the
// script); prints SKIPPED without it. Needs the Xiph libraries. Meant to be
// run with --asan as well.
//
//   ./test/common/stream_decoders_fuzz_test.sh [--asan] [--strict] [--seed N]

#include "audiobuffer/flac_stream_decoder.h"
#include "audiobuffer/mp3_stream_decoder.h"
#include "audiobuffer/ogg_flac_stream_decoder.h"
#include "audiobuffer/opus_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"
#include "audiobuffer/vorbis_stream_decoder.h"
#include "audiobuffer/wav_stream_decoder.h"
#include "../support/line_buffered_stdout.h"

#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using Bytes = std::vector<unsigned char>;

static constexpr int kMaxDrainCalls = 200;
static constexpr size_t kDrainCap = 4096 * 4;
static constexpr unsigned kChildSeconds = 60;

// ------------------------------------------------------------- fixtures

static bool readFile(const std::string &path, Bytes &out, size_t limit) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>());
  if (out.size() > limit) out.resize(limit);
  return !out.empty();
}

static FLAC__StreamEncoderWriteStatus collect(const FLAC__StreamEncoder *,
                                              const FLAC__byte bytes[],
                                              size_t length, unsigned,
                                              unsigned, void *opaque) {
  auto &out = *static_cast<Bytes *>(opaque);
  out.insert(out.end(), bytes, bytes + length);
  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static Bytes encodeFlac(bool ogg) {
  Bytes out;
  const unsigned frames = 22050;
  std::vector<FLAC__int32> pcm(frames * 2);
  for (unsigned i = 0; i < frames; ++i) {
    pcm[2 * i] = (FLAC__int32)std::lround(9000 * std::sin(i * 0.0627));
    pcm[2 * i + 1] = (FLAC__int32)((i * 2654435761u) >> 20) - 2048;
  }
  FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
  if (!enc) return out;
  FLAC__stream_encoder_set_channels(enc, 2);
  FLAC__stream_encoder_set_bits_per_sample(enc, 16);
  FLAC__stream_encoder_set_sample_rate(enc, 44100);
  FLAC__stream_encoder_set_total_samples_estimate(enc, frames);
  FLAC__StreamEncoderInitStatus st =
      ogg ? FLAC__stream_encoder_init_ogg_stream(enc, nullptr, collect, nullptr,
                                                 nullptr, nullptr, &out)
          : FLAC__stream_encoder_init_stream(enc, collect, nullptr, nullptr,
                                             nullptr, &out);
  bool ok = st == FLAC__STREAM_ENCODER_INIT_STATUS_OK &&
            FLAC__stream_encoder_process_interleaved(enc, pcm.data(), frames);
  if (ok) ok = FLAC__stream_encoder_finish(enc);
  FLAC__stream_encoder_delete(enc);
  if (!ok) out.clear();
  return out;
}

struct Seed {
  std::string name;
  Bytes data;
};

static std::vector<Seed> gSeeds;

// ------------------------------------------------------------- inputs

enum Group {
  kRandom,     // pure random bytes
  kMagic,      // a format magic followed by random bytes
  kTruncated,  // a real file cut at a random point
  kCorrupted,  // a real file with random byte changes, maybe truncated
  kWrappers,   // each wrapper directly on random/corrupted data
  kGroupCount
};
static const char *kGroupNames[kGroupCount] = {"random", "magic", "truncated",
                                               "corrupted", "wrappers"};

struct Input {
  Bytes data;
  std::string what;
  std::vector<size_t> chunks; // delivery sizes, cycled
  int wrapper = -1;           // kWrappers: which wrapper
};

static const char *kMagics[][2] = {
    {"OggS", "ogg"},          {"fLaC", "flac"},   {"ID3\x04", "id3"},
    {"RIFF", "riff"},         {"\xff\xfb\x90\x64", "mp3 sync"},
    {"\xff\xf1\x50\x80", "adts"}, {"\x0b\x77", "ac3"},
    {"\0\0\0\x20" "ftypM4A ", "m4a"}};

static Input makeInput(Group g, uint32_t seed) {
  std::mt19937 rng(seed);
  auto rnd = [&](size_t n) { return n ? size_t(rng() % n) : 0; };
  Input in;
  switch (g) {
  case kRandom: {
    in.data.resize(rnd(20000));
    for (auto &b : in.data) b = (unsigned char)rng();
    in.what = "random " + std::to_string(in.data.size()) + " bytes";
    break;
  }
  case kMagic: {
    const size_t m = rnd(sizeof(kMagics) / sizeof(kMagics[0]));
    const char *magic = kMagics[m][0];
    size_t ml = m == 7 ? 12 : std::strlen(magic);
    if (m == 6) ml = 2;
    in.data.assign(magic, magic + ml);
    if (m == 3) { // RIFF size + WAVE fmt, the rest random
      const char tail[] = "\x24\x80\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0"
                          "\x44\xac\0\0\x10\xb1\x02\0\x04\0\x10\0data";
      in.data.insert(in.data.end(), tail, tail + sizeof(tail) - 1);
    }
    const size_t n = rnd(20000);
    for (size_t i = 0; i < n; ++i) in.data.push_back((unsigned char)rng());
    in.what = std::string("magic ") + kMagics[m][1] + " + " +
              std::to_string(n) + " random bytes";
    break;
  }
  case kTruncated:
  case kCorrupted:
  case kWrappers: {
    const Seed &s = gSeeds[rnd(gSeeds.size())];
    in.data = s.data;
    in.what = s.name;
    if (g != kTruncated) {
      const size_t edits = 1 + rnd(40);
      const size_t from = rnd(3) == 0 ? 0 : std::min<size_t>(64, in.data.size());
      for (size_t i = 0; i < edits && in.data.size() > from; ++i) {
        const size_t p = from + rnd(in.data.size() - from);
        switch (rnd(3)) {
        case 0: in.data[p] = (unsigned char)rng(); break;
        case 1: in.data[p] ^= (unsigned char)(1u << rnd(8)); break;
        default: in.data[p] = rnd(2) ? 0xff : 0x00; break;
        }
      }
      in.what += " +" + std::to_string(edits) + " edits";
    }
    if (g == kTruncated || rnd(2)) {
      const size_t cut = rnd(in.data.size() + 1);
      in.data.resize(cut);
      in.what += " cut at " + std::to_string(cut);
    }
    if (g == kWrappers) in.wrapper = int(rnd(6));
    break;
  }
  default:
    break;
  }
  // Delivery: mostly network-sized pieces, sometimes tiny ones.
  const int mode = int(rnd(4));
  for (int i = 0; i < 8; ++i)
    in.chunks.push_back(mode == 0 ? 1 + rnd(64)
                                  : mode == 1 ? 1 + rnd(1500)
                                              : 512 + rnd(16384));
  return in;
}

// ------------------------------------------------------------- running

struct Verdict {
  bool clean = true;
  std::string problem;
  bool drainEnded = true;
  int type = BUFFER_UNKNOWN;
  size_t samples = 0;
};

static void checkOutput(const std::vector<float> &pcm, int sr, int ch,
                        DecoderError err, Verdict &v) {
  if ((int)err < (int)DecoderError::NoError ||
      (int)err > (int)DecoderError::CodecError) {
    v.clean = false;
    v.problem = "DecoderError out of range";
  }
  if (pcm.empty()) return;
  v.samples += pcm.size();
  if (ch < 1 || ch > 255 || sr < 1 || sr > 768000) {
    v.clean = false;
    v.problem = "samples with format " + std::to_string(sr) + " Hz " +
                std::to_string(ch) + " ch";
    return;
  }
  if (pcm.size() % size_t(ch) != 0) {
    v.clean = false;
    v.problem = std::to_string(pcm.size()) + " samples for " +
                std::to_string(ch) + " channels";
  }
  for (float f : pcm)
    if (!std::isfinite(f)) {
      v.clean = false;
      v.problem = "non-finite sample";
      break;
    }
}

static Verdict runStream(const Input &in) {
  Verdict v;
  StreamDecoder sd;
  sd.setTotalAudioSizeBytes(in.data.size());
  Bytes pending;
  bool failed = false;
  auto cb = [](const AudioMetadata &m) {
    // Touch the strings the way the Dart bridge copies them.
    volatile size_t n = m.mp3Metadata.title.size() + m.oggMetadata.vendor.size();
    for (auto &kv : m.oggMetadata.comments) n += kv.first.size() + kv.second.size();
    (void)n;
  };
  auto run = [&](Bytes &buf, size_t cap) {
    int sr = 48000, ch = 2;
    auto r = sd.decode(buf, &sr, &ch, cb, cap);
    checkOutput(r.first, sr, ch, r.second, v);
    if (r.second != DecoderError::NoError) failed = true; // BufferStream stops
  };
  size_t off = 0, k = 0;
  while (off < in.data.size() && !failed) {
    const size_t n = std::min(in.chunks[k++ % in.chunks.size()],
                              in.data.size() - off);
    pending.insert(pending.end(), in.data.begin() + off,
                   in.data.begin() + off + n);
    off += n;
    if (pending.size() > 4096 || sd.hasPendingData()) run(pending, 0);
  }
  sd.setDataEnded();
  if (!failed) {
    run(pending, kDrainCap);
    for (int i = 0; i < kMaxDrainCalls && !failed && sd.hasPendingData(); ++i) {
      Bytes none;
      run(none, kDrainCap);
    }
    v.drainEnded = failed || !sd.hasPendingData();
  }
  v.type = sd.getWrapperType();
  // Seek queries on whatever state the decoder is in must not misbehave.
  (void)sd.canSeekToTime(1.0);
  (void)sd.timeToByteOffset(1.0);
  (void)sd.getDuration();
  return v;
}

static const char *kWrapperNames[6] = {"MP3", "WAV", "Opus", "Vorbis", "FLAC",
                                       "OggFLAC"};

static Verdict runWrapper(const Input &in) {
  Verdict v;
  std::unique_ptr<IDecoderWrapper> w;
  switch (in.wrapper) {
  case 0: w.reset(new MP3DecoderWrapper()); w->detectedType = BUFFER_MP3_STREAM; break;
  case 1: w.reset(new WavDecoderWrapper()); w->detectedType = BUFFER_WAV; break;
  case 2: w.reset(new OpusDecoderWrapper()); w->detectedType = BUFFER_OGG_OPUS; break;
  case 3: w.reset(new VorbisDecoderWrapper()); w->detectedType = BUFFER_OGG_VORBIS; break;
  case 4: w.reset(new FlacDecoderWrapper()); w->detectedType = BUFFER_FLAC; break;
  default: w.reset(new OggFlacDecoderWrapper()); w->detectedType = BUFFER_OGG_FLAC; break;
  }
  v.type = w->detectedType;
  if (!w->initializeDecoder(48000, 2)) return v;
  bool failed = false;
  auto run = [&](Bytes &buf, size_t cap) {
    int sr = 48000, ch = 2;
    auto r = w->decode(buf, &sr, &ch, cap);
    checkOutput(r.first, sr, ch, r.second, v);
    if (r.second != DecoderError::NoError) failed = true;
  };
  size_t off = 0, k = 0;
  while (off < in.data.size() && !failed) {
    // Any delivery size, also 1-3 bytes first (regression for S2).
    const size_t n = std::min(in.chunks[k++ % in.chunks.size()],
                              in.data.size() - off);
    Bytes buf(in.data.begin() + off, in.data.begin() + off + n);
    off += n;
    run(buf, 0);
  }
  w->setDataEnded();
  for (int i = 0; i < kMaxDrainCalls && !failed && w->hasPendingData(); ++i) {
    Bytes none;
    run(none, kDrainCap);
  }
  v.drainEnded = failed || !w->hasPendingData();
  (void)w->canSeekToTime(0.5);
  (void)w->timeToByteOffset(0.5);
  (void)w->getDuration();
  return v;
}

static Verdict runInput(const Input &in) {
  return in.wrapper >= 0 ? runWrapper(in) : runStream(in);
}

#ifdef _WIN32
// No fork() on Windows: run the input in this process. A crash or a hang
// stops the whole run instead of being reported per input.
static bool runInChild(const Input &in, Verdict &v, std::string &) {
  v = runInput(in);
  return true;
}
#else
// Child protocol: one line "OK <drainEnded> <type> <samples>" or
// "BAD <problem>" on a pipe.
static bool runInChild(const Input &in, Verdict &v, std::string &crash) {
  int fds[2];
  if (pipe(fds) != 0) return false;
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    alarm(kChildSeconds);
    Verdict r = runInput(in);
    char line[512];
    if (r.clean)
      std::snprintf(line, sizeof line, "OK %d %d %zu\n", r.drainEnded ? 1 : 0,
                    r.type, r.samples);
    else
      std::snprintf(line, sizeof line, "BAD %s\n", r.problem.c_str());
    ssize_t ignored = write(fds[1], line, std::strlen(line));
    (void)ignored;
    _exit(0);
  }
  close(fds[1]);
  std::string out;
  char buf[256];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, size_t(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFSIGNALED(status)) {
    crash = WTERMSIG(status) == SIGALRM
                ? "hang (no result within " + std::to_string(kChildSeconds) + " s)"
                : std::string("signal ") + std::to_string(WTERMSIG(status));
    return false;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || out.empty()) {
    crash = "exit status " + std::to_string(WEXITSTATUS(status)) +
            " (sanitizer report above?)";
    return false;
  }
  if (out.compare(0, 3, "BAD") == 0) {
    v.clean = false;
    v.problem = out.substr(4, out.size() - 5);
    return true;
  }
  int ended = 1, type = 0;
  size_t samples = 0;
  std::sscanf(out.c_str(), "OK %d %d %zu", &ended, &type, &samples);
  v.drainEnded = ended != 0;
  v.type = type;
  v.samples = samples;
  return true;
}
#endif

static const char *typeName(int t) {
  static const char *names[] = {"unknown", "no-data", "ogg-opus", "ogg-vorbis",
                                "ogg-flac", "flac", "mp3-id3", "mp3",
                                "wav", "m4a", "aac", "ac3", "eac3"};
  return t >= 0 && t <= 12 ? names[t] : "?";
}

int main(int argc, char **argv) {
  lineBufferedStdout();
  uint32_t baseSeed = 20261119;
  int iterations = 40;
  long only = -1;
  bool strict = false;
  std::string dir;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--strict")) strict = true;
    else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) baseSeed = std::strtoul(argv[++i], nullptr, 10);
    else if (!std::strcmp(argv[i], "--iterations") && i + 1 < argc) iterations = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) only = std::atol(argv[++i]);
    else dir = argv[i];
  }
  if (dir.empty()) {
    std::printf("usage: %s <assets dir> [--strict] [--seed N] [--iterations N] [--only N]\n", argv[0]);
    return 2;
  }

  // Real files, limited to a prefix: every case truncates or corrupts them.
  const char *assets[] = {"sample-MP3.mp3", "explosion.mp3",
                          "IveSeenThings.mp3", "test_mpeg2_24khz_mono.mp3",
                          "sample-OPUS.opus", "sample-vorbis.ogg",
                          "sample-WAV.wav", "sample-FLAC.flac",
                          "sample-AAC.aac", "sample-AC3.ac3"};
  for (const char *a : assets) {
    Seed s{a, {}};
    if (!readFile(dir + "/" + a, s.data, 96 * 1024)) {
      std::printf("SKIPPED: %s/%s not found\n", dir.c_str(), a);
      return 0;
    }
    gSeeds.push_back(std::move(s));
  }
  Bytes flac = encodeFlac(false), oggFlac = encodeFlac(true);
  if (!flac.empty()) gSeeds.push_back({"generated.flac", flac});
  if (!oggFlac.empty()) gSeeds.push_back({"generated.oga (Ogg FLAC)", oggFlac});

  if (only >= 0) iterations = std::max(iterations, int(only % 100000) + 1);
  int failures = 0, cases = 0;
  std::map<std::string, int> knownDrain;
  for (int g = 0; g < kGroupCount; ++g) {
    if (only < 0) std::printf("group: %s (%d inputs)\n", kGroupNames[g], iterations);
    for (int i = 0; i < iterations; ++i) {
      const long id = long(g) * 100000 + i;
      if (only >= 0 && id != only) continue;
      const uint32_t seed = baseSeed + uint32_t(id) * 7919u;
      const Input in = makeInput(Group(g), seed);
      std::string label = in.what;
      if (in.wrapper >= 0) label = std::string(kWrapperNames[in.wrapper]) + " wrapper: " + label;
      ++cases;
      Verdict v;
      if (only >= 0) {
        std::printf("input %ld: %s\n", id, label.c_str());
        v = runInput(in);
      } else {
        std::string crash;
        if (!runInChild(in, v, crash)) {
          ++failures;
          std::printf("  FAIL input %ld (%s): %s\n    reproduce: --only %ld --seed %u\n",
                      id, label.c_str(), crash.c_str(), id, baseSeed);
          continue;
        }
      }
      if (!v.clean) {
        ++failures;
        std::printf("  FAIL input %ld (%s): %s\n    reproduce: --only %ld --seed %u\n",
                    id, label.c_str(), v.problem.c_str(), id, baseSeed);
      } else if (!v.drainEnded) {
        const std::string key = in.wrapper >= 0
                                    ? std::string(kWrapperNames[in.wrapper]) + " wrapper"
                                    : typeName(v.type);
        // Only the platform AAC / AC-3 decoders still have a known drain bug.
        const bool known = in.wrapper < 0 &&
                           (v.type == BUFFER_AAC || v.type == BUFFER_AC3 ||
                            v.type == BUFFER_EAC3);
        if (known) ++knownDrain[key];
        std::printf("  %s input %ld (%s, %s): hasPendingData() still true after %d drain calls\n",
                    (strict || !known) ? "FAIL" : "KNOWN BUG", id, label.c_str(),
                    key.c_str(), kMaxDrainCalls);
        if (strict || !known) ++failures;
      } else if (only >= 0) {
        std::printf("  ok: type %s, %zu samples\n", typeName(v.type), v.samples);
      }
    }
  }

  int known = 0;
  for (auto &kv : knownDrain) {
    std::printf("known bug, drain never ends: %s x%d\n", kv.first.c_str(), kv.second);
    known += kv.second;
  }
  std::printf("%d inputs, %d failures, %d known bugs (seed %u)\n", cases, failures,
              known, baseSeed);
  if (failures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}

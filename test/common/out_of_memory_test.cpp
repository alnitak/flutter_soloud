// Out-of-memory tests: one allocation fails (test/support/
// out_of_memory_allocator.h replaces operator new) while a sound is loaded,
// a voice is created or a stream decoder is fed. For N = 0, 1, 2, ... the
// N-th allocation of the operation fails, until the operation completes
// without reaching it. At every N:
//
//   - no crash and no deadlock: every scenario runs in its own child
//     process, and the engine is probed from another thread with a timeout
//     after each failure;
//   - the result is an error or a complete success, never a half-valid
//     state (a sound shorter than it is, a voice counted although play()
//     failed);
//   - the object is usable again afterwards (reload, replay);
//   - nothing allocated during the failed operation is left behind once the
//     object is released (allocations are tracked through operator new; LSan
//     is not available on macOS arm64);
//   - for the FFI exports of bindings.cpp: no C++ exception escapes. Dart
//     calls them through FFI, where an exception aborts the process, so an
//     escaping std::bad_alloc is a crash in the app.
//
// The SoLoud C++ classes (Wav, WavStream, Soloud::play) may throw
// std::bad_alloc, which is how C++ reports it, as long as no state is lost.
//
// Only operator new fails: malloc inside dr_libs / miniaudio / libc is not
// intercepted (a macOS binary cannot interpose malloc on itself).
//
//   ./test/common/out_of_memory_test.sh [--asan] [--strict] [scenario]

#include "../support/out_of_memory_allocator.h"
#include "../support/robustness_checks.h"

#include "bindings.h"
#include "enums.h"

#include "soloud.h"
#include "soloud_bus.h"
#include "soloud_echofilter.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"

#include "audiobuffer/stream_decoder.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;
std::string gAssets = "example/assets/audio";

// The child exits with this when a scenario hit a known bug that ends it.
constexpr int kChildKnownBugExit = 3;

Bytes readFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return Bytes(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
}

// A 16-bit stereo 44.1 kHz WAV of [frames] frames of a sine.
Bytes makeWav(unsigned frames) {
  const unsigned ch = 2, rate = 44100, bytes = frames * ch * 2;
  Bytes b;
  auto u32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff);
  };
  auto u16 = [&](uint16_t v) {
    b.push_back(v & 0xff);
    b.push_back(v >> 8);
  };
  b.insert(b.end(), {'R', 'I', 'F', 'F'});
  u32(36 + bytes);
  b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  u32(16);
  u16(1);
  u16(ch);
  u32(rate);
  u32(rate * ch * 2);
  u16(ch * 2);
  u16(16);
  b.insert(b.end(), {'d', 'a', 't', 'a'});
  u32(bytes);
  for (unsigned i = 0; i < frames; ++i) {
    const int16_t s = static_cast<int16_t>(
        12000 * std::sin(2.0 * 3.14159265 * 440.0 * i / rate));
    for (unsigned c = 0; c < ch; ++c) u16(static_cast<uint16_t>(s));
  }
  return b;
}

// Run [fn] on another thread. False when it did not finish in [ms]: the
// engine is wedged (the thread is abandoned; the child exits soon after).
bool finishesWithin(std::function<void()> fn, int ms) {
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread([fn, done] {
    fn();
    done->store(true);
  }).detach();
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!done->load()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

[[noreturn]] void wedged(const char *what, long n) {
  CHECK(false, "%s: engine deadlocked after allocation #%ld failed", what, n);
  std::fflush(stdout);
  _exit(1);
}

// Next countdown value: every N up to 400, then sparser.
long nextN(long n) { return n < 400 ? n + 1 : n + n / 40; }
constexpr long kMaxN = 200000;

// ---------------------------------------------------------------------------
// SoLoud::Wav::loadMem for every container the in-memory loader handles.
void scenarioWavLoad(const char *label, const Bytes &data) {
  if (data.empty()) {
    std::printf("  %s: asset missing, skipped\n", label);
    return;
  }
  double refLength = 0;
  { // warm-up, and the length a complete load gives
    SoLoud::Wav w;
    if (w.loadMem(data.data(), static_cast<unsigned>(data.size()), true,
                  false) != SoLoud::SO_NO_ERROR) {
      std::printf("  %s: cannot be loaded here, skipped\n", label);
      return;
    }
    refLength = w.getLength();
  }
  int threw = 0, errors = 0, partial = 0, unusable = 0, leaks = 0;
  int cLeaks = 0;
  long long cLeakBytes = 0;
  long n = 0;
  for (; n < kMaxN; n = nextN(n)) {
    oom::clearTracked();
    bool injected = false;
    const long long heapBefore = oom::mallocInUse();
    {
      SoLoud::Wav w;
      SoLoud::result r = SoLoud::UNKNOWN_ERROR;
      bool t = false;
      oom::arm(n);
      try {
        r = w.loadMem(data.data(), static_cast<unsigned>(data.size()), true,
                      false);
      } catch (const std::bad_alloc &) {
        t = true;
      }
      injected = oom::disarm();
      if (t) ++threw;
      else if (r != SoLoud::SO_NO_ERROR) ++errors;
      else if (std::fabs(w.getLength() - refLength) > 1e-9) ++partial;
      if (t || r != SoLoud::SO_NO_ERROR) {
        // The C heap (dr_libs decoders are malloc'd) is back where it was
        // once the failed load returned: measured before the reload below.
        const long long heapAfter = oom::mallocInUse();
        if (heapBefore >= 0 && heapAfter > heapBefore + 64) {
          ++cLeaks;
          cLeakBytes = heapAfter - heapBefore;
        }
        // The same object must load again.
        if (w.loadMem(data.data(), static_cast<unsigned>(data.size()), true,
                      false) != SoLoud::SO_NO_ERROR ||
            std::fabs(w.getLength() - refLength) > 1e-9)
          ++unusable;
      }
    }
    if (oom::liveTracked() != 0) {
      ++leaks;
      if (leaks <= 3)
        std::printf("    %s: N=%ld left %ld allocations behind\n", label, n,
                    oom::liveTracked());
    }
    if (!injected) break;
  }
  std::printf("  %s: %ld allocation points, %d threw, %d errors\n", label, n,
              threw, errors);
  CHECK(n < kMaxN, "%s: never completed", label);
  CHECK(partial == 0, "%s: %d loads reported success with a wrong length",
        label, partial);
  CHECK(unusable == 0, "%s: %d failed loads left the Wav unusable", label,
        unusable);
  CHECK(leaks == 0, "%s: %d failed loads leaked", label, leaks);
  CHECK(cLeaks == 0, "%s: %d failed loads leaked %lld malloc'd bytes", label,
        cLeaks, cLeakBytes);
}

void scenarioWav() {
  std::printf("Wav::loadMem\n");
  scenarioWavLoad("wav (generated)", makeWav(3000));
  scenarioWavLoad("wav (tic-1.wav)", readFile(gAssets + "/tic-1.wav"));
  scenarioWavLoad("mp3 (explosion.mp3)", readFile(gAssets + "/explosion.mp3"));
  scenarioWavLoad("ogg (sample-vorbis.ogg)",
                  readFile(gAssets + "/sample-vorbis.ogg"));
  scenarioWavLoad("flac (sample-FLAC.flac)",
                  readFile(gAssets + "/sample-FLAC.flac"));
}

// ---------------------------------------------------------------------------
// SoLoud::WavStream: load and voice creation, mixed on the null driver.
void scenarioWavStream() {
  std::printf("WavStream::loadMem + play\n");
  SoLoud::Soloud engine;
  CHECK(engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    44100, 512, 2) == SoLoud::SO_NO_ERROR,
        "engine init");
  std::vector<float> out(512 * 2);
  for (const char *file : {"", "explosion.mp3"}) {
    const Bytes data = file[0] ? readFile(gAssets + "/" + file) : makeWav(8000);
    if (data.empty()) continue;
    const char *label = file[0] ? file : "generated wav";
    { // warm-up
      SoLoud::WavStream s;
      s.loadMem(data.data(), static_cast<unsigned>(data.size()), true, false);
      engine.play(s);
      engine.mix(out.data(), 512);
      engine.stopAll();
      engine.mix(out.data(), 512);
    }
    int threw = 0, bad = 0, leaks = 0;
    long n = 0;
    for (; n < kMaxN; n = nextN(n)) {
      oom::clearTracked();
      bool injected;
      {
        SoLoud::WavStream s;
        SoLoud::result r = SoLoud::UNKNOWN_ERROR;
        SoLoud::handle h = 0;
        bool t = false;
        oom::arm(n);
        try {
          r = s.loadMem(data.data(), static_cast<unsigned>(data.size()), true,
                        false);
          if (r == SoLoud::SO_NO_ERROR) h = engine.play(s);
        } catch (const std::bad_alloc &) {
          t = true;
        }
        injected = oom::disarm();
        if (t) ++threw;
        if (!finishesWithin([&] { engine.mix(out.data(), 512); }, 5000))
          wedged(label, n);
        if (!t && r == SoLoud::SO_NO_ERROR && h != 0 &&
            !engine.isValidVoiceHandle(h))
          ++bad;
        engine.stopAll();
        engine.mix(out.data(), 512);
      }
      if (oom::liveTracked() != 0) {
        ++leaks;
        if (leaks <= 3)
          std::printf("    %s: N=%ld left %ld allocations behind\n", label, n,
                      oom::liveTracked());
      }
      if (!injected) break;
    }
    std::printf("  %s: %ld allocation points, %d threw\n", label, n, threw);
    CHECK(n < kMaxN, "%s: never completed", label);
    CHECK(bad == 0, "%s: %d plays returned a dead handle", label, bad);
    CHECK(leaks == 0, "%s: %d failures leaked", label, leaks);
  }
  engine.deinit();
}

// ---------------------------------------------------------------------------
// Soloud::play / play3d / Bus voice creation.
void scenarioPlay(const char *label, bool withFilter, int mode) {
  SoLoud::Soloud engine;
  CHECK(engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    44100, 512, 2) == SoLoud::SO_NO_ERROR,
        "engine init");
  std::vector<float> out(512 * 2);
  const Bytes data = makeWav(20000);
  SoLoud::Wav wav;
  wav.loadMem(data.data(), static_cast<unsigned>(data.size()), true, false);
  SoLoud::EchoFilter echo;
  echo.setParams(0.05f, 0.5f);
  if (withFilter) wav.setFilter(0, &echo);
  SoLoud::Bus bus;
  SoLoud::handle busHandle = 0;
  if (mode == 2) busHandle = engine.play(bus);

  auto playOnce = [&]() -> SoLoud::handle {
    switch (mode) {
    case 1: return engine.play3d(wav, 1.0f, 0.0f, 0.0f);
    case 2: return bus.play(wav);
    case 3: return engine.play(bus);
    default: return engine.play(wav);
    }
  };
  { // warm-up
    playOnce();
    engine.mix(out.data(), 512);
    if (mode == 3) {
      engine.stop(engine.play(bus));
    }
    engine.stopAudioSource(wav);
    engine.mix(out.data(), 512);
  }

  int threw = 0, ghost = 0, leaks = 0, dead = 0;
  long n = 0;
  for (; n < kMaxN; n = nextN(n)) {
    oom::clearTracked();
    unsigned before = 0;
    if (!finishesWithin([&] { before = engine.getActiveVoiceCount(); }, 5000))
      wedged(label, n);
    SoLoud::handle h = 0;
    bool t = false;
    oom::arm(n);
    try {
      h = playOnce();
    } catch (const std::bad_alloc &) {
      t = true;
    }
    const bool injected = oom::disarm();
    if (t) ++threw;
    unsigned after = 0;
    if (!finishesWithin(
            [&] {
              engine.mix(out.data(), 512);
              after = engine.getActiveVoiceCount();
            },
            5000))
      wedged(label, n);
    if (t || h == 0) {
      // Nothing was created, so nothing new plays.
      if (after > before) ++ghost;
    } else if (!engine.isValidVoiceHandle(h)) {
      ++dead;
    }
    if (mode == 3)
      engine.stop(h);
    else
      engine.stopAudioSource(wav);
    engine.mix(out.data(), 512);
    if (oom::liveTracked() != 0) {
      ++leaks;
      if (leaks <= 3)
        std::printf("    %s: N=%ld left %ld allocations behind\n", label, n,
                    oom::liveTracked());
    }
    if (!injected) break;
  }
  std::printf("  %s: %ld allocation points, %d threw\n", label, n, threw);
  CHECK(n < kMaxN, "%s: never completed", label);
  CHECK(ghost == 0, "%s: %d failed plays left a voice behind (counted, playing)",
        label, ghost);
  CHECK(dead == 0, "%s: %d plays returned a dead handle", label, dead);
  CHECK(leaks == 0, "%s: %d failed plays leaked", label, leaks);
  wav.setFilter(0, nullptr);
  (void)busHandle;
  engine.deinit();
}

void scenarioVoices() {
  std::printf("Soloud voice creation\n");
  scenarioPlay("play", false, 0);
  scenarioPlay("play3d", false, 1);
  scenarioPlay("Bus::play", false, 2);
  scenarioPlay("play(bus)", false, 3);
}

void scenarioVoicesWithFilter() {
  std::printf("Soloud voice creation, sound with a filter\n");
  scenarioPlay("play with filter", true, 0);
}

// ---------------------------------------------------------------------------
// Stream decoders fed directly.
void scenarioStreamDecoders() {
  std::printf("stream decoders (StreamDecoder::decode)\n");
  struct Input {
    const char *label;
    Bytes data;
  } inputs[] = {{"wav", makeWav(6000)},
                {"mp3", readFile(gAssets + "/explosion.mp3")}};
  for (auto &in : inputs) {
    if (in.data.empty()) continue;
    auto decodeAll = [&](size_t &frames) {
      StreamDecoder sd;
      int sr = 0, ch = 0;
      std::vector<unsigned char> pending;
      frames = 0;
      const size_t chunk = 8192;
      for (size_t off = 0; off < in.data.size(); off += chunk) {
        const size_t len = std::min(chunk, in.data.size() - off);
        pending.insert(pending.end(), in.data.begin() + off,
                       in.data.begin() + off + len);
        if (off + len >= in.data.size()) sd.setDataEnded();
        auto r = sd.decode(pending, &sr, &ch, nullptr);
        frames += r.first.size();
      }
      for (int i = 0; i < 64 && sd.hasPendingData(); ++i) {
        auto r = sd.decode(pending, &sr, &ch, nullptr);
        frames += r.first.size();
      }
    };
    size_t refFrames = 0;
    decodeAll(refFrames);
    int threw = 0, leaks = 0;
    long n = 0;
    for (; n < kMaxN; n = nextN(n)) {
      oom::clearTracked();
      size_t frames = 0;
      bool t = false;
      oom::arm(n);
      try {
        decodeAll(frames);
      } catch (const std::bad_alloc &) {
        t = true;
      }
      const bool injected = oom::disarm();
      if (t) ++threw;
      if (oom::liveTracked() != 0) {
        ++leaks;
        if (leaks <= 3)
          std::printf("    %s: N=%ld left %ld allocations behind\n", in.label,
                      n, oom::liveTracked());
      }
      if (!injected) break;
    }
    std::printf("  %s: %ld allocation points, %d threw (%zu samples)\n",
                in.label, n, threw, refFrames);
    CHECK(n < kMaxN, "%s: never completed", in.label);
    CHECK(leaks == 0, "%s: %d failures leaked", in.label, leaks);
  }
}

// ---------------------------------------------------------------------------
// The FFI exports, on miniaudio's null backend.
struct ExportResult {
  bool threw = false;
  bool injected = false;
  PlayerErrors error = noError;
};

template <typename F> ExportResult callExport(long n, F f) {
  ExportResult r;
  oom::arm(n);
  try {
    r.error = f();
  } catch (const std::bad_alloc &) {
    r.threw = true;
  }
  r.injected = oom::disarm();
  return r;
}

// Run [op] for N = 0, 1, ... [cleanup] releases what a success made.
// Counts exceptions that escaped the export.
void exportCountdown(const char *label, std::function<PlayerErrors()> op,
                     std::function<void(PlayerErrors)> cleanup,
                     std::function<bool()> engineAlive,
                     const char *leakBugId = nullptr) {
  // Warm-up.
  cleanup(op());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  int escaped = 0, errors = 0, leaks = 0, broken = 0;
  long firstEscape = -1;
  long n = 0;
  for (; n < kMaxN; n = nextN(n)) {
    oom::clearTracked();
    ExportResult r = callExport(n, op);
    if (r.threw) {
      ++escaped;
      if (firstEscape < 0) firstEscape = n;
    } else if (r.error != noError) {
      ++errors;
    }
    bool alive = false;
    if (!finishesWithin([&] { alive = engineAlive(); }, 5000))
      wedged(label, n);
    if (!alive) ++broken;
    cleanup(r.threw ? unknownError : r.error);
    // Voices are deleted by the audio thread.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (oom::liveTracked() != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      if (oom::liveTracked() != 0) {
        ++leaks;
        if (leaks <= 3)
          std::printf("    %s: N=%ld left %ld allocations behind\n", label, n,
                      oom::liveTracked());
      }
    }
    if (!r.injected) break;
  }
  std::printf("  %s: %ld allocation points, %d escaped, %d errors\n", label, n,
              escaped, errors);
  CHECK(n < kMaxN, "%s: never completed", label);
  CHECK(escaped == 0,
        "%s: std::bad_alloc escaped the FFI export %d times (first at "
        "allocation #%ld); through Dart FFI that aborts the app",
        label, escaped, firstEscape);
  CHECK(broken == 0, "%s: engine unusable after %d failures", label, broken);
  if (leakBugId != nullptr)
    KNOWN_BUG(leakBugId, leaks == 0, "%s: %d failures leaked", label, leaks);
  else
    CHECK(leaks == 0, "%s: %d failures leaked", label, leaks);
}

void scenarioBindings() {
  std::printf("bindings exports (miniaudio null backend)\n");
  if (initEngine(-1, 44100, 1024, 2, 0, 0, 0) != noError) {
    CHECK(false, "initEngine on the null backend failed");
    return;
  }
  const Bytes wav = makeWav(4000);
  const Bytes mp3 = readFile(gAssets + "/explosion.mp3");

  // A known sound to check the engine still plays after each failure.
  unsigned int probeHash = 0;
  auto loadProbe = [&] {
    Bytes probe = makeWav(500);
    char name[] = "oom-probe.wav";
    loadMem(name, probe.data(), static_cast<int>(probe.size()), 1, &probeHash);
  };
  loadProbe();
  auto alive = [&]() -> bool {
    unsigned int h = 0;
    if (play(probeHash, 0, 1.0f, 0.0f, false, false, 0, 0, -1, -1, 1.0f, &h) !=
        noError)
      return false;
    const bool ok = getIsValidVoiceHandle(h) != 0 || h != 0;
    stop(h);
    return ok;
  };

  unsigned int hash = 0;
  auto disposeLoaded = [&](PlayerErrors e) {
    if (e == noError && hash != 0) disposeSound(hash);
    hash = 0;
  };
  for (int intoMem = 1; intoMem >= 0; --intoMem) {
    for (const Bytes *data : {&wav, &mp3}) {
      if (data->empty()) continue;
      std::string label = std::string("loadMem ") +
                          (data == &wav ? "wav" : "mp3") +
                          (intoMem ? " (memory)" : " (stream)");
      // Copied here, not in the operation: an allocation of the test's own
      // would fail there and throw outside the export.
      Bytes copy = *data;
      exportCountdown(
          label.c_str(),
          [&] {
            char name[] = "oom-load";
            return loadMem(name, copy.data(), static_cast<int>(copy.size()),
                           intoMem, &hash);
          },
          disposeLoaded, alive);
    }
  }

  // play: the sound stays loaded, every voice is stopped afterwards.
  unsigned int playHash = 0;
  auto loadPlayed = [&] {
    Bytes copy = wav;
    char name[] = "oom-play.wav";
    loadMem(name, copy.data(), static_cast<int>(copy.size()), 1, &playHash);
  };
  loadPlayed();
  unsigned int handle = 0;
  auto stopVoice = [&](PlayerErrors) {
    stopAudioSource(playHash);
    handle = 0;
  };
  exportCountdown(
      "play",
      [&] {
        return play(playHash, 0, 1.0f, 0.0f, false, false, 0, 0, -1, -1, 1.0f,
                    &handle);
      },
      stopVoice, alive);
  exportCountdown(
      "play3d",
      [&] {
        return play3d(playHash, 0, 0, 0, 0, 0, 0, 0, 1.0f, false, false, 0,
                      &handle);
      },
      stopVoice, alive);

  exportCountdown(
      "loadWaveform",
      [&] { return loadWaveform(0, false, 0.25f, 1.0f, &hash); },
      disposeLoaded, alive);
  // The calls that walk the sound list after the failed loadWaveform calls.
  CHECK(finishesWithin(
            [] {
              stopAll();
              disposeAllSound();
            },
            5000),
        "stopAll / disposeAllSound after failed loadWaveform calls");
  loadProbe();
  loadPlayed();

  // Buffer stream: set up, then feed MP3 data (decoded on this thread).
  if (!mp3.empty()) {
    exportCountdown(
        "setBufferStream + addAudioDataStream (mp3)",
        [&] {
          PlayerErrors e = setBufferStream(&hash, 1u << 22, 0, 0.5, 44100, 2,
                                           BufferType::AUTO, nullptr, nullptr);
          if (e != noError) return e;
          for (size_t off = 0; off < mp3.size(); off += 4096) {
            const unsigned len = static_cast<unsigned>(
                std::min<size_t>(4096, mp3.size() - off));
            e = addAudioDataStream(hash, mp3.data() + off, len);
            if (e != noError) return e;
          }
          return setDataIsEnded(hash);
        },
        [&](PlayerErrors) {
          if (hash != 0) disposeSound(hash);
          hash = 0;
        },
        alive);
  }

  unsigned int busId = 0;
  exportCountdown(
      "createBus + busPlayOnEngine",
      [&] {
        busId = createBus();
        if (busId == 0) return unknownError;
        return busPlayOnEngine(busId, 1.0f, false, &handle);
      },
      [&](PlayerErrors) {
        if (busId != 0) destroyBus(busId);
        busId = 0;
      },
      alive);

  // A sound with a filter: the filter instance is created with the voice.
  addFilter(playHash, 0, EchoFilter);
  exportCountdown(
      "play with a sound filter",
      [&] {
        return play(playHash, 0, 1.0f, 0.0f, false, false, 0, 0, -1, -1, 1.0f,
                    &handle);
      },
      stopVoice, alive);

  dispose();
}

struct Scenario {
  const char *name;
  void (*run)();
};
const Scenario kScenarios[] = {
    {"wav", scenarioWav},
    {"wavstream", scenarioWavStream},
    {"voices", scenarioVoices},
    {"voices_filter", scenarioVoicesWithFilter},
    {"stream_decoders", scenarioStreamDecoders},
    {"bindings", scenarioBindings},
};

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  const char *only = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] != '-') only = argv[i];
  }
  const char *assets = std::getenv("SOLOUD_TEST_ASSETS");
  if (assets != nullptr) gAssets = assets;

  for (const Scenario &s : kScenarios) {
    if (only != nullptr && std::strcmp(only, s.name) != 0) continue;
    std::printf("== %s\n", s.name);
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
      robustness::gChecks = 0;
      robustness::gFailures = 0;
      robustness::gKnownBugs = 0;
      s.run();
      std::printf("   %d checks, %d failures, %d known bugs\n",
                  robustness::gChecks.load(), robustness::gFailures.load(),
                  robustness::gKnownBugs.load());
      std::fflush(stdout);
      _exit(robustness::gFailures > 0 ? 1
                                      : (robustness::gKnownBugs > 0 ? 2 : 0));
    }
    // Generous: ASan builds walk thousands of allocation points.
    int status = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(900);
    pid_t done = 0;
    while ((done = waitpid(pid, &status, WNOHANG)) == 0 &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ++robustness::gChecks;
    if (done == 0) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      ++robustness::gFailures;
      std::printf("  FAIL scenario %s timed out\n", s.name);
    } else if (WIFSIGNALED(status)) {
      ++robustness::gFailures;
      std::printf("  FAIL scenario %s crashed (signal %d)\n", s.name,
                  WTERMSIG(status));
    } else {
      const int code = WEXITSTATUS(status);
      if (code == 1 || code > kChildKnownBugExit) {
        ++robustness::gFailures;
        std::printf("  FAIL scenario %s (exit %d)\n", s.name, code);
      } else if (code == 2 || code == kChildKnownBugExit) {
        ++robustness::gKnownBugs;
      }
    }
    std::fflush(stdout);
  }
  return robustness::finish("out_of_memory_test");
}

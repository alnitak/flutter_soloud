// Standalone tests for SoLoud::CustomAudioSource (src/synth/custom_source.cpp),
// the audio source whose samples come from a user DSP callback, mixed by a
// real engine on SoLoud's null driver (mix() called by the test).
//
//   - the callback gets the voice's channel count, a buffer stride that fits
//     the request, and its user data; the lifecycle callback reports
//     "created" once per voice before the first block and "destroyed" once;
//   - a callback that writes fewer samples than asked (and leaves garbage in
//     the rest of the block): the rest is silence, never stale data or NaN,
//     both on the direct path (source rate == engine rate) and the resampled
//     one;
//   - a callback that reports more samples than asked: clamped, no crash;
//   - a callback that ends the stream (*aOutHasEnded = 1): the voice stops;
//   - 1..8 channels, into a stereo and into an 8-channel engine (3, 5 and 7
//     only must not crash: SoLoud has no mapping for them);
//   - several voices of one source (each with its own play index), and a
//     single-instance source (a new play replaces the old voice);
//   - destroying the source while it plays, from the mixing thread's point of
//     view and from another thread while mix() runs (run with --tsan);
//   - playing through a bus, and in 3D with distance attenuation.
//
//   ./test/common/custom_source_test.sh [--asan|--tsan] [--strict]

#include "synth/custom_source.h"
#include "soloud_bus.h"

#include "../support/robustness_checks.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kRate = 48000;
constexpr unsigned int kBlock = 512;

struct SynthState {
  float value = 0.5f;          // DC written on channel c as value * (c + 1)
  unsigned int writeLimit = 0; // when > 0, write/return at most this many
  bool garbageAfterLimit = false;
  unsigned int returnOverride = 0; // when > 0, returned instead
  int endAfterCalls = -1;          // set *aOutHasEnded on this call
  std::atomic<int> calls{0};
  std::atomic<int> badArgs{0};
  std::atomic<unsigned int> lastChannels{0};
  void *expectedUserData = nullptr;
};

unsigned int synth(float *buf, unsigned int samples, unsigned int bufferSize,
                   unsigned int channels, float samplerate, double, double,
                   uint32_t *outHasEnded, void *userData) {
  auto *s = static_cast<SynthState *>(userData);
  const int call = ++s->calls;
  if (userData != s->expectedUserData || bufferSize < samples ||
      channels == 0 || channels > MAX_CHANNELS || samplerate <= 0.0f ||
      outHasEnded == nullptr)
    ++s->badArgs;
  s->lastChannels.store(channels);
  const unsigned int n =
      s->writeLimit > 0 && s->writeLimit < samples ? s->writeLimit : samples;
  for (unsigned int c = 0; c < channels; ++c) {
    for (unsigned int i = 0; i < n; ++i)
      buf[c * bufferSize + i] = s->value * static_cast<float>(c + 1) / 8.0f;
    if (s->garbageAfterLimit)
      for (unsigned int i = n; i < samples; ++i)
        buf[c * bufferSize + i] = std::nanf("");
  }
  if (s->endAfterCalls > 0 && call >= s->endAfterCalls) *outHasEnded = 1;
  return s->returnOverride > 0 ? s->returnOverride : n;
}

struct Lifecycle {
  std::mutex m;
  std::vector<std::pair<uint32_t, unsigned int>> events; // (created, index)
  int created(unsigned int idx) {
    std::lock_guard<std::mutex> l(m);
    int n = 0;
    for (auto &e : events) n += (e.first == 1 && e.second == idx);
    return n;
  }
  int destroyed(unsigned int idx) {
    std::lock_guard<std::mutex> l(m);
    int n = 0;
    for (auto &e : events) n += (e.first == 0 && e.second == idx);
    return n;
  }
  size_t count(uint32_t kind) {
    std::lock_guard<std::mutex> l(m);
    size_t n = 0;
    for (auto &e : events) n += e.first == kind;
    return n;
  }
};
Lifecycle *gLifecycle = nullptr;
void lifecycle(uint32_t isCreated, unsigned int playIndex, void *) {
  if (gLifecycle == nullptr) return;
  std::lock_guard<std::mutex> l(gLifecycle->m);
  gLifecycle->events.push_back({isCreated, playIndex});
}

struct Engine {
  explicit Engine(unsigned int channels = 2) : channels(channels) {
    const SoLoud::result r = soloud.init(0, SoLoud::Soloud::NULLDRIVER, kRate,
                                         kBlock, channels);
    ok = r == SoLoud::SO_NO_ERROR;
    CHECK(ok, "engine init failed (%d)", r);
    out.assign(kBlock * channels, 0.0f);
  }
  ~Engine() { soloud.deinit(); }
  void mix(int blocks = 1) {
    for (int i = 0; i < blocks; ++i) soloud.mix(out.data(), kBlock);
  }
  float peak() const {
    float p = 0.0f;
    for (float v : out) p = std::max(p, std::fabs(v));
    return p;
  }
  bool hasNaN() const {
    for (float v : out)
      if (std::isnan(v)) return true;
    return false;
  }
  float at(unsigned int frame, unsigned int ch) const {
    return out[frame * channels + ch];
  }
  SoLoud::Soloud soloud;
  unsigned int channels;
  bool ok = false;
  std::vector<float> out;
};

void testBasicAndLifecycle() {
  std::printf("callback arguments and lifecycle\n");
  Engine e;
  Lifecycle lc;
  gLifecycle = &lc;
  SynthState st;
  st.expectedUserData = &st;
  {
    SoLoud::CustomAudioSource src(kRate, 2, synth, &st, lifecycle, false);
    SoLoud::handle h = e.soloud.play(src);
    CHECK(h != 0, "play");
    const unsigned int idx = (h >> 12) & 0xfffff;
    CHECK(lc.count(1) == 0, "not created before the first block");
    e.mix(3);
    CHECK(st.calls > 0, "callback called");
    CHECK(st.badArgs == 0, "%d calls with wrong arguments", st.badArgs.load());
    CHECK(st.lastChannels == 2, "channels %u", st.lastChannels.load());
    CHECK(lc.created(idx) == 1, "created once for play index %u (%d)", idx,
          lc.created(idx));
    CHECK(e.peak() > 0.01f && !e.hasNaN(), "audible output (peak %g)",
          e.peak());
    e.soloud.stop(h);
    CHECK(lc.destroyed(idx) == 1, "destroyed once after stop (%d)",
          lc.destroyed(idx));
    // A voice that never produced a block reports nothing.
    SoLoud::handle h2 = e.soloud.play(src);
    e.soloud.stop(h2);
    CHECK(lc.count(1) == 1 && lc.count(0) == 1,
          "a voice stopped before its first block reports neither event "
          "(%zu created, %zu destroyed)",
          lc.count(1), lc.count(0));
  }
  gLifecycle = nullptr;
}

void testShortWrite(unsigned int sourceRate, const char *path) {
  std::printf("callback writes fewer samples than asked (%s)\n", path);
  Engine e;
  SynthState st;
  st.expectedUserData = &st;
  st.writeLimit = 100;
  st.garbageAfterLimit = true;
  SoLoud::CustomAudioSource src(sourceRate, 2, synth, &st, nullptr, false);
  e.soloud.play(src);
  int nanBlocks = 0, silentFrames = 0, loudFrames = 0;
  for (int b = 0; b < 8; ++b) {
    e.mix();
    if (e.hasNaN()) ++nanBlocks;
    if (b == 0) continue; // volume ramp
    for (unsigned int f = 0; f < kBlock; ++f) {
      if (e.at(f, 0) == 0.0f && e.at(f, 1) == 0.0f)
        ++silentFrames;
      else
        ++loudFrames;
    }
  }
  CHECK(nanBlocks == 0, "%d blocks with NaN: unwritten samples leaked",
        nanBlocks);
  CHECK(loudFrames > 0, "the written samples were played");
  CHECK(silentFrames > 0, "the unwritten part is silence");
  CHECK(st.badArgs == 0, "callback arguments");
}

void testOverReport() {
  std::printf("callback reports more samples than asked\n");
  Engine e;
  SynthState st;
  st.expectedUserData = &st;
  st.returnOverride = 0xFFFFFFFFu;
  SoLoud::CustomAudioSource src(kRate, 2, synth, &st, nullptr, false);
  e.soloud.play(src);
  e.mix(4);
  CHECK(!e.hasNaN() && e.peak() > 0.01f && e.peak() < 1.0f,
        "output sane (peak %g)", e.peak());
  SoLoud::CustomAudioSource slow(kRate / 2, 2, synth, &st, nullptr, false);
  e.soloud.play(slow);
  e.mix(4);
  CHECK(!e.hasNaN() && e.peak() < 2.0f, "resampled output sane (peak %g)",
        e.peak());
}

void testEnd() {
  std::printf("callback ends the stream\n");
  Engine e;
  Lifecycle lc;
  gLifecycle = &lc;
  SynthState st;
  st.expectedUserData = &st;
  st.endAfterCalls = 3;
  SoLoud::CustomAudioSource src(kRate, 2, synth, &st, lifecycle, false);
  SoLoud::handle h = e.soloud.play(src);
  e.mix(10);
  CHECK(!e.soloud.isValidVoiceHandle(h), "voice stopped after the end");
  CHECK(e.soloud.getActiveVoiceCount() == 0, "no active voices (%u)",
        e.soloud.getActiveVoiceCount());
  const int calls = st.calls;
  CHECK(calls >= 3 && calls <= 4, "no calls after the end (%d)", calls);
  e.mix(3);
  CHECK(st.calls == calls, "still no calls");
  CHECK(lc.count(1) == 1 && lc.count(0) == 1,
        "created / destroyed once (%zu / %zu)", lc.count(1), lc.count(0));
  gLifecycle = nullptr;
}

void testChannels() {
  std::printf("1..8 channels into stereo and 8-channel engines\n");
  for (unsigned int engineCh : {2u, 8u}) {
    Engine e(engineCh);
    if (!e.ok) continue;
    for (unsigned int ch = 1; ch <= 8; ++ch) {
      SynthState st;
      st.expectedUserData = &st;
      SoLoud::CustomAudioSource src(kRate, ch, synth, &st, nullptr, false);
      SoLoud::handle h = e.soloud.play(src);
      e.mix(3);
      CHECK(st.lastChannels == ch, "engine %u: callback got %u channels, "
            "want %u", engineCh, st.lastChannels.load(), ch);
      CHECK(st.badArgs == 0, "engine %u ch %u: callback arguments",
            engineCh, ch);
      // SoLoud's mixer (panAndExpand) only maps 1, 2, 4, 6 and 8 channel
      // voices; 3, 5 and 7 play silence. The Dart API (Channels) only offers
      // the supported counts, so only check those for sound.
      const bool supported = ch == 1 || ch == 2 || ch == 4 || ch == 6 ||
                             ch == 8;
      CHECK(!e.hasNaN(), "engine %u ch %u: no NaN", engineCh, ch);
      if (supported)
        CHECK(e.peak() > 0.0f, "engine %u ch %u: output", engineCh, ch);
      else if (engineCh == 2 && e.peak() == 0.0f)
        std::printf("  note: a %u-channel voice plays silence (no mapping "
                    "in panAndExpand)\n", ch);
      e.soloud.stop(h);
    }
    // 0 channels / rate fall back to the defaults.
    SynthState st;
    st.expectedUserData = &st;
    SoLoud::CustomAudioSource defaults(0, 0, synth, &st, nullptr, false);
    CHECK(defaults.mChannels == 2 && defaults.mBaseSamplerate == 48000.0f,
          "0 channels / rate -> 2 / 48000");
  }
}

void testMultipleVoices() {
  std::printf("several voices of one source, single instance\n");
  Engine e;
  Lifecycle lc;
  gLifecycle = &lc;
  SynthState st;
  st.expectedUserData = &st;
  {
    SoLoud::CustomAudioSource src(kRate, 2, synth, &st, lifecycle, false);
    SoLoud::handle h[3];
    std::set<unsigned int> idx;
    for (auto &x : h) {
      x = e.soloud.play(src);
      idx.insert((x >> 12) & 0xfffff);
    }
    CHECK(idx.size() == 3, "distinct play indices");
    e.mix(2);
    CHECK(e.soloud.getActiveVoiceCount() == 3, "3 voices");
    CHECK(st.calls >= 6, "each voice calls back (%d)", st.calls.load());
    CHECK(lc.count(1) == 3, "3 created events (%zu)", lc.count(1));
    for (unsigned int i : idx) CHECK(lc.created(i) == 1, "created %u", i);
    e.soloud.stop(h[1]);
    CHECK(lc.count(0) == 1 && lc.destroyed((h[1] >> 12) & 0xfffff) == 1,
          "stopping one voice destroys only it");
    e.mix();
  }
  CHECK(lc.count(0) == 3, "destroying the source destroys the rest (%zu)",
        lc.count(0));

  lc.events.clear();
  SynthState one;
  one.expectedUserData = &one;
  SoLoud::CustomAudioSource single(kRate, 2, synth, &one, lifecycle, true);
  SoLoud::handle a = e.soloud.play(single);
  e.mix();
  SoLoud::handle b = e.soloud.play(single);
  e.mix();
  CHECK(!e.soloud.isValidVoiceHandle(a) && e.soloud.isValidVoiceHandle(b),
        "single instance: the new voice replaces the old one");
  CHECK(e.soloud.getActiveVoiceCount() == 1, "one voice");
  CHECK(lc.count(1) == 2 && lc.count(0) == 1, "events %zu / %zu",
        lc.count(1), lc.count(0));
  e.soloud.stopAll();
  gLifecycle = nullptr;
}

void testDestroyWhilePlaying() {
  std::printf("destroy the source while it plays\n");
  {
    Engine e;
    SynthState st;
    st.expectedUserData = &st;
    auto src = std::make_unique<SoLoud::CustomAudioSource>(
        kRate, 2, synth, &st, nullptr, false);
    e.soloud.play(*src);
    e.soloud.play(*src);
    e.mix(2);
    src.reset();
    CHECK(e.soloud.getVoiceCount() == 0, "voices gone with the source");
    const int calls = st.calls;
    e.mix(3);
    CHECK(st.calls == calls, "no callback after the source is destroyed");
  }
  // From another thread while mix() runs.
  Engine e;
  SynthState st;
  st.expectedUserData = &st;
  std::atomic<bool> stop{false};
  std::thread mixer([&] {
    std::vector<float> buf(kBlock * 2);
    while (!stop.load()) e.soloud.mix(buf.data(), kBlock);
  });
  int lateCalls = 0;
  for (int round = 0; round < 200; ++round) {
    auto src = std::make_unique<SoLoud::CustomAudioSource>(
        kRate, 2, synth, &st, nullptr, false);
    for (int i = 0; i < 3; ++i) e.soloud.play(*src);
    const int before = st.calls;
    while (st.calls == before && round % 2 == 0) std::this_thread::yield();
    src.reset();
    const int after = st.calls;
    for (int i = 0; i < 50; ++i) std::this_thread::yield();
    if (st.calls != after) ++lateCalls;
  }
  stop.store(true);
  mixer.join();
  CHECK(lateCalls == 0, "%d callbacks ran after their source was destroyed",
        lateCalls);
  CHECK(st.badArgs == 0, "callback arguments");
}

void testBusAnd3d() {
  std::printf("through a bus, and in 3D\n");
  Engine e;
  SynthState st;
  st.expectedUserData = &st;
  SoLoud::CustomAudioSource src(kRate, 2, synth, &st, nullptr, false);
  SoLoud::Bus bus;
  SoLoud::handle bh = e.soloud.play(bus);
  CHECK(bh != 0, "bus playing");
  SoLoud::handle h = bus.play(src);
  CHECK(h != 0, "voice on the bus");
  e.mix(3);
  CHECK(st.calls > 0 && e.peak() > 0.01f, "bus output (peak %g)", e.peak());
  CHECK(bus.getActiveVoiceCount() == 1, "bus has the voice");
  e.soloud.stopAll();

  // Not `near` / `far`: those are macros in the Windows headers.
  SynthState nearState, farState;
  nearState.expectedUserData = &nearState;
  farState.expectedUserData = &farState;
  SoLoud::CustomAudioSource nearSrc(kRate, 1, synth, &nearState, nullptr, false);
  SoLoud::CustomAudioSource farSrc(kRate, 1, synth, &farState, nullptr, false);
  auto level3d = [&](SoLoud::AudioSource &s, float dist) {
    SoLoud::handle v = e.soloud.play3d(s, dist, 0, 0);
    e.soloud.set3dSourceAttenuation(v, SoLoud::AudioSource::INVERSE_DISTANCE,
                                    1.0f);
    e.soloud.set3dSourceMinMaxDistance(v, 1.0f, 1000.0f);
    e.soloud.update3dAudio();
    e.mix(4);
    const float p = e.peak();
    e.soloud.stop(v);
    return p;
  };
  const float pNear = level3d(nearSrc, 1.0f);
  const float pFar = level3d(farSrc, 50.0f);
  CHECK(nearState.calls > 0 && pNear > 0.0f, "3D voice audible (%g)", pNear);
  CHECK(pFar < pNear * 0.2f, "distance attenuates (%g vs %g)", pFar, pNear);
  CHECK(!e.hasNaN(), "no NaN");

  // A 3D voice on a bus.
  SoLoud::handle bh2 = e.soloud.play(bus);
  SoLoud::handle v = bus.play3d(src, 2.0f, 0, 0);
  e.soloud.update3dAudio();
  e.mix(3);
  CHECK(bh2 != 0 && v != 0 && e.peak() > 0.0f, "3D voice on a bus");
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  testBasicAndLifecycle();
  testShortWrite(kRate, "direct path");
  testShortWrite(kRate / 2, "resampled path");
  testOverReport();
  testEnd();
  testChannels();
  testMultipleVoices();
  testDestroyWhilePlaying();
  testBusAnd3d();
  return robustness::finish("custom_source_test");
}

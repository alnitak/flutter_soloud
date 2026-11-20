// Standalone native tests for the fader operations in
// src/soloud/src/core/soloud_core_faderops.cpp (with the flutter_soloud
// local patches) and the Fader they drive (soloud_fader.cpp), mixed through
// the engine with the null backend.
//
// A constant (DC) source makes the output level equal to the voice gain, so
// the envelope can be read sample by sample. The faders are evaluated once
// per mixed block at the voice's stream time and the volume is ramped
// linearly across the block, so a linear fade comes out linear to within one
// block's worth of change: |to - from| * block / (rate * time).
//
//   fade_volume     a volume fade follows its line and holds the target;
//   fade_interrupt  a fade started during another starts from the current
//                   level (no jump); setVolume() cancels a fade;
//   oscillate       oscillateVolume swings between from and to with the given
//                   period; a fade replaces the oscillation;
//   pan             fadePan ends at the setPan() channel gains, each channel
//                   moving monotonically; oscillatePan stays finite;
//   speed           fadeRelativePlaySpeed 1 -> 2 over 1 s advances the
//                   stream by 1.5 s;
//   global          fadeGlobalVolume / oscillateGlobalVolume;
//   pause_fade      schedulePause during a fade pauses the fade with the
//                   voice, and it resumes where it was;
//   stop_fade       scheduleStop during or at the end of a fade stops the
//                   voice on time and without a step;
//   zero_negative   duration 0 and negative apply the target at once
//                   (fades, oscillations, schedulePause/Stop, global).
//
// No audio device and no assets are used.
//
//   ./test/common/fader_ops_test.sh [--asan] [--strict] [case]

#include "soloud.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr unsigned int kRate = 44100;
constexpr unsigned int kBlock = 512;
constexpr float kDc = 0.5f;

class DcInstance : public SoLoud::AudioSourceInstance {
 public:
  unsigned int getAudio(float *buf, unsigned int n,
                        unsigned int bufSize) override {
    for (unsigned int c = 0; c < mChannels; c++)
      for (unsigned int i = 0; i < n; i++) buf[c * bufSize + i] = kDc;
    return n;
  }
  bool hasEnded() override { return false; }
};

class DcSource : public SoLoud::AudioSource {
 public:
  DcSource() {
    mChannels = 1;
    mBaseSamplerate = (float)kRate;
  }
  ~DcSource() override { stop(); }
  SoLoud::AudioSourceInstance *createInstance() override {
    return new DcInstance();
  }
};

struct Engine {
  SoLoud::Soloud s;
  unsigned int channels;
  explicit Engine(unsigned int ch = 1) : channels(ch) {
    s.init(0, SoLoud::Soloud::NULLDRIVER, kRate, kBlock, ch);
  }
  ~Engine() { s.deinit(); }
  /// Mixes whole blocks covering [frames]; returns channel [c].
  std::vector<float> mix(size_t frames, unsigned int c = 0) {
    std::vector<float> out;
    std::vector<float> buf(kBlock * channels);
    for (size_t pos = 0; pos < frames; pos += kBlock) {
      s.mix(buf.data(), kBlock);
      for (unsigned int i = 0; i < kBlock; i++)
        out.push_back(buf[i * channels + c]);
    }
    return out;
  }
  std::vector<float> mixStereo(size_t frames, std::vector<float> &right) {
    std::vector<float> left;
    std::vector<float> buf(kBlock * 2);
    right.clear();
    for (size_t pos = 0; pos < frames; pos += kBlock) {
      s.mix(buf.data(), kBlock);
      for (unsigned int i = 0; i < kBlock; i++) {
        left.push_back(buf[i * 2]);
        right.push_back(buf[i * 2 + 1]);
      }
    }
    return left;
  }
};

size_t frames(double seconds) {
  return ((size_t)(seconds * kRate) / kBlock) * kBlock;  // whole blocks
}

/// Plays the DC source and settles; returns the output level at volume 1.
float start(Engine &e, DcSource &src, SoLoud::handle &h) {
  h = e.s.play(src, 1.0f);
  std::vector<float> o = e.mix(kBlock * 4);
  return o.back();
}

/// Max deviation of env/level0 from a linear fade from [from] to [to]
/// over [T] seconds starting at sample 0, and the samples it ignored.
double fadeError(const std::vector<float> &env, float level0, float from,
                 float to, double T) {
  double err = 0;
  for (size_t n = 0; n < env.size(); n++) {
    double p = std::min(1.0, (double)n / (T * kRate));
    double v = from + (to - from) * p;
    err = std::max(err, std::fabs(env[n] / level0 - v));
  }
  return err;
}

double blockTol(float from, float to, double T) {
  return std::fabs(to - from) * (kBlock + 1) / (T * kRate) + 1e-5;
}

double maxStep(const std::vector<float> &v, size_t from = 1) {
  double m = 0;
  for (size_t i = std::max<size_t>(from, 1); i < v.size(); i++)
    m = std::max(m, (double)std::fabs(v[i] - v[i - 1]));
  return m;
}

void fadeVolume() {
  Engine e;
  DcSource src;
  SoLoud::handle h;
  const float L = start(e, src, h);
  CHECK(L > 0.1f, "reference level %g", L);
  for (double T : {0.5, 0.05, 0.003}) {
    e.s.setVolume(h, 1.0f);
    e.mix(kBlock * 2);
    e.s.fadeVolume(h, 0.25f, T);
    std::vector<float> env = e.mix(frames(T) + kBlock * 8);
    const double err = fadeError(env, L, 1.0f, 0.25f, T);
    CHECK(err <= blockTol(1.0f, 0.25f, T), "fade over %g s: error %g (tol %g)",
          T, err, blockTol(1.0f, 0.25f, T));
    CHECK(std::fabs(env.back() / L - 0.25f) < 1e-5, "holds 0.25, got %g",
          env.back() / L);
    CHECK(std::fabs(e.s.getVolume(h) - 0.25f) < 1e-6, "getVolume %g",
          e.s.getVolume(h));
  }
}

void fadeInterrupt() {
  Engine e;
  DcSource src;
  SoLoud::handle h;
  const float L = start(e, src, h);
  e.s.fadeVolume(h, 0.0f, 1.0);
  std::vector<float> a = e.mix(frames(0.5));
  const float mid = a.back() / L;
  CHECK(std::fabs(mid - 0.5f) < 0.02f, "halfway level %g", mid);
  e.s.fadeVolume(h, 1.0f, 0.5);
  std::vector<float> b = e.mix(frames(0.5) + kBlock * 4);
  // The second fade starts from where the first one was.
  const double err = fadeError(b, L, mid, 1.0f, 0.5);
  CHECK(err <= blockTol(mid, 1.0f, 0.5) + 0.002,
        "second fade from %g: error %g", mid, err);
  CHECK(std::fabs(b.front() - a.back()) < 2.0 * L / (kRate * 0.5) * kBlock,
        "no jump at the interruption: %g -> %g", a.back(), b.front());
  CHECK(std::fabs(b.back() / L - 1.0f) < 1e-5, "ends at 1");

  // setVolume during a fade cancels it.
  e.s.fadeVolume(h, 0.0f, 1.0);
  e.mix(frames(0.2));
  e.s.setVolume(h, 0.6f);
  std::vector<float> c = e.mix(frames(1.2));
  CHECK(std::fabs(c.back() / L - 0.6f) < 1e-5,
        "setVolume cancels the fade: level %g", c.back() / L);
}

void oscillate() {
  Engine e;
  DcSource src;
  SoLoud::handle h;
  const float L = start(e, src, h);
  const double T = 0.25;
  e.s.oscillateVolume(h, 0.2f, 0.8f, T);
  std::vector<float> env = e.mix(frames(2.0));
  float lo = 1e9f, hi = -1e9f;
  int upCrossings = 0;
  long firstUp = -1, lastUp = -1;
  // The LFO starts at its midpoint whatever the current volume, so the first
  // block ramps from 1 to ~0.5: skip it.
  for (size_t n = kBlock + 1; n < env.size(); n++) {
    const float v = env[n] / L;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
    if (env[n - 1] / L < 0.5f && v >= 0.5f) {
      upCrossings++;
      if (firstUp < 0) firstUp = (long)n;
      lastUp = (long)n;
    }
  }
  // Sampled once per block (11.6 ms) on a 250 ms period: the extremes are
  // reached to within 1 - cos(pi * block / (rate * T)) of the swing.
  const double sampTol = 0.3 * (1 - std::cos(M_PI * kBlock / (kRate * T))) + 1e-4;
  CHECK(std::fabs(lo - 0.2f) <= sampTol && std::fabs(hi - 0.8f) <= sampTol,
        "swings %g..%g, expected 0.2..0.8", lo, hi);
  CHECK(lo >= 0.2f - 1e-4f && hi <= 0.8f + 1e-4f, "stays inside 0.2..0.8");
  const double period =
      upCrossings > 1 ? (double)(lastUp - firstUp) / (upCrossings - 1) / kRate : 0;
  CHECK(std::fabs(period - T) < (double)kBlock / kRate,
        "period %g s, expected %g", period, T);
  // A fade replaces the oscillation.
  e.s.fadeVolume(h, 0.1f, 0.1);
  std::vector<float> f = e.mix(frames(0.3));
  CHECK(std::fabs(f.back() / L - 0.1f) < 1e-5, "fade ends the LFO: %g",
        f.back() / L);
}

void pan() {
  Engine e(2);
  DcSource src;
  SoLoud::handle h = e.s.play(src, 1.0f);
  std::vector<float> r;
  e.s.setPan(h, 1.0f);
  std::vector<float> l = e.mixStereo(kBlock * 4, r);
  const float L1 = l.back(), R1 = r.back();
  e.s.setPan(h, 0.0f);
  l = e.mixStereo(kBlock * 4, r);
  const float L0 = l.back(), R0 = r.back();
  CHECK(R1 > R0 && L1 < L0, "setPan(1) moves to the right");
  e.s.fadePan(h, 1.0f, 0.3);
  l = e.mixStereo(frames(0.4), r);
  bool mono = true;
  for (size_t i = 1; i < l.size(); i++)
    if (l[i] > l[i - 1] + 1e-5f || r[i] < r[i - 1] - 1e-5f) {  // float noise
      if (mono)
        std::printf("    at %zu: L %g -> %g, R %g -> %g\n", i, l[i - 1], l[i],
                    r[i - 1], r[i]);
      mono = false;
    }
  CHECK(mono, "left only falls, right only rises during the pan fade");
  CHECK(std::fabs(l.back() - L1) < 1e-5 && std::fabs(r.back() - R1) < 1e-5,
        "pan fade ends at setPan(1) gains: L %g/%g R %g/%g", l.back(), L1,
        r.back(), R1);
  CHECK(std::fabs(e.s.getPan(h) - 1.0f) < 1e-6, "getPan %g", e.s.getPan(h));
  e.s.oscillatePan(h, -1.0f, 1.0f, 0.1);
  l = e.mixStereo(frames(0.5), r);
  CHECK(ctest::allFinite(l) && ctest::allFinite(r), "oscillatePan finite");
  CHECK(ctest::peak(l.data(), l.size()) <= std::max(std::max(L0, L1), R1) * 1.0001f + 1e-6f,
        "oscillatePan stays within the pan law");
}

void speed() {
  Engine e;
  DcSource src;
  SoLoud::handle h = e.s.play(src, 1.0f);
  e.mix(kBlock);
  const double p0 = e.s.getStreamPosition(h);
  e.s.fadeRelativePlaySpeed(h, 2.0f, 1.0);
  e.mix(frames(1.0));
  const double t = (double)frames(1.0) / kRate;
  const double p1 = e.s.getStreamPosition(h);
  // Integral of 1 + t over [0, t] (t ~ 1 s): t + t^2/2.
  const double expected = t + t * t / 2;
  // The position advances with the previous block's speed: <= one block of
  // the speed change (block/rate * 1/1 s), plus the end-of-fade block.
  CHECK(std::fabs((p1 - p0) - expected) <= 2.0 * kBlock / kRate,
        "stream advanced %g s, expected %g", p1 - p0, expected);
  e.mix(frames(0.2));
  CHECK(std::fabs(e.s.getRelativePlaySpeed(h) - 2.0f) < 1e-6,
        "speed ends at 2, got %g", e.s.getRelativePlaySpeed(h));
  e.s.oscillateRelativePlaySpeed(h, 0.5f, 1.5f, 0.2);
  e.mix(frames(0.5));
  const float sp = e.s.getRelativePlaySpeed(h);
  CHECK(sp >= 0.5f - 1e-4f && sp <= 1.5f + 1e-4f, "oscillating speed %g", sp);
}

void global() {
  Engine e;
  DcSource src;
  SoLoud::handle h;
  const float L = start(e, src, h);
  e.s.fadeGlobalVolume(0.5f, 0.4);
  std::vector<float> env = e.mix(frames(0.4) + kBlock * 4);
  const double err = fadeError(env, L, 1.0f, 0.5f, 0.4);
  CHECK(err <= blockTol(1.0f, 0.5f, 0.4), "global fade error %g", err);
  CHECK(std::fabs(e.s.getGlobalVolume() - 0.5f) < 1e-6, "global volume %g",
        e.s.getGlobalVolume());
  e.s.oscillateGlobalVolume(0.25f, 0.75f, 0.2);
  env = e.mix(frames(1.0));
  float lo = 1e9f, hi = 0;
  for (float v : env) {
    lo = std::min(lo, v / L);
    hi = std::max(hi, v / L);
  }
  CHECK(lo >= 0.25f - 1e-4f && hi <= 0.75f + 1e-4f && hi - lo > 0.4f,
        "global LFO %g..%g", lo, hi);
  e.s.setGlobalVolume(1.0f);
  env = e.mix(frames(0.5));
  CHECK(std::fabs(env.back() / L - 1.0f) < 1e-5,
        "setGlobalVolume cancels the LFO");
}

void pauseFade() {
  Engine e;
  DcSource src;
  SoLoud::handle h;
  const float L = start(e, src, h);
  e.s.fadeVolume(h, 0.0f, 1.0);
  e.s.schedulePause(h, 0.5);
  std::vector<float> a = e.mix(frames(0.6));
  CHECK(e.s.getPause(h), "paused by the schedule");
  // The voice is paused from ~0.5 s: silence at the end of [a].
  CHECK(a.back() == 0.f, "paused voice is silent");
  // The last audible level is ~0.5.
  float lastLevel = 0;
  for (float v : a)
    if (v > 0) lastLevel = v / L;
  CHECK(std::fabs(lastLevel - 0.5f) < 0.03f, "paused at level %g", lastLevel);
  e.mix(frames(1.0));  // paused: voice time and the fade stand still
  e.s.setPause(h, false);
  std::vector<float> b = e.mix(frames(0.7));
  // Resumes at ~0.5 (not at 0, the value at wall-clock end of the fade) and
  // fades to 0 over the remaining ~0.5 s.
  CHECK(std::fabs(b[kBlock] / L - lastLevel) < 0.03f,
        "resumes at %g, paused at %g", b[kBlock] / L, lastLevel);
  CHECK(b.back() == 0.f, "then fades out");
  CHECK(e.s.isValidVoiceHandle(h), "voice still alive");
}

void stopFade() {
  // Stop at the end of a fade-out: valid until then, gone after, no step.
  {
    Engine e;
    DcSource src;
    SoLoud::handle h;
    const float L = start(e, src, h);
    e.s.fadeVolume(h, 0.0f, 0.5);
    e.s.scheduleStop(h, 0.5);
    std::vector<float> a = e.mix(frames(0.45));
    CHECK(e.s.isValidVoiceHandle(h), "alive before the stop");
    std::vector<float> b = e.mix(frames(0.2));
    CHECK(!e.s.isValidVoiceHandle(h), "stopped after 0.5 s");
    a.insert(a.end(), b.begin(), b.end());
    CHECK(maxStep(a) <= 2.0 * L / (0.5 * kRate) + 1e-5,
          "no step: largest sample change %g", maxStep(a));
  }
  // Stop in the middle of a fade: the voice ramps to 0 over one block.
  {
    Engine e;
    DcSource src;
    SoLoud::handle h;
    const float L = start(e, src, h);
    e.s.fadeVolume(h, 0.0f, 1.0);
    e.s.scheduleStop(h, 0.3);
    std::vector<float> a = e.mix(frames(0.5));
    CHECK(!e.s.isValidVoiceHandle(h), "stopped mid-fade");
    // ~0.7 * L ramped to 0 over 512 samples: steps of ~L * 0.7 / 512.
    CHECK(maxStep(a) <= L * 1.0 / kBlock + 1e-5,
          "mid-fade stop ramps (largest change %g, %g per sample allowed)",
          maxStep(a), L * 1.0 / kBlock);
    CHECK(a.back() == 0.f, "silent after the stop");
  }
}

void zeroNegative() {
  Engine e(2);
  DcSource src;
  SoLoud::handle h = e.s.play(src, 1.0f);
  e.mix(kBlock * 2);
  for (double T : {0.0, -1.0, -1e-9}) {
    e.s.setVolume(h, 1.0f);
    e.s.fadeVolume(h, 0.3f, T);
    CHECK(std::fabs(e.s.getVolume(h) - 0.3f) < 1e-6, "fadeVolume(T %g) sets "
          "at once: %g", T, e.s.getVolume(h));
    e.s.fadePan(h, -0.5f, T);
    CHECK(std::fabs(e.s.getPan(h) + 0.5f) < 1e-6, "fadePan(T %g): %g", T,
          e.s.getPan(h));
    e.s.fadeRelativePlaySpeed(h, 1.5f, T);
    CHECK(std::fabs(e.s.getRelativePlaySpeed(h) - 1.5f) < 1e-6,
          "fadeRelativePlaySpeed(T %g): %g", T, e.s.getRelativePlaySpeed(h));
    e.s.fadeGlobalVolume(0.8f, T);
    CHECK(std::fabs(e.s.getGlobalVolume() - 0.8f) < 1e-6,
          "fadeGlobalVolume(T %g): %g", T, e.s.getGlobalVolume());
    e.s.oscillateVolume(h, 0.1f, 0.9f, T);
    CHECK(std::fabs(e.s.getVolume(h) - 0.9f) < 1e-6,
          "oscillateVolume(T %g) sets 'to': %g", T, e.s.getVolume(h));
    e.s.oscillatePan(h, 0.1f, 0.2f, T);
    CHECK(std::fabs(e.s.getPan(h) - 0.2f) < 1e-6, "oscillatePan(T %g)", T);
    e.s.oscillateRelativePlaySpeed(h, 0.5f, 0.75f, T);
    CHECK(std::fabs(e.s.getRelativePlaySpeed(h) - 0.75f) < 1e-6,
          "oscillateRelativePlaySpeed(T %g)", T);
    e.s.oscillateGlobalVolume(0.2f, 0.4f, T);
    CHECK(std::fabs(e.s.getGlobalVolume() - 0.4f) < 1e-6,
          "oscillateGlobalVolume(T %g)", T);
    // Nothing keeps fading afterwards.
    std::vector<float> r;
    std::vector<float> l = e.mixStereo(frames(0.2), r);
    CHECK(ctest::allFinite(l) && ctest::allFinite(r), "finite");
    CHECK(std::fabs(e.s.getVolume(h) - 0.9f) < 1e-6 &&
              std::fabs(e.s.getGlobalVolume() - 0.4f) < 1e-6,
          "no fader left running");
    e.s.setGlobalVolume(1.0f);
    e.s.setRelativePlaySpeed(h, 1.0f);
  }
  // schedulePause / scheduleStop with 0 and negative times act at once.
  for (double T : {0.0, -2.0}) {
    SoLoud::handle v = e.s.play(src, 1.0f);
    e.s.schedulePause(v, T);
    CHECK(e.s.getPause(v), "schedulePause(%g) pauses at once", T);
    e.s.scheduleStop(v, T);
    CHECK(!e.s.isValidVoiceHandle(v), "scheduleStop(%g) stops at once", T);
  }
  // A tiny positive time completes within one block.
  e.s.setVolume(h, 1.0f);
  e.s.fadeVolume(h, 0.0f, 1e-7);
  e.mix(kBlock * 2);
  CHECK(e.s.getVolume(h) == 0.0f, "1e-7 s fade done after a block: %g",
        e.s.getVolume(h));
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("fade_volume")) fadeVolume();
  if (ctest::selected("fade_interrupt")) fadeInterrupt();
  if (ctest::selected("oscillate")) oscillate();
  if (ctest::selected("pan")) pan();
  if (ctest::selected("speed")) speed();
  if (ctest::selected("global")) global();
  if (ctest::selected("pause_fade")) pauseFade();
  if (ctest::selected("stop_fade")) stopFade();
  if (ctest::selected("zero_negative")) zeroNegative();
  return ctest::summary("fader_ops_test");
}

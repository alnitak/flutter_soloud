// Standalone native tests for voice virtualization in the SoLoud core
// (Soloud::calcActiveVoices_internal in src/soloud/src/core/soloud.cpp, the
// inaudible handling in soloud_core_3d.cpp), mixed through the engine with
// the null backend.
//
// With more playing voices than setMaxActiveVoiceCount(), only the loudest
// are mixed; the others are virtual: not mixed, their source not read. A
// protected voice is always among the mixed ones; a voice set to "tick when
// inaudible" always is too. Independently, a 3D voice whose volume falls
// below 0.001 is flagged inaudible: setInaudibleBehavior() then decides
// whether it is stopped (kill), keeps advancing (tick) or freezes.
//
// Each test source counts the frames it has been asked for (its position)
// and outputs either a constant or its position, so the test sees which
// voices are mixed and where they are.
//
//   selection     6 voices, 4 slots: the 4 loudest are mixed, the output is
//                 their sum;
//   protected     a quiet protected voice takes a slot from the loudest
//                 unprotected ones;
//   reevaluate    changing volumes, stopping a voice, and raising the limit
//                 move voices in and out of the mixed set;
//   resume_point  a virtualized voice resumes exactly where it stopped
//                 being mixed; getStreamPosition() agrees with what is heard;
//   must_tick     tick-when-inaudible voices are always mixed, even quiet,
//                 and more of them than slots does not break the mixer;
//   inaudible_3d  a 3D voice below the audibility threshold: default
//                 freezes and resumes in place; tick keeps advancing and
//                 resumes at the elapsed position; kill stops it.
//
// No audio device and no assets are used.
//
//   ./test/common/voice_virtualization_test.sh [--asan] [--strict] [case]

#include "soloud.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr unsigned int kRate = 44100;
constexpr unsigned int kBlock = 512;
// Engine output gain for a centred mono voice: post-clip scaler 0.95 times
// the pan law's cos(pi/4).
const double kGain = 0.95 * std::cos(M_PI / 4);
constexpr double kRampStep = 1e-5;  // ramp sources output position * 1e-5

class CountInstance : public SoLoud::AudioSourceInstance {
 public:
  CountInstance(long long *counter, float value, bool ramp)
      : mCounter(counter), mValue(value), mRamp(ramp) {}
  unsigned int getAudio(float *buf, unsigned int n, unsigned int) override {
    for (unsigned int i = 0; i < n; i++, (*mCounter)++)
      buf[i] = mRamp ? (float)((*mCounter % 50000) * kRampStep) : mValue;
    return n;
  }
  bool hasEnded() override { return false; }
  long long *mCounter;
  float mValue;
  bool mRamp;
};

class CountSource : public SoLoud::AudioSource {
 public:
  explicit CountSource(float value = 1.0f, bool ramp = false)
      : mValue(value), mRamp(ramp) {
    mChannels = 1;
    mBaseSamplerate = (float)kRate;
  }
  ~CountSource() override { stop(); }
  SoLoud::AudioSourceInstance *createInstance() override {
    mFrames = 0;
    return new CountInstance(&mFrames, mValue, mRamp);
  }
  long long mFrames = 0;
  float mValue;
  bool mRamp;
};

struct Engine {
  SoLoud::Soloud s;
  explicit Engine(unsigned int maxActive) {
    s.init(0, SoLoud::Soloud::NULLDRIVER, kRate, kBlock, 1);
    s.setMaxActiveVoiceCount(maxActive);
  }
  ~Engine() { s.deinit(); }
  std::vector<float> mix(int blocks) {
    std::vector<float> out((size_t)blocks * kBlock);
    for (int b = 0; b < blocks; b++) s.mix(out.data() + (size_t)b * kBlock, kBlock);
    return out;
  }
};

/// Which of [srcs] were read during the last mix (their counters moved).
std::vector<bool> mixedSet(Engine &e, std::vector<std::unique_ptr<CountSource>> &srcs,
                           std::vector<float> *out = nullptr) {
  std::vector<long long> before;
  for (auto &s : srcs) before.push_back(s->mFrames);
  std::vector<float> o = e.mix(2);
  if (out) *out = o;
  std::vector<bool> mixed;
  for (size_t i = 0; i < srcs.size(); i++) mixed.push_back(srcs[i]->mFrames != before[i]);
  return mixed;
}

/// The source position heard at b[0], read from a settled stretch of a ramp
/// (b[i] = gain * kRampStep * position(i)), so neither the gain nor a
/// volume ramp at the start matters. -1 if the ramp is not there.
long long positionAtStart(const std::vector<float> &b, size_t at = 700) {
  const double slope = (b[at + 100] - b[at]) / 100.0;
  if (!(slope > 0)) return -1;
  return std::llround(b[at] / slope) - (long long)at;
}

std::string setString(const std::vector<bool> &m) {
  std::string s;
  for (bool b : m) s += b ? '1' : '0';
  return s;
}

void selection() {
  Engine e(4);
  const float vols[] = {0.9f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f};
  std::vector<std::unique_ptr<CountSource>> srcs;
  for (int i = 0; i < 6; i++) {
    srcs.emplace_back(new CountSource(0.1f));
    e.s.play(*srcs.back(), vols[i]);
  }
  e.mix(2);  // settle volume ramps
  std::vector<float> out;
  std::vector<bool> m = mixedSet(e, srcs, &out);
  CHECK(setString(m) == "101011", "mixed set %s, expected 101011 "
        "(volumes 0.9 0.1 0.8 0.2 0.7 0.3, 4 slots)", setString(m).c_str());
  CHECK(e.s.getActiveVoiceCount() == 4, "active voices %u",
        e.s.getActiveVoiceCount());
  CHECK(e.s.getVoiceCount() == 6, "playing voices %u", e.s.getVoiceCount());
  const double expected = kGain * 0.1 * (0.9 + 0.8 + 0.7 + 0.3);
  CHECK(std::fabs(out.back() - expected) < 1e-5,
        "output %g = the 4 loudest summed (%g)", out.back(), expected);
}

void protectedVoice() {
  Engine e(4);
  const float vols[] = {0.9f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f};
  std::vector<std::unique_ptr<CountSource>> srcs;
  std::vector<SoLoud::handle> h;
  for (int i = 0; i < 6; i++) {
    srcs.emplace_back(new CountSource(0.1f));
    h.push_back(e.s.play(*srcs.back(), vols[i]));
  }
  e.s.setProtectVoice(h[1], true);  // the quietest
  e.mix(2);
  std::vector<bool> m = mixedSet(e, srcs);
  CHECK(setString(m) == "111010", "with voice 1 protected: %s, expected 111010",
        setString(m).c_str());
  // Two protected voices.
  e.s.setProtectVoice(h[3], true);
  e.mix(1);
  m = mixedSet(e, srcs);
  CHECK(setString(m) == "111100",
        "protecting a playing, virtual voice applies at the next mix: %s, "
        "expected 111100", setString(m).c_str());
  // Any change that re-evaluates (here a volume set to its current value)
  // applies it.
  e.s.setVolume(h[0], 0.9f);
  e.mix(1);
  m = mixedSet(e, srcs);
  CHECK(setString(m) == "111100", "voices 1 and 3 protected after a "
        "re-evaluation: %s, expected 111100", setString(m).c_str());
  // More protected voices than slots: still 4 mixed, all protected.
  for (int i = 0; i < 6; i++) e.s.setProtectVoice(h[i], true);
  e.s.setVolume(h[0], 0.9f);  // re-evaluate
  e.mix(1);
  m = mixedSet(e, srcs);
  CHECK(std::count(m.begin(), m.end(), true) == 4, "6 protected, 4 slots: %s",
        setString(m).c_str());
  CHECK(e.s.getProtectVoice(h[1]), "getProtectVoice");
}

void reevaluate() {
  Engine e(4);
  const float vols[] = {0.9f, 0.1f, 0.8f, 0.2f, 0.7f, 0.3f};
  std::vector<std::unique_ptr<CountSource>> srcs;
  std::vector<SoLoud::handle> h;
  for (int i = 0; i < 6; i++) {
    srcs.emplace_back(new CountSource(0.1f));
    h.push_back(e.s.play(*srcs.back(), vols[i]));
  }
  e.mix(2);
  e.s.setVolume(h[1], 1.0f);  // the quietest becomes the loudest
  e.mix(1);
  std::vector<bool> m = mixedSet(e, srcs);
  CHECK(setString(m) == "111010", "after raising voice 1: %s, expected 111010",
        setString(m).c_str());
  e.s.stop(h[0]);
  e.mix(1);
  m = mixedSet(e, srcs);
  m.erase(m.begin());  // voice 0 stopped
  CHECK(setString(m) == "11011", "after stopping voice 0: %s, expected 11011",
        setString(m).c_str());
  e.s.setMaxActiveVoiceCount(16);
  e.mix(1);
  m = mixedSet(e, srcs);
  m.erase(m.begin());
  CHECK(setString(m) == "11111", "limit 16: all mixed (%s)", setString(m).c_str());
  // A fade crossing another voice's volume swaps them.
  e.s.setMaxActiveVoiceCount(4);
  e.s.fadeVolume(h[5], 1.0f, 0.05);  // 0.3 -> 1.0 over ~4 blocks
  e.mix(8);
  m = mixedSet(e, srcs);
  m.erase(m.begin());
  CHECK(m[4], "faded-up voice 5 is mixed (%s)", setString(m).c_str());
  CHECK(std::count(m.begin(), m.end(), true) == 4, "still 4 mixed (%s)",
        setString(m).c_str());
}

void resumePoint() {
  Engine e(4);
  CountSource ramp(0.f, true);
  std::vector<std::unique_ptr<CountSource>> loud;
  SoLoud::handle hv = e.s.play(ramp, 0.5f);
  for (int i = 0; i < 3; i++) {
    loud.emplace_back(new CountSource(0.0f));  // silent but loud
    e.s.play(*loud.back(), 1.0f);
  }
  std::vector<float> a = e.mix(10);
  const long long framesBefore = ramp.mFrames;
  const double posBefore = e.s.getStreamPosition(hv);
  // A fifth loud voice pushes the ramp out.
  CountSource extra(0.0f);
  SoLoud::handle hx = e.s.play(extra, 1.0f);
  std::vector<float> v = e.mix(20);
  const long long framesDuring = ramp.mFrames - framesBefore;
  // One block may still be mixed before the re-evaluation takes effect.
  CHECK(framesDuring <= (long long)kBlock, "virtual voice was read %lld frames",
        framesDuring);
  CHECK(std::fabs(v.back()) < 1e-7f, "virtual voice silent");
  const double posVirtual = e.s.getStreamPosition(hv);
  e.s.stop(hx);
  std::vector<float> b = e.mix(4);
  // The ramp continues where it was: the source position heard right after
  // the gap is the one it had reached (up to the resampler's 1-2 samples).
  const long long heard = positionAtStart(b);
  CHECK(heard >= framesBefore + framesDuring - 4 &&
            heard <= framesBefore + framesDuring + 4,
        "resumes at source frame %lld, it had produced %lld", heard,
        framesBefore + framesDuring);
  // Reported position vs heard position: the source is at mFrames.
  const double reported = e.s.getStreamPosition(hv);
  const double actual = (double)ramp.mFrames / kRate;
  CHECK(std::fabs(reported - actual) < 2.0 * kBlock / kRate,
        "getStreamPosition() reports %.4f s for a voice whose source is at "
        "%.4f s: it must not advance (%.4f -> %.4f s) while the voice is "
        "virtual and not read", reported, actual, posBefore, posVirtual);
}

void mustTick() {
  Engine e(4);
  std::vector<std::unique_ptr<CountSource>> srcs;
  std::vector<SoLoud::handle> h;
  const float vols[] = {0.9f, 0.8f, 0.7f, 0.6f, 0.05f};
  for (int i = 0; i < 5; i++) {
    srcs.emplace_back(new CountSource(0.1f));
    h.push_back(e.s.play(*srcs.back(), vols[i]));
  }
  e.s.setInaudibleBehavior(h[4], true, false);  // the quietest must tick
  e.mix(2);
  std::vector<bool> m = mixedSet(e, srcs);
  CHECK(setString(m) == "11101", "tick voice always mixed: %s, expected 11101",
        setString(m).c_str());
  // More tick voices than slots: 4 are mixed, the mixer keeps running.
  for (int i = 0; i < 5; i++) e.s.setInaudibleBehavior(h[i], true, false);
  std::vector<float> out;
  m = mixedSet(e, srcs, &out);
  CHECK(std::count(m.begin(), m.end(), true) == 4, "5 tick voices, 4 slots: %s",
        setString(m).c_str());
  CHECK(ctest::allFinite(out), "finite");
}

void inaudible3d() {
  for (int mode = 0; mode < 3; mode++) {  // 0 default, 1 tick, 2 kill
    Engine e(16);
    CountSource ramp(0.f, true);
    SoLoud::handle h = e.s.play3d(ramp, 0, 0, 0, 0, 0, 0, 1.0f);
    if (mode == 1) e.s.setInaudibleBehavior(h, true, false);
    if (mode == 2) e.s.setInaudibleBehavior(h, false, true);
    e.s.update3dAudio();
    e.mix(4);
    const long long before = ramp.mFrames;
    e.s.setVolume(h, 0.0005f);  // below the 0.001 threshold
    e.s.update3dAudio();
    std::vector<float> v = e.mix(20);
    const long long during = ramp.mFrames - before;
    if (mode == 2) {
      CHECK(!e.s.isValidVoiceHandle(h), "kill: inaudible voice stopped");
      continue;
    }
    CHECK(e.s.isValidVoiceHandle(h), "mode %d: voice alive", mode);
    if (mode == 0)
      CHECK(during == 0, "default: inaudible voice frozen (read %lld frames)",
            during);
    else
      CHECK(during == 20 * (long long)kBlock,
            "tick: inaudible voice advanced %lld frames, expected %u", during,
            20 * kBlock);
    CHECK(ctest::peak(v.data(), v.size()) < 1e-6f, "mode %d: inaudible voice "
          "not heard (peak %g)", mode, ctest::peak(v.data(), v.size()));
    e.s.setVolume(h, 1.0f);
    e.s.update3dAudio();
    std::vector<float> b = e.mix(2);
    const long long heard = positionAtStart(b);
    const long long expectAt = mode == 0 ? before : before + during;
    CHECK(heard >= expectAt - 4 && heard <= expectAt + 4,
          "mode %d: resumes at source frame %lld, expected ~%lld", mode, heard,
          expectAt);
  }
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("selection")) selection();
  if (ctest::selected("protected")) protectedVoice();
  if (ctest::selected("reevaluate")) reevaluate();
  if (ctest::selected("resume_point")) resumePoint();
  if (ctest::selected("must_tick")) mustTick();
  if (ctest::selected("inaudible_3d")) inaudible3d();
  return ctest::summary("voice_virtualization_test");
}

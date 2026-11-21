// Standalone native tests for the ADSR envelope (src/synth/soloud_adsr.h)
// and its use in Basicwave (src/synth/basic_wave.cpp).
//
// ADSR::val(t, relTime) takes seconds: a linear attack 0 -> 1 over mA, a
// linear decay 1 -> mS over mD, mS for relTime seconds, then a linear
// release mS -> 0 over mR. The cases:
//
//   segments     every segment follows its line, sampled at 44.1 kHz; the
//                attack lasts ceil(mA * sr) samples, the release ends at
//                A + D + relTime + R;
//   sustain      the sustain level for mS in {0, 0.25, 0.5, 1};
//   zero_times   mA, mD, mR, relTime = 0: no division by zero, the segment
//                is skipped;
//   continuity   no jump at any segment boundary;
//   range        random parameters (times 0..2 s, including 0 and tiny
//                values, mS in 0..1) and random times: always finite and in
//                [0, 1];
//   early_release  release requested before the attack ends (negative
//                relTime): informational only, see the NOTE it prints;
//   basicwave    a Basicwave with a custom ADSR, mixed by the engine (null
//                backend): sample by sample the output is the envelope times
//                the same wave rendered with the default (flat) envelope.
//
// No audio device and no assets are used.
//
//   ./test/common/adsr_test.sh [--asan] [--strict] [case]

#include "soloud.h"
#include "synth/basic_wave.h"
#include "synth/soloud_adsr.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr double kSr = 44100.0;

/// The ADSR's definition, written independently of the implementation.
double expectedEnv(double a, double d, double s, double r, double t,
                   double rel) {
  if (t < a) return t / a;
  if (t < a + d) return 1.0 - (t - a) / d * (1.0 - s);
  if (t < a + d + rel) return s;
  const double tr = t - a - d - rel;
  if (tr >= r) return 0.0;
  return s * (1.0 - tr / r);
}

void segments() {
  const double A = 0.01, D = 0.02, S = 0.5, R = 0.03, H = 0.015;
  ADSR env(A, D, S, R);
  const int total = (int)((A + D + H + R) * kSr) + 200;
  double maxErr = 0;
  int attackSamples = 0;
  int lastNonZero = -1;
  for (int n = 0; n < total; n++) {
    const double t = n / kSr;
    const double v = env.val(t, H);
    maxErr = std::max(maxErr, std::fabs(v - expectedEnv(A, D, S, R, t, H)));
    if (t < A) attackSamples++;
    if (v > 0) lastNonZero = n;
  }
  CHECK(maxErr < 1e-12, "max deviation from the ADSR lines %g", maxErr);
  // Attack: samples 0 .. ceil(A*sr)-1 are on the ramp; 441 at 44.1 kHz.
  CHECK(attackSamples == (int)std::ceil(A * kSr), "attack lasts %d samples",
        attackSamples);
  CHECK(env.val(0.0, H) == 0.0, "envelope starts at 0, got %g",
        env.val(0.0, H));
  CHECK(std::fabs(env.val(A, H) - 1.0) < 1e-12, "peak 1 at t = A, got %g",
        env.val(A, H));
  CHECK(std::fabs(env.val(A + D, H) - S) < 1e-12, "sustain at t = A + D");
  // Release ends at A + D + H + R: the last non-zero sample is just before.
  const int endSample = (int)std::ceil((A + D + H + R) * kSr);
  CHECK(lastNonZero >= endSample - 2 && lastNonZero < endSample + 1,
        "last non-zero sample %d, release ends at sample %d", lastNonZero,
        endSample);
  // Halfway through the release the level is S / 2.
  CHECK(std::fabs(env.val(A + D + H + R / 2, H) - S / 2) < 1e-12,
        "half release level %g", env.val(A + D + H + R / 2, H));
  // Monotonic: rising in attack, falling in decay and release.
  bool mono = true;
  double prev = -1;
  for (int n = 0; n < (int)(A * kSr); n++) {
    double v = env.val(n / kSr, H);
    if (v < prev) mono = false;
    prev = v;
  }
  for (int n = (int)(A * kSr) + 1; n < total; n++) {
    double v = env.val(n / kSr, H), p = env.val((n - 1) / kSr, H);
    if (v > p + 1e-15) mono = false;
  }
  CHECK(mono, "attack rises and the rest never rises");
}

void sustain() {
  for (double S : {0.0, 0.25, 0.5, 1.0}) {
    ADSR env(0.005, 0.005, S, 0.01);
    bool ok = true;
    for (int n = (int)(0.0101 * kSr); n < (int)(0.5 * kSr); n++)
      if (std::fabs(env.val(n / kSr, 1.0) - S) > 1e-12) ok = false;
    CHECK(ok, "sustain holds at %g", S);
    CHECK(env.val(1.0 + 0.01 + 0.0101, 1.0) == 0.0, "after release 0 (S %g)",
          S);
  }
}

void zeroTimes() {
  // All zero: no attack/decay; with relTime 0 and R 0 the envelope is 0.
  {
    ADSR env(0, 0, 0.7, 0);
    CHECK(env.val(0.0, 0.0) == 0.0, "A=D=R=rel=0 -> 0, got %g",
          env.val(0.0, 0.0));
    CHECK(env.val(0.0, 1.0) == 0.7, "A=D=0: sustain from t = 0, got %g",
          env.val(0.0, 1.0));
    CHECK(env.val(1.0, 1.0) == 0.0, "R=0: drops to 0 at release");
  }
  // Default ADSR (the plugin's): flat 1 with the plugin's huge relTime.
  {
    ADSR env;
    bool ok = true;
    for (double t : {0.0, 1e-9, 0.5, 100.0, 1e6, 1e9})
      if (env.val(t, 10000000000000.0) != 1.0) ok = false;
    CHECK(ok, "default ADSR is flat 1");
  }
  // A = 0 with D > 0: starts at the peak.
  {
    ADSR env(0, 0.01, 0.5, 0.01);
    CHECK(env.val(0.0, 1.0) == 1.0, "A=0 starts at 1, got %g",
          env.val(0.0, 1.0));
  }
  // D = 0: attack straight into sustain.
  {
    ADSR env(0.01, 0, 0.3, 0.01);
    CHECK(std::fabs(env.val(0.01 - 1e-9, 1.0) - 1.0) < 1e-6 &&
              env.val(0.01, 1.0) == 0.3,
          "D=0: 1 -> S at t = A");
  }
  // Every combination of zero / non-zero times: finite.
  bool finite = true;
  for (int mask = 0; mask < 16; mask++) {
    ADSR env(mask & 1 ? 0.01 : 0, mask & 2 ? 0.01 : 0, 0.5,
             mask & 4 ? 0.01 : 0);
    const double rel = mask & 8 ? 0.01 : 0;
    for (int n = 0; n < 2000; n++) {
      double v = env.val(n / kSr, rel);
      if (!std::isfinite(v) || v < 0 || v > 1) finite = false;
    }
  }
  CHECK(finite, "zero times never give NaN/Inf or leave [0, 1]");
}

void continuity() {
  const double A = 0.013, D = 0.007, S = 0.4, R = 0.021, H = 0.05;
  ADSR env(A, D, S, R);
  const double eps = 1e-9;
  for (double b : {A, A + D, A + D + H, A + D + H + R}) {
    const double lo = env.val(b - eps, H), hi = env.val(b + eps, H);
    // Steepest slope is 1/A ~ 77 per second: 2e-9 s moves < 2e-7.
    CHECK(std::fabs(lo - hi) < 1e-6, "jump %g at boundary t = %g", hi - lo,
          b);
  }
}

void range() {
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  auto time = [&]() {
    const double x = u(rng);
    if (x < 0.15) return 0.0;
    if (x < 0.25) return 1e-300 * u(rng);  // subnormal-ish divisors
    if (x < 0.35) return 1e-9 * u(rng);
    return 2.0 * u(rng);
  };
  int bad = 0;
  double worst = 0;
  for (int i = 0; i < 20000; i++) {
    ADSR env(time(), time(), u(rng), time());
    const double rel = time();
    for (int k = 0; k < 20; k++) {
      double t = u(rng) < 0.1 ? time() * 1e6 : u(rng) * 8.0;
      double v = env.val(t, rel);
      if (!std::isfinite(v) || v < 0.0 || v > 1.0) {
        if (bad < 3)
          std::printf("    val(%g, %g) = %g with A %g D %g S %g R %g\n", t,
                      rel, v, env.mA, env.mD, env.mS, env.mR);
        bad++;
        worst = v;
      }
    }
  }
  CHECK(bad == 0, "%d values outside [0, 1] or not finite (e.g. %g)", bad,
        worst);
}

void earlyRelease() {
  // ADSR cannot express a release that starts during the attack: relTime is
  // measured from the end of the decay. A negative relTime (release asked at
  // t = A + D + relTime < A) keeps ramping up until t = A, then jumps into
  // the release from S. The plugin never releases (it passes 1e13), so this
  // is reported as information only.
  const double A = 0.02, D = 0.01, S = 0.5, R = 0.02;
  ADSR env(A, D, S, R);
  const double rel = -(A + D) + 0.005;  // release asked at t = 5 ms
  const double vRel = env.val(0.005, rel);
  const double vLater = env.val(0.015, rel);
  bool finite = true;
  for (int n = 0; n < 4000; n++) {
    double v = env.val(n / kSr, rel);
    if (!std::isfinite(v) || v < 0 || v > 1) finite = false;
  }
  CHECK(finite, "negative relTime stays finite and in [0, 1]");
  if (vLater > vRel)
    NOTE("release asked during the attack (t = 5 ms, level %.3f): the "
         "envelope keeps rising (%.3f at 15 ms); ADSR has no "
         "release-from-current-level",
         vRel, vLater);
}

/// Renders [frames] samples of a Basicwave through the engine (mono, no
/// resampling, no clipping).
std::vector<float> renderBasicwave(const ADSR &adsr, unsigned int frames) {
  SoLoud::Soloud soloud;
  soloud.init(0, SoLoud::Soloud::NULLDRIVER, (unsigned int)kSr, 512, 1);
  Basicwave wave(SoLoud::Soloud::WAVE_SQUARE, false, 0.25, 1.0);
  wave.setFreq(441.0);
  wave.mADSR = adsr;
  soloud.play(wave, 1.0f);
  std::vector<float> out(frames);
  for (unsigned int pos = 0; pos < frames; pos += 512)
    soloud.mix(out.data() + pos, std::min(512u, frames - pos));
  soloud.deinit();
  return out;
}

void basicwave() {
  const unsigned int frames = 44100 / 4;
  const double A = 0.02, D = 0.03, S = 0.25;
  std::vector<float> ref = renderBasicwave(ADSR(), frames);
  std::vector<float> out = renderBasicwave(ADSR(A, D, S, 0.1), frames);
  CHECK(ctest::allFinite(out), "output finite");
  // The mixer may delay the source by a sample or two (resampler history);
  // both renders share the delay, so find it and compare at that offset.
  double maxErr = 1e9;
  int worstN = -1, bestDelay = 0;
  for (int delay = 0; delay <= 4; delay++) {
    double err = 0;
    int wn = -1;
    for (unsigned int n = delay; n < frames; n++) {
      if (std::fabs(ref[n]) < 1e-3f) continue;
      const double env = out[n] / ref[n];
      // Basicwave evaluates the envelope at mT = k / sr for source sample k.
      const double e = expectedEnv(A, D, S, 0.1, (n - delay) / kSr, 1e13);
      if (std::fabs(env - e) > err) {
        err = std::fabs(env - e);
        wn = (int)n;
      }
    }
    if (err < maxErr) {
      maxErr = err;
      worstN = wn;
      bestDelay = delay;
    }
  }
  int attackSamples = -1;
  for (unsigned int n = 0; n < frames; n++) {
    if (std::fabs(ref[n]) < 1e-3f) continue;
    if (attackSamples < 0 && out[n] / ref[n] >= 1.0 - 1e-6) {
      attackSamples = (int)n - bestDelay;
      break;
    }
  }
  std::printf("    mixer delay %d samples\n", bestDelay);
  // float output of a double envelope: ~1e-7 relative, plus the accumulated
  // mT error (frames * 1 ulp of 1/sr, far below 1e-6).
  CHECK(maxErr < 1e-5, "envelope ratio deviates %g at sample %d", maxErr,
        worstN);
  CHECK(attackSamples == (int)std::ceil(A * kSr),
        "Basicwave attack reaches 1 at source sample %d (expected %d)",
        attackSamples, (int)std::ceil(A * kSr));
  // Sustain region: |out| = S * |ref|.
  const unsigned int s0 = (unsigned int)((A + D) * kSr) + 10;
  CHECK(std::fabs(ctest::rms(out.data() + s0, 4410) /
                      ctest::rms(ref.data() + s0, 4410) -
                  S) < 1e-5,
        "sustain level in the mix");
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("segments")) segments();
  if (ctest::selected("sustain")) sustain();
  if (ctest::selected("zero_times")) zeroTimes();
  if (ctest::selected("continuity")) continuity();
  if (ctest::selected("range")) range();
  if (ctest::selected("early_release")) earlyRelease();
  if (ctest::selected("basicwave")) basicwave();
  return ctest::summary("adsr_test");
}

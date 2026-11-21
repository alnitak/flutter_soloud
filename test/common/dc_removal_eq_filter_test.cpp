// Standalone native tests for SoLoud's DCRemovalFilter
// (src/soloud/src/filter/soloud_dcremovalfilter.cpp) and EqFilter
// (soloud_eqfilter.cpp), and for the FFTFilter framing they share with the
// plugin's Bassboost filter (soloud_fftfilter.cpp).
//
// DCRemovalFilter subtracts the moving average of the last L = ceil(length
// * rate) input samples (current one included), scaled by wet. So for a DC
// step of height a the output is a * (1 - (n + 1) / L) for n < L, then 0,
// and a tone at f is scaled by |1 - H(f)| with |H(f)| = |sin(pi f L / sr) /
// (L sin(pi f / sr))|.
//
// EqFilter and Bassboost run an STFT (FFTFilterInstance, N = 256, hop 128,
// triangular synthesis window, which sums to 1). With every gain at 1 (EQ)
// or a band far from the bass (Bassboost) the filter must give its input
// back, delayed.
//
//   dc_step        exact step response against the moving-average model;
//   dc_tone        DC removed from DC + tone, the tone kept within |1 - H|;
//   dc_params      length -> L (and setParams refusing <= 0), wet 0 and 0.5,
//                  channels independent, a rate change;
//   dc_long        10 minutes of DC + noise: no drift of the running sum;
//   dc_nan         one NaN sample (informational: see NOTE);
//   eq_transparent all 8 bands at 1: output == input delayed;
//   eq_limits      setParam refuses bad bands/values; bands at 0 and 4 and
//                  random in-range gains give finite, bounded output; wet 0
//                  is exact bypass;
//   bassboost      boosting the bass leaves 1 kHz and 22 kHz alone and
//                  adds no mirror image at sr/2 - f.
//
// No audio device and no assets are used.
//
//   ./test/common/dc_removal_eq_filter_test.sh [--asan] [--strict] [case]

#include "soloud.h"
#include "soloud_bassboostfilter.h"
#include "soloud_dcremovalfilter.h"
#include "soloud_eqfilter.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace {

constexpr float kSr = 44100.f;
constexpr unsigned int kBlock = 512;

/// Planar processing of [channels] equal-length channels, block by block.
std::vector<std::vector<float>> run(SoLoud::FilterInstance *inst,
                                    std::vector<std::vector<float>> in,
                                    float sr = kSr) {
  const unsigned int ch = (unsigned int)in.size();
  const size_t n = in[0].size();
  std::vector<float> buf(kBlock * ch);
  for (size_t pos = 0; pos < n; pos += kBlock) {
    const unsigned int k = (unsigned int)std::min<size_t>(kBlock, n - pos);
    for (unsigned int c = 0; c < ch; c++)
      std::memcpy(&buf[c * kBlock], &in[c][pos], k * sizeof(float));
    inst->filter(buf.data(), k, kBlock, ch, sr, pos / (double)sr);
    for (unsigned int c = 0; c < ch; c++)
      std::memcpy(&in[c][pos], &buf[c * kBlock], k * sizeof(float));
  }
  return in;
}
std::vector<float> run1(SoLoud::FilterInstance *inst, std::vector<float> in,
                        float sr = kSr) {
  return run(inst, {std::move(in)}, sr)[0];
}

std::vector<float> sine(double f, float a, size_t n, float dc = 0.f) {
  std::vector<float> s(n);
  for (size_t i = 0; i < n; i++)
    s[i] = dc + a * (float)std::sin(2 * M_PI * f * i / kSr);
  return s;
}

double mean(const float *p, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; i++) s += p[i];
  return s / n;
}

std::unique_ptr<SoLoud::FilterInstance> dcInstance(float length) {
  static std::vector<std::unique_ptr<SoLoud::DCRemovalFilter>> keep;
  keep.emplace_back(new SoLoud::DCRemovalFilter());
  keep.back()->setParams(length);
  return std::unique_ptr<SoLoud::FilterInstance>(keep.back()->createInstance());
}

void dcStep() {
  for (float length : {0.01f, 0.1f, 0.0123f}) {
    auto inst = dcInstance(length);
    const int L = (int)std::ceil(length * kSr);
    std::vector<float> in(L * 3, 0.7f);
    std::vector<float> out = run1(inst.get(), in);
    double err = 0;
    for (int n = 0; n < (int)in.size(); n++) {
      const double e = n < L ? 0.7 * (1.0 - (n + 1.0) / L) : 0.0;
      err = std::max(err, std::fabs(out[n] - e));
    }
    // The running float sum (up to 0.7 * L) gains up to half an ulp per
    // update, i.e. 0.7 * L * 2^-24 each; over L updates, divided by L:
    // 0.7 * L * 2^-24.
    const double tol = 1e-6 + 0.7 * L * std::ldexp(1.0, -24);
    CHECK(err < tol, "length %g (L %d): step response off by %g (tol %g)", length,
          L, err, tol);
  }
}

void dcTone() {
  const float length = 0.1f;
  const int L = (int)std::ceil(length * kSr);
  for (double f : {50.0, 440.0, 5000.0}) {
    auto inst = dcInstance(length);
    std::vector<float> out = run1(inst.get(), sine(f, 0.25f, (size_t)kSr * 2, 0.5f));
    const size_t s = L * 2, n = (size_t)kSr;
    const double m = mean(out.data() + s, n);
    // The moving average of the tone itself: its mean over 1 s is
    // at most 0.25 * |H(f)|.
    const double H = std::fabs(std::sin(M_PI * f * L / kSr) /
                               (L * std::sin(M_PI * f / kSr)));
    CHECK(std::fabs(m) < 1e-4 + 0.25 * H, "%g Hz: DC left %g", f, m);
    const double a = ctest::toneAmplitude(out.data() + s, n, f, kSr);
    CHECK(std::fabs(a - 0.25) <= 0.25 * H + 1e-3,
          "%g Hz: tone amplitude %g, expected 0.25 +- %g", f, a, 0.25 * H);
  }
}

void dcParams() {
  {
    SoLoud::DCRemovalFilter f;
    CHECK(f.setParams(0.f) == SoLoud::INVALID_PARAMETER, "length 0 refused");
    CHECK(f.setParams(-1.f) == SoLoud::INVALID_PARAMETER, "negative refused");
    CHECK(f.mLength == 0.1f, "length unchanged: %g", f.mLength);
  }
  // Tiny length: L = 1: output = in - in = 0.
  {
    auto inst = dcInstance(1e-9f);
    std::vector<float> out = run1(inst.get(), sine(440, 0.5f, 8192, 0.2f));
    CHECK(ctest::peak(out.data(), out.size()) < 1e-6f,
          "L = 1 removes everything: peak %g", ctest::peak(out.data(), out.size()));
  }
  // wet 0: identity; wet 0.5: half the DC removed.
  {
    auto inst = dcInstance(0.01f);
    inst->setFilterParameter(0, 0.0f);
    std::vector<float> in = sine(440, 0.3f, 8192, 0.4f);
    std::vector<float> out = run1(inst.get(), in);
    CHECK(out == in, "wet 0 is bypass");
    auto inst2 = dcInstance(0.01f);
    inst2->setFilterParameter(0, 0.5f);
    std::vector<float> out2 = run1(inst2.get(), std::vector<float>(8192, 0.4f));
    CHECK(std::fabs(out2.back() - 0.2f) < 1e-5, "wet 0.5 leaves half: %g",
          out2.back());
  }
  // Channels are independent.
  {
    auto inst = dcInstance(0.01f);
    auto out = run(inst.get(), {std::vector<float>(8192, 0.4f),
                                std::vector<float>(8192, -0.25f),
                                sine(300, 0.5f, 8192)});
    CHECK(std::fabs(out[0].back()) < 1e-5 && std::fabs(out[1].back()) < 1e-5,
          "per-channel DC removed: %g %g", out[0].back(), out[1].back());
    CHECK(std::fabs(ctest::toneAmplitude(out[2].data() + 4096, 4096, 300, kSr) -
                    0.5) < 0.02,
          "third channel's tone kept");
  }
  // A rate change rebuilds the window (L = ceil(0.01 * 48000) = 480).
  {
    auto inst = dcInstance(0.01f);
    run1(inst.get(), std::vector<float>(4096, 0.3f));
    std::vector<float> out = run1(inst.get(), std::vector<float>(1024, 0.6f), 48000.f);
    CHECK(std::fabs(out[0] - 0.6f * (1 - 1.0f / 480)) < 1e-5 &&
              std::fabs(out[479]) < 1e-5,
          "48 kHz window restarts with L 480: %g, %g", out[0], out[479]);
  }
}

void dcLong() {
  auto inst = dcInstance(0.1f);
  std::mt19937 rng(5);
  std::uniform_real_distribution<float> u(-0.6f, 0.6f);
  std::vector<float> buf(kBlock);
  const size_t total = (size_t)kSr * 600;
  double lastSum = 0;
  size_t lastN = 0;
  bool finite = true;
  for (size_t pos = 0; pos < total; pos += kBlock) {
    for (auto &x : buf) x = 0.3f + u(rng);
    inst->filter(buf.data(), kBlock, kBlock, 1, kSr, 0);
    if (!ctest::allFinite(buf)) finite = false;
    if (pos >= total - (size_t)kSr) {
      for (float x : buf) lastSum += x;
      lastN += kBlock;
    }
  }
  CHECK(finite, "finite over 10 minutes");
  // Mean of zero-mean noise minus its moving average over 1 s: ~0.35 /
  // sqrt(44100) ~ 2e-3 at 1 sigma; drift of the running sum would add to it.
  CHECK(std::fabs(lastSum / lastN) < 0.01, "DC after 10 minutes: %g",
        lastSum / lastN);
}

void dcNan() {
  auto inst = dcInstance(0.01f);
  std::vector<float> in(4096, 0.1f);
  in[100] = NAN;
  std::vector<float> out = run1(inst.get(), in);
  const bool recovered = std::isfinite(out.back());
  CHECK(!std::isfinite(out[100]), "the NaN sample itself is NaN");
  if (!recovered)
    NOTE("one NaN input sample leaves DCRemovalFilter's running sum NaN "
         "forever (soloud_dcremovalfilter.cpp:80): all later output is NaN. "
         "The plugin has no FilterType for this filter.");
}

/// Output vs input delayed: returns the best delay's max error.
double delayedError(const std::vector<float> &in, const std::vector<float> &out,
                    int maxDelay, int *bestDelay) {
  double best = 1e9;
  for (int d = 0; d <= maxDelay; d++) {
    double e = 0;
    for (size_t i = 2048; i < in.size(); i++)
      e = std::max(e, (double)std::fabs(out[i] - in[i - d]));
    if (e < best) {
      best = e;
      *bestDelay = d;
    }
  }
  return best;
}

void eqTransparent() {
  for (double f : {100.0, 1000.0, 5000.0, 15000.0}) {
    SoLoud::EqFilter eq;
    std::unique_ptr<SoLoud::FilterInstance> inst(eq.createInstance());
    std::vector<float> in = sine(f, 0.5f, 22050);
    std::vector<float> out = run1(inst.get(), in);
    int d = 0;
    const double err = delayedError(in, out, 600, &d);
    const double a = ctest::toneAmplitude(out.data() + 4096, 17640, f, kSr);
    CHECK(err < 1e-3,
          "EqFilter with all bands at 1 is not transparent at %g Hz: "
          "amplitude %.3f for 0.5 in, max error %.3f vs the input delayed "
          "by %d", f, a, err, d);
  }
}

void eqLimits() {
  SoLoud::EqFilter eq;
  CHECK(eq.setParam(0, 1.f) == SoLoud::INVALID_PARAMETER, "band 0 (wet) refused");
  CHECK(eq.setParam(9, 1.f) == SoLoud::INVALID_PARAMETER, "band 9 refused");
  CHECK(eq.setParam(1, -0.1f) == SoLoud::INVALID_PARAMETER, "gain < 0 refused");
  CHECK(eq.setParam(8, 4.1f) == SoLoud::INVALID_PARAMETER, "gain > 4 refused");
  CHECK(eq.setParam(8, 4.0f) == SoLoud::SO_NO_ERROR, "gain 4 accepted");
  CHECK(eq.setParam(1, 0.0f) == SoLoud::SO_NO_ERROR, "gain 0 accepted");
  CHECK(eq.getParamCount() == 9, "9 params");

  std::mt19937 rng(9);
  std::uniform_real_distribution<float> u(-1.f, 1.f), g(0.f, 4.f);
  std::vector<float> noise((size_t)kSr);
  for (auto &x : noise) x = u(rng);
  for (int trial = 0; trial < 12; trial++) {
    SoLoud::EqFilter e;
    std::unique_ptr<SoLoud::FilterInstance> inst(e.createInstance());
    for (unsigned int b = 1; b <= 8; b++)
      inst->setFilterParameter(b, trial == 0 ? 0.f : trial == 1 ? 4.f : g(rng));
    std::vector<float> out = run1(inst.get(), noise);
    // Each bin is scaled by at most 2 * 4 (catmull-rom of values in [0, 4]
    // overshoots by at most 1/8 of the range): bounded by 9 * the input sum
    // of a window of 256 samples, far below 256 * 9.
    CHECK(ctest::allFinite(out), "trial %d finite", trial);
    CHECK(ctest::peak(out.data(), out.size()) < 256.f * 9.f, "trial %d bounded (%g)",
          trial, ctest::peak(out.data(), out.size()));
  }
  // wet 0: exact bypass (the FFT path still runs, its result is unused).
  SoLoud::EqFilter e;
  std::unique_ptr<SoLoud::FilterInstance> inst(e.createInstance());
  inst->setFilterParameter(0, 0.f);
  inst->setFilterParameter(3, 4.f);
  std::vector<float> out = run1(inst.get(), noise);
  CHECK(out == noise, "wet 0 is bypass");
}

void bassboost() {
  SoLoud::BassboostFilter bb;
  auto measure = [&](double f, double probe) {
    std::unique_ptr<SoLoud::FilterInstance> inst(bb.createInstance());
    inst->setFilterParameter(SoLoud::BassboostFilter::BOOST, 4.0f);
    std::vector<float> out = run1(inst.get(), sine(f, 0.5f, 44100));
    return ctest::toneAmplitude(out.data() + 4410, 39690, probe, kSr);
  };
  const double low = measure(60, 60);
  CHECK(low > 1.0, "60 Hz boosted: %g for 0.5 in", low);
  const double mid = measure(1000, 1000);
  CHECK(std::fabs(mid - 0.5) < 0.01, "1 kHz untouched: %g", mid);
  // 22 kHz is no bass: it must come out as it went in.
  const double nyq = measure(22000, 22000);
  CHECK(std::fabs(nyq - 0.5) < 0.05,
        "Bassboost boosts a 22 kHz tone like the bass: %.3f for 0.5 in", nyq);
  // A 172 Hz tone (one FFT bin) must not create a tone at sr/2 - 172 Hz.
  const double image = measure(172.27, 22050 - 172.27);
  CHECK(image < 0.01,
        "Bassboost on a 172 Hz tone creates a %.3f amplitude image at "
        "21878 Hz (0.5 in)", image);
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("dc_step")) dcStep();
  if (ctest::selected("dc_tone")) dcTone();
  if (ctest::selected("dc_params")) dcParams();
  if (ctest::selected("dc_long")) dcLong();
  if (ctest::selected("dc_nan")) dcNan();
  if (ctest::selected("eq_transparent")) eqTransparent();
  if (ctest::selected("eq_limits")) eqLimits();
  if (ctest::selected("bassboost")) bassboost();
  return ctest::summary("dc_removal_eq_filter_test");
}

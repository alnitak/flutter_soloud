// Standalone native tests for the output quality of SoLoud's three
// resamplers (point, linear, Catmull-Rom; Soloud::mixBus_internal and the
// resample_* functions in src/soloud/src/core/soloud.cpp), mixed through the
// engine with the null backend.
//
// A sine is resampled with ratios 0.25..4 (play speed, or a source rate
// different from the engine's). The output must be the same sine at the
// resampled frequency up to the resampler's own interpolation error. That
// error has an exact worst case: interpolating e^(i w t) (w in radians per
// source sample) at fraction u in [0, 1) is off by
//
//   K(w) = max_u | e^(i w u) - interp(u) |,
//
// with interp the resampler's kernel on the neighbouring samples (point:
// the sample at or before; linear: the two around; Catmull-Rom: the four
// around). Every output sample of a sine of amplitude A is therefore within
// A * K(w) of the ideal one. The test fits a sinusoid (least squares, so the
// residual can grow up to ~3x a per-sample error) and checks:
//
//   - every residual sample <= 3 * A * K(w) + float noise: also catches a
//     click (a skipped or repeated sample at a 512-sample block edge costs
//     ~A * w, far above K(w) for linear and Catmull-Rom);
//   - the fitted amplitude is A (times the engine's fixed output gain).
//
// The positions follow the engine's 20-bit fixed-point step,
// floor(rate * speed / engineRate * 2^20) / 2^20, so the ideal frequency is
// exact.
//
//   constant   each resampler, ratios 0.25, 0.5, 0.9, 1, 1.37, 2, 4 by
//              speed, and 0.25 and 4 by source rate, at 440 Hz and 5 kHz;
//   speed_ramp the speed changes before every mixed block (a ramp 0.5 -> 2
//              and random speeds in 0.25..4): the output follows the phase
//              accumulated block by block, without clicks.
//
// No audio device and no assets are used.
//
//   ./test/common/resampler_quality_test.sh [--asan] [--strict] [case]

#include "soloud.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <random>
#include <vector>

namespace {

constexpr unsigned int kRate = 44100;
constexpr unsigned int kBlock = 512;
constexpr double kAmp = 0.5;
constexpr int kFracBits = 20;

const char *kResName[] = {"point", "linear", "catmullrom"};
double gWorstRatio[3] = {0, 0, 0};  // largest deviation / bound, per resampler

class SineInstance : public SoLoud::AudioSourceInstance {
 public:
  SineInstance(double w) : mW(w) {}
  unsigned int getAudio(float *buf, unsigned int n, unsigned int) override {
    for (unsigned int i = 0; i < n; i++, mN++)
      buf[i] = (float)(kAmp * std::sin(mW * (double)mN));
    return n;
  }
  bool hasEnded() override { return false; }
  double mW;
  long long mN = 0;
};

class SineSource : public SoLoud::AudioSource {
 public:
  SineSource(double freq, float rate) : mW(2 * M_PI * freq / rate) {
    mChannels = 1;
    mBaseSamplerate = rate;
  }
  ~SineSource() override { stop(); }
  SoLoud::AudioSourceInstance *createInstance() override {
    return new SineInstance(mW);
  }
  double mW;  // radians per source sample
};

/// Worst-case interpolation error of e^(i w t) for [resampler].
double kernelError(int resampler, double w) {
  using cd = std::complex<double>;
  auto x = [&](double t) { return std::polar(1.0, w * t); };
  double worst = 0;
  for (int k = 0; k < 2000; k++) {
    const double u = k / 2000.0;
    cd v;
    if (resampler == SoLoud::Soloud::RESAMPLER_POINT) {
      v = x(0);
    } else if (resampler == SoLoud::Soloud::RESAMPLER_LINEAR) {
      v = x(0) * (1 - u) + x(1) * u;
    } else {
      const cd p0 = x(-1), p1 = x(0), p2 = x(1), p3 = x(2);
      v = 0.5 * (2.0 * p1 + (-p0 + p2) * u +
                 (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u * u +
                 (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u * u * u);
    }
    worst = std::max(worst, std::abs(x(u) - v));
  }
  return worst;
}

/// The engine's fixed-point step for a voice at [rate] * [speed].
double fixedStep(float rate, float speed) {
  const float voiceRate = rate * (1.0f * speed);
  const float step = voiceRate / (float)kRate;
  return std::floor(step * (1 << kFracBits)) / (double)(1 << kFracBits);
}

struct Fit {
  double amp = 0, maxResidual = 0;
};

/// Least-squares fit of a*sin(theta) + b*cos(theta) + c to y over [from, n).
Fit fitSine(const std::vector<float> &y, const std::vector<double> &theta,
            size_t from) {
  double m[3][3] = {}, r[3] = {};
  for (size_t n = from; n < y.size(); n++) {
    const double f[3] = {std::sin(theta[n]), std::cos(theta[n]), 1.0};
    for (int i = 0; i < 3; i++) {
      r[i] += f[i] * y[n];
      for (int j = 0; j < 3; j++) m[i][j] += f[i] * f[j];
    }
  }
  // Solve 3x3 (Gaussian elimination).
  for (int i = 0; i < 3; i++) {
    int p = i;
    for (int k = i + 1; k < 3; k++)
      if (std::fabs(m[k][i]) > std::fabs(m[p][i])) p = k;
    std::swap(m[i], m[p]);
    std::swap(r[i], r[p]);
    for (int k = i + 1; k < 3; k++) {
      const double f = m[k][i] / m[i][i];
      for (int j = i; j < 3; j++) m[k][j] -= f * m[i][j];
      r[k] -= f * r[i];
    }
  }
  double c[3];
  for (int i = 2; i >= 0; i--) {
    double s = r[i];
    for (int j = i + 1; j < 3; j++) s -= m[i][j] * c[j];
    c[i] = s / m[i][i];
  }
  Fit fit;
  fit.amp = std::hypot(c[0], c[1]);
  for (size_t n = from; n < y.size(); n++) {
    const double e = y[n] - (c[0] * std::sin(theta[n]) +
                             c[1] * std::cos(theta[n]) + c[2]);
    fit.maxResidual = std::max(fit.maxResidual, std::fabs(e));
  }
  return fit;
}

/// Mixes [blocks] blocks of a sine voice; [speedAt] gives the speed of each
/// block. Returns the output and fills [pos] with each output sample's
/// source position (in source samples, before any resampler delay).
std::vector<float> render(int resampler, double freq, float srcRate,
                          int blocks, const std::function<float(int)> &speedAt,
                          std::vector<double> &pos, double &gain) {
  SoLoud::Soloud s;
  s.init(0, SoLoud::Soloud::NULLDRIVER, kRate, kBlock, 1);
  s.setMainResampler(resampler);
  SineSource src(freq, srcRate);
  SoLoud::handle h = s.play(src, 1.0f, 0.0f, true);
  std::vector<float> out((size_t)blocks * kBlock);
  pos.assign(out.size(), 0.0);
  double p = 0;
  for (int b = 0; b < blocks; b++) {
    const float sp = speedAt(b);
    s.setRelativePlaySpeed(h, sp);
    if (b == 0) s.setPause(h, false);
    const double step = fixedStep(srcRate, sp);
    for (unsigned int i = 0; i < kBlock; i++) {
      pos[(size_t)b * kBlock + i] = p;
      p += step;
    }
    s.mix(out.data() + (size_t)b * kBlock, kBlock);
  }
  // Engine output gain for a centred mono voice: post-clip scaler 0.95 times
  // the pan law's cos(pi/4).
  gain = 0.95 * std::cos(M_PI / 4);
  s.deinit();
  return out;
}

/// Fits the output against the ideal sine read at pos - D for the resampler
/// delay D in {0..3} source samples, and checks the best fit.
void check(const char *label, int resampler, double w, double gain,
           const std::vector<float> &out, const std::vector<double> &pos) {
  Fit best;
  int bestD = -1;
  for (int D = 0; D <= 3; D++) {
    std::vector<double> theta(out.size());
    for (size_t n = 0; n < out.size(); n++) theta[n] = w * (pos[n] - D);
    Fit f = fitSine(out, theta, 4 * kBlock);
    if (bestD < 0 || f.maxResidual < best.maxResidual) {
      best = f;
      bestD = D;
    }
  }
  const double A = kAmp * gain;
  const double K = kernelError(resampler, w);
  // float samples (~6e-8 relative), fixed-point positions (2^-20 sample).
  const double noise = A * (4e-6 + w * std::ldexp(1.0, -kFracBits));
  const double bound = 3.0 * A * K + noise;
  gWorstRatio[resampler] = std::max(gWorstRatio[resampler], best.maxResidual / bound);
  CHECK(best.maxResidual <= bound,
        "%s %s: largest deviation from the ideal sine %.3g, bound %.3g "
        "(K %.3g, w %.3f rad/sample, delay %d)",
        kResName[resampler], label, best.maxResidual, bound, K, w, bestD);
  CHECK(std::fabs(best.amp - A) <= A * K + noise,
        "%s %s: amplitude %.5f, expected %.5f", kResName[resampler], label,
        best.amp, A);
  CHECK(ctest::allFinite(out), "%s %s: finite", kResName[resampler], label);
}

void constant() {
  struct Case {
    float srcRate, speed;
  } cases[] = {{44100, 0.25f}, {44100, 0.5f}, {44100, 0.9f}, {44100, 1.0f},
               {44100, 1.37f}, {44100, 2.0f}, {44100, 4.0f}, {11025, 1.0f},
               {176400, 1.0f}, {32000, 1.0f}};
  for (int r = 0; r <= 2; r++)
    for (const Case &c : cases)
      for (double freq : {440.0, 5000.0}) {
        const double outFreq = freq * c.speed * kRate / c.srcRate * c.srcRate / kRate;
        if (freq > 0.45 * c.srcRate || outFreq > 0.45 * kRate) continue;
        std::vector<double> pos;
        double gain;
        std::vector<float> out = render(
            r, freq, c.srcRate, 86, [&](int) { return c.speed; }, pos, gain);
        char label[96];
        std::snprintf(label, sizeof label, "%g Hz, source %g Hz, speed %g",
                      freq, c.srcRate, c.speed);
        check(label, r, 2 * M_PI * freq / c.srcRate, gain, out, pos);
      }
}

void speedRamp() {
  for (int r = 0; r <= 2; r++)
    for (double freq : {440.0, 3000.0}) {
      std::vector<double> pos;
      double gain;
      const int blocks = 172;
      std::vector<float> out = render(
          r, freq, 44100, blocks,
          [&](int b) { return 0.5f + 1.5f * (float)b / (blocks - 1); }, pos,
          gain);
      char label[64];
      std::snprintf(label, sizeof label, "%g Hz, speed ramp 0.5 -> 2", freq);
      check(label, r, 2 * M_PI * freq / 44100, gain, out, pos);

      std::mt19937 rng(17 + r);
      std::uniform_real_distribution<float> u(0.25f, 4.0f);
      std::vector<float> speeds(blocks);
      for (auto &s : speeds) s = u(rng);
      out = render(r, freq, 44100, blocks, [&](int b) { return speeds[b]; },
                   pos, gain);
      std::snprintf(label, sizeof label, "%g Hz, random speed per block",
                    freq);
      check(label, r, 2 * M_PI * freq / 44100, gain, out, pos);
    }
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("constant")) constant();
  if (ctest::selected("speed_ramp")) speedRamp();
  for (int r = 0; r <= 2; r++)
    std::printf("  %s: largest deviation used %.0f%% of its bound\n",
                kResName[r], 100 * gWorstRatio[r]);
  return ctest::summary("resampler_quality_test");
}

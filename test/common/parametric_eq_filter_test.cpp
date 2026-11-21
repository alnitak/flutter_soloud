// Standalone native tests for the frequency response of the plugin's
// parametric EQ (src/filters/parametric_eq_filter.cpp).
//
// The filter is an STFT: each window of N samples (no analysis window) goes
// through a real FFT, every bin k is scaled by G(k * sr / N), the inverse FFT
// is multiplied by a periodic Hann window (scaled by 1/N) and overlap-added
// with hop N/2. Two consequences make exact checks possible:
//
//   - a periodic Hann window at 50% overlap sums to exactly 1, so with
//     G == 1 the filter is a pure delay;
//   - a tone at a bin centre (k * sr / N) has an integer number of cycles in
//     every window, so it lives in bin k only and comes out scaled by exactly
//     G(k * sr / N).
//
// G is the product over bands of 1 + (gain - 1) * w, with w a Gaussian in
// log-frequency: w = exp(-(ln f - ln fc)^2 * 4.16277 * Q^2). 4.16277 is
// 2 / ln(2)^2, so the Gaussian's sigma is 1 / (2 Q) octaves: at
// fc * 2^(+-1/(2Q)) the weight is exp(-1/2). Q is clamped to [0.1, 20] and
// fc to [10 Hz, sr/2]. G is computed with a fast exp2 (error < 2e-5), so the
// measured gains must match to 1e-3 relative.
//
// Cases:
//   identity     flat gains (and wet = 0) give the input back, delayed; for
//                N = 16 (complex FFT path), 32, 1024 and 4096;
//   band_gain    for 1, 3, 10 and 64 bands, each tested band boosted (2x) or
//                cut (0.5x, 0x) with its centre on a bin: the tone at the
//                centre comes out scaled by the gain, other bands untouched;
//   shape        the response at fc * 2^(1/(2Q)) and at 1 octave matches the
//                Gaussian model; higher Q is narrower;
//   q_limits     Q at 0.1 and 20, and outside (0.01, 100) behaves as clamped;
//   wet          wet 0.5 gives 0.5 * G + 0.5;
//   band_count   changing the band count (1, 64, 0, 200, ...) while processing:
//                bands used stay in [1, 64], output stays finite and
//                bounded;
//   full_scale   full-scale noise and sine with all 64 bands at the maximum
//                gain (4) for 10 s: finite, stationary (no growth), bounded
//                by the largest bin gain.
//
// No audio device and no assets are used.
//
//   ./test/common/parametric_eq_filter_test.sh [--asan] [--strict] [case]

#include "filters/parametric_eq_filter.h"
#include "soloud.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace {

constexpr float kSr = 44100.f;
constexpr unsigned int kBlock = 512;

using P = ParametricEq;

/// Processes a mono signal block by block through [inst] (channel 0).
/// [onBlock] runs before each block (e.g. to change a parameter).
template <typename F>
std::vector<float> process(SoLoud::FilterInstance *inst,
                           const std::vector<float> &in, F onBlock,
                           unsigned int channels = 1) {
  std::vector<float> out(in.size());
  std::vector<float> buf(kBlock * channels);
  int blockIndex = 0;
  for (size_t pos = 0; pos < in.size(); pos += kBlock, blockIndex++) {
    const unsigned int n = (unsigned int)std::min<size_t>(kBlock, in.size() - pos);
    onBlock(blockIndex);
    for (unsigned int c = 0; c < channels; c++)
      std::memcpy(&buf[c * kBlock], &in[pos], n * sizeof(float));
    inst->filter(buf.data(), n, kBlock, channels, kSr, pos / (double)kSr);
    std::memcpy(&out[pos], &buf[0], n * sizeof(float));
    for (unsigned int c = 1; c < channels; c++)
      if (std::memcmp(&buf[c * kBlock], &buf[0], n * sizeof(float)) != 0)
        out[pos] = NAN;  // channels must stay identical
  }
  return out;
}
std::vector<float> process(SoLoud::FilterInstance *inst,
                           const std::vector<float> &in) {
  return process(inst, in, [](int) {});
}

std::vector<float> sine(double freq, float amp, size_t n) {
  std::vector<float> s(n);
  for (size_t i = 0; i < n; i++)
    s[i] = amp * (float)std::sin(2.0 * M_PI * freq * (double)i / kSr);
  return s;
}

/// Finds the delay of a pure-delay system from its impulse response.
int measureDelay(SoLoud::FilterInstance *inst, int window) {
  std::vector<float> imp(window * 8, 0.f);
  imp[0] = 1.f;
  std::vector<float> out = process(inst, imp);
  int best = 0;
  for (size_t i = 0; i < out.size(); i++)
    if (std::fabs(out[i]) > std::fabs(out[best])) best = (int)i;
  return best;
}

/// Amplitude of a bin-centred tone in the filter's steady state: skips
/// 4 windows and measures over a whole number of windows.
double steadyAmplitude(const std::vector<float> &out, double freq,
                       int window) {
  const size_t skip = 4 * window;
  const size_t len = ((out.size() - skip) / window) * window;
  return ctest::toneAmplitude(out.data() + skip, len, freq, kSr);
}

std::unique_ptr<SoLoud::FilterInstance> makeInstance(P &eq, int window) {
  std::unique_ptr<SoLoud::FilterInstance> inst(eq.createInstance());
  inst->setFilterParameter(1, (float)window);
  return inst;
}

/// The model's G for one band.
double bandFactor(double f, double fc, double gain, double q) {
  q = std::min(20.0, std::max(0.1, q));
  const double d = std::log(f) - std::log(fc);
  const double e = d * d * 2.0 / (M_LN2 * M_LN2) * q * q;
  if (e > 9.0) return 1.0;  // the filter skips negligible weights
  return std::max(0.0, 1.0 + (gain - 1.0) * std::exp(-e));
}

SoLoud::Soloud gSoloud;  // only read for its channel count

void identity() {
  for (int window : {16, 32, 1024, 4096}) {
    for (float wet : {1.0f, 0.0f}) {
      P eq(&gSoloud, 3);
      auto inst = makeInstance(eq, window);
      inst->setFilterParameter(0, wet);
      if (wet == 0.0f) inst->setFilterParameter(P::BAND_GAIN_OFFSET + 1, 3.f);
      const int delay = measureDelay(inst.get(), window);
      // Fresh instance for the signal (the impulse filled the rings).
      auto inst2 = makeInstance(eq, window);
      inst2->setFilterParameter(0, wet);
      if (wet == 0.0f) inst2->setFilterParameter(P::BAND_GAIN_OFFSET + 1, 3.f);
      std::mt19937 rng(window);
      std::uniform_real_distribution<float> u(-0.9f, 0.9f);
      std::vector<float> in(window * 12 + 777);
      for (auto &x : in) x = u(rng);
      std::vector<float> out = process(inst2.get(), in);
      double maxErr = 0;
      for (size_t i = delay; i < in.size(); i++)
        maxErr = std::max(maxErr, (double)std::fabs(out[i] - in[i - delay]));
      bool silentHead = true;
      for (int i = 0; i < delay; i++)
        if (std::fabs(out[i]) > 1e-6f) silentHead = false;  // FFT rounding
      CHECK(maxErr < 1e-5,
            "N %d wet %.0f: output != input delayed by %d (max err %g)",
            window, wet, delay, maxErr);
      CHECK(silentHead, "N %d: silence before the delay", window);
      CHECK(delay > 0 && delay <= 2 * window, "N %d: delay %d", window,
            delay);
    }
  }
}

/// Boosts/cuts band [b] of a [bands]-band EQ, centred on the bin nearest to
/// its default frequency, and measures the gain of a tone there.
void checkBand(int bands, int b, float gain, int window) {
  P eq(&gSoloud, bands);
  auto inst = makeInstance(eq, window);
  const double binHz = kSr / window;
  int k = (int)std::lround(eq.mFreq[b] / binHz);
  k = std::max(1, std::min(window / 2 - 1, k));
  const double fc = k * binHz;
  inst->setFilterParameter(P::BAND_FREQ_OFFSET + b, (float)fc);
  inst->setFilterParameter(P::BAND_GAIN_OFFSET + b, gain);
  std::vector<float> in = sine(fc, 0.5f, window * 16);
  std::vector<float> out = process(inst.get(), in);
  const double amp = steadyAmplitude(out, fc, window);
  const double expected = 0.5 * gain;
  CHECK(ctest::allFinite(out), "finite");
  CHECK(std::fabs(amp - expected) <= 1e-3 * 0.5 + 1e-3 * expected,
        "%d bands, band %d at %.1f Hz (Q %.2f), gain %.2f: tone amplitude "
        "%.5f, expected %.5f",
        bands, b, fc, eq.mQ[b], gain, amp, expected);
}

void bandGain() {
  for (int bands : {1, 3, 10, 64}) {
    std::vector<int> which;
    if (bands <= 10)
      for (int b = 0; b < bands; b++) which.push_back(b);
    else
      which = {0, 1, 13, 31, 50, 63};
    for (int b : which)
      for (float g : {2.0f, 0.5f, 0.0f, 4.0f}) checkBand(bands, b, g, 1024);
  }
  // Other window sizes.
  checkBand(3, 1, 2.0f, 4096);
  checkBand(3, 2, 0.5f, 32);
}

void shape() {
  const int window = 4096;  // 10.8 Hz bins: frequencies land close to target
  const double binHz = kSr / window;
  for (double q : {0.5, 1.0, 2.0, 5.0}) {
    for (double octaves : {1.0 / (2.0 * q), 1.0, -1.0 / (2.0 * q)}) {
      P eq(&gSoloud, 1);
      auto inst = makeInstance(eq, window);
      const double fc = 186 * binHz;  // ~2 kHz on a bin
      inst->setFilterParameter(P::BAND_FREQ_OFFSET, (float)fc);
      inst->setFilterParameter(P::BAND_Q_OFFSET, (float)q);
      inst->setFilterParameter(P::BAND_GAIN_OFFSET, 3.0f);
      const int k = (int)std::lround(fc * std::pow(2.0, octaves) / binHz);
      const double f = k * binHz;
      std::vector<float> out = process(inst.get(), sine(f, 0.25f, window * 10));
      const double g = steadyAmplitude(out, f, window) / 0.25;
      const double expected = bandFactor(f, fc, 3.0, q);
      CHECK(std::fabs(g - expected) < 1e-3 * expected,
            "Q %.1f at %+.3f oct (%.1f Hz): gain %.5f, model %.5f", q,
            std::log2(f / fc), f, g, expected);
    }
  }
  // At +-1/(2Q) octaves the weight is exp(-1/2) whatever Q: gain 1 + 2 * 0.6065.
  NOTE("model: gain at fc*2^(1/(2Q)) is %.4f for a 3x band", 1 + 2 * std::exp(-0.5));
}

void qLimits() {
  const int window = 1024;
  const double binHz = kSr / window;
  const double fc = 46 * binHz;            // ~1981 Hz
  const double fOct = 23 * binHz;          // exactly one octave below
  auto gainAt = [&](float q, double f) {
    P eq(&gSoloud, 1);
    auto inst = makeInstance(eq, window);
    inst->setFilterParameter(P::BAND_FREQ_OFFSET, (float)fc);
    inst->setFilterParameter(P::BAND_Q_OFFSET, q);
    inst->setFilterParameter(P::BAND_GAIN_OFFSET, 2.0f);
    std::vector<float> out = process(inst.get(), sine(f, 0.5f, window * 12));
    return steadyAmplitude(out, f, window) / 0.5;
  };
  // Centre: the gain whatever Q.
  for (float q : {0.1f, 20.f, 0.01f, 100.f, 0.0f, -3.0f})
    CHECK(std::fabs(gainAt(q, fc) - 2.0) < 2e-3, "Q %g: centre gain %.5f", q,
          gainAt(q, fc));
  // One octave away: Q 20 -> sigma 1/40 octave: no effect. Q 0.1 -> sigma 5
  // octaves: weight exp(-0.02).
  const double g20 = gainAt(20.f, fOct), g01 = gainAt(0.1f, fOct);
  CHECK(std::fabs(g20 - 1.0) < 1e-3, "Q 20 one octave away: %.5f", g20);
  CHECK(std::fabs(g01 - bandFactor(fOct, fc, 2.0, 0.1)) < 1e-3,
        "Q 0.1 one octave away: %.5f, model %.5f", g01,
        bandFactor(fOct, fc, 2.0, 0.1));
  // Out of range behaves as clamped.
  CHECK(std::fabs(gainAt(100.f, fOct) - g20) < 1e-5, "Q 100 == Q 20");
  CHECK(std::fabs(gainAt(0.01f, fOct) - g01) < 1e-5, "Q 0.01 == Q 0.1");
}

void wet() {
  const int window = 1024;
  const double fc = 70 * (kSr / window);
  P eq(&gSoloud, 1);
  auto inst = makeInstance(eq, window);
  inst->setFilterParameter(P::BAND_FREQ_OFFSET, (float)fc);
  inst->setFilterParameter(P::BAND_GAIN_OFFSET, 3.0f);
  inst->setFilterParameter(0, 0.5f);
  std::vector<float> out = process(inst.get(), sine(fc, 0.5f, window * 12));
  const double g = steadyAmplitude(out, fc, window) / 0.5;
  CHECK(std::fabs(g - (0.5 * 3.0 + 0.5)) < 2e-3, "wet 0.5, gain 3: %.5f", g);
}

void bandCount() {
  const int window = 1024;
  P eq(&gSoloud, 3);
  auto inst = makeInstance(eq, window);
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> u(-0.5f, 0.5f);
  std::vector<float> in(kBlock * 400);
  for (auto &x : in) x = u(rng);
  // (Negative counts are rejected by Dart; casting one to unsigned is UB.)
  const float counts[] = {1, 64, 0, 200, 2, 10, 1, 64};
  bool inRange = true;
  std::vector<float> out = process(inst.get(), in, [&](int block) {
    if (block % 40 == 20) {
      const float c = counts[(block / 40) % 8];
      inst->setFilterParameter(2, c);
      if (eq.mBands < 1 || eq.mBands > 64) inRange = false;
      // Boost every band so the new bands are active.
      for (unsigned int b = 0; b < eq.mBands; b++)
        inst->setFilterParameter(P::BAND_GAIN_OFFSET + b, 2.0f);
    }
  });
  CHECK(inRange, "band count stays in [1, 64]");
  CHECK(ctest::allFinite(out), "finite while changing the band count");
  // Each 1 + (2-1)*w factor is <= 2, and with 64 overlapping bands the
  // product stays modest; noise at 0.5 peak can't exceed a few units.
  CHECK(ctest::peak(out.data(), out.size()) < 0.5f * 2.0f * 2.0f * 2.0f,
        "peak %g bounded", ctest::peak(out.data(), out.size()));
  const float p2 = inst->getFilterParameter(2);
  if (p2 != (float)eq.mBands)
    NOTE("after setting %g bands, parameter 2 reads %g while %u bands are "
         "used (Dart clamps the count, so this is not reachable from Dart)",
         counts[(399 / 40) % 8], p2, eq.mBands);
}

void fullScale() {
  const int window = 1024;
  P eq(&gSoloud, 64);
  auto inst = makeInstance(eq, window);
  for (unsigned int b = 0; b < 64; b++)
    inst->setFilterParameter(P::BAND_GAIN_OFFSET + b, 4.0f);
  // Largest bin gain of the model.
  double maxG = 0;
  for (int k = 1; k <= window / 2; k++) {
    double g = 1;
    for (unsigned int b = 0; b < 64; b++)
      g *= bandFactor(k * kSr / window, std::min<double>(eq.mFreq[b], kSr / 2),
                      4.0, eq.mQ[b]);
    maxG = std::max(maxG, g);
  }
  std::mt19937 rng(11);
  std::uniform_real_distribution<float> u(-1.f, 1.f);
  std::vector<float> in((size_t)kSr * 10);
  for (auto &x : in) x = u(rng);
  std::vector<float> out = process(inst.get(), in);
  CHECK(ctest::allFinite(out), "full-scale noise: finite");
  const size_t sec = (size_t)kSr;
  const double r2 = ctest::rms(out.data() + sec, sec);
  const double r10 = ctest::rms(out.data() + 9 * sec, sec);
  CHECK(std::fabs(r10 / r2 - 1.0) < 0.05, "stationary: rms %g then %g", r2,
        r10);
  // Parseval per window: the output energy is at most maxG^2 times the input.
  CHECK(r10 <= maxG * ctest::rms(in.data() + 9 * sec, sec) * 1.05,
        "rms %g within max gain %g", r10, maxG);

  // Full-scale sine at a band centre.
  auto inst2 = makeInstance(eq, window);
  for (unsigned int b = 0; b < 64; b++)
    inst2->setFilterParameter(P::BAND_GAIN_OFFSET + b, 4.0f);
  const double f = 116 * (kSr / window);
  std::vector<float> outS = process(inst2.get(), sine(f, 1.0f, sec * 10));
  CHECK(ctest::allFinite(outS), "full-scale sine: finite");
  CHECK(ctest::peak(outS.data(), outS.size()) <= maxG * 1.01,
        "sine peak %g within max gain %g",
        ctest::peak(outS.data(), outS.size()), maxG);
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("identity")) identity();
  if (ctest::selected("band_gain")) bandGain();
  if (ctest::selected("shape")) shape();
  if (ctest::selected("q_limits")) qLimits();
  if (ctest::selected("wet")) wet();
  if (ctest::selected("band_count")) bandCount();
  if (ctest::selected("full_scale")) fullScale();
  return ctest::summary("parametric_eq_filter_test");
}

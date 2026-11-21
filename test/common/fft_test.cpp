// Standalone native tests for the FFTs used in the plugin:
//
//   - SoLoud::FFT (src/soloud/src/core/soloud_fft.cpp, Ooura's split-radix
//     complex FFT): fft(a, n) transforms n/2 complex points stored
//     interleaved (re, im) in n floats with X[k] = sum x[j] e^(-2 pi i jk/N);
//     ifft is its inverse (scaled by 2/n). Used by the FFT filters and by
//     Soloud::calcFFT.
//   - Soloud::calcFFT (soloud.cpp): the 256 visualization samples, zero
//     padded to 512 complex points; magnitudes of bins 0..255.
//   - PFFFT's ordered real forward transform (src/pffft), which the
//     visualization Analyzer (src/analyzer.cpp) and the parametric EQ use.
//
// Every transform is compared with a reference DFT computed in double. A
// float FFT's error grows like eps * log2(N) * ||x||, so the tolerance is
// 2e-6 * log2(N) * ||x|| (~30 float ulps per stage).
//
//   ooura_dft     random input, N = 2 .. 8192 complex points, vs the DFT;
//   ooura_inverse ifft(fft(x)) == x; ifft vs the inverse DFT;
//   ooura_tone    a complex exponential lands in its bin only; a real cosine
//                 in bins k and N - k at N/2 each;
//   ooura_linear  fft(a x + b y) == a fft(x) + b fft(y);
//   ooura_special all zeros -> exactly zeros; impulses -> unit-magnitude
//                 phase ramps; fft256/fft1024/ifft256 == the generic calls;
//   calc_fft      the engine's calcFFT() vs the DFT of getWave()'s samples,
//                 and a tone peaks in its bin;
//   pffft_real    PFFFT_REAL ordered forward vs the DFT of a real signal,
//                 N = 32 .. 8192; a tone in its bin; zeros and impulse.
//
// No audio device and no assets are used.
//
//   ./test/common/fft_test.sh [--asan] [--strict] [case]

#include "pffft/pffft.h"
#include "soloud.h"
#include "soloud_fft.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

namespace {

using cd = std::complex<double>;

/// Reference DFT of N complex points; sign -1 forward, +1 inverse (unscaled).
std::vector<cd> dft(const std::vector<cd> &x, int sign) {
  const size_t N = x.size();
  std::vector<cd> tw(N);
  for (size_t k = 0; k < N; k++)
    tw[k] = std::polar(1.0, sign * 2.0 * M_PI * (double)k / (double)N);
  std::vector<cd> X(N);
  for (size_t k = 0; k < N; k++) {
    cd s = 0;
    size_t idx = 0;
    for (size_t j = 0; j < N; j++) {
      s += x[j] * tw[idx];
      idx += k;
      if (idx >= N) idx -= N;
    }
    X[k] = s;
  }
  return X;
}

std::vector<cd> toComplex(const std::vector<float> &a) {
  std::vector<cd> x(a.size() / 2);
  for (size_t j = 0; j < x.size(); j++) x[j] = cd(a[2 * j], a[2 * j + 1]);
  return x;
}

double norm(const std::vector<cd> &x) {
  double s = 0;
  for (auto &v : x) s += std::norm(v);
  return std::sqrt(s);
}

double maxDiff(const std::vector<float> &got, const std::vector<cd> &ref,
               double scale = 1.0) {
  double m = 0;
  for (size_t k = 0; k < ref.size(); k++)
    m = std::max(m, std::abs(cd(got[2 * k], got[2 * k + 1]) - ref[k] * scale));
  return m;
}

double tol(size_t N, double xnorm) {
  return 2e-6 * std::max(1.0, std::log2((double)N)) * xnorm + 1e-6;
}

std::vector<float> randomData(size_t floats, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> u(-1.f, 1.f);
  std::vector<float> a(floats);
  for (auto &v : a) v = u(rng);
  return a;
}

void oouraDft() {
  for (unsigned int N = 2; N <= 8192; N *= 2) {
    std::vector<float> a = randomData(2 * N, N);
    const std::vector<cd> x = toComplex(a);
    const std::vector<cd> X = dft(x, -1);
    SoLoud::FFT::fft(a.data(), 2 * N);
    const double err = maxDiff(a, X);
    CHECK(err <= tol(N, norm(x)), "N %u: max error %g (tol %g)", N, err,
          tol(N, norm(x)));
    if (err > tol(N, norm(x))) {
      // Tell a sign-convention mix-up from garbage.
      const double errPlus = maxDiff(a, dft(x, +1));
      if (errPlus <= tol(N, norm(x)))
        NOTE("N %u matches e^(+2 pi i jk/N) instead", N);
    }
    CHECK(ctest::allFinite(a), "N %u finite", N);
  }
}

void oouraInverse() {
  for (unsigned int N = 2; N <= 8192; N *= 4) {
    std::vector<float> a = randomData(2 * N, 100 + N);
    const std::vector<float> orig = a;
    const std::vector<cd> x = toComplex(a);
    // ifft vs the inverse DFT scaled by 1/N.
    std::vector<float> b = a;
    SoLoud::FFT::ifft(b.data(), 2 * N);
    const double e1 = maxDiff(b, dft(x, +1), 1.0 / N);
    CHECK(e1 <= tol(N, norm(x)) / std::sqrt((double)N) + 1e-6,
          "N %u: ifft error %g", N, e1);
    // Round trip.
    SoLoud::FFT::fft(a.data(), 2 * N);
    SoLoud::FFT::ifft(a.data(), 2 * N);
    double m = 0;
    for (size_t i = 0; i < a.size(); i++)
      m = std::max(m, (double)std::fabs(a[i] - orig[i]));
    CHECK(m <= 4e-6 * std::log2(std::max(2u, N)) + 1e-6,
          "N %u: round trip error %g", N, m);
  }
}

void oouraTone() {
  for (unsigned int N : {16u, 256u, 512u, 4096u}) {
    for (unsigned int k : {1u, 3u, N / 4, N / 2 - 1}) {
      // Complex exponential e^(+2 pi i k j / N): all energy in bin k.
      std::vector<float> a(2 * N);
      for (unsigned int j = 0; j < N; j++) {
        a[2 * j] = (float)std::cos(2 * M_PI * k * j / N);
        a[2 * j + 1] = (float)std::sin(2 * M_PI * k * j / N);
      }
      SoLoud::FFT::fft(a.data(), 2 * N);
      double other = 0;
      for (unsigned int b = 0; b < N; b++)
        if (b != k) other = std::max(other, (double)std::hypot(a[2 * b], a[2 * b + 1]));
      const double peak = std::hypot(a[2 * k], a[2 * k + 1]);
      CHECK(std::fabs(peak - N) <= tol(N, std::sqrt((double)N)) &&
                other <= tol(N, std::sqrt((double)N)),
            "N %u exp bin %u: |X[k]| %g (expected %u), largest other %g", N,
            k, peak, N, other);
      // Real cosine: N/2 in bins k and N - k.
      std::vector<float> c(2 * N, 0.f);
      for (unsigned int j = 0; j < N; j++)
        c[2 * j] = (float)std::cos(2 * M_PI * k * j / N);
      SoLoud::FFT::fft(c.data(), 2 * N);
      const double pk = std::hypot(c[2 * k], c[2 * k + 1]);
      const double pm = std::hypot(c[2 * (N - k)], c[2 * (N - k) + 1]);
      CHECK(std::fabs(pk - N / 2.0) <= tol(N, std::sqrt(N / 2.0)) &&
                std::fabs(pm - N / 2.0) <= tol(N, std::sqrt(N / 2.0)),
            "N %u cos bin %u: %g and %g, expected %g", N, k, pk, pm, N / 2.0);
    }
  }
}

void oouraLinear() {
  const unsigned int N = 1024;
  std::vector<float> x = randomData(2 * N, 1), y = randomData(2 * N, 2);
  const float a = 0.37f, b = -2.5f;
  std::vector<float> z(2 * N);
  for (size_t i = 0; i < z.size(); i++) z[i] = a * x[i] + b * y[i];
  const double zn = norm(toComplex(z));
  SoLoud::FFT::fft(x.data(), 2 * N);
  SoLoud::FFT::fft(y.data(), 2 * N);
  SoLoud::FFT::fft(z.data(), 2 * N);
  double m = 0;
  for (size_t i = 0; i < z.size(); i++)
    m = std::max(m, (double)std::fabs(z[i] - (a * x[i] + b * y[i])));
  CHECK(m <= 2 * tol(N, zn), "linearity error %g", m);
}

void oouraSpecial() {
  for (unsigned int N : {2u, 8u, 128u, 2048u}) {
    std::vector<float> z(2 * N, 0.f);
    SoLoud::FFT::fft(z.data(), 2 * N);
    CHECK(std::all_of(z.begin(), z.end(), [](float v) { return v == 0.f; }),
          "N %u: zeros stay exactly zero", N);
    for (unsigned int m : {0u, 1u, N / 2}) {
      std::vector<float> d(2 * N, 0.f);
      d[2 * m] = 1.f;
      SoLoud::FFT::fft(d.data(), 2 * N);
      double err = 0;
      for (unsigned int k = 0; k < N; k++) {
        const cd e = std::polar(1.0, -2.0 * M_PI * (double)k * m / N);
        err = std::max(err, std::abs(cd(d[2 * k], d[2 * k + 1]) - e));
      }
      CHECK(err <= tol(N, 1.0), "N %u impulse at %u: error %g", N, m, err);
    }
  }
  // Fixed-size entry points match the generic ones.
  std::vector<float> a = randomData(256, 7), b = a;
  SoLoud::FFT::fft256(a.data());
  SoLoud::FFT::fft(b.data(), 256);
  CHECK(a == b, "fft256 == fft(.., 256)");
  a = randomData(256, 8);
  b = a;
  SoLoud::FFT::ifft256(a.data());
  SoLoud::FFT::ifft(b.data(), 256);
  CHECK(a == b, "ifft256 == ifft(.., 256)");
  a = randomData(1024, 9);
  b = a;
  SoLoud::FFT::fft1024(a.data());
  SoLoud::FFT::fft(b.data(), 1024);
  CHECK(a == b, "fft1024 == fft(.., 1024)");
}

class ToneInstance : public SoLoud::AudioSourceInstance {
 public:
  explicit ToneInstance(double f) : mF(f) {}
  unsigned int getAudio(float *buf, unsigned int n, unsigned int) override {
    for (unsigned int i = 0; i < n; i++, mN++)
      buf[i] = 0.5f * (float)std::sin(2 * M_PI * mF * mN / 44100.0);
    return n;
  }
  bool hasEnded() override { return false; }
  double mF;
  long mN = 0;
};

class ToneSource : public SoLoud::AudioSource {
 public:
  explicit ToneSource(double f) : mF(f) {
    mChannels = 1;
    mBaseSamplerate = 44100;
  }
  ~ToneSource() override { stop(); }
  SoLoud::AudioSourceInstance *createInstance() override {
    return new ToneInstance(mF);
  }
  double mF;
};

void calcFft() {
  SoLoud::Soloud s;
  s.init(0, SoLoud::Soloud::NULLDRIVER, 44100, 512, 1);
  s.setVisualizationEnable(true);
  // Tone on bin 40 of a 256-sample window: bin 80 of the 512-point
  // zero-padded transform.
  const double f = 40 * 44100.0 / 256;
  ToneSource tone(f);
  s.play(tone);
  std::vector<float> buf(512);
  for (int i = 0; i < 8; i++) s.mix(buf.data(), 512);
  std::vector<float> wave(s.getWave(), s.getWave() + 256);
  const float *fftData = s.calcFFT();
  std::vector<float> got(fftData, fftData + 256);
  std::vector<cd> x(512, 0.0);
  for (int i = 0; i < 256; i++) x[i] = wave[i];
  const std::vector<cd> X = dft(x, -1);
  double err = 0;
  for (int k = 0; k < 256; k++) err = std::max(err, (double)std::fabs(got[k] - std::abs(X[k])));
  CHECK(err <= tol(512, norm(x)), "calcFFT vs DFT: max error %g", err);
  int peak = 0;
  for (int k = 1; k < 256; k++)
    if (got[k] > got[peak]) peak = k;
  CHECK(peak == 80, "calcFFT peak in bin %d, expected 80", peak);
  CHECK(ctest::peak(wave.data(), 256) > 0.1f, "visualization captured audio");
  s.deinit();
}

void pffftReal() {
  for (int N = 32; N <= 8192; N *= 2) {
    PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
    CHECK(setup != nullptr, "setup for N %d", N);
    if (!setup) continue;
    float *in = (float *)pffft_aligned_malloc(N * sizeof(float));
    float *out = (float *)pffft_aligned_malloc(N * sizeof(float));
    float *work = (float *)pffft_aligned_malloc(N * sizeof(float));
    auto transform = [&](const std::vector<float> &x) {
      std::memcpy(in, x.data(), N * sizeof(float));
      pffft_transform_ordered(setup, in, out, work, PFFFT_FORWARD);
      // Ordered real format: out[0] = X0, out[1] = X(N/2), then re/im pairs.
      std::vector<cd> X(N / 2 + 1);
      X[0] = out[0];
      X[N / 2] = out[1];
      for (int k = 1; k < N / 2; k++) X[k] = cd(out[2 * k], out[2 * k + 1]);
      return X;
    };
    // Random real signal vs the DFT.
    std::vector<float> x = randomData(N, 300 + N);
    std::vector<cd> xc(x.begin(), x.end());
    const std::vector<cd> ref = dft(xc, -1);
    const std::vector<cd> X = transform(x);
    double err = 0;
    for (int k = 0; k <= N / 2; k++) err = std::max(err, std::abs(X[k] - ref[k]));
    CHECK(err <= tol(N, norm(xc)), "pffft N %d: max error %g (tol %g)", N,
          err, tol(N, norm(xc)));
    // Bin-centred cosine: N/2 in bin k.
    const int k0 = N / 8 + 1;
    std::vector<float> c(N);
    for (int j = 0; j < N; j++) c[j] = (float)std::cos(2 * M_PI * k0 * j / N);
    const std::vector<cd> C = transform(c);
    double other = 0;
    for (int k = 0; k <= N / 2; k++)
      if (k != k0) other = std::max(other, std::abs(C[k]));
    CHECK(std::fabs(std::abs(C[k0]) - N / 2.0) <= tol(N, std::sqrt(N / 2.0)) &&
              other <= tol(N, std::sqrt(N / 2.0)),
          "pffft N %d: cosine bin %d = %g, largest other %g", N, k0,
          std::abs(C[k0]), other);
    // Zeros and impulse.
    const std::vector<cd> Z = transform(std::vector<float>(N, 0.f));
    CHECK(std::all_of(Z.begin(), Z.end(), [](cd v) { return v == cd(0, 0); }),
          "pffft N %d: zeros", N);
    std::vector<float> d(N, 0.f);
    d[0] = 1.f;
    const std::vector<cd> D = transform(d);
    double derr = 0;
    for (auto &v : D) derr = std::max(derr, std::abs(v - cd(1, 0)));
    CHECK(derr <= tol(N, 1.0), "pffft N %d: impulse error %g", N, derr);
    pffft_aligned_free(in);
    pffft_aligned_free(out);
    pffft_aligned_free(work);
    pffft_destroy_setup(setup);
  }
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("ooura_dft")) oouraDft();
  if (ctest::selected("ooura_inverse")) oouraInverse();
  if (ctest::selected("ooura_tone")) oouraTone();
  if (ctest::selected("ooura_linear")) oouraLinear();
  if (ctest::selected("ooura_special")) oouraSpecial();
  if (ctest::selected("calc_fft")) calcFft();
  if (ctest::selected("pffft_real")) pffftReal();
  return ctest::summary("fft_test");
}

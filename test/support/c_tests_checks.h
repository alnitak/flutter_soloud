// Shared check macros and signal helpers for the native tests
// adsr_test, parametric_eq_filter_test, soloud_queue_test, fader_ops_test,
// dc_removal_eq_filter_test, fft_test, mixer_output_encoder_factory_test,
// output_encoders_fuzz_test, resampler_quality_test and
// voice_virtualization_test (test/common/).
//
// Usage, in exactly one translation unit (the test's .cpp):
//
//   #include "../support/c_tests_checks.h"
//   int main(int argc, char **argv) {
//     ctest::parseArgs(argc, argv);     // --strict, optional filter
//     if (ctest::selected("foo")) foo();
//     return ctest::summary("my_test");
//   }
//
// CHECK(cond, fmt, ...) counts a failure when cond is false.
// KNOWN_BUG("C1", cond, fmt, ...) prints `KNOWN BUG C1 (...)` when cond is
// false and fails the run only with --strict.
// NOTE(fmt, ...) prints an informational line.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef M_PI  // MSVC without _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif
#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif

namespace ctest {

inline int gFailures = 0;
inline int gChecks = 0;
inline int gKnownBugs = 0;
inline bool gStrict = false;
inline std::string gFilter;

inline void parseArgs(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--strict") == 0)
      gStrict = true;
    else
      gFilter = argv[i];
  }
  if (const char *s = std::getenv("SOLOUD_TEST_STRICT"))
    if (s[0] == '1') gStrict = true;
}

inline bool selected(const char *name) {
  if (gFilter.empty() || gFilter == name) {
    std::printf("== %s\n", name);
    return true;
  }
  return false;
}

inline int summary(const char *name) {
  std::printf("%s: %d checks, %d failures, %d known bugs%s\n", name, gChecks,
              gFailures, gKnownBugs, gStrict ? " (strict)" : "");
  if (gFailures == 0) std::printf("ALL PASSED\n");
  return gFailures == 0 ? 0 : 1;
}

inline bool allFinite(const float *p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (!std::isfinite(p[i])) return false;
  return true;
}
inline bool allFinite(const std::vector<float> &v) {
  return allFinite(v.data(), v.size());
}

inline double rms(const float *p, size_t n) {
  if (n == 0) return 0.0;
  double s = 0;
  for (size_t i = 0; i < n; i++) s += (double)p[i] * p[i];
  return std::sqrt(s / (double)n);
}

inline float peak(const float *p, size_t n) {
  float m = 0.f;
  for (size_t i = 0; i < n; i++) m = std::fmax(m, std::fabs(p[i]));
  return m;
}

/// Amplitude of the [freq] component of p[0..n) (sampled at [sr]), by
/// projection on sin/cos. Exact for a tone with an integer number of cycles
/// in the span; otherwise accurate to ~1/(cycles) of leakage.
inline double toneAmplitude(const float *p, size_t n, double freq, double sr) {
  double re = 0, im = 0;
  const double w = 2.0 * M_PI * freq / sr;
  for (size_t i = 0; i < n; i++) {
    re += p[i] * std::cos(w * (double)i);
    im += p[i] * std::sin(w * (double)i);
  }
  return 2.0 * std::sqrt(re * re + im * im) / (double)n;
}

}  // namespace ctest

#define CHECK(cond, ...)                                                     \
  do {                                                                       \
    ++ctest::gChecks;                                                        \
    if (!(cond)) {                                                           \
      ++ctest::gFailures;                                                    \
      std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);         \
      std::printf(__VA_ARGS__);                                              \
      std::printf("\n");                                                     \
    }                                                                        \
  } while (0)

#define KNOWN_BUG(id, cond, ...)                                             \
  do {                                                                       \
    ++ctest::gChecks;                                                        \
    if (!(cond)) {                                                           \
      if (ctest::gStrict)                                                    \
        ++ctest::gFailures;                                                  \
      else                                                                   \
        ++ctest::gKnownBugs;                                                 \
      std::printf("  %s %s ([%s:%d] %s: ",                                   \
                  ctest::gStrict ? "FAIL" : "KNOWN BUG", id, __FILE__,       \
                  __LINE__, #cond);                                          \
      std::printf(__VA_ARGS__);                                              \
      std::printf(")\n");                                                    \
    }                                                                        \
  } while (0)

#define NOTE(...)                                                            \
  do {                                                                       \
    std::printf("  NOTE: ");                                                 \
    std::printf(__VA_ARGS__);                                                \
    std::printf("\n");                                                       \
  } while (0)

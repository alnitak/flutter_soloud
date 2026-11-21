// Shared check macros for the robustness tests (dart_callback_gate,
// realtime_ring_buffer, vst3_param_changes, simd_pcm, custom_source,
// custom_filter, dart_port, bindings_null_args, player_concurrency,
// out_of_memory).
//
//   CHECK(cond, fmt, ...)      counts a failure when cond is false.
//   KNOWN_BUG(id, cond, fmt, ...)
//                              a check that fails because of a known bug in
//                              src/. Printed as "KNOWN BUG <id> (...)"; it
//                              fails the run only with --strict.
//   robustness::parseArgs(argc, argv) reads --strict.
//   robustness::finish(name) prints the summary and returns the exit code.
#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace robustness {

inline std::atomic<int> gChecks{0};
inline std::atomic<int> gFailures{0};
inline std::atomic<int> gKnownBugs{0};
inline bool gStrict = false;

inline void parseArgs(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
  const char *env = std::getenv("SOLOUD_TEST_STRICT");
  if (env != nullptr && env[0] == '1') gStrict = true;
}

inline int finish(const char *name) {
  std::printf("%s: %d checks, %d failures, %d known bugs\n", name,
              gChecks.load(), gFailures.load(), gKnownBugs.load());
  if (gFailures.load() == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("FAILED\n");
  return 1;
}

} // namespace robustness

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    ++robustness::gChecks;                                                     \
    if (!(cond)) {                                                             \
      ++robustness::gFailures;                                                 \
      std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);           \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
      std::fflush(stdout);                                                     \
    }                                                                          \
  } while (0)

#define KNOWN_BUG(id, cond, ...)                                               \
  do {                                                                         \
    ++robustness::gChecks;                                                     \
    if (!(cond)) {                                                             \
      if (robustness::gStrict)                                                 \
        ++robustness::gFailures;                                               \
      else                                                                     \
        ++robustness::gKnownBugs;                                              \
      std::printf("  %s %s (%s:%d %s: ",                                       \
                  robustness::gStrict ? "FAIL" : "KNOWN BUG", id, __FILE__,    \
                  __LINE__, #cond);                                            \
      std::printf(__VA_ARGS__);                                                \
      std::printf(")\n");                                                      \
      std::fflush(stdout);                                                     \
    }                                                                          \
  } while (0)

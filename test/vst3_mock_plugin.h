// State shared between the mock VST3 plug-in (test/vst3_mock_plugin.cpp,
// built as a .vst3 bundle) and the native host test that loads it
// (test/vst3_host_test.cpp). The test reaches it through the plug-in's
// exported mockVst3State().

#ifndef VST3_MOCK_PLUGIN_H
#define VST3_MOCK_PLUGIN_H

#include <atomic>
#include <cstdint>

namespace vst3mock {

constexpr int kParamCount = 300;
// Level an instrument renders, and the garbage a failing process() leaves in
// its outputs.
constexpr float kInstrumentLevel = 0.25f;
constexpr float kGarbage = 9.0f;

struct State {
  std::atomic<double> setupSampleRate{0.0};
  std::atomic<int> setupMaxBlock{0};
  std::atomic<long> processCalls{0};
  // process() writes kGarbage into its outputs and returns kResultFalse.
  std::atomic<bool> failProcess{false};
  // Parameter changes handed to process(): the last point of each queue,
  // per parameter id (-1 until one arrives), and totals.
  std::atomic<double> lastValue[kParamCount];
  std::atomic<long> pointsReceived{0};
  std::atomic<int> maxQueuesInBlock{0};

  void reset() {
    setupSampleRate.store(0.0);
    setupMaxBlock.store(0);
    processCalls.store(0);
    failProcess.store(false);
    for (auto &v : lastValue) {
      v.store(-1.0);
    }
    pointsReceived.store(0);
    maxQueuesInBlock.store(0);
  }
};

using StateFn = State *(*)();

} // namespace vst3mock

#endif // VST3_MOCK_PLUGIN_H

#ifndef SOLOUD_VST3_EDITOR_SHM_H
#define SOLOUD_VST3_EDITOR_SHM_H

#ifdef SOLOUD_ENABLE_VST3

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SoLoud {

constexpr uint32_t kVst3TelemetryMagic = 0x56535433U; // 'VST3'
constexpr uint32_t kVst3TelemetryVersion = 2U;
constexpr int32_t kVst3TelemetryMaxSamples = 4096;
constexpr size_t kVst3TelemetryTotalSize = 69632;

constexpr size_t kVst3TelemetryOffsetMagic = 0;
constexpr size_t kVst3TelemetryOffsetVersion = 4;
constexpr size_t kVst3TelemetryOffsetSampleRate = 8;
constexpr size_t kVst3TelemetryOffsetMaxBlockSize = 16;
constexpr size_t kVst3TelemetryOffsetNumSamples = 24;
constexpr size_t kVst3TelemetryOffsetInputL = 4096;
constexpr size_t kVst3TelemetryOffsetInputR = 20480;
constexpr size_t kVst3TelemetryOffsetOutputL = 36864;
constexpr size_t kVst3TelemetryOffsetOutputR = 53248;

constexpr size_t kVst3EditorParamQueueSize = 256;
constexpr size_t kVst3EditorMaxInitialParams = 1024;
constexpr size_t kVst3EditorMaxStateBytes = 65536;

struct SoLoudVst3ParamMsg {
  uint32_t paramId;
  uint32_t reserved;
  double value;
};

/// Shared memory layout mapped between the host process (flutter_soloud) and
/// the out-of-process macOS VST3 editor helper.
///
/// The first 69,632 bytes at offset 0 match the flutter_vst3 Vst3SharedMemory
/// binary layout so that flutter_vst3 plugins (such as echo.vst3) can attach
/// their Vst3AudioBufferReader directly to &vst3Telemetry[0] for zero-copy
/// real-time waveform visualization.
struct SoLoudVst3EditorShm {
  alignas(4096) uint8_t vst3Telemetry[kVst3TelemetryTotalSize];

  std::atomic<uint32_t> hostClosed;
  std::atomic<uint32_t> helperClosed;
  std::atomic<uint32_t> helperReady; // 0 = starting, 1 = open, 2 = failed

  uint32_t initialStateSize;
  uint8_t initialState[kVst3EditorMaxStateBytes];

  uint32_t initialParamCount;
  SoLoudVst3ParamMsg initialParams[kVst3EditorMaxInitialParams];

  std::atomic<uint32_t> guiToHostWrite;
  std::atomic<uint32_t> guiToHostRead;
  SoLoudVst3ParamMsg guiToHostQueue[kVst3EditorParamQueueSize];

  std::atomic<uint32_t> hostToGuiWrite;
  std::atomic<uint32_t> hostToGuiRead;
  SoLoudVst3ParamMsg hostToGuiQueue[kVst3EditorParamQueueSize];
};

static_assert(offsetof(SoLoudVst3EditorShm, vst3Telemetry) == 0,
              "vst3Telemetry must be at offset 0");

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3
#endif // SOLOUD_VST3_EDITOR_SHM_H


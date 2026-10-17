#ifndef SOLOUD_VST3_HOST_H
#define SOLOUD_VST3_HOST_H

#ifdef SOLOUD_ENABLE_VST3

#include "../enums.h"
#include "vst3_component_handler.h"
#include "vst3_editor_window.h"
#include "vst3_param_changes.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace SoLoud {

struct Vst3DiscoveredClass {
  std::string classUid;
  std::string name;
  std::string subCategories;
};

struct Vst3ParameterMetadata {
  uint32_t paramId;
  std::string title;
  std::string units;
  double defaultNormalized;
  int32_t stepCount;
  uint32_t flags;
};

/// Represents one loaded VST3 plugin instance (Audio Effect or Instrument).
class Vst3PluginInstance {
public:
  Vst3PluginInstance(int handle, VST3::Hosting::Module::Ptr module,
                     const VST3::Hosting::ClassInfo &classInfo);
  ~Vst3PluginInstance();

  Vst3PluginInstance(const Vst3PluginInstance &) = delete;
  Vst3PluginInstance &operator=(const Vst3PluginInstance &) = delete;

  bool initialize(unsigned int sampleRate, unsigned int maxBlockSize,
                  unsigned int channels,
                  Vst3ComponentHandler::PerformEditCallback onGuiEdit,
                  Vst3ComponentHandler::RestartComponentCallback onRestart);

  void shutdown();
  void stopProcessing() noexcept {
    mIsProcessing.store(false, std::memory_order_release);
  }

  int handle() const noexcept { return mHandle; }
  uint32_t getLatencySamples() const noexcept;

  // --- Real-Time Audio Callbacks (Called on SoLoud Audio Thread) ---
  void processAudioEffectBlock(float *aBuffer, unsigned int aSamples,
                               unsigned int aBufferSize, unsigned int aChannels,
                               float aSamplerate, double aTime,
                               const float *aParams,
                               uint64_t aParamChangedMask,
                               unsigned int aParamCount) noexcept;

  unsigned int processInstrumentBlock(float *aBuffer,
                                      unsigned int aSamplesToRead,
                                      unsigned int aBufferSize,
                                      unsigned int aChannels,
                                      float aSamplerate,
                                      double aStreamTime) noexcept;

  // --- Parameter Reflection & Control (Main / UI Thread) ---
  int getParameterCount();
  bool getParameterInfo(int index, Vst3ParameterMetadata &outInfo) const;
  void syncComponentStateToController();
  double getParameterNormalized(uint32_t paramId,
                                std::string *outDisplayText) const;
  bool setParameterNormalized(uint32_t paramId, double normalizedValue);
  bool mapSlotParameter(int slotParamIndex, uint32_t vst3ParamId);

  // --- Transport & MIDI Events ---
  void setTransport(double bpm, int timeSigNum, int timeSigDenom,
                    bool isPlaying) noexcept;
  bool sendMidiNote(bool isNoteOn, int channel, int pitch, float velocity,
                    int sampleOffset) noexcept;

  // --- Preset / State Serialization ---
  bool getState(std::vector<uint8_t> &outState) const;
  bool setState(const uint8_t *data, size_t size);

  // --- Native Desktop GUI Editor Window (Main Thread Only) ---
  bool hasEditor() const;
  bool openEditor(const char *windowTitle);
  void closeEditor();
  bool isEditorOpen() const noexcept;

  /// Enqueues a parameter change from the native VST3 GUI into the real-time
  /// audio processor queue.
  void enqueueGuiParamChange(Steinberg::Vst::ParamID id,
                             Steinberg::Vst::ParamValue value) noexcept;

private:
  void updateProcessContext(unsigned int aSamples, float aSamplerate,
                            double aTime) noexcept;

  int mHandle = 0;
  // Note: mModule MUST be declared before mPlugProvider, mComponent,
  // mController, mProcessor, and mEditorWindow so that it is destroyed LAST
  // (after all COM pointers from the dynamic library have been released).
  VST3::Hosting::Module::Ptr mModule;
  VST3::Hosting::ClassInfo mClassInfo;
  Steinberg::IPtr<Steinberg::Vst::PlugProvider> mPlugProvider;
  Steinberg::IPtr<Steinberg::Vst::IComponent> mComponent;
  Steinberg::IPtr<Steinberg::Vst::IEditController> mController;
  Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> mProcessor;
  Steinberg::IPtr<Vst3ComponentHandler> mComponentHandler;
  std::unique_ptr<Vst3EditorWindow> mEditorWindow;

  unsigned int mSampleRate = 48000;
  unsigned int mMaxBlockSize = 2048;
  unsigned int mMaxChannels = 2;
  bool mHasAudioInput = false;

  std::atomic<bool> mIsProcessing{false};
  std::atomic<bool> mIsSyncingState{false};
  std::mutex mProcessMutex;

  // Pre-allocated real-time audio buffers (zero heap allocation in process)
  std::vector<float> mScratchInput;
  std::vector<float> mScratchOutput;
  std::array<float *, 8> mInChannelPtrs{};
  std::array<float *, 8> mOutChannelPtrs{};
  Steinberg::Vst::AudioBusBuffers mInputBusBuffers{};
  Steinberg::Vst::AudioBusBuffers mOutputBusBuffers{};
  Steinberg::Vst::ProcessData mProcessData{};
  Steinberg::Vst::ProcessContext mProcessContext{};

  PreallocatedParameterChanges mInputParamChanges;
  PreallocatedParameterChanges mOutputParamChanges;
  PreallocatedEventList mInputEvents;

  LockFreeSpscQueue<Vst3ParamChangeEvent, 1024> mPendingParamQueue;
  LockFreeSpscQueue<Vst3MidiNoteEvent, 1024> mPendingMidiQueue;

  // Maps SoLoud CustomFilterSlot param index (0..15) -> VST3 ParamID (-1 = unmapped)
  std::array<std::atomic<int64_t>, 16> mMappedSlotParams{};

  // Transport state (written from Dart, read on audio thread)
  std::atomic<double> mTempoBpm{120.0};
  std::atomic<int> mTimeSigNum{4};
  std::atomic<int> mTimeSigDenom{4};
  std::atomic<bool> mTransportPlaying{true};
  int64_t mContinuousSamples = 0;
};

/// Singleton manager that tracks loaded VST3 plugin instances and dispatches
/// GUI parameter edit events to Dart.
class Vst3HostManager {
public:
  static Vst3HostManager &instance();

  PlayerErrors scanBundle(const char *bundlePath,
                          std::vector<Vst3DiscoveredClass> &outClasses);

  PlayerErrors loadPlugin(const char *bundlePath, const char *classUidHex,
                          unsigned int sampleRate, unsigned int maxBlockSize,
                          unsigned int channels, int *outPluginHandle);

  PlayerErrors unloadPlugin(int pluginHandle);

  void unloadAll();

  Vst3PluginInstance *getPlugin(int pluginHandle);

  VST3::Hosting::Module::Ptr getOrLoadModule(const std::string &bundlePath,
                                            std::string &outError);

  void setParameterEditCallback(
      void (*callback)(int pluginHandle, unsigned int paramId,
                       double normalizedValue));

  void setRestartComponentCallback(
      dartVst3RestartComponentCallback_t callback);

private:
  Vst3HostManager();
  ~Vst3HostManager();

  mutable std::mutex mMutex;
  Steinberg::Vst::HostApplication mHostApp;
  int mNextHandle = 1;
  std::unordered_map<int, std::unique_ptr<Vst3PluginInstance>> mPlugins;
  std::unordered_map<std::string, VST3::Hosting::Module::Ptr> mLoadedModules;
  std::atomic<void (*)(int, unsigned int, double)> mParamEditCallback{nullptr};
  std::atomic<dartVst3RestartComponentCallback_t> mRestartCallback{nullptr};
};

} // namespace SoLoud

extern "C" {

void vst3FilterDspCallback(float *aBuffer, unsigned int aSamples,
                           unsigned int aBufferSize, unsigned int aChannels,
                           float aSamplerate, double aTime,
                           const float *aParams, uint64_t aParamChangedMask,
                           unsigned int aParamCount, void *aUserData);

unsigned int vst3SourceDspCallback(float *aBuffer, unsigned int aSamplesToRead,
                                   unsigned int aBufferSize,
                                   unsigned int aChannels, float aSamplerate,
                                   double aStreamTime, double aStreamPosition,
                                   uint32_t *aOutHasEnded, void *aUserData);

} // extern "C"

#endif // SOLOUD_ENABLE_VST3
#endif // SOLOUD_VST3_HOST_H

// A minimal VST3 plug-in for the native host tests (run_vst3_host_test.sh).
//
// It exports two classes: "Mock Effect" (stereo in and out, passes its input
// through) and "Mock Instrument" (an event input and a stereo output at a
// constant level). One object is component, processor and edit controller.
// It records what the host hands it in vst3mock::State, which the test
// reads through the exported mockVst3State().

#include "vst3_mock_plugin.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <array>
#include <atomic>
#include <cstring>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

vst3mock::State gState;

const TUID kEffectUid =
    INLINE_UID(0x5D1A0E11, 0x7E2B4C3D, 0x9A8B7C6D, 0x5E4F3A21);
const TUID kInstrumentUid =
    INLINE_UID(0x5D1A0E12, 0x7E2B4C3D, 0x9A8B7C6D, 0x5E4F3A22);

class MockPlugin final : public IComponent,
                         public IAudioProcessor,
                         public IEditController {
public:
  explicit MockPlugin(bool instrument) : mInstrument(instrument) {
    mParams.fill(0.0);
  }

  // FUnknown -----------------------------------------------------------------
  tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override {
    if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(iid, IPluginBase::iid) ||
        FUnknownPrivate::iidEqual(iid, IComponent::iid)) {
      *obj = static_cast<IComponent *>(this);
    } else if (FUnknownPrivate::iidEqual(iid, IAudioProcessor::iid)) {
      *obj = static_cast<IAudioProcessor *>(this);
    } else if (FUnknownPrivate::iidEqual(iid, IEditController::iid)) {
      *obj = static_cast<IEditController *>(this);
    } else {
      *obj = nullptr;
      return kNoInterface;
    }
    addRef();
    return kResultOk;
  }
  uint32 PLUGIN_API addRef() override { return ++mRefs; }
  uint32 PLUGIN_API release() override {
    const uint32 refs = --mRefs;
    if (refs == 0) {
      delete this;
    }
    return refs;
  }

  // IPluginBase (component and controller) ------------------------------------
  tresult PLUGIN_API initialize(FUnknown *) override { return kResultOk; }
  tresult PLUGIN_API terminate() override { return kResultOk; }

  // IComponent ----------------------------------------------------------------
  tresult PLUGIN_API getControllerClassId(TUID) override {
    return kNotImplemented;
  }
  tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
  int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override {
    if (type == kAudio) {
      return (dir == kOutput || !mInstrument) ? 1 : 0;
    }
    if (type == kEvent) {
      return (dir == kInput && mInstrument) ? 1 : 0;
    }
    return 0;
  }
  tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index,
                                BusInfo &bus) override {
    if (index != 0 || getBusCount(type, dir) == 0) {
      return kInvalidArgument;
    }
    std::memset(&bus, 0, sizeof(bus));
    bus.mediaType = type;
    bus.direction = dir;
    bus.channelCount = type == kAudio ? 2 : 16;
    bus.busType = kMain;
    bus.flags = BusInfo::kDefaultActive;
    return kResultOk;
  }
  tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override {
    return kNotImplemented;
  }
  tresult PLUGIN_API activateBus(MediaType, BusDirection, int32,
                                 TBool) override {
    return kResultOk;
  }
  tresult PLUGIN_API setActive(TBool) override { return kResultOk; }
  // Component and controller state (one object): nothing to save.
  tresult PLUGIN_API setState(IBStream *) override { return kResultOk; }
  tresult PLUGIN_API getState(IBStream *) override { return kResultOk; }

  // IAudioProcessor -----------------------------------------------------------
  tresult PLUGIN_API setBusArrangements(SpeakerArrangement *, int32,
                                        SpeakerArrangement *, int32) override {
    return kResultTrue;
  }
  tresult PLUGIN_API getBusArrangement(BusDirection, int32,
                                       SpeakerArrangement &arr) override {
    arr = SpeakerArr::kStereo;
    return kResultOk;
  }
  tresult PLUGIN_API canProcessSampleSize(int32 size) override {
    return size == kSample32 ? kResultTrue : kResultFalse;
  }
  uint32 PLUGIN_API getLatencySamples() override { return 0; }
  tresult PLUGIN_API setupProcessing(ProcessSetup &setup) override {
    gState.setupSampleRate.store(setup.sampleRate);
    gState.setupMaxBlock.store(setup.maxSamplesPerBlock);
    return kResultOk;
  }
  tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
  uint32 PLUGIN_API getTailSamples() override { return 0; }

  tresult PLUGIN_API process(ProcessData &data) override {
    gState.processCalls.fetch_add(1);
    if (IParameterChanges *changes = data.inputParameterChanges) {
      const int32 queues = changes->getParameterCount();
      int seen = gState.maxQueuesInBlock.load();
      while (queues > seen &&
             !gState.maxQueuesInBlock.compare_exchange_weak(seen, queues)) {
      }
      for (int32 i = 0; i < queues; ++i) {
        IParamValueQueue *q = changes->getParameterData(i);
        if (q == nullptr) {
          continue;
        }
        const int32 points = q->getPointCount();
        gState.pointsReceived.fetch_add(points);
        int32 offset = 0;
        ParamValue value = 0.0;
        const ParamID id = q->getParameterId();
        if (points > 0 && id < static_cast<ParamID>(vst3mock::kParamCount) &&
            q->getPoint(points - 1, offset, value) == kResultTrue) {
          gState.lastValue[id].store(value);
        }
      }
    }
    if (data.numOutputs < 1 || data.outputs == nullptr) {
      return kResultOk;
    }
    const bool fail = gState.failProcess.load();
    AudioBusBuffers &out = data.outputs[0];
    for (int32 ch = 0; ch < out.numChannels; ++ch) {
      float *dst = out.channelBuffers32[ch];
      const float *src = nullptr;
      if (!mInstrument && data.numInputs > 0 && data.inputs != nullptr &&
          ch < data.inputs[0].numChannels) {
        src = data.inputs[0].channelBuffers32[ch];
      }
      for (int32 i = 0; i < data.numSamples; ++i) {
        if (fail) {
          dst[i] = vst3mock::kGarbage;
        } else if (mInstrument) {
          dst[i] = vst3mock::kInstrumentLevel;
        } else {
          dst[i] = src != nullptr ? src[i] : 0.0f;
        }
      }
    }
    return fail ? kResultFalse : kResultOk;
  }

  // IEditController -----------------------------------------------------------
  tresult PLUGIN_API setComponentState(IBStream *) override {
    return kResultOk;
  }
  int32 PLUGIN_API getParameterCount() override {
    return vst3mock::kParamCount;
  }
  tresult PLUGIN_API getParameterInfo(int32 index,
                                      ParameterInfo &info) override {
    if (index < 0 || index >= vst3mock::kParamCount) {
      return kInvalidArgument;
    }
    std::memset(&info, 0, sizeof(info));
    info.id = static_cast<ParamID>(index);
    info.title[0] = 'P';
    info.flags = ParameterInfo::kCanAutomate;
    return kResultOk;
  }
  tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue,
                                           String128 string) override {
    string[0] = 0;
    return kResultFalse;
  }
  tresult PLUGIN_API getParamValueByString(ParamID, TChar *,
                                           ParamValue &) override {
    return kResultFalse;
  }
  ParamValue PLUGIN_API normalizedParamToPlain(ParamID,
                                               ParamValue value) override {
    return value;
  }
  ParamValue PLUGIN_API plainParamToNormalized(ParamID,
                                               ParamValue value) override {
    return value;
  }
  ParamValue PLUGIN_API getParamNormalized(ParamID id) override {
    return id < mParams.size() ? mParams[id] : 0.0;
  }
  tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override {
    if (id < mParams.size()) {
      mParams[id] = value;
    }
    return kResultOk;
  }
  tresult PLUGIN_API setComponentHandler(IComponentHandler *) override {
    return kResultOk;
  }
  // No editor view: the host still creates its editor window object.
  IPlugView *PLUGIN_API createView(FIDString) override { return nullptr; }

private:
  const bool mInstrument;
  std::atomic<uint32> mRefs{1};
  std::array<double, vst3mock::kParamCount> mParams{};
};

class MockFactory final : public IPluginFactory {
public:
  tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override {
    if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(iid, IPluginFactory::iid)) {
      *obj = static_cast<IPluginFactory *>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  // Static lifetime.
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override {
    *info = PFactoryInfo("flutter_soloud tests", "", "", PFactoryInfo::kNoFlags);
    return kResultOk;
  }
  int32 PLUGIN_API countClasses() override { return 2; }
  tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *info) override {
    if (index == 0) {
      *info = PClassInfo(kEffectUid, PClassInfo::kManyInstances,
                         kVstAudioEffectClass, "Mock Effect");
    } else if (index == 1) {
      *info = PClassInfo(kInstrumentUid, PClassInfo::kManyInstances,
                         kVstAudioEffectClass, "Mock Instrument");
    } else {
      return kInvalidArgument;
    }
    return kResultOk;
  }
  tresult PLUGIN_API createInstance(FIDString cid, FIDString iid,
                                    void **obj) override {
    bool instrument = false;
    if (FUnknownPrivate::iidEqual(cid, kInstrumentUid)) {
      instrument = true;
    } else if (!FUnknownPrivate::iidEqual(cid, kEffectUid)) {
      *obj = nullptr;
      return kNoInterface;
    }
    auto *plugin = new MockPlugin(instrument);
    const tresult result =
        plugin->queryInterface(reinterpret_cast<const char *>(iid), obj);
    plugin->release();
    return result;
  }
};

} // namespace

#define MOCK_EXPORT extern "C" __attribute__((visibility("default")))

MOCK_EXPORT IPluginFactory *PLUGIN_API GetPluginFactory() {
  static MockFactory factory;
  return &factory;
}

#if defined(__APPLE__)
MOCK_EXPORT bool bundleEntry(CFBundleRef) { return true; }
MOCK_EXPORT bool bundleExit() { return true; }
#else
MOCK_EXPORT bool ModuleEntry(void *) { return true; }
MOCK_EXPORT bool ModuleExit() { return true; }
#endif

MOCK_EXPORT vst3mock::State *mockVst3State() { return &gState; }

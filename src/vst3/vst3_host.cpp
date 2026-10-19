#ifdef SOLOUD_ENABLE_VST3

#include "vst3_host.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/common/commonstringconvert.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
#endif

namespace SoLoud {

namespace {

constexpr uint32_t kSoLoudVst3StateMagic = 0x534C5633U; // 'SLV3'

bool equalsIgnoreCase(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string string128ToUtf8(const Steinberg::Vst::String128 &str) {
  size_t len = 0;
  while (len < 128 && str[len] != 0) {
    ++len;
  }
  std::u16string u16(reinterpret_cast<const char16_t *>(str), len);
  return Steinberg::StringConvert::convert(u16);
}

} // namespace

// ============================================================================
// Vst3PluginInstance
// ============================================================================

Vst3PluginInstance::Vst3PluginInstance(
    int handle, VST3::Hosting::Module::Ptr module,
    const VST3::Hosting::ClassInfo &classInfo)
    : mHandle(handle), mModule(std::move(module)), mClassInfo(classInfo) {
  for (auto &slot : mMappedSlotParams) {
    slot.store(-1, std::memory_order_relaxed);
  }
}

Vst3PluginInstance::~Vst3PluginInstance() { shutdown(); }

bool Vst3PluginInstance::initialize(
    unsigned int sampleRate, unsigned int maxBlockSize, unsigned int channels,
    Vst3ComponentHandler::PerformEditCallback onGuiEdit,
    Vst3ComponentHandler::RestartComponentCallback onRestart) {
  mSampleRate = sampleRate > 0 ? sampleRate : 48000;
  mMaxBlockSize = maxBlockSize > 0 ? maxBlockSize : 2048;
  mMaxChannels = std::clamp<unsigned int>(channels, 1, 8);

  mPlugProvider = Steinberg::owned(new Steinberg::Vst::PlugProvider(
      mModule->getFactory(), mClassInfo, true));
  if (!mPlugProvider) {
    return false;
  }

  // Note: PlugProvider::initialize() calls setupPlugin() and
  // connectComponents(). Even if connectComponents() returns false on a minimal
  // plugin that omits IConnectionPoint, getComponentPtr() will still be valid.
  mPlugProvider->initialize();

  mComponent = mPlugProvider->getComponentPtr();
  mController = mPlugProvider->getControllerPtr();
  if (!mComponent) {
    return false;
  }

  mProcessor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(
      mComponent.get());
  if (!mProcessor) {
    return false;
  }

  // Synchronize initial component state to the edit controller
  if (mController) {
    Steinberg::MemoryStream stream;
    if (mComponent->getState(&stream) == Steinberg::kResultTrue &&
        stream.getSize() > 0) {
      stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
      mController->setComponentState(&stream);
    }

    mComponentHandler = Steinberg::owned(
        new Vst3ComponentHandler(
            [this, onGuiEdit = std::move(onGuiEdit)](
                Steinberg::Vst::ParamID id,
                Steinberg::Vst::ParamValue val) {
              enqueueGuiParamChange(id, val);
              if (onGuiEdit) {
                onGuiEdit(id, val);
              }
            },
            [this, onRestart = std::move(onRestart)](Steinberg::int32 flags) {
              constexpr auto kMask =
                  Steinberg::Vst::RestartFlags::kReloadComponent |
                  Steinberg::Vst::RestartFlags::kParamTitlesChanged;
              if ((flags & kMask) != 0) {
                syncComponentStateToController();
              }
              if (onRestart) {
                onRestart(flags);
              }
            }));
    mController->setComponentHandler(mComponentHandler);
  }

  // Inspect audio and event buses
  const int32_t numAudioInputs = mComponent->getBusCount(
      Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
  const int32_t numAudioOutputs = mComponent->getBusCount(
      Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
  const int32_t numEventInputs = mComponent->getBusCount(
      Steinberg::Vst::kEvent, Steinberg::Vst::kInput);

  mHasAudioInput = (numAudioInputs > 0);

  // Negotiate speaker arrangements
  const Steinberg::Vst::SpeakerArrangement spk =
      (mMaxChannels == 1) ? Steinberg::Vst::SpeakerArr::kMono
                          : Steinberg::Vst::SpeakerArr::kStereo;
  if (numAudioInputs > 0 && numAudioOutputs > 0) {
    std::vector<Steinberg::Vst::SpeakerArrangement> inArr(numAudioInputs, spk);
    std::vector<Steinberg::Vst::SpeakerArrangement> outArr(numAudioOutputs,
                                                           spk);
    mProcessor->setBusArrangements(inArr.data(), numAudioInputs, outArr.data(),
                                   numAudioOutputs);
  } else if (numAudioOutputs > 0) {
    std::vector<Steinberg::Vst::SpeakerArrangement> outArr(numAudioOutputs,
                                                           spk);
    mProcessor->setBusArrangements(nullptr, 0, outArr.data(), numAudioOutputs);
  }

  // Activate primary audio input/output buses (index 0) and deactivate
  // auxiliary sidechain/multi-out buses so the plugin knows only bus 0 is
  // active.
  for (int32_t i = 0; i < numAudioInputs; ++i) {
    mComponent->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i,
                            i == 0 ? 1 : 0);
  }
  for (int32_t i = 0; i < numAudioOutputs; ++i) {
    mComponent->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i,
                            i == 0 ? 1 : 0);
  }
  for (int32_t i = 0; i < numEventInputs; ++i) {
    mComponent->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, i,
                            i == 0 ? 1 : 0);
  }

  // Configure ProcessSetup
  Steinberg::Vst::ProcessSetup setup{};
  setup.processMode = Steinberg::Vst::kRealtime;
  setup.symbolicSampleSize = Steinberg::Vst::kSample32;
  setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(mMaxBlockSize);
  setup.sampleRate = static_cast<Steinberg::Vst::SampleRate>(mSampleRate);

  if (mProcessor->setupProcessing(setup) != Steinberg::kResultOk) {
    return false;
  }

  // Pre-allocate scratch buffers for real-time audio processing
  mScratchInput.assign(static_cast<size_t>(mMaxChannels) * mMaxBlockSize, 0.0f);
  mScratchOutput.assign(static_cast<size_t>(mMaxChannels) * mMaxBlockSize,
                        0.0f);
  for (unsigned int ch = 0; ch < mMaxChannels; ++ch) {
    mInChannelPtrs[ch] =
        mScratchInput.data() + static_cast<size_t>(ch) * mMaxBlockSize;
    mOutChannelPtrs[ch] =
        mScratchOutput.data() + static_cast<size_t>(ch) * mMaxBlockSize;
  }

  mInputBusBuffers.numChannels = static_cast<Steinberg::int32>(mMaxChannels);
  mInputBusBuffers.silenceFlags = 0;
  mInputBusBuffers.channelBuffers32 = mInChannelPtrs.data();

  mOutputBusBuffers.numChannels = static_cast<Steinberg::int32>(mMaxChannels);
  mOutputBusBuffers.silenceFlags = 0;
  mOutputBusBuffers.channelBuffers32 = mOutChannelPtrs.data();

  mProcessData.processMode = Steinberg::Vst::kRealtime;
  mProcessData.symbolicSampleSize = Steinberg::Vst::kSample32;
  mProcessData.numInputs = mHasAudioInput ? 1 : 0;
  mProcessData.numOutputs = (numAudioOutputs > 0) ? 1 : 0;
  mProcessData.inputs = mHasAudioInput ? &mInputBusBuffers : nullptr;
  mProcessData.outputs = (numAudioOutputs > 0) ? &mOutputBusBuffers : nullptr;
  mProcessData.inputParameterChanges = &mInputParamChanges;
  mProcessData.outputParameterChanges = &mOutputParamChanges;
  mProcessData.inputEvents = &mInputEvents;
  mProcessData.outputEvents = nullptr;
  mProcessData.processContext = &mProcessContext;

  if (mComponent->setActive(true) != Steinberg::kResultOk) {
    return false;
  }

  // Note: Some VST3 plugins return kNotImplemented from setProcessing(true),
  // which is valid per the VST3 spec.
  mProcessor->setProcessing(true);
  mIsProcessing.store(true, std::memory_order_release);
  return true;
}

void Vst3PluginInstance::shutdown() {
  mIsProcessing.store(false, std::memory_order_release);

  closeEditor();
  if (mEditorWindow) {
    mEditorWindow.reset();
  }

  std::lock_guard<std::mutex> procLock(mProcessMutex);
  if (mProcessor) {
    mProcessor->setProcessing(false);
  }
  if (mComponent) {
    mComponent->setActive(false);
  }
  if (mController) {
    mController->setComponentHandler(nullptr);
  }

  mComponentHandler = nullptr;
  mProcessor = nullptr;
  mController = nullptr;
  mComponent = nullptr;
  mPlugProvider = nullptr;
  mModule = nullptr;
}

uint32_t Vst3PluginInstance::getLatencySamples() const noexcept {
  if (!mProcessor) {
    return 0;
  }
  return mProcessor->getLatencySamples();
}

void Vst3PluginInstance::enqueueGuiParamChange(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) noexcept {
  const double clamped = std::clamp<double>(value, 0.0, 1.0);
  mPendingParamQueue.push(Vst3ParamChangeEvent{id, clamped, 0});
}

void Vst3PluginInstance::updateProcessContext(unsigned int aSamples,
                                              float aSamplerate,
                                              double aTime) noexcept {
  const double sr = aSamplerate > 0.0f ? static_cast<double>(aSamplerate)
                                       : static_cast<double>(mSampleRate);
  const double bpm = mTempoBpm.load(std::memory_order_relaxed);
  const int num = mTimeSigNum.load(std::memory_order_relaxed);
  const int denom = mTimeSigDenom.load(std::memory_order_relaxed);
  const bool playing = mTransportPlaying.load(std::memory_order_relaxed);

  mProcessContext.state =
      (playing ? Steinberg::Vst::ProcessContext::kPlaying : 0) |
      Steinberg::Vst::ProcessContext::kTempoValid |
      Steinberg::Vst::ProcessContext::kTimeSigValid |
      Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
      Steinberg::Vst::ProcessContext::kBarPositionValid |
      Steinberg::Vst::ProcessContext::kSystemTimeValid;

  mProcessContext.sampleRate = sr;
  mProcessContext.projectTimeSamples = mContinuousSamples;
  mProcessContext.systemTime = static_cast<int64_t>(aTime * 1e9);
  mProcessContext.tempo = bpm > 1.0 ? bpm : 120.0;
  mProcessContext.timeSigNumerator = num > 0 ? num : 4;
  mProcessContext.timeSigDenominator = denom > 0 ? denom : 4;

  const double seconds = static_cast<double>(mContinuousSamples) / sr;
  const double ppq = seconds * (mProcessContext.tempo / 60.0);
  mProcessContext.projectTimeMusic = ppq;

  const double quartersPerBar =
      static_cast<double>(mProcessContext.timeSigNumerator) *
      (4.0 / static_cast<double>(mProcessContext.timeSigDenominator));
  if (quartersPerBar > 0.0) {
    mProcessContext.barPositionMusic =
        std::floor(ppq / quartersPerBar) * quartersPerBar;
  } else {
    mProcessContext.barPositionMusic = 0.0;
  }

  mContinuousSamples += static_cast<int64_t>(aSamples);
}

void Vst3PluginInstance::processAudioEffectBlock(
    float *aBuffer, unsigned int aSamples, unsigned int aBufferSize,
    unsigned int aChannels, float aSamplerate, double aTime,
    const float *aParams, uint64_t aParamChangedMask,
    unsigned int aParamCount) noexcept {
  std::unique_lock<std::mutex> procLock(mProcessMutex, std::try_to_lock);
  if (!procLock.owns_lock() ||
      !mIsProcessing.load(std::memory_order_acquire) || !mProcessor ||
      aBuffer == nullptr || aSamples == 0) {
    return;
  }

  mInputParamChanges.clear();
  mOutputParamChanges.clear();
  mInputEvents.clear();

  // 1. Drain lock-free parameter changes queued from Dart or native VST3 GUI
  Vst3ParamChangeEvent evt;
  while (mPendingParamQueue.pop(evt)) {
    Steinberg::int32 queueIdx = 0;
    if (auto *q = mInputParamChanges.addParameterData(evt.paramId, queueIdx)) {
      Steinberg::int32 ptIdx = 0;
      q->addPoint(evt.sampleOffset, evt.normalizedValue, ptIdx);
    }
  }

  // 2. Forward mapped SoLoud CustomFilterSlot parameters (1..7 / 1..15) when
  //    automated by SoLoud's Fader or LFO (indicated by aParamChangedMask)
  if (aParams != nullptr) {
    const unsigned int maxSlot = std::min<unsigned int>(aParamCount, 16);
    for (unsigned int slotParam = 1; slotParam < maxSlot; ++slotParam) {
      if ((aParamChangedMask & (1ULL << slotParam)) != 0ULL) {
        const int64_t mappedId =
            mMappedSlotParams[slotParam].load(std::memory_order_relaxed);
        if (mappedId >= 0) {
          Steinberg::int32 queueIdx = 0;
          if (auto *q = mInputParamChanges.addParameterData(
                  static_cast<Steinberg::Vst::ParamID>(mappedId), queueIdx)) {
            Steinberg::int32 ptIdx = 0;
            const double clamped =
                std::clamp<double>(aParams[slotParam], 0.0, 1.0);
            q->addPoint(0, clamped, ptIdx);
          }
        }
      }
    }
  }

  const unsigned int numCh = std::min<unsigned int>(aChannels, mMaxChannels);
  unsigned int offset = 0;
  while (offset < aSamples) {
    const unsigned int chunkSamples =
        std::min<unsigned int>(aSamples - offset, mMaxBlockSize);

    if (offset > 0) {
      mInputParamChanges.clear();
      mOutputParamChanges.clear();
      mInputEvents.clear();
    }

    // 3. Map SoLoud's non-interleaved planar buffer
    //    (aBuffer + ch * aBufferSize)
    for (unsigned int ch = 0; ch < numCh; ++ch) {
      mInChannelPtrs[ch] =
          aBuffer + (static_cast<size_t>(ch) * aBufferSize) + offset;
      mOutChannelPtrs[ch] =
          mScratchOutput.data() + (static_cast<size_t>(ch) * mMaxBlockSize);
      std::memset(mOutChannelPtrs[ch], 0, chunkSamples * sizeof(float));
    }

    mInputBusBuffers.numChannels = static_cast<Steinberg::int32>(numCh);
    mInputBusBuffers.silenceFlags = 0;
    mInputBusBuffers.channelBuffers32 = mInChannelPtrs.data();

    mOutputBusBuffers.numChannels = static_cast<Steinberg::int32>(numCh);
    mOutputBusBuffers.silenceFlags = 0;
    mOutputBusBuffers.channelBuffers32 = mOutChannelPtrs.data();

    // 4. Populate synthetic musical transport context
    updateProcessContext(chunkSamples, aSamplerate, aTime);

    mProcessData.numSamples = static_cast<Steinberg::int32>(chunkSamples);
    mProcessData.inputs = mHasAudioInput ? &mInputBusBuffers : nullptr;
    mProcessData.numInputs = mHasAudioInput ? 1 : 0;
    mProcessData.outputs = &mOutputBusBuffers;
    mProcessData.numOutputs = 1;
    mProcessData.inputParameterChanges = &mInputParamChanges;
    mProcessData.outputParameterChanges = &mOutputParamChanges;
    mProcessData.inputEvents = &mInputEvents;
    mProcessData.processContext = &mProcessContext;

    // 5. Invoke VST3 real-time audio processor
    if (mProcessor->process(mProcessData) == Steinberg::kResultOk) {
      for (unsigned int ch = 0; ch < numCh; ++ch) {
        std::memcpy(
            aBuffer + (static_cast<size_t>(ch) * aBufferSize) + offset,
            mOutChannelPtrs[ch], chunkSamples * sizeof(float));
      }
    }
    offset += chunkSamples;
  }
}

unsigned int Vst3PluginInstance::processInstrumentBlock(
    float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize,
    unsigned int aChannels, float aSamplerate, double aStreamTime) noexcept {
  std::unique_lock<std::mutex> procLock(mProcessMutex, std::try_to_lock);
  if (!procLock.owns_lock() ||
      !mIsProcessing.load(std::memory_order_acquire) || !mProcessor ||
      aBuffer == nullptr || aSamplesToRead == 0) {
    return 0;
  }

  const unsigned int numCh = std::min<unsigned int>(aChannels, mMaxChannels);
  const unsigned int firstChunkSamples =
      std::min<unsigned int>(aSamplesToRead, mMaxBlockSize);

  mInputParamChanges.clear();
  mOutputParamChanges.clear();
  mInputEvents.clear();

  // 1. Drain lock-free parameter changes
  Vst3ParamChangeEvent paramEvt;
  while (mPendingParamQueue.pop(paramEvt)) {
    Steinberg::int32 queueIdx = 0;
    if (auto *q =
            mInputParamChanges.addParameterData(paramEvt.paramId, queueIdx)) {
      Steinberg::int32 ptIdx = 0;
      q->addPoint(paramEvt.sampleOffset, paramEvt.normalizedValue, ptIdx);
    }
  }

  // 2. Drain lock-free MIDI NoteOn / NoteOff events
  Vst3MidiNoteEvent midiEvt;
  while (mPendingMidiQueue.pop(midiEvt)) {
    Steinberg::Vst::Event vstEvent{};
    vstEvent.busIndex = 0;
    vstEvent.sampleOffset = std::clamp<int32_t>(
        midiEvt.sampleOffset, 0, static_cast<int32_t>(firstChunkSamples) - 1);
    vstEvent.ppqPosition = mProcessContext.projectTimeMusic;
    vstEvent.flags = Steinberg::Vst::Event::kIsLive;

    if (midiEvt.type == 1) {
      vstEvent.type = Steinberg::Vst::Event::kNoteOnEvent;
      vstEvent.noteOn.channel = midiEvt.channel;
      vstEvent.noteOn.pitch = midiEvt.pitch;
      vstEvent.noteOn.tuning = 0.0f;
      vstEvent.noteOn.velocity = std::clamp(midiEvt.velocity, 0.0f, 1.0f);
      vstEvent.noteOn.length = 0;
      vstEvent.noteOn.noteId = midiEvt.noteId;
    } else {
      vstEvent.type = Steinberg::Vst::Event::kNoteOffEvent;
      vstEvent.noteOff.channel = midiEvt.channel;
      vstEvent.noteOff.pitch = midiEvt.pitch;
      vstEvent.noteOff.velocity = std::clamp(midiEvt.velocity, 0.0f, 1.0f);
      vstEvent.noteOff.noteId = midiEvt.noteId;
      vstEvent.noteOff.tuning = 0.0f;
    }
    mInputEvents.addEvent(vstEvent);
  }

  unsigned int offset = 0;
  while (offset < aSamplesToRead) {
    const unsigned int chunkSamples =
        std::min<unsigned int>(aSamplesToRead - offset, mMaxBlockSize);

    if (offset > 0) {
      mInputParamChanges.clear();
      mOutputParamChanges.clear();
      mInputEvents.clear();
    }

    // 3. Prepare input (silent if instrument declares audio input)
    //    and output channel buffers pointing into SoLoud's buffer
    for (unsigned int ch = 0; ch < numCh; ++ch) {
      mInChannelPtrs[ch] =
          mScratchInput.data() + (static_cast<size_t>(ch) * mMaxBlockSize);
      std::memset(mInChannelPtrs[ch], 0, chunkSamples * sizeof(float));

      mOutChannelPtrs[ch] =
          aBuffer + (static_cast<size_t>(ch) * aBufferSize) + offset;
      std::memset(mOutChannelPtrs[ch], 0, chunkSamples * sizeof(float));
    }

    mInputBusBuffers.numChannels = static_cast<Steinberg::int32>(numCh);
    mInputBusBuffers.silenceFlags = (1ULL << numCh) - 1ULL;
    mInputBusBuffers.channelBuffers32 = mInChannelPtrs.data();

    mOutputBusBuffers.numChannels = static_cast<Steinberg::int32>(numCh);
    mOutputBusBuffers.silenceFlags = 0;
    mOutputBusBuffers.channelBuffers32 = mOutChannelPtrs.data();

    // 4. Populate synthetic musical transport context
    updateProcessContext(chunkSamples, aSamplerate, aStreamTime);

    mProcessData.numSamples = static_cast<Steinberg::int32>(chunkSamples);
    mProcessData.inputs = mHasAudioInput ? &mInputBusBuffers : nullptr;
    mProcessData.numInputs = mHasAudioInput ? 1 : 0;
    mProcessData.outputs = &mOutputBusBuffers;
    mProcessData.numOutputs = 1;
    mProcessData.inputParameterChanges = &mInputParamChanges;
    mProcessData.outputParameterChanges = &mOutputParamChanges;
    mProcessData.inputEvents = &mInputEvents;
    mProcessData.processContext = &mProcessContext;

    mProcessor->process(mProcessData);
    offset += chunkSamples;
  }
  return aSamplesToRead;
}

void Vst3PluginInstance::syncComponentStateToController() {
  if (mIsSyncingState.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  if (mComponent && mController) {
    Steinberg::MemoryStream stream;
    if (mComponent->getState(&stream) == Steinberg::kResultTrue &&
        stream.getSize() > 0) {
      stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
      mController->setComponentState(&stream);
    }
  }
  mIsSyncingState.store(false, std::memory_order_release);
}

int Vst3PluginInstance::getParameterCount() {
  syncComponentStateToController();
  if (!mController) {
    return 0;
  }
  return mController->getParameterCount();
}

bool Vst3PluginInstance::getParameterInfo(
    int index, Vst3ParameterMetadata &outInfo) const {
  if (!mController || index < 0 || index >= mController->getParameterCount()) {
    return false;
  }
  Steinberg::Vst::ParameterInfo info{};
  if (mController->getParameterInfo(index, info) != Steinberg::kResultOk) {
    return false;
  }
  outInfo.paramId = info.id;
  outInfo.title = string128ToUtf8(info.title);
  outInfo.units = string128ToUtf8(info.units);
  outInfo.defaultNormalized = info.defaultNormalizedValue;
  outInfo.stepCount = info.stepCount;
  outInfo.flags = static_cast<uint32_t>(info.flags);
  return true;
}

double Vst3PluginInstance::getParameterNormalized(
    uint32_t paramId, std::string *outDisplayText) const {
  if (!mController) {
    if (outDisplayText) {
      outDisplayText->clear();
    }
    return 0.0;
  }
  const double value = mController->getParamNormalized(paramId);
  if (outDisplayText) {
    Steinberg::Vst::String128 str{};
    if (mController->getParamStringByValue(paramId, value, str) ==
        Steinberg::kResultOk) {
      *outDisplayText = string128ToUtf8(str);
    } else {
      *outDisplayText = std::to_string(value);
    }
  }
  return value;
}

bool Vst3PluginInstance::setParameterNormalized(uint32_t paramId,
                                                double normalizedValue) {
  const double clamped = std::clamp<double>(normalizedValue, 0.0, 1.0);
  mPendingParamQueue.push(Vst3ParamChangeEvent{paramId, clamped, 0});
  if (mController) {
    mController->setParamNormalized(paramId, clamped);
  }
  return true;
}

bool Vst3PluginInstance::mapSlotParameter(int slotParamIndex,
                                          uint32_t vst3ParamId) {
  if (slotParamIndex < 1 ||
      slotParamIndex >= static_cast<int>(mMappedSlotParams.size())) {
    return false;
  }
  mMappedSlotParams[static_cast<size_t>(slotParamIndex)].store(
      static_cast<int64_t>(vst3ParamId), std::memory_order_relaxed);
  return true;
}

void Vst3PluginInstance::setTransport(double bpm, int timeSigNum,
                                      int timeSigDenom,
                                      bool isPlaying) noexcept {
  mTempoBpm.store(bpm > 1.0 ? bpm : 120.0, std::memory_order_relaxed);
  mTimeSigNum.store(timeSigNum > 0 ? timeSigNum : 4, std::memory_order_relaxed);
  mTimeSigDenom.store(timeSigDenom > 0 ? timeSigDenom : 4,
                      std::memory_order_relaxed);
  mTransportPlaying.store(isPlaying, std::memory_order_relaxed);
}

bool Vst3PluginInstance::sendMidiNote(bool isNoteOn, int channel, int pitch,
                                      float velocity,
                                      int sampleOffset) noexcept {
  Vst3MidiNoteEvent evt{};
  evt.type = isNoteOn ? 1 : 0;
  evt.channel = static_cast<int16_t>(std::clamp(channel, 0, 15));
  evt.pitch = static_cast<int16_t>(std::clamp(pitch, 0, 127));
  evt.velocity = std::clamp(velocity, 0.0f, 1.0f);
  evt.noteId = -1;
  evt.sampleOffset = std::max(0, sampleOffset);
  return mPendingMidiQueue.push(evt);
}

bool Vst3PluginInstance::getState(std::vector<uint8_t> &outState) const {
  if (!mComponent) {
    return false;
  }

  Steinberg::MemoryStream compStream;
  mComponent->getState(&compStream);
  const auto compSize = static_cast<uint32_t>(
      std::max<Steinberg::TSize>(0, compStream.getSize()));

  Steinberg::MemoryStream ctrlStream;
  if (mController) {
    mController->getState(&ctrlStream);
  }
  const auto ctrlSize = static_cast<uint32_t>(
      std::max<Steinberg::TSize>(0, ctrlStream.getSize()));

  // Also snapshot parameter values from IEditController so plugins that don't
  // implement custom IBStream state still round-trip all parameter values
  const uint32_t paramCount = mController
      ? static_cast<uint32_t>(std::max(0, mController->getParameterCount()))
      : 0;

  const size_t headerSize = 4 * sizeof(uint32_t);
  const size_t paramBlockSize =
      static_cast<size_t>(paramCount) * (sizeof(uint32_t) + sizeof(double));
  outState.resize(headerSize + compSize + ctrlSize + paramBlockSize);

  uint8_t *dst = outState.data();
  const uint32_t magic = kSoLoudVst3StateMagic;
  std::memcpy(dst, &magic, sizeof(uint32_t));
  dst += sizeof(uint32_t);
  std::memcpy(dst, &compSize, sizeof(uint32_t));
  dst += sizeof(uint32_t);
  std::memcpy(dst, &ctrlSize, sizeof(uint32_t));
  dst += sizeof(uint32_t);
  std::memcpy(dst, &paramCount, sizeof(uint32_t));
  dst += sizeof(uint32_t);

  if (compSize > 0 && compStream.getData() != nullptr) {
    std::memcpy(dst, compStream.getData(), compSize);
    dst += compSize;
  }
  if (ctrlSize > 0 && ctrlStream.getData() != nullptr) {
    std::memcpy(dst, ctrlStream.getData(), ctrlSize);
    dst += ctrlSize;
  }

  for (uint32_t i = 0; i < paramCount; ++i) {
    Steinberg::Vst::ParameterInfo pInfo{};
    uint32_t pid = i;
    double val = 0.0;
    if (mController->getParameterInfo(static_cast<int32_t>(i), pInfo) ==
        Steinberg::kResultOk) {
      pid = pInfo.id;
      val = mController->getParamNormalized(pid);
    }
    std::memcpy(dst, &pid, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    std::memcpy(dst, &val, sizeof(double));
    dst += sizeof(double);
  }

  return true;
}

bool Vst3PluginInstance::setState(const uint8_t *data, size_t size) {
  if (!mComponent || data == nullptr || size < 4 * sizeof(uint32_t)) {
    return false;
  }

  uint32_t magic = 0;
  uint32_t compSize = 0;
  uint32_t ctrlSize = 0;
  uint32_t paramCount = 0;
  std::memcpy(&magic, data, sizeof(uint32_t));
  if (magic != kSoLoudVst3StateMagic) {
    // Treat as raw component stream
    Steinberg::MemoryStream rawStream(const_cast<uint8_t *>(data),
                                      static_cast<Steinberg::TSize>(size));
    if (mComponent->setState(&rawStream) != Steinberg::kResultOk) {
      return false;
    }
    if (mController) {
      rawStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
      mController->setComponentState(&rawStream);
    }
    return true;
  }

  std::memcpy(&compSize, data + sizeof(uint32_t), sizeof(uint32_t));
  std::memcpy(&ctrlSize, data + 2 * sizeof(uint32_t), sizeof(uint32_t));
  std::memcpy(&paramCount, data + 3 * sizeof(uint32_t), sizeof(uint32_t));

  const size_t headerSize = 4 * sizeof(uint32_t);
  if (headerSize + compSize + ctrlSize > size) {
    return false;
  }

  const uint8_t *ptr = data + headerSize;
  if (compSize > 0) {
    Steinberg::MemoryStream compStream(const_cast<uint8_t *>(ptr), compSize);
    mComponent->setState(&compStream);
    if (mController) {
      compStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
      mController->setComponentState(&compStream);
    }
    ptr += compSize;
  }

  if (ctrlSize > 0 && mController) {
    Steinberg::MemoryStream ctrlStream(const_cast<uint8_t *>(ptr), ctrlSize);
    mController->setState(&ctrlStream);
    ptr += ctrlSize;
  }

  const size_t remaining = size - (headerSize + compSize + ctrlSize);
  const size_t entrySize = sizeof(uint32_t) + sizeof(double);
  if (compSize == 0 && paramCount > 0 &&
      remaining >= static_cast<size_t>(paramCount) * entrySize) {
    for (uint32_t i = 0; i < paramCount; ++i) {
      uint32_t pid = 0;
      double val = 0.0;
      std::memcpy(&pid, ptr, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      std::memcpy(&val, ptr, sizeof(double));
      ptr += sizeof(double);
      setParameterNormalized(pid, val);
    }
  }

  return true;
}

bool Vst3PluginInstance::hasEditor() const {
  if (!mController) {
    return false;
  }
  if (mEditorWindow && mEditorWindow->isOpen()) {
    mHasEditorCached.store(1, std::memory_order_release);
    return true;
  }
  const int cached = mHasEditorCached.load(std::memory_order_acquire);
  if (cached >= 0) {
    return cached != 0;
  }
#if defined(__APPLE__)
  __block Steinberg::IPtr<Steinberg::IPlugView> view;
  void (^checkBlock)(void) = ^{
    view = Steinberg::owned(
        mController->createView(Steinberg::Vst::ViewType::kEditor));
  };
  if (pthread_main_np() != 0) {
    checkBlock();
  } else {
    dispatch_sync(dispatch_get_main_queue(), checkBlock);
  }
  const bool result = (view != nullptr);
  mHasEditorCached.store(result ? 1 : 0, std::memory_order_release);
  return result;
#else
  Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(
      mController->createView(Steinberg::Vst::ViewType::kEditor));
  const bool result = (view != nullptr);
  mHasEditorCached.store(result ? 1 : 0, std::memory_order_release);
  return result;
#endif
}

bool Vst3PluginInstance::openEditor(const char *windowTitle) {
  if (!mController) {
    return false;
  }
  if (mEditorWindow && mEditorWindow->isOpen()) {
    return true;
  }
  const char *title =
      (windowTitle != nullptr && windowTitle[0] != '\0')
          ? windowTitle
          : mClassInfo.name().c_str();

#if defined(__linux__) && !defined(__ANDROID__)
  if (mEditorWindow) {
    if (mEditorWindow->open(nullptr, title)) {
      return true;
    }
  }
#endif

#if defined(__APPLE__)
  __block Steinberg::IPtr<Steinberg::IPlugView> view;
  void (^createBlock)(void) = ^{
    view = Steinberg::owned(
        mController->createView(Steinberg::Vst::ViewType::kEditor));
  };
  if (pthread_main_np() != 0) {
    createBlock();
  } else {
    dispatch_sync(dispatch_get_main_queue(), createBlock);
  }
#else
  Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(
      mController->createView(Steinberg::Vst::ViewType::kEditor));
#endif
  if (!view) {
    mHasEditorCached.store(0, std::memory_order_release);
    return false;
  }
  mHasEditorCached.store(1, std::memory_order_release);

  if (!mEditorWindow) {
    mEditorWindow = Vst3EditorWindow::create();
  }
  if (!mEditorWindow) {
    return false;
  }

  return mEditorWindow->open(view.get(), title);
}

void Vst3PluginInstance::closeEditor() {
  if (mEditorWindow) {
    mEditorWindow->close();
#if !defined(__linux__) || defined(__ANDROID__)
    mEditorWindow.reset();
#endif
  }
}

bool Vst3PluginInstance::isEditorOpen() const noexcept {
  return mEditorWindow && mEditorWindow->isOpen();
}

// ============================================================================
// Vst3HostManager
// ============================================================================

Vst3HostManager &Vst3HostManager::instance() {
  static Vst3HostManager mgr;
  return mgr;
}

Vst3HostManager::Vst3HostManager() {
  Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&mHostApp);
  installVst3TerminationObserver();
}

Vst3HostManager::~Vst3HostManager() {
  unloadAll();
}

VST3::Hosting::Module::Ptr Vst3HostManager::getOrLoadModule(
    const std::string &bundlePath, std::string &outError) {
  std::lock_guard<std::mutex> lock(mMutex);
  auto it = mLoadedModules.find(bundlePath);
  if (it != mLoadedModules.end() && it->second) {
    return it->second;
  }
  auto module = VST3::Hosting::Module::create(bundlePath, outError);
  if (module) {
    mLoadedModules[bundlePath] = module;
  }
  return module;
}

PlayerErrors Vst3HostManager::scanBundle(
    const char *bundlePath, std::vector<Vst3DiscoveredClass> &outClasses) {
  outClasses.clear();
  if (bundlePath == nullptr || bundlePath[0] == '\0') {
    return invalidParameter;
  }

  std::string err;
  auto module = getOrLoadModule(bundlePath, err);
  if (!module) {
    return fileLoadFailed;
  }

  const auto &factory = module->getFactory();
  for (const auto &ci : factory.classInfos()) {
    if (ci.category() == kVstAudioEffectClass) {
      Vst3DiscoveredClass dc;
      dc.classUid = ci.ID().toString();
      dc.name = ci.name();
      dc.subCategories = ci.subCategoriesString();
      outClasses.push_back(std::move(dc));
    }
  }

  return noError;
}

PlayerErrors Vst3HostManager::loadPlugin(
    const char *bundlePath, const char *classUidHex, unsigned int sampleRate,
    unsigned int maxBlockSize, unsigned int channels, int *outPluginHandle) {
  if (outPluginHandle != nullptr) {
    *outPluginHandle = 0;
  }
  if (bundlePath == nullptr || bundlePath[0] == '\0' ||
      outPluginHandle == nullptr) {
    return invalidParameter;
  }

  std::string err;
  auto module = getOrLoadModule(bundlePath, err);
  if (!module) {
    return fileLoadFailed;
  }

  const auto &factory = module->getFactory();
  factory.setHostContext(&mHostApp);

  const std::string targetUid =
      (classUidHex != nullptr) ? std::string(classUidHex) : std::string();

  VST3::Hosting::ClassInfo selectedClass;
  bool found = false;
  for (const auto &ci : factory.classInfos()) {
    if (ci.category() != kVstAudioEffectClass) {
      continue;
    }
    if (targetUid.empty() ||
        equalsIgnoreCase(ci.ID().toString(false), targetUid) ||
        equalsIgnoreCase(ci.ID().toString(true), targetUid)) {
      selectedClass = ci;
      found = true;
      break;
    }
  }

  if (!found) {
    return fileLoadFailed;
  }

  std::lock_guard<std::mutex> lock(mMutex);
  const int handle = mNextHandle++;
  auto instance =
      std::make_unique<Vst3PluginInstance>(handle, module, selectedClass);

  auto onGuiEdit = [this, handle](Steinberg::Vst::ParamID id,
                                  Steinberg::Vst::ParamValue val) {
    auto cb = mParamEditCallback.load(std::memory_order_acquire);
    if (cb != nullptr) {
      cb(handle, static_cast<unsigned int>(id), static_cast<double>(val));
    }
  };

  auto onRestart = [this, handle](Steinberg::int32 flags) {
    auto cb = mRestartCallback.load(std::memory_order_acquire);
    if (cb != nullptr) {
      cb(handle, static_cast<int>(flags));
    }
  };

  if (!instance->initialize(sampleRate, maxBlockSize, channels,
                            std::move(onGuiEdit), std::move(onRestart))) {
    return fileLoadFailed;
  }

  *outPluginHandle = handle;
  mPlugins.emplace(handle, std::move(instance));
  return noError;
}

PlayerErrors Vst3HostManager::unloadPlugin(int pluginHandle) {
  std::unique_ptr<Vst3PluginInstance> removed;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mPlugins.find(pluginHandle);
    if (it == mPlugins.end()) {
      return invalidParameter;
    }
    removed = std::move(it->second);
    mPlugins.erase(it);
  }
  if (removed) {
    removed->shutdown();
  }
  return noError;
}

void Vst3HostManager::unloadAll() {
  std::unordered_map<int, std::unique_ptr<Vst3PluginInstance>> toUnload;
  std::unordered_map<std::string, VST3::Hosting::Module::Ptr> modulesToUnload;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    toUnload.swap(mPlugins);
    modulesToUnload.swap(mLoadedModules);
  }
  // 1. Shutdown all active plugin instances: close editors, stop processing,
  //    deactivate, and terminate components/controllers before modules unload.
  for (auto &kv : toUnload) {
    if (kv.second) {
      kv.second->shutdown();
    }
  }
  toUnload.clear();

  // 2. Clear cached hosting modules so bundleExit() is called only after
  //    all instances have been cleanly terminated and released.
  modulesToUnload.clear();
}

Vst3PluginInstance *Vst3HostManager::getPlugin(int pluginHandle) {
  std::lock_guard<std::mutex> lock(mMutex);
  auto it = mPlugins.find(pluginHandle);
  return it != mPlugins.end() ? it->second.get() : nullptr;
}

void Vst3HostManager::setParameterEditCallback(
    void (*callback)(int pluginHandle, unsigned int paramId,
                     double normalizedValue)) {
  mParamEditCallback.store(callback, std::memory_order_release);
}

void Vst3HostManager::setRestartComponentCallback(
    dartVst3RestartComponentCallback_t callback) {
  mRestartCallback.store(callback, std::memory_order_release);
}

} // namespace SoLoud

// ============================================================================
// C Callbacks for CustomFilterSlot and CustomAudioSource
// ============================================================================

extern "C" {

void vst3FilterDspCallback(float *aBuffer, unsigned int aSamples,
                           unsigned int aBufferSize, unsigned int aChannels,
                           float aSamplerate, double aTime,
                           const float *aParams, uint64_t aParamChangedMask,
                           unsigned int aParamCount, void *aUserData) {
  const int handle = static_cast<int>(reinterpret_cast<uintptr_t>(aUserData));
  auto *plugin = SoLoud::Vst3HostManager::instance().getPlugin(handle);
  if (plugin != nullptr) {
    plugin->processAudioEffectBlock(aBuffer, aSamples, aBufferSize, aChannels,
                                    aSamplerate, aTime, aParams,
                                    aParamChangedMask, aParamCount);
  }
}

unsigned int vst3SourceDspCallback(float *aBuffer, unsigned int aSamplesToRead,
                                   unsigned int aBufferSize,
                                   unsigned int aChannels, float aSamplerate,
                                   double aStreamTime, double aStreamPosition,
                                   uint32_t *aOutHasEnded, void *aUserData) {
  (void)aStreamPosition;
  const int handle = static_cast<int>(reinterpret_cast<uintptr_t>(aUserData));
  auto *plugin = SoLoud::Vst3HostManager::instance().getPlugin(handle);
  if (plugin == nullptr) {
    if (aOutHasEnded != nullptr) {
      *aOutHasEnded = 1;
    }
    return 0;
  }
  if (aOutHasEnded != nullptr) {
    *aOutHasEnded = 0;
  }
  return plugin->processInstrumentBlock(
      aBuffer, aSamplesToRead, aBufferSize, aChannels, aSamplerate,
      aStreamTime);
}

} // extern "C"

static_assert(
    std::is_same<decltype(&vst3FilterDspCallback),
                 SoLoudFilterDspCallback_t>::value,
    "vst3FilterDspCallback must match SoLoudFilterDspCallback_t signature");
static_assert(
    std::is_same<decltype(&vst3SourceDspCallback),
                 SoLoudSourceDspCallback_t>::value,
    "vst3SourceDspCallback must match SoLoudSourceDspCallback_t signature");

#endif // SOLOUD_ENABLE_VST3

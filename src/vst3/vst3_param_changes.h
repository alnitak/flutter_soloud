#ifndef SOLOUD_VST3_PARAM_CHANGES_H
#define SOLOUD_VST3_PARAM_CHANGES_H

#ifdef SOLOUD_ENABLE_VST3

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace SoLoud {

struct Vst3ParamChangeEvent {
  Steinberg::Vst::ParamID paramId;
  double normalizedValue;
  int32_t sampleOffset;
};

struct Vst3MidiNoteEvent {
  uint8_t type; // 0 = NoteOff, 1 = NoteOn
  int16_t channel;
  int16_t pitch;
  float velocity;
  int32_t noteId;
  int32_t sampleOffset;
};

/// Single-Producer Single-Consumer (SPSC) lock-free ring buffer.
template <typename T, size_t Capacity>
class LockFreeSpscQueue {
public:
  bool push(const T &item) noexcept {
    const size_t w = mWriteIdx.load(std::memory_order_relaxed);
    const size_t next = (w + 1) % Capacity;
    if (next == mReadIdx.load(std::memory_order_acquire)) {
      return false; // Full
    }
    mStorage[w] = item;
    mWriteIdx.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T &outItem) noexcept {
    const size_t r = mReadIdx.load(std::memory_order_relaxed);
    if (r == mWriteIdx.load(std::memory_order_acquire)) {
      return false; // Empty
    }
    outItem = mStorage[r];
    mReadIdx.store((r + 1) % Capacity, std::memory_order_release);
    return true;
  }

  void clear() noexcept {
    mReadIdx.store(mWriteIdx.load(std::memory_order_relaxed),
                   std::memory_order_release);
  }

private:
  std::array<T, Capacity> mStorage{};
  std::atomic<size_t> mWriteIdx{0};
  std::atomic<size_t> mReadIdx{0};
};

/// Pre-allocated IParamValueQueue with up to kMaxPoints points per block.
class PreallocatedParamValueQueue final
    : public Steinberg::Vst::IParamValueQueue {
public:
  static constexpr int32_t kMaxPoints = 16;

  void reset(Steinberg::Vst::ParamID id) noexcept {
    mParamId = id;
    mPointCount = 0;
  }

  Steinberg::Vst::ParamID PLUGIN_API getParameterId() override {
    return mParamId;
  }

  Steinberg::int32 PLUGIN_API getPointCount() override { return mPointCount; }

  Steinberg::tresult PLUGIN_API getPoint(
      Steinberg::int32 index, Steinberg::int32 &sampleOffset,
      Steinberg::Vst::ParamValue &value) override {
    if (index < 0 || index >= mPointCount) {
      return Steinberg::kResultFalse;
    }
    sampleOffset = mOffsets[index];
    value = mValues[index];
    return Steinberg::kResultTrue;
  }

  Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset,
                                         Steinberg::Vst::ParamValue value,
                                         Steinberg::int32 &index) override {
    if (mPointCount >= kMaxPoints) {
      return Steinberg::kResultFalse;
    }
    index = mPointCount;
    mOffsets[mPointCount] = sampleOffset;
    mValues[mPointCount] = value;
    ++mPointCount;
    return Steinberg::kResultTrue;
  }

  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                               void **obj) override {
    if (Steinberg::FUnknownPrivate::iidEqual(
            _iid, Steinberg::Vst::IParamValueQueue::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid,
                                             Steinberg::FUnknown::iid)) {
      *obj = this;
      return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }

private:
  Steinberg::Vst::ParamID mParamId = 0;
  int32_t mPointCount = 0;
  std::array<int32_t, kMaxPoints> mOffsets{};
  std::array<Steinberg::Vst::ParamValue, kMaxPoints> mValues{};
};

/// Pre-allocated IParameterChanges container for the real-time audio thread.
class PreallocatedParameterChanges final
    : public Steinberg::Vst::IParameterChanges {
public:
  static constexpr int32_t kMaxQueues = 128;

  void clear() noexcept { mUsedCount = 0; }

  Steinberg::int32 PLUGIN_API getParameterCount() override { return mUsedCount; }

  Steinberg::Vst::IParamValueQueue *PLUGIN_API
  getParameterData(Steinberg::int32 index) override {
    if (index < 0 || index >= mUsedCount) {
      return nullptr;
    }
    return &mQueues[index];
  }

  Steinberg::Vst::IParamValueQueue *PLUGIN_API
  addParameterData(const Steinberg::Vst::ParamID &id,
                   Steinberg::int32 &index) override {
    for (int32_t i = 0; i < mUsedCount; ++i) {
      if (mQueues[i].getParameterId() == id) {
        index = i;
        return &mQueues[i];
      }
    }
    if (mUsedCount >= kMaxQueues) {
      return nullptr;
    }
    index = mUsedCount++;
    mQueues[index].reset(id);
    return &mQueues[index];
  }

  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                               void **obj) override {
    if (Steinberg::FUnknownPrivate::iidEqual(
            _iid, Steinberg::Vst::IParameterChanges::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid,
                                             Steinberg::FUnknown::iid)) {
      *obj = this;
      return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }

private:
  int32_t mUsedCount = 0;
  std::array<PreallocatedParamValueQueue, kMaxQueues> mQueues{};
};

/// Pre-allocated IEventList for VST3 Instrument MIDI NoteOn/NoteOff events.
class PreallocatedEventList final : public Steinberg::Vst::IEventList {
public:
  static constexpr int32_t kMaxEvents = 128;

  void clear() noexcept { mCount = 0; }

  Steinberg::int32 PLUGIN_API getEventCount() override { return mCount; }

  Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                         Steinberg::Vst::Event &e) override {
    if (index < 0 || index >= mCount) {
      return Steinberg::kResultFalse;
    }
    e = mEvents[index];
    return Steinberg::kResultTrue;
  }

  Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event &e) override {
    if (mCount >= kMaxEvents) {
      return Steinberg::kResultFalse;
    }
    mEvents[mCount++] = e;
    return Steinberg::kResultTrue;
  }

  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                               void **obj) override {
    if (Steinberg::FUnknownPrivate::iidEqual(
            _iid, Steinberg::Vst::IEventList::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid,
                                             Steinberg::FUnknown::iid)) {
      *obj = this;
      return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }

private:
  int32_t mCount = 0;
  std::array<Steinberg::Vst::Event, kMaxEvents> mEvents{};
};

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3
#endif // SOLOUD_VST3_PARAM_CHANGES_H

#ifndef SOLOUD_VST3_COMPONENT_HANDLER_H
#define SOLOUD_VST3_COMPONENT_HANDLER_H

#ifdef SOLOUD_ENABLE_VST3

#include "../enums.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <atomic>
#include <functional>

namespace SoLoud {

/// Implements Steinberg::Vst::IComponentHandler (and IComponentHandler2) so
/// that parameter edits performed inside a plugin's native editor GUI window
/// are forwarded both to the real-time audio processor queue and to Dart.
class Vst3ComponentHandler final
    : public Steinberg::Vst::IComponentHandler,
      public Steinberg::Vst::IComponentHandler2 {
public:
  using PerformEditCallback =
      std::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue)>;

  explicit Vst3ComponentHandler(PerformEditCallback onPerformEdit)
      : mOnPerformEdit(std::move(onPerformEdit)) {}

  ~Vst3ComponentHandler() = default;

  // --- IComponentHandler ---
  Steinberg::tresult PLUGIN_API
  beginEdit(Steinberg::Vst::ParamID /*id*/) override {
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API
  performEdit(Steinberg::Vst::ParamID id,
              Steinberg::Vst::ParamValue valueNormalized) override {
    if (mOnPerformEdit) {
      mOnPerformEdit(id, valueNormalized);
    }
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API
  endEdit(Steinberg::Vst::ParamID /*id*/) override {
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API
  restartComponent(Steinberg::int32 /*flags*/) override {
    return Steinberg::kResultOk;
  }

  // --- IComponentHandler2 ---
  Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool /*state*/) override {
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API
  requestOpenEditor(Steinberg::FIDString /*name*/) override {
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API startGroupEdit() override {
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API finishGroupEdit() override {
    return Steinberg::kResultOk;
  }

  // --- FUnknown ---
  Steinberg::uint32 PLUGIN_API addRef() override {
    return mRefCount.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  Steinberg::uint32 PLUGIN_API release() override {
    const uint32_t r = mRefCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (r == 0) {
      delete this;
    }
    return r;
  }

  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                               void **obj) override {
    if (Steinberg::FUnknownPrivate::iidEqual(
            _iid, Steinberg::Vst::IComponentHandler::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid,
                                             Steinberg::FUnknown::iid)) {
      addRef();
      *obj = static_cast<Steinberg::Vst::IComponentHandler *>(this);
      return Steinberg::kResultTrue;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(
            _iid, Steinberg::Vst::IComponentHandler2::iid)) {
      addRef();
      *obj = static_cast<Steinberg::Vst::IComponentHandler2 *>(this);
      return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }

private:
  std::atomic<uint32_t> mRefCount{1};
  PerformEditCallback mOnPerformEdit;
};

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3
#endif // SOLOUD_VST3_COMPONENT_HANDLER_H

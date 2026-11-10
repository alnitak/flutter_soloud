#ifndef SOLOUD_VST3_EDITOR_WINDOW_H
#define SOLOUD_VST3_EDITOR_WINDOW_H

#ifdef SOLOUD_ENABLE_VST3

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vst3_editor_shm.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace SoLoud {

struct Vst3RemoteEditorContext {
  std::string bundlePath;
  std::string classUid;
  std::string title;
  Steinberg::Vst::IComponent *component = nullptr;
  Steinberg::Vst::IEditController *controller = nullptr;
  std::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue)>
      onGuiParamEdit;
};

/// Abstract platform window hosting a native VST3 IPlugView editor on desktop
/// (NSWindow on macOS, HWND on Windows, X11 Window + IRunLoop on Linux).
class Vst3EditorWindow : public Steinberg::IPlugFrame {
public:
  virtual ~Vst3EditorWindow() = default;

  /// Creates the platform-specific Vst3EditorWindow implementation.
  static std::unique_ptr<Vst3EditorWindow> create();

  /// Returns true if this plugin bundle should be hosted out-of-process in a
  /// dedicated helper window (e.g., macOS VST3 bundles that embed their own
  /// FlutterMacOS.framework / App.framework).
  virtual bool supportsRemoteEditor(const std::string &bundlePath) const {
    (void)bundlePath;
    return false;
  }

  /// Opens the VST3 editor in an out-of-process helper window.
  virtual bool openRemote(const Vst3RemoteEditorContext &ctx) {
    (void)ctx;
    return false;
  }

  /// Forwards a host parameter change to an out-of-process editor window.
  void notifyParameterChanged(uint32_t paramId,
                              double normalizedValue) noexcept {
    const RemoteShmUse use(*this);
    SoLoudVst3EditorShm *shm = use.shm;
    if (shm == nullptr) {
      return;
    }
    const uint32_t w = shm->hostToGuiWrite.load(std::memory_order_relaxed);
    const uint32_t r = shm->hostToGuiRead.load(std::memory_order_acquire);
    if (w - r < kVst3EditorParamQueueSize) {
      auto &slot = shm->hostToGuiQueue[w % kVst3EditorParamQueueSize];
      slot.paramId = paramId;
      slot.reserved = 0;
      slot.value = normalizedValue;
      shm->hostToGuiWrite.store(w + 1, std::memory_order_release);
    }
  }

  /// Writes real-time audio telemetry to an out-of-process editor window's
  /// shared memory buffer (used by flutter_vst3 oscilloscope visualizers).
  /// Called on the audio thread.
  void writeAudioTelemetry(const float *inL, const float *inR,
                           const float *outL, const float *outR,
                           int32_t numSamples, double sampleRate) noexcept {
    const RemoteShmUse use(*this);
    writeVst3EditorShmTelemetry(use.shm, inL, inR, outL, outR, numSamples,
                                sampleRate);
  }

  /// Opens the floating native OS window and attaches [plugView].
  /// Takes ownership of [plugView]'s reference via IPtr.
  virtual bool open(Steinberg::IPlugView *plugView, const char *title) = 0;

  /// Detaches the view and closes the native OS window if open.
  virtual void close() = 0;

  /// Returns true if the native editor window is currently open.
  virtual bool isOpen() const noexcept = 0;

  // Static lifetime owned by Vst3PluginInstance; refcounting is a no-op.
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                               void **obj) override {
    if (Steinberg::FUnknownPrivate::iidEqual(_iid,
                                             Steinberg::IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid,
                                             Steinberg::FUnknown::iid)) {
      *obj = static_cast<Steinberg::IPlugFrame *>(this);
      return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }

protected:
  /// Makes [shm], the memory mapped for an out-of-process editor, the target
  /// of notifyParameterChanged() and writeAudioTelemetry(). Call it once the
  /// helper is ready and the editor is open.
  void publishRemoteShm(SoLoudVst3EditorShm *shm) noexcept {
    mRemoteShm.store(shm, std::memory_order_seq_cst);
  }

  /// Stops new writes into the published memory and waits for the ones in
  /// progress, on the audio thread for instance. Call it before unmapping.
  void retireRemoteShm() noexcept {
    mRemoteShm.store(nullptr, std::memory_order_seq_cst);
    while (mRemoteShmUsers.load(std::memory_order_seq_cst) != 0) {
      std::this_thread::yield();
    }
  }

private:
  // Pins the published memory for one write. A writer counts itself in
  // before it loads the pointer and retireRemoteShm() clears the pointer
  // before it reads the count, so either the writer sees null or the
  // retiring thread waits for it.
  struct RemoteShmUse {
    explicit RemoteShmUse(Vst3EditorWindow &window) noexcept
        : owner(window) {
      owner.mRemoteShmUsers.fetch_add(1, std::memory_order_seq_cst);
      shm = owner.mRemoteShm.load(std::memory_order_seq_cst);
    }
    ~RemoteShmUse() {
      owner.mRemoteShmUsers.fetch_sub(1, std::memory_order_release);
    }
    RemoteShmUse(const RemoteShmUse &) = delete;
    RemoteShmUse &operator=(const RemoteShmUse &) = delete;

    Vst3EditorWindow &owner;
    SoLoudVst3EditorShm *shm = nullptr;
  };

  std::atomic<SoLoudVst3EditorShm *> mRemoteShm{nullptr};
  std::atomic<uint32_t> mRemoteShmUsers{0};
};

/// Registers a platform-level termination observer (e.g.,
/// NSApplicationWillTerminateNotification on macOS) to cleanly tear down
/// all VST3 instances and host modules before process exit().
void installVst3TerminationObserver();

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3
#endif // SOLOUD_VST3_EDITOR_WINDOW_H

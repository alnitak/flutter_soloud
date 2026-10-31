#ifndef SOLOUD_VST3_EDITOR_WINDOW_H
#define SOLOUD_VST3_EDITOR_WINDOW_H

#ifdef SOLOUD_ENABLE_VST3

#include "pluginterfaces/gui/iplugview.h"
#include <memory>

namespace SoLoud {

/// Abstract platform window hosting a native VST3 IPlugView editor on desktop
/// (NSWindow on macOS, HWND on Windows, X11 Window + IRunLoop on Linux).
class Vst3EditorWindow : public Steinberg::IPlugFrame {
public:
  virtual ~Vst3EditorWindow() = default;

  /// Creates the platform-specific Vst3EditorWindow implementation.
  static std::unique_ptr<Vst3EditorWindow> create();

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
};

/// Registers a platform-level termination observer (e.g.,
/// NSApplicationWillTerminateNotification on macOS) to cleanly tear down
/// all VST3 instances and host modules before process exit().
void installVst3TerminationObserver();

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3
#endif // SOLOUD_VST3_EDITOR_WINDOW_H

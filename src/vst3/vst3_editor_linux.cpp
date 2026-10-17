#if defined(SOLOUD_ENABLE_VST3) && defined(__linux__) && !defined(__ANDROID__)

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vst3_editor_window.h"
#include <atomic>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>

namespace SoLoud {

namespace {

// Minimal X11 & GLib dynamic symbol table so Linux builds do not require
// compile-time libx11-dev or libglib2.0-dev headers.
using XDisplay = void;
using XWindow = unsigned long;
using XAtom = unsigned long;

using FnXOpenDisplay = XDisplay *(*)(const char *);
using FnXCloseDisplay = int (*)(XDisplay *);
using FnXDefaultScreen = int (*)(XDisplay *);
using FnXRootWindow = XWindow (*)(XDisplay *, int);
using FnXCreateSimpleWindow = XWindow (*)(XDisplay *, XWindow, int, int,
                                          unsigned int, unsigned int,
                                          unsigned int, unsigned long,
                                          unsigned long);
using FnXStoreName = int (*)(XDisplay *, XWindow, const char *);
using FnXMapWindow = int (*)(XDisplay *, XWindow);
using FnXDestroyWindow = int (*)(XDisplay *, XWindow);
using FnXResizeWindow = int (*)(XDisplay *, XWindow, unsigned int,
                                unsigned int);
using FnXFlush = int (*)(XDisplay *);
using FnXConnectionNumber = int (*)(XDisplay *);
using FnXPending = int (*)(XDisplay *);
using FnXNextEvent = int (*)(XDisplay *, void *);
using FnXInternAtom = XAtom (*)(XDisplay *, const char *, int);
using FnXSetWMProtocols = int (*)(XDisplay *, XWindow, XAtom *, int);

using GSourceFunc = int (*)(void *);
using GUnixFDSourceFunc = int (*)(int, int, void *);
using FnGTimeoutAdd = unsigned int (*)(unsigned int, GSourceFunc, void *);
using FnGUnixFdAdd = unsigned int (*)(int, int, GUnixFDSourceFunc, void *);
using FnGSourceRemove = int (*)(unsigned int);

struct LinuxGuiSymbols {
  void *x11Lib = nullptr;
  void *glibLib = nullptr;

  FnXOpenDisplay XOpenDisplay = nullptr;
  FnXCloseDisplay XCloseDisplay = nullptr;
  FnXDefaultScreen XDefaultScreen = nullptr;
  FnXRootWindow XRootWindow = nullptr;
  FnXCreateSimpleWindow XCreateSimpleWindow = nullptr;
  FnXStoreName XStoreName = nullptr;
  FnXMapWindow XMapWindow = nullptr;
  FnXDestroyWindow XDestroyWindow = nullptr;
  FnXResizeWindow XResizeWindow = nullptr;
  FnXFlush XFlush = nullptr;
  FnXConnectionNumber XConnectionNumber = nullptr;
  FnXPending XPending = nullptr;
  FnXNextEvent XNextEvent = nullptr;
  FnXInternAtom XInternAtom = nullptr;
  FnXSetWMProtocols XSetWMProtocols = nullptr;

  FnGTimeoutAdd g_timeout_add = nullptr;
  FnGUnixFdAdd g_unix_fd_add = nullptr;
  FnGSourceRemove g_source_remove = nullptr;

  bool load() {
    if (x11Lib != nullptr && glibLib != nullptr) {
      return true;
    }
    x11Lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (x11Lib == nullptr) {
      x11Lib = dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    }
    glibLib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (glibLib == nullptr) {
      glibLib = dlopen("libglib-2.0.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (x11Lib == nullptr || glibLib == nullptr) {
      return false;
    }

    XOpenDisplay = reinterpret_cast<FnXOpenDisplay>(dlsym(x11Lib, "XOpenDisplay"));
    XCloseDisplay = reinterpret_cast<FnXCloseDisplay>(dlsym(x11Lib, "XCloseDisplay"));
    XDefaultScreen = reinterpret_cast<FnXDefaultScreen>(dlsym(x11Lib, "XDefaultScreen"));
    XRootWindow = reinterpret_cast<FnXRootWindow>(dlsym(x11Lib, "XRootWindow"));
    XCreateSimpleWindow = reinterpret_cast<FnXCreateSimpleWindow>(
        dlsym(x11Lib, "XCreateSimpleWindow"));
    XStoreName = reinterpret_cast<FnXStoreName>(dlsym(x11Lib, "XStoreName"));
    XMapWindow = reinterpret_cast<FnXMapWindow>(dlsym(x11Lib, "XMapWindow"));
    XDestroyWindow = reinterpret_cast<FnXDestroyWindow>(dlsym(x11Lib, "XDestroyWindow"));
    XResizeWindow = reinterpret_cast<FnXResizeWindow>(dlsym(x11Lib, "XResizeWindow"));
    XFlush = reinterpret_cast<FnXFlush>(dlsym(x11Lib, "XFlush"));
    XConnectionNumber =
        reinterpret_cast<FnXConnectionNumber>(dlsym(x11Lib, "XConnectionNumber"));
    XPending = reinterpret_cast<FnXPending>(dlsym(x11Lib, "XPending"));
    XNextEvent = reinterpret_cast<FnXNextEvent>(dlsym(x11Lib, "XNextEvent"));
    XInternAtom = reinterpret_cast<FnXInternAtom>(dlsym(x11Lib, "XInternAtom"));
    XSetWMProtocols =
        reinterpret_cast<FnXSetWMProtocols>(dlsym(x11Lib, "XSetWMProtocols"));

    g_timeout_add = reinterpret_cast<FnGTimeoutAdd>(dlsym(glibLib, "g_timeout_add"));
    g_unix_fd_add = reinterpret_cast<FnGUnixFdAdd>(dlsym(glibLib, "g_unix_fd_add"));
    g_source_remove =
        reinterpret_cast<FnGSourceRemove>(dlsym(glibLib, "g_source_remove"));

    return XOpenDisplay && XCloseDisplay && XDefaultScreen && XRootWindow &&
           XCreateSimpleWindow && XStoreName && XMapWindow && XDestroyWindow &&
           XResizeWindow && XFlush && g_timeout_add && g_unix_fd_add &&
           g_source_remove;
  }
};

LinuxGuiSymbols &guiSymbols() {
  static LinuxGuiSymbols syms;
  return syms;
}

} // namespace

class Vst3EditorWindowLinux final : public Vst3EditorWindow,
                                    public Steinberg::Linux::IRunLoop {
public:
  Vst3EditorWindowLinux() = default;
  ~Vst3EditorWindowLinux() override { close(); }

  bool open(Steinberg::IPlugView *plugView, const char *title) override {
    if (plugView == nullptr) {
      return false;
    }
    if (plugView->isPlatformTypeSupported(
            Steinberg::kPlatformTypeX11EmbedWindowID) !=
        Steinberg::kResultTrue) {
      return false;
    }
    if (!guiSymbols().load()) {
      return false;
    }

    close();

    mDisplay = guiSymbols().XOpenDisplay(nullptr);
    if (mDisplay == nullptr) {
      return false;
    }

    Steinberg::ViewRect rect{0, 0, 800, 600};
    plugView->getSize(&rect);
    const unsigned int w =
        rect.getWidth() > 64 ? static_cast<unsigned int>(rect.getWidth()) : 800u;
    const unsigned int h =
        rect.getHeight() > 64 ? static_cast<unsigned int>(rect.getHeight()) : 600u;

    const int screen = guiSymbols().XDefaultScreen(mDisplay);
    const XWindow root = guiSymbols().XRootWindow(mDisplay, screen);
    mWindow = guiSymbols().XCreateSimpleWindow(mDisplay, root, 100, 100, w, h,
                                               0, 0, 0);
    if (mWindow == 0) {
      guiSymbols().XCloseDisplay(mDisplay);
      mDisplay = nullptr;
      return false;
    }

    guiSymbols().XStoreName(mDisplay, mWindow,
                            (title && title[0] != '\0') ? title : "VST3 Plugin");
    if (guiSymbols().XInternAtom && guiSymbols().XSetWMProtocols) {
      mWmDeleteMessage =
          guiSymbols().XInternAtom(mDisplay, "WM_DELETE_WINDOW", 0);
      guiSymbols().XSetWMProtocols(mDisplay, mWindow, &mWmDeleteMessage, 1);
    }
    guiSymbols().XMapWindow(mDisplay, mWindow);
    guiSymbols().XFlush(mDisplay);

    mPlugView = plugView;
    mPlugView->setFrame(this);

    if (mPlugView->attached(reinterpret_cast<void *>(mWindow),
                            Steinberg::kPlatformTypeX11EmbedWindowID) !=
        Steinberg::kResultTrue) {
      mPlugView->setFrame(nullptr);
      mPlugView = nullptr;
      guiSymbols().XDestroyWindow(mDisplay, mWindow);
      mWindow = 0;
      guiSymbols().XCloseDisplay(mDisplay);
      mDisplay = nullptr;
      return false;
    }

    guiSymbols().XFlush(mDisplay);
    mIsOpen.store(true, std::memory_order_release);
    return true;
  }

  void close() override {
    mIsOpen.store(false, std::memory_order_release);
    if (mPlugView != nullptr) {
      mPlugView->removed();
      mPlugView->setFrame(nullptr);
      mPlugView = nullptr;
    }
    for (const auto &kv : mFdSources) {
      if (guiSymbols().g_source_remove) {
        guiSymbols().g_source_remove(kv.second.sourceId);
      }
    }
    mFdSources.clear();
    for (const auto &kv : mTimerSources) {
      if (guiSymbols().g_source_remove) {
        guiSymbols().g_source_remove(kv.second);
      }
    }
    mTimerSources.clear();

    if (mDisplay != nullptr && mWindow != 0) {
      guiSymbols().XDestroyWindow(mDisplay, mWindow);
      guiSymbols().XFlush(mDisplay);
      mWindow = 0;
    }
    if (mDisplay != nullptr) {
      guiSymbols().XCloseDisplay(mDisplay);
      mDisplay = nullptr;
    }
  }

  bool isOpen() const noexcept override {
    return mIsOpen.load(std::memory_order_acquire);
  }

  Steinberg::tresult PLUGIN_API
  resizeView(Steinberg::IPlugView *view,
             Steinberg::ViewRect *newSize) override {
    if (view == nullptr || newSize == nullptr || mDisplay == nullptr ||
        mWindow == 0) {
      return Steinberg::kInvalidArgument;
    }
    guiSymbols().XResizeWindow(
        mDisplay, mWindow, static_cast<unsigned int>(newSize->getWidth()),
        static_cast<unsigned int>(newSize->getHeight()));
    guiSymbols().XFlush(mDisplay);
    view->onSize(newSize);
    return Steinberg::kResultTrue;
  }

  // --- Steinberg::Linux::IRunLoop ---
  Steinberg::tresult PLUGIN_API
  registerEventHandler(Steinberg::Linux::IEventHandler *handler,
                       Steinberg::Linux::FileDescriptor fd) override {
    if (handler == nullptr || !guiSymbols().g_unix_fd_add) {
      return Steinberg::kInvalidArgument;
    }
    // G_IO_IN = 1
    const unsigned int sourceId =
        guiSymbols().g_unix_fd_add(fd, 1, &onFdReadyStatic, handler);
    mFdSources[handler] = FdEntry{fd, sourceId};
    return Steinberg::kResultTrue;
  }

  Steinberg::tresult PLUGIN_API
  unregisterEventHandler(Steinberg::Linux::IEventHandler *handler) override {
    auto it = mFdSources.find(handler);
    if (it == mFdSources.end()) {
      return Steinberg::kResultFalse;
    }
    if (guiSymbols().g_source_remove) {
      guiSymbols().g_source_remove(it->second.sourceId);
    }
    mFdSources.erase(it);
    return Steinberg::kResultTrue;
  }

  Steinberg::tresult PLUGIN_API
  registerTimer(Steinberg::Linux::ITimerHandler *handler,
                Steinberg::Linux::TimerInterval milliseconds) override {
    if (handler == nullptr || !guiSymbols().g_timeout_add) {
      return Steinberg::kInvalidArgument;
    }
    const unsigned int interval =
        milliseconds > 0 ? static_cast<unsigned int>(milliseconds) : 16u;
    const unsigned int sourceId =
        guiSymbols().g_timeout_add(interval, &onTimerStatic, handler);
    mTimerSources[handler] = sourceId;
    return Steinberg::kResultTrue;
  }

  Steinberg::tresult PLUGIN_API
  unregisterTimer(Steinberg::Linux::ITimerHandler *handler) override {
    auto it = mTimerSources.find(handler);
    if (it == mTimerSources.end()) {
      return Steinberg::kResultFalse;
    }
    if (guiSymbols().g_source_remove) {
      guiSymbols().g_source_remove(it->second);
    }
    mTimerSources.erase(it);
    return Steinberg::kResultTrue;
  }

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
    if (Steinberg::FUnknownPrivate::iidEqual(
            _iid, Steinberg::Linux::IRunLoop::iid)) {
      *obj = static_cast<Steinberg::Linux::IRunLoop *>(this);
      return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }

private:
  struct FdEntry {
    int fd;
    unsigned int sourceId;
  };

  static int onFdReadyStatic(int fd, int /*condition*/, void *userData) {
    auto *handler =
        reinterpret_cast<Steinberg::Linux::IEventHandler *>(userData);
    if (handler != nullptr) {
      handler->onFDIsSet(fd);
    }
    return 1; // G_SOURCE_CONTINUE
  }

  static int onTimerStatic(void *userData) {
    auto *handler =
        reinterpret_cast<Steinberg::Linux::ITimerHandler *>(userData);
    if (handler != nullptr) {
      handler->onTimer();
    }
    return 1; // G_SOURCE_CONTINUE
  }

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  XDisplay *mDisplay = nullptr;
  XWindow mWindow = 0;
  XAtom mWmDeleteMessage = 0;
  std::map<Steinberg::Linux::IEventHandler *, FdEntry> mFdSources;
  std::map<Steinberg::Linux::ITimerHandler *, unsigned int> mTimerSources;
  std::atomic<bool> mIsOpen{false};
};

std::unique_ptr<Vst3EditorWindow> Vst3EditorWindow::create() {
  return std::make_unique<Vst3EditorWindowLinux>();
}

void installVst3TerminationObserver() {}

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3 && __linux__ && !__ANDROID__

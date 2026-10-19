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

using FnXInitThreads = int (*)();
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
using FnXMapRaised = int (*)(XDisplay *, XWindow);
using FnXMapSubwindows = int (*)(XDisplay *, XWindow);
using FnXUnmapWindow = int (*)(XDisplay *, XWindow);
using FnXDestroyWindow = int (*)(XDisplay *, XWindow);
using FnXResizeWindow = int (*)(XDisplay *, XWindow, unsigned int,
                                unsigned int);
using FnXFlush = int (*)(XDisplay *);
using FnXConnectionNumber = int (*)(XDisplay *);
using FnXPending = int (*)(XDisplay *);
using FnXNextEvent = int (*)(XDisplay *, void *);
using FnXInternAtom = XAtom (*)(XDisplay *, const char *, int);
using FnXSetWMProtocols = int (*)(XDisplay *, XWindow, XAtom *, int);
using FnXSetErrorHandler = int (*)(int (*)(XDisplay *, void *));
using FnXSync = int (*)(XDisplay *, int);
using FnXCheckTypedWindowEvent = int (*)(XDisplay *, XWindow, int, void *);

using GSourceFunc = int (*)(void *);
using GUnixFDSourceFunc = int (*)(int, int, void *);
using FnGTimeoutAdd = unsigned int (*)(unsigned int, GSourceFunc, void *);
using FnGUnixFdAdd = unsigned int (*)(int, int, GUnixFDSourceFunc, void *);
using FnGSourceRemove = int (*)(unsigned int);
using FnGMainContextIteration = int (*)(void *, int);

using FnGdkErrorTrapPush = void (*)();
using FnGdkErrorTrapPopIgnored = void (*)();
using FnGdkDisplayGetDefault = void *(*)();
using FnGdkX11DisplayGetType = unsigned long (*)();
using FnGTypeCheckInstanceIsA = int (*)(void *, unsigned long);
using FnGdkX11DisplayGetXdisplay = XDisplay *(*)(void *);
using FnGdkSetAllowedBackends = void (*)(const char *);

struct LinuxGuiSymbols {
  void *x11Lib = nullptr;
  void *glibLib = nullptr;
  void *gdkLib = nullptr;

  FnXInitThreads XInitThreads = nullptr;
  FnXOpenDisplay XOpenDisplay = nullptr;
  FnXCloseDisplay XCloseDisplay = nullptr;
  FnXDefaultScreen XDefaultScreen = nullptr;
  FnXRootWindow XRootWindow = nullptr;
  FnXCreateSimpleWindow XCreateSimpleWindow = nullptr;
  FnXStoreName XStoreName = nullptr;
  FnXMapWindow XMapWindow = nullptr;
  FnXMapRaised XMapRaised = nullptr;
  FnXMapSubwindows XMapSubwindows = nullptr;
  FnXUnmapWindow XUnmapWindow = nullptr;
  FnXDestroyWindow XDestroyWindow = nullptr;
  FnXResizeWindow XResizeWindow = nullptr;
  FnXFlush XFlush = nullptr;
  FnXSync XSync = nullptr;
  FnXConnectionNumber XConnectionNumber = nullptr;
  FnXPending XPending = nullptr;
  FnXNextEvent XNextEvent = nullptr;
  FnXInternAtom XInternAtom = nullptr;
  FnXSetWMProtocols XSetWMProtocols = nullptr;
  FnXSetErrorHandler XSetErrorHandler = nullptr;
  FnXCheckTypedWindowEvent XCheckTypedWindowEvent = nullptr;

  FnGTimeoutAdd g_timeout_add = nullptr;
  FnGUnixFdAdd g_unix_fd_add = nullptr;
  FnGSourceRemove g_source_remove = nullptr;
  FnGMainContextIteration g_main_context_iteration = nullptr;

  FnGdkErrorTrapPush gdk_error_trap_push = nullptr;
  FnGdkErrorTrapPopIgnored gdk_error_trap_pop_ignored = nullptr;
  FnGdkDisplayGetDefault gdk_display_get_default = nullptr;
  FnGdkX11DisplayGetType gdk_x11_display_get_type = nullptr;
  FnGTypeCheckInstanceIsA g_type_check_instance_is_a = nullptr;
  FnGdkX11DisplayGetXdisplay gdk_x11_display_get_xdisplay = nullptr;
  FnGdkSetAllowedBackends gdk_set_allowed_backends = nullptr;

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
    gdkLib = dlopen("libgdk-3.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (gdkLib == nullptr) {
      gdkLib = dlopen("libgdk-3.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (x11Lib == nullptr || glibLib == nullptr) {
      return false;
    }

    XInitThreads = reinterpret_cast<FnXInitThreads>(dlsym(x11Lib, "XInitThreads"));
    XOpenDisplay = reinterpret_cast<FnXOpenDisplay>(dlsym(x11Lib, "XOpenDisplay"));
    XCloseDisplay = reinterpret_cast<FnXCloseDisplay>(dlsym(x11Lib, "XCloseDisplay"));
    XDefaultScreen = reinterpret_cast<FnXDefaultScreen>(dlsym(x11Lib, "XDefaultScreen"));
    XRootWindow = reinterpret_cast<FnXRootWindow>(dlsym(x11Lib, "XRootWindow"));
    XCreateSimpleWindow = reinterpret_cast<FnXCreateSimpleWindow>(
        dlsym(x11Lib, "XCreateSimpleWindow"));
    XStoreName = reinterpret_cast<FnXStoreName>(dlsym(x11Lib, "XStoreName"));
    XMapWindow = reinterpret_cast<FnXMapWindow>(dlsym(x11Lib, "XMapWindow"));
    XMapRaised = reinterpret_cast<FnXMapRaised>(dlsym(x11Lib, "XMapRaised"));
    XMapSubwindows = reinterpret_cast<FnXMapSubwindows>(dlsym(x11Lib, "XMapSubwindows"));
    XUnmapWindow = reinterpret_cast<FnXUnmapWindow>(dlsym(x11Lib, "XUnmapWindow"));
    XDestroyWindow = reinterpret_cast<FnXDestroyWindow>(dlsym(x11Lib, "XDestroyWindow"));
    XResizeWindow = reinterpret_cast<FnXResizeWindow>(dlsym(x11Lib, "XResizeWindow"));
    XFlush = reinterpret_cast<FnXFlush>(dlsym(x11Lib, "XFlush"));
    XSync = reinterpret_cast<FnXSync>(dlsym(x11Lib, "XSync"));
    XConnectionNumber =
        reinterpret_cast<FnXConnectionNumber>(dlsym(x11Lib, "XConnectionNumber"));
    XPending = reinterpret_cast<FnXPending>(dlsym(x11Lib, "XPending"));
    XNextEvent = reinterpret_cast<FnXNextEvent>(dlsym(x11Lib, "XNextEvent"));
    XInternAtom = reinterpret_cast<FnXInternAtom>(dlsym(x11Lib, "XInternAtom"));
    XSetWMProtocols =
        reinterpret_cast<FnXSetWMProtocols>(dlsym(x11Lib, "XSetWMProtocols"));
    XSetErrorHandler =
        reinterpret_cast<FnXSetErrorHandler>(dlsym(x11Lib, "XSetErrorHandler"));
    XCheckTypedWindowEvent =
        reinterpret_cast<FnXCheckTypedWindowEvent>(dlsym(x11Lib, "XCheckTypedWindowEvent"));

    g_timeout_add = reinterpret_cast<FnGTimeoutAdd>(dlsym(glibLib, "g_timeout_add"));
    g_unix_fd_add = reinterpret_cast<FnGUnixFdAdd>(dlsym(glibLib, "g_unix_fd_add"));
    g_source_remove =
        reinterpret_cast<FnGSourceRemove>(dlsym(glibLib, "g_source_remove"));
    g_main_context_iteration = reinterpret_cast<FnGMainContextIteration>(
        dlsym(glibLib, "g_main_context_iteration"));

    if (gdkLib != nullptr) {
      gdk_error_trap_push = reinterpret_cast<FnGdkErrorTrapPush>(dlsym(gdkLib, "gdk_error_trap_push"));
      gdk_error_trap_pop_ignored = reinterpret_cast<FnGdkErrorTrapPopIgnored>(dlsym(gdkLib, "gdk_error_trap_pop_ignored"));
      gdk_display_get_default = reinterpret_cast<FnGdkDisplayGetDefault>(dlsym(gdkLib, "gdk_display_get_default"));
      gdk_x11_display_get_type = reinterpret_cast<FnGdkX11DisplayGetType>(dlsym(gdkLib, "gdk_x11_display_get_type"));
      g_type_check_instance_is_a = reinterpret_cast<FnGTypeCheckInstanceIsA>(dlsym(gdkLib, "g_type_check_instance_is_a"));
      gdk_x11_display_get_xdisplay = reinterpret_cast<FnGdkX11DisplayGetXdisplay>(dlsym(gdkLib, "gdk_x11_display_get_xdisplay"));
      gdk_set_allowed_backends = reinterpret_cast<FnGdkSetAllowedBackends>(dlsym(gdkLib, "gdk_set_allowed_backends"));
    }

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

struct XErrorEventPartial {
  int type;
  void *display;
  unsigned long resourceid;
  unsigned long serial;
  unsigned char error_code;
  unsigned char request_code;
  unsigned char minor_code;
};

static int customXErrorHandler(XDisplay * /*d*/, void *ev) {
  if (ev != nullptr) {
    auto *err = reinterpret_cast<XErrorEventPartial *>(ev);
    fprintf(stderr,
            "[vst3_editor_linux] X11 error: code=%u, req=%u, minor=%u, res=0x%lx\n",
            static_cast<unsigned int>(err->error_code),
            static_cast<unsigned int>(err->request_code),
            static_cast<unsigned int>(err->minor_code),
            err->resourceid);
    // Ignore harmless asynchronous X11 race condition errors during unmap/destroy
    if (err->error_code == 3 /* BadWindow */ ||
        err->error_code == 9 /* BadDrawable */ ||
        err->error_code == 8 /* BadMatch */) {
      return 0;
    }
  }
  return 0;
}

} // namespace

class Vst3EditorWindowLinux final : public Vst3EditorWindow,
                                    public Steinberg::Linux::IRunLoop {
public:
  static inline XDisplay *sSharedDisplay = nullptr;
  static inline XWindow sSharedWindow = 0;
  static inline XAtom sSharedWmDelete = 0;
  static inline Vst3EditorWindowLinux *sActiveInstance = nullptr;

  Vst3EditorWindowLinux() = default;
  ~Vst3EditorWindowLinux() override { destroy(); }

  bool open(Steinberg::IPlugView *plugView, const char *title) override {
    fprintf(stderr, "[vst3_editor_linux] open() called for title='%s', plugView=%p\n",
            title ? title : "", plugView);
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

    if (guiSymbols().XInitThreads) {
      guiSymbols().XInitThreads();
    }

    setenv("GDK_BACKEND", "x11", 1);
    if (guiSymbols().gdk_set_allowed_backends) {
      guiSymbols().gdk_set_allowed_backends("x11");
    }

    if (sSharedDisplay == nullptr) {
      if (guiSymbols().gdk_display_get_default &&
          guiSymbols().gdk_x11_display_get_type &&
          guiSymbols().g_type_check_instance_is_a) {
        void* disp = guiSymbols().gdk_display_get_default();
        if (disp != nullptr) {
          if (!guiSymbols().g_type_check_instance_is_a(
                  disp, guiSymbols().gdk_x11_display_get_type())) {
            fprintf(stderr,
                    "flutter_soloud: VST3 plugin GUI embedding requires an X11 backend (GDK_BACKEND=x11).\n"
                    "The current process is running with Wayland GDK backend. Plugin GUI cannot be embedded.\n");
            return false;
          }
        }
      }
      if (guiSymbols().XOpenDisplay) {
        sSharedDisplay = guiSymbols().XOpenDisplay(nullptr);
      }
    }
    if (sSharedDisplay == nullptr) {
      return false;
    }

    if (guiSymbols().XSetErrorHandler) {
      guiSymbols().XSetErrorHandler(&customXErrorHandler);
    }

    Steinberg::ViewRect rect{0, 0, 800, 600};
    plugView->getSize(&rect);
    const unsigned int w =
        rect.getWidth() > 64 ? static_cast<unsigned int>(rect.getWidth()) : 800u;
    const unsigned int h =
        rect.getHeight() > 64 ? static_cast<unsigned int>(rect.getHeight()) : 600u;

    if (sSharedWindow == 0) {
      const int screen = guiSymbols().XDefaultScreen(sSharedDisplay);
      const XWindow root = guiSymbols().XRootWindow(sSharedDisplay, screen);
      sSharedWindow = guiSymbols().XCreateSimpleWindow(sSharedDisplay, root, 100, 100, w, h,
                                                       0, 0, 0);
      if (sSharedWindow == 0) {
        return false;
      }
      if (guiSymbols().XInternAtom && guiSymbols().XSetWMProtocols) {
        sSharedWmDelete =
            guiSymbols().XInternAtom(sSharedDisplay, "WM_DELETE_WINDOW", 0);
        if (sSharedWmDelete != 0) {
          guiSymbols().XSetWMProtocols(sSharedDisplay, sSharedWindow, &sSharedWmDelete, 1);
        }
      }
    } else {
      if (guiSymbols().XResizeWindow) {
        guiSymbols().XResizeWindow(sSharedDisplay, sSharedWindow, w, h);
      }
    }

    if (title != nullptr && title[0] != '\0' && guiSymbols().XStoreName) {
      guiSymbols().XStoreName(sSharedDisplay, sSharedWindow, title);
    }

    if (guiSymbols().XMapRaised) {
      guiSymbols().XMapRaised(sSharedDisplay, sSharedWindow);
    } else if (guiSymbols().XMapWindow) {
      guiSymbols().XMapWindow(sSharedDisplay, sSharedWindow);
    }
    if (guiSymbols().XMapSubwindows) {
      guiSymbols().XMapSubwindows(sSharedDisplay, sSharedWindow);
    }
    if (guiSymbols().XFlush) {
      guiSymbols().XFlush(sSharedDisplay);
    }

    if (mPlugView != plugView) {
      if (mPlugView != nullptr) {
        fprintf(stderr, "[vst3_editor_linux] removing old plugView %p\n", mPlugView.get());
        mPlugView->removed();
        mPlugView->setFrame(nullptr);
      }
      mPlugView = plugView;
      mPlugView->setFrame(this);
      fprintf(stderr, "[vst3_editor_linux] attaching plugView %p to window 0x%lx...\n",
              mPlugView.get(), sSharedWindow);
      if (mPlugView->attached(reinterpret_cast<void *>(sSharedWindow),
                              Steinberg::kPlatformTypeX11EmbedWindowID) !=
          Steinberg::kResultTrue) {
        fprintf(stderr, "[vst3_editor_linux] plugView->attached FAILED!\n");
        mPlugView->setFrame(nullptr);
        mPlugView = nullptr;
        return false;
      }
      fprintf(stderr, "[vst3_editor_linux] plugView->attached SUCCESS\n");
    }

    if (guiSymbols().XMapRaised) {
      guiSymbols().XMapRaised(sSharedDisplay, sSharedWindow);
    } else if (guiSymbols().XMapWindow) {
      guiSymbols().XMapWindow(sSharedDisplay, sSharedWindow);
    }
    if (guiSymbols().XMapSubwindows) {
      guiSymbols().XMapSubwindows(sSharedDisplay, sSharedWindow);
    }
    if (guiSymbols().XFlush) {
      guiSymbols().XFlush(sSharedDisplay);
    }
    if (guiSymbols().g_main_context_iteration) {
      while (guiSymbols().g_main_context_iteration(nullptr, 0)) {}
    }
    mPlugView->onFocus(1);

    sActiveInstance = this;
    if (guiSymbols().g_timeout_add && mEventTimerId == 0) {
      mEventTimerId = guiSymbols().g_timeout_add(16, &onEventPollStatic, this);
    }

    mIsOpen.store(true, std::memory_order_release);
    return true;
  }

  void close() override {
    fprintf(stderr, "[vst3_editor_linux] close() called, window=0x%lx\n", sSharedWindow);
    mIsOpen.store(false, std::memory_order_release);
    if (mEventTimerId != 0 && guiSymbols().g_source_remove) {
      guiSymbols().g_source_remove(mEventTimerId);
      mEventTimerId = 0;
    }
    if (sActiveInstance == this) {
      sActiveInstance = nullptr;
    }
    if (sSharedDisplay != nullptr && sSharedWindow != 0) {
      if (guiSymbols().gdk_error_trap_push) {
        guiSymbols().gdk_error_trap_push();
      }
      if (guiSymbols().XUnmapWindow) {
        guiSymbols().XUnmapWindow(sSharedDisplay, sSharedWindow);
      }
      if (guiSymbols().XFlush) {
        guiSymbols().XFlush(sSharedDisplay);
      }
      if (guiSymbols().g_main_context_iteration) {
        while (guiSymbols().g_main_context_iteration(nullptr, 0)) {}
      }
      if (guiSymbols().gdk_error_trap_pop_ignored) {
        guiSymbols().gdk_error_trap_pop_ignored();
      }
    }
  }

  void destroy() {
    fprintf(stderr, "[vst3_editor_linux] destroy() called, window=0x%lx, plugView=%p\n",
            sSharedWindow, mPlugView.get());
    close();
    if (guiSymbols().gdk_error_trap_push) {
      guiSymbols().gdk_error_trap_push();
    }
    if (guiSymbols().XSetErrorHandler) {
      guiSymbols().XSetErrorHandler(&customXErrorHandler);
    }
    if (mPlugView != nullptr) {
      fprintf(stderr, "[vst3_editor_linux] calling mPlugView->removed()...\n");
      mPlugView->removed();
      mPlugView->setFrame(nullptr);
      mPlugView = nullptr;
    }
    if (guiSymbols().g_main_context_iteration) {
      while (guiSymbols().g_main_context_iteration(nullptr, 0)) {}
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

    if (guiSymbols().gdk_error_trap_pop_ignored) {
      guiSymbols().gdk_error_trap_pop_ignored();
    }
  }

  bool isOpen() const noexcept override {
    return mIsOpen.load(std::memory_order_acquire);
  }

  Steinberg::tresult PLUGIN_API
  resizeView(Steinberg::IPlugView *view,
             Steinberg::ViewRect *newSize) override {
    if (view == nullptr || newSize == nullptr || view != mPlugView ||
        sSharedDisplay == nullptr || sSharedWindow == 0) {
      return Steinberg::kInvalidArgument;
    }
    if (mInResize.load(std::memory_order_acquire)) {
      return Steinberg::kResultFalse;
    }
    Steinberg::ViewRect r{0, 0, 0, 0};
    if (view->getSize(&r) != Steinberg::kResultTrue) {
      return Steinberg::kInternalError;
    }
    if (r.left == newSize->left && r.top == newSize->top &&
        r.right == newSize->right && r.bottom == newSize->bottom) {
      return Steinberg::kResultTrue;
    }
    mInResize.store(true, std::memory_order_release);
    const unsigned int w = static_cast<unsigned int>(newSize->getWidth());
    const unsigned int h = static_cast<unsigned int>(newSize->getHeight());
    if (guiSymbols().XResizeWindow) {
      guiSymbols().XResizeWindow(sSharedDisplay, sSharedWindow, w, h);
    }
    if (r.left != newSize->left || r.top != newSize->top ||
        r.right != newSize->right || r.bottom != newSize->bottom) {
      view->onSize(newSize);
    }
    guiSymbols().XFlush(sSharedDisplay);
    mInResize.store(false, std::memory_order_release);
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

  static int onEventPollStatic(void *userData) {
    auto *self = static_cast<Vst3EditorWindowLinux *>(userData);
    if (!self || !sSharedDisplay || sSharedWindow == 0) {
      return 0; // G_SOURCE_REMOVE
    }
    if (guiSymbols().XCheckTypedWindowEvent) {
      unsigned long eventBuf[32] = {0};
      while (guiSymbols().XCheckTypedWindowEvent(sSharedDisplay, sSharedWindow,
                                                 33 /* ClientMessage */,
                                                 eventBuf)) {
        unsigned long data0 = eventBuf[8];
        if (data0 == sSharedWmDelete) {
          self->mEventTimerId = 0;
          self->close();
          return 0; // G_SOURCE_REMOVE
        }
      }
    }
    return 1; // G_SOURCE_CONTINUE
  }

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  unsigned int mEventTimerId = 0;
  std::map<Steinberg::Linux::IEventHandler *, FdEntry> mFdSources;
  std::map<Steinberg::Linux::ITimerHandler *, unsigned int> mTimerSources;
  std::atomic<bool> mIsOpen{false};
  std::atomic<bool> mInResize{false};
};

std::unique_ptr<Vst3EditorWindow> Vst3EditorWindow::create() {
  return std::make_unique<Vst3EditorWindowLinux>();
}

void installVst3TerminationObserver() {}

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3 && __linux__ && !__ANDROID__

#if defined(SOLOUD_ENABLE_VST3) && defined(__linux__) && !defined(__ANDROID__)

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "vst3_editor_shm.h"
#include "vst3_editor_window.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <map>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
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
using FnXSelectInput = int (*)(XDisplay *, XWindow, long);
using FnXSetWMNormalHints = void (*)(XDisplay *, XWindow, void *);

struct XSizeHintsDef {
  long flags;
  int x, y;
  int width, height;
  int min_width, min_height;
  int max_width, max_height;
  int width_inc, height_inc;
  struct {
    int x;
    int y;
  } min_aspect, max_aspect;
  int base_width, base_height;
  int win_gravity;
};

constexpr long kPMinSize = (1L << 4);
constexpr long kPMaxSize = (1L << 5);
constexpr long kPAspect = (1L << 7);

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
  FnXSelectInput XSelectInput = nullptr;
  FnXSetWMNormalHints XSetWMNormalHints = nullptr;

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
    XSelectInput =
        reinterpret_cast<FnXSelectInput>(dlsym(x11Lib, "XSelectInput"));
    XSetWMNormalHints =
        reinterpret_cast<FnXSetWMNormalHints>(dlsym(x11Lib, "XSetWMNormalHints"));

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

  static std::string resolveHelperPath() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&Vst3EditorWindowLinux::resolveHelperPath),
               &info) &&
        info.dli_fname != nullptr) {
      std::string dir(info.dli_fname);
      const size_t slash = dir.find_last_of('/');
      if (slash != std::string::npos) {
        std::string adjacent = dir.substr(0, slash + 1) + "soloud_vst3_editor_helper";
        if (::access(adjacent.c_str(), X_OK) == 0) {
          return adjacent;
        }
      }
    }
#if defined(SOLOUD_VST3_HELPER_PATH)
    if (::access(SOLOUD_VST3_HELPER_PATH, X_OK) == 0) {
      return SOLOUD_VST3_HELPER_PATH;
    }
#endif
    return {};
  }

  bool supportsRemoteEditor(const std::string &bundlePath) const override {
#if defined(SOLOUD_VST3_HELPER_MAIN)
    (void)bundlePath;
    return false;
#else
    if (bundlePath.empty() || resolveHelperPath().empty()) {
      return false;
    }
    static const char *const kMarkers[] = {
        "/Contents/x86_64-linux/libflutter_linux_gtk.so",
        "/Contents/aarch64-linux/libflutter_linux_gtk.so",
        "/Contents/x86_64-linux/data/flutter_assets",
        "/Contents/aarch64-linux/data/flutter_assets",
    };
    struct stat st{};
    for (const char *suffix : kMarkers) {
      const std::string p = bundlePath + suffix;
      if (::stat(p.c_str(), &st) == 0) {
        return true;
      }
    }
    return false;
#endif
  }

  bool openRemote(const Vst3RemoteEditorContext &ctx) override {
#if defined(SOLOUD_VST3_HELPER_MAIN)
    (void)ctx;
    return false;
#else
    if (!supportsRemoteEditor(ctx.bundlePath) || ctx.controller == nullptr) {
      return false;
    }
    close();
    guiSymbols().load();

    static std::atomic<uint32_t> sShmCounter{0};
    const std::string shmPath =
        "/tmp/soloud_vst3_shm_" + std::to_string(getpid()) + "_" +
        std::to_string(++sShmCounter) + ".bin";
    const int fd = ::open(shmPath.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) {
      return false;
    }
    if (::ftruncate(fd, sizeof(SoLoudVst3EditorShm)) != 0) {
      ::close(fd);
      ::unlink(shmPath.c_str());
      return false;
    }
    void *mapped = ::mmap(nullptr, sizeof(SoLoudVst3EditorShm),
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED || mapped == nullptr) {
      ::unlink(shmPath.c_str());
      return false;
    }

    mShm = static_cast<SoLoudVst3EditorShm *>(mapped);
    initVst3EditorShmHeader(mShm);

    if (ctx.component != nullptr) {
      Steinberg::MemoryStream compStream;
      if (ctx.component->getState(&compStream) == Steinberg::kResultOk &&
          compStream.getSize() > 0 &&
          static_cast<size_t>(compStream.getSize()) <=
              kVst3EditorMaxStateBytes &&
          compStream.getData() != nullptr) {
        mShm->initialStateSize = static_cast<uint32_t>(compStream.getSize());
        std::memcpy(mShm->initialState, compStream.getData(),
                    mShm->initialStateSize);
      }
    }

    const int32_t pCount = ctx.controller->getParameterCount();
    uint32_t stored = 0;
    for (int32_t i = 0;
         i < pCount && stored < kVst3EditorMaxInitialParams; ++i) {
      Steinberg::Vst::ParameterInfo pInfo{};
      if (ctx.controller->getParameterInfo(i, pInfo) == Steinberg::kResultOk) {
        mShm->initialParams[stored].paramId = pInfo.id;
        mShm->initialParams[stored].reserved = 0;
        mShm->initialParams[stored].value =
            ctx.controller->getParamNormalized(pInfo.id);
        ++stored;
      }
    }
    mShm->initialParamCount = stored;

    const std::string helperPath = resolveHelperPath();
    const std::string parentPidStr = std::to_string(getpid());
    const pid_t pid = ::fork();
    if (pid < 0) {
      ::munmap(mShm, sizeof(SoLoudVst3EditorShm));
      mShm = nullptr;
      ::unlink(shmPath.c_str());
      return false;
    }
    if (pid == 0) {
      const char *argv[] = {helperPath.c_str(),   ctx.bundlePath.c_str(),
                            ctx.classUid.c_str(), ctx.title.c_str(),
                            shmPath.c_str(),      parentPidStr.c_str(),
                            nullptr};
      ::execv(helperPath.c_str(), const_cast<char *const *>(argv));
      ::_exit(127);
    }

    mHelperPid = pid;
    mOnGuiParamEdit = ctx.onGuiParamEdit;

    bool ready = false;
    for (int i = 0; i < 400; ++i) {
      const uint32_t st = mShm->helperReady.load(std::memory_order_acquire);
      if (st == 1) {
        ready = true;
        break;
      }
      if (st == 2) {
        break;
      }
      int status = 0;
      if (::waitpid(mHelperPid, &status, WNOHANG) == mHelperPid) {
        mHelperPid = 0;
        break;
      }
      if (guiSymbols().g_main_context_iteration) {
        guiSymbols().g_main_context_iteration(nullptr, 0);
      }
      ::usleep(10000);
    }

    if (!ready) {
      if (mHelperPid > 0) {
        ::kill(mHelperPid, SIGTERM);
        int status = 0;
        ::waitpid(mHelperPid, &status, 0);
        mHelperPid = 0;
      }
      ::munmap(mShm, sizeof(SoLoudVst3EditorShm));
      mShm = nullptr;
      ::unlink(shmPath.c_str());
      return false;
    }

    mIsOpen.store(true, std::memory_order_release);
    if (guiSymbols().g_timeout_add && mRemotePollTimerId == 0) {
      mRemotePollTimerId =
          guiSymbols().g_timeout_add(8, &onRemotePollStatic, this);
    }
    return true;
#endif
  }

  void notifyParameterChanged(uint32_t paramId,
                              double normalizedValue) override {
    if (mShm == nullptr || !mIsOpen.load(std::memory_order_acquire)) {
      return;
    }
    const uint32_t w = mShm->hostToGuiWrite.load(std::memory_order_relaxed);
    const uint32_t r = mShm->hostToGuiRead.load(std::memory_order_acquire);
    if (w - r < kVst3EditorParamQueueSize) {
      auto &slot = mShm->hostToGuiQueue[w % kVst3EditorParamQueueSize];
      slot.paramId = paramId;
      slot.reserved = 0;
      slot.value = normalizedValue;
      mShm->hostToGuiWrite.store(w + 1, std::memory_order_release);
    }
  }

  void writeAudioTelemetry(const float *inL, const float *inR,
                           const float *outL, const float *outR,
                           int32_t numSamples,
                           double sampleRate) noexcept override {
    if (!mIsOpen.load(std::memory_order_acquire)) {
      return;
    }
    writeVst3EditorShmTelemetry(mShm, inL, inR, outL, outR, numSamples,
                                sampleRate);
  }

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

    bool nativeResize = (plugView->canResize() == Steinberg::kResultTrue);
    if (!nativeResize) {
      Steinberg::ViewRect testRect{0, 0, static_cast<Steinberg::int32>(w * 1.5),
                                   static_cast<Steinberg::int32>(h * 1.5)};
      if (plugView->checkSizeConstraint(&testRect) == Steinberg::kResultTrue &&
          (testRect.getWidth() != w || testRect.getHeight() != h)) {
        nativeResize = true;
      }
    }
    mNativeResize = nativeResize;

    if (guiSymbols().XSelectInput) {
      // StructureNotifyMask = (1L << 17)
      guiSymbols().XSelectInput(sSharedDisplay, sSharedWindow, (1L << 17));
    }

    if (guiSymbols().XSetWMNormalHints) {
      XSizeHintsDef hints{};
      if (!mNativeResize) {
        hints.flags = kPMinSize | kPMaxSize;
        hints.min_width = static_cast<int>(w);
        hints.min_height = static_cast<int>(h);
        hints.max_width = static_cast<int>(w);
        hints.max_height = static_cast<int>(h);
      } else {
        hints.flags = kPMinSize;
        hints.min_width = static_cast<int>(w / 2 > 64 ? w / 2 : 64);
        hints.min_height = static_cast<int>(h / 2 > 64 ? h / 2 : 64);
      }
      guiSymbols().XSetWMNormalHints(sSharedDisplay, sSharedWindow, &hints);
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
        mPlugView->removed();
        mPlugView->setFrame(nullptr);
      }
      mPlugView = plugView;
      mPlugView->setFrame(this);
      if (mPlugView->attached(reinterpret_cast<void *>(sSharedWindow),
                              Steinberg::kPlatformTypeX11EmbedWindowID) !=
          Steinberg::kResultTrue) {
        mPlugView->setFrame(nullptr);
        mPlugView = nullptr;
        return false;
      }
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
    mIsOpen.store(false, std::memory_order_release);
    if (mRemotePollTimerId != 0 && guiSymbols().g_source_remove) {
      guiSymbols().g_source_remove(mRemotePollTimerId);
      mRemotePollTimerId = 0;
    }
    if (mShm != nullptr) {
      mShm->hostClosed.store(1, std::memory_order_release);
    }
    if (mHelperPid > 0) {
      int st = 0;
      bool reaped = false;
      for (int i = 0; i < 30; ++i) {
        if (::waitpid(mHelperPid, &st, WNOHANG) == mHelperPid) {
          reaped = true;
          break;
        }
        ::usleep(5000);
      }
      if (!reaped) {
        ::kill(mHelperPid, SIGTERM);
        for (int i = 0; i < 20; ++i) {
          if (::waitpid(mHelperPid, &st, WNOHANG) == mHelperPid) {
            reaped = true;
            break;
          }
          ::usleep(5000);
        }
        if (!reaped) {
          ::kill(mHelperPid, SIGKILL);
          ::waitpid(mHelperPid, &st, 0);
        }
      }
      mHelperPid = 0;
    }
    if (mShm != nullptr) {
      ::munmap(mShm, sizeof(SoLoudVst3EditorShm));
      mShm = nullptr;
    }
    mOnGuiParamEdit = nullptr;

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
    close();
    if (guiSymbols().gdk_error_trap_push) {
      guiSymbols().gdk_error_trap_push();
    }
    if (guiSymbols().XSetErrorHandler) {
      guiSymbols().XSetErrorHandler(&customXErrorHandler);
    }
    if (mPlugView != nullptr) {
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
    mInResize.store(true, std::memory_order_release);
    const unsigned int w = static_cast<unsigned int>(newSize->getWidth());
    const unsigned int h = static_cast<unsigned int>(newSize->getHeight());
    if (guiSymbols().XResizeWindow) {
      guiSymbols().XResizeWindow(sSharedDisplay, sSharedWindow, w, h);
    }
    if (!mNativeResize && guiSymbols().XSetWMNormalHints) {
      XSizeHintsDef hints{};
      hints.flags = kPMinSize | kPMaxSize;
      hints.min_width = static_cast<int>(w);
      hints.min_height = static_cast<int>(h);
      hints.max_width = static_cast<int>(w);
      hints.max_height = static_cast<int>(h);
      guiSymbols().XSetWMNormalHints(sSharedDisplay, sSharedWindow, &hints);
    }
    view->onSize(newSize);
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

      while (guiSymbols().XCheckTypedWindowEvent(sSharedDisplay, sSharedWindow,
                                                 22 /* ConfigureNotify */,
                                                 eventBuf)) {
        if (self->mInResize.load(std::memory_order_acquire) ||
            self->mPlugView == nullptr || !self->mNativeResize) {
          continue;
        }
        // In 64-bit LP64 XConfigureEvent:
        // offset 56: int width, offset 60: int height
        const char *raw = reinterpret_cast<const char *>(eventBuf);
        int newW = *reinterpret_cast<const int *>(raw + 56);
        int newH = *reinterpret_cast<const int *>(raw + 60);
        if (newW > 64 && newH > 64) {
          Steinberg::ViewRect curR{0, 0, 0, 0};
          if (self->mPlugView->getSize(&curR) == Steinberg::kResultTrue) {
            if (curR.getWidth() != newW || curR.getHeight() != newH) {
              Steinberg::ViewRect newR{0, 0, static_cast<Steinberg::int32>(newW),
                                       static_cast<Steinberg::int32>(newH)};
              if (self->mPlugView->checkSizeConstraint(&newR) ==
                  Steinberg::kResultTrue) {
                if (newR.getWidth() != newW || newR.getHeight() != newH) {
                  if (guiSymbols().XResizeWindow) {
                    guiSymbols().XResizeWindow(
                        sSharedDisplay, sSharedWindow,
                        static_cast<unsigned int>(newR.getWidth()),
                        static_cast<unsigned int>(newR.getHeight()));
                  }
                }
              }
              self->mPlugView->onSize(&newR);
            }
          }
        }
      }
    }
    return 1; // G_SOURCE_CONTINUE
  }

  static int onRemotePollStatic(void *userData) {
    auto *self = static_cast<Vst3EditorWindowLinux *>(userData);
    if (self == nullptr || self->mShm == nullptr || self->mHelperPid <= 0) {
      return 0; // G_SOURCE_REMOVE
    }
    int status = 0;
    const pid_t wres = ::waitpid(self->mHelperPid, &status, WNOHANG);
    if (wres == self->mHelperPid ||
        self->mShm->helperClosed.load(std::memory_order_acquire) != 0) {
      self->mRemotePollTimerId = 0;
      if (wres != self->mHelperPid && self->mHelperPid > 0) {
        ::waitpid(self->mHelperPid, &status, WNOHANG);
      }
      self->mHelperPid = 0;
      ::munmap(self->mShm, sizeof(SoLoudVst3EditorShm));
      self->mShm = nullptr;
      self->mIsOpen.store(false, std::memory_order_release);
      return 0; // G_SOURCE_REMOVE
    }

    uint32_t r = self->mShm->guiToHostRead.load(std::memory_order_relaxed);
    const uint32_t w =
        self->mShm->guiToHostWrite.load(std::memory_order_acquire);
    while (r != w) {
      const auto msg =
          self->mShm->guiToHostQueue[r % kVst3EditorParamQueueSize];
      if (self->mOnGuiParamEdit) {
        self->mOnGuiParamEdit(msg.paramId, msg.value);
      }
      ++r;
    }
    self->mShm->guiToHostRead.store(r, std::memory_order_release);
    return 1; // G_SOURCE_CONTINUE
  }

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  unsigned int mEventTimerId = 0;
  unsigned int mRemotePollTimerId = 0;
  pid_t mHelperPid = 0;
  SoLoudVst3EditorShm *mShm = nullptr;
  std::function<void(uint32_t, double)> mOnGuiParamEdit;
  std::map<Steinberg::Linux::IEventHandler *, FdEntry> mFdSources;
  std::map<Steinberg::Linux::ITimerHandler *, unsigned int> mTimerSources;
  std::atomic<bool> mIsOpen{false};
  std::atomic<bool> mInResize{false};
  bool mNativeResize = false;
};

std::unique_ptr<Vst3EditorWindow> Vst3EditorWindow::create() {
  return std::make_unique<Vst3EditorWindowLinux>();
}

void installVst3TerminationObserver() {}

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3 && __linux__ && !__ANDROID__

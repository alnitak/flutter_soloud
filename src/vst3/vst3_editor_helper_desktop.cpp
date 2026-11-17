#if defined(SOLOUD_ENABLE_VST3) && (defined(__linux__) || defined(_WIN32)) && \
    !defined(__ANDROID__)

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <windows.h>
#endif

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include "vst3_component_handler.h"
#include "vst3_editor_shm.h"
#include "vst3_editor_window.h"
#include "vst3_host.h"

namespace SoLoud {
// Stubs so the helper links without pulling in the audio engine.
Vst3HostManager &Vst3HostManager::instance() {
  static Vst3HostManager sMgr;
  return sMgr;
}
Vst3HostManager::Vst3HostManager() = default;
Vst3HostManager::~Vst3HostManager() = default;
void Vst3HostManager::unloadAll(void (*)(void *), void *) {}
} // namespace SoLoud

namespace {

bool equalsHexIgnoreCase(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(b[i]);
    const int la = (ca >= 'A' && ca <= 'Z') ? (ca - 'A' + 'a') : ca;
    const int lb = (cb >= 'A' && cb <= 'Z') ? (cb - 'A' + 'a') : cb;
    if (la != lb) {
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 6) {
    return 1;
  }
  const std::string bundlePath = argv[1];
  const std::string classUidHex = argv[2];
  const std::string windowTitle = argv[3];
  const char *shmIdentifier = argv[4];
  const long parentPid = std::strtol(argv[5], nullptr, 10);

#if defined(__linux__)
  const int shmFd = ::open(shmIdentifier, O_RDWR);
  ::unlink(shmIdentifier);
  if (shmFd < 0) {
    return 2;
  }
  void *mapped = ::mmap(nullptr, sizeof(SoLoud::SoLoudVst3EditorShm),
                        PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  ::close(shmFd);
  if (mapped == MAP_FAILED || mapped == nullptr) {
    return 3;
  }
  auto *shm = static_cast<SoLoud::SoLoudVst3EditorShm *>(mapped);
#elif defined(_WIN32)
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HANDLE hMap =
      OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmIdentifier);
  if (hMap == nullptr) {
    return 2;
  }
  void *mapped = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0,
                               sizeof(SoLoud::SoLoudVst3EditorShm));
  if (mapped == nullptr) {
    CloseHandle(hMap);
    return 3;
  }
  auto *shm = static_cast<SoLoud::SoLoudVst3EditorShm *>(mapped);
  HANDLE hParent = nullptr;
  if (parentPid > 0) {
    hParent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parentPid));
  }
#endif

  std::string err;
  auto module = VST3::Hosting::Module::create(bundlePath, err);
  if (!module) {
    shm->helperReady.store(2, std::memory_order_release);
    return 4;
  }

  Steinberg::Vst::HostApplication hostApp;
  Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&hostApp);
  const auto &factory = module->getFactory();
  factory.setHostContext(&hostApp);

  Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
  Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider;

  // Prefer instantiating kVstComponentControllerClass directly so plugins
  // that spawn an external DSP worker in IComponent (like flutter_vst3) do
  // not instantiate a second IComponent in the UI helper process.
  for (const auto &ci : factory.classInfos()) {
    if (ci.category() == kVstComponentControllerClass) {
      controller =
          factory.createInstance<Steinberg::Vst::IEditController>(ci.ID());
      if (controller &&
          controller->initialize(&hostApp) == Steinberg::kResultOk) {
        break;
      }
      controller = nullptr;
    }
  }

  if (!controller) {
    for (const auto &ci : factory.classInfos()) {
      if (ci.category() != kVstAudioEffectClass) {
        continue;
      }
      if (classUidHex.empty() ||
          equalsHexIgnoreCase(ci.ID().toString(false), classUidHex) ||
          equalsHexIgnoreCase(ci.ID().toString(true), classUidHex)) {
        provider = Steinberg::owned(
            new Steinberg::Vst::PlugProvider(factory, ci, true));
        if (provider && provider->initialize()) {
          controller = provider->getControllerPtr();
        }
        break;
      }
    }
  }

  if (!controller) {
    shm->helperReady.store(2, std::memory_order_release);
    return 5;
  }

  if (shm->initialStateSize > 0 &&
      shm->initialStateSize <= SoLoud::kVst3EditorMaxStateBytes) {
    Steinberg::MemoryStream stream(
        shm->initialState,
        static_cast<Steinberg::TSize>(shm->initialStateSize));
    controller->setComponentState(&stream);
  }

  const uint32_t initCount = std::min<uint32_t>(
      shm->initialParamCount, SoLoud::kVst3EditorMaxInitialParams);
  for (uint32_t i = 0; i < initCount; ++i) {
    controller->setParamNormalized(shm->initialParams[i].paramId,
                                   shm->initialParams[i].value);
  }

  auto compHandler = Steinberg::owned(new SoLoud::Vst3ComponentHandler(
      [shm](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue val) {
        uint32_t w = shm->guiToHostWrite.load(std::memory_order_relaxed);
        const uint32_t r = shm->guiToHostRead.load(std::memory_order_acquire);
        if (w - r < SoLoud::kVst3EditorParamQueueSize) {
          auto &slot =
              shm->guiToHostQueue[w % SoLoud::kVst3EditorParamQueueSize];
          slot.paramId = id;
          slot.reserved = 0;
          slot.value = val;
          shm->guiToHostWrite.store(w + 1, std::memory_order_release);
        }
      },
      [](Steinberg::int32) {}));
  controller->setComponentHandler(compHandler);

  // Notify flutter_vst3 controllers of the shared-memory telemetry address
  // in this helper process so real-time waveform visualizers attach cleanly.
  Steinberg::Vst::HostMessage shmMsg;
  shmMsg.setMessageID("Vst3Shm");
  if (auto *attrs = shmMsg.getAttributes()) {
    attrs->setInt("address",
                  static_cast<Steinberg::int64>(
                      reinterpret_cast<intptr_t>(&shm->vst3Telemetry[0])));
#if defined(__linux__)
    attrs->setInt("pid", static_cast<Steinberg::int64>(getpid()));
#elif defined(_WIN32)
    attrs->setInt("pid", static_cast<Steinberg::int64>(GetCurrentProcessId()));
#endif
  }
  if (auto conn =
          Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(
              controller)) {
    conn->notify(&shmMsg);
  }

  Steinberg::IPtr<Steinberg::IPlugView> view = Steinberg::owned(
      controller->createView(Steinberg::Vst::ViewType::kEditor));
  if (!view) {
    shm->helperReady.store(2, std::memory_order_release);
    return 6;
  }

  auto editorWin = SoLoud::Vst3EditorWindow::create();
  if (!editorWin || !editorWin->open(view.get(), windowTitle.c_str())) {
    shm->helperReady.store(2, std::memory_order_release);
    return 7;
  }

  shm->helperReady.store(1, std::memory_order_release);

#if defined(__linux__)
  using FnGMainContextIteration = int (*)(void *, int);
  FnGMainContextIteration fnIter = nullptr;
  if (void *glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL)) {
    fnIter = reinterpret_cast<FnGMainContextIteration>(
        dlsym(glib, "g_main_context_iteration"));
  }

  while (true) {
    const bool parentAlive =
        (parentPid <= 0) || (kill(static_cast<pid_t>(parentPid), 0) == 0);
    if (!parentAlive ||
        shm->hostClosed.load(std::memory_order_acquire) != 0 ||
        !editorWin->isOpen()) {
      break;
    }

    if (fnIter) {
      while (fnIter(nullptr, 0)) {
      }
    }

    uint32_t r = shm->hostToGuiRead.load(std::memory_order_relaxed);
    const uint32_t w = shm->hostToGuiWrite.load(std::memory_order_acquire);
    while (r != w) {
      const auto msg =
          shm->hostToGuiQueue[r % SoLoud::kVst3EditorParamQueueSize];
      controller->setParamNormalized(msg.paramId, msg.value);
      ++r;
    }
    shm->hostToGuiRead.store(r, std::memory_order_release);

    usleep(4000);
  }

  shm->helperClosed.store(1, std::memory_order_release);
  munmap(shm, sizeof(SoLoud::SoLoudVst3EditorShm));
  _exit(0);
#elif defined(_WIN32)
  while (true) {
    const bool parentAlive =
        (hParent == nullptr) ||
        (WaitForSingleObject(hParent, 0) == WAIT_TIMEOUT);
    if (!parentAlive ||
        shm->hostClosed.load(std::memory_order_acquire) != 0 ||
        !editorWin->isOpen()) {
      break;
    }

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        editorWin->close();
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    uint32_t r = shm->hostToGuiRead.load(std::memory_order_relaxed);
    const uint32_t w = shm->hostToGuiWrite.load(std::memory_order_acquire);
    while (r != w) {
      const auto pmsg =
          shm->hostToGuiQueue[r % SoLoud::kVst3EditorParamQueueSize];
      controller->setParamNormalized(pmsg.paramId, pmsg.value);
      ++r;
    }
    shm->hostToGuiRead.store(r, std::memory_order_release);

    MsgWaitForMultipleObjectsEx(0, nullptr, 4, QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE);
  }

  shm->helperClosed.store(1, std::memory_order_release);
  UnmapViewOfFile(shm);
  CloseHandle(hMap);
  if (hParent != nullptr) {
    CloseHandle(hParent);
  }
  TerminateProcess(GetCurrentProcess(), 0);
#endif
  return 0;
}

#endif // SOLOUD_ENABLE_VST3 && (__linux__ || _WIN32) && !__ANDROID__


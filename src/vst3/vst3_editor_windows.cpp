#if defined(SOLOUD_ENABLE_VST3) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "vst3_editor_shm.h"
#include "vst3_editor_window.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <string>

namespace SoLoud {

static inline bool rectEqual(const Steinberg::ViewRect &a,
                             const Steinberg::ViewRect &b) noexcept {
  return a.left == b.left && a.top == b.top &&
         a.right == b.right && a.bottom == b.bottom;
}

static std::wstring utf8ToWide(const std::string &str) {
  if (str.empty()) {
    return {};
  }
  const int needed = MultiByteToWideChar(
      CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()),
                      out.data(), needed);
  return out;
}

static std::wstring quoteWinArg(const std::wstring &arg) {
  std::wstring out = L"\"";
  for (wchar_t c : arg) {
    if (c == L'\"') {
      out += L"\\\"";
    } else {
      out += c;
    }
  }
  out += L"\"";
  return out;
}

class Vst3EditorWindowWindows final : public Vst3EditorWindow {
public:
  Vst3EditorWindowWindows() = default;
  ~Vst3EditorWindowWindows() override { close(); }

  static std::wstring resolveHelperPath() {
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(
                               &Vst3EditorWindowWindows::resolveHelperPath),
                           &hMod) &&
        hMod != nullptr) {
      wchar_t buf[MAX_PATH] = {0};
      if (GetModuleFileNameW(hMod, buf, MAX_PATH) > 0) {
        std::wstring dir(buf);
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
          std::wstring adj =
              dir.substr(0, slash + 1) + L"soloud_vst3_editor_helper.exe";
          if (GetFileAttributesW(adj.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return adj;
          }
        }
      }
    }
#if defined(SOLOUD_VST3_HELPER_PATH)
    std::wstring configured = utf8ToWide(SOLOUD_VST3_HELPER_PATH);
    if (!configured.empty() &&
        GetFileAttributesW(configured.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return configured;
    }
#endif
    return {};
  }

  bool supportsRemoteEditor(
      const std::string &bundlePath) const override {
#if defined(SOLOUD_VST3_HELPER_MAIN)
    (void)bundlePath;
    return false;
#else
    if (bundlePath.empty() || resolveHelperPath().empty()) {
      return false;
    }
    const std::wstring base = utf8ToWide(bundlePath);
    static const wchar_t *const kMarkers[] = {
        L"\\Contents\\x86_64-win\\flutter_windows.dll",
        L"\\Contents\\arm64-win\\flutter_windows.dll",
        L"\\Contents\\x86_64-win\\data\\flutter_assets",
        L"\\Contents\\arm64-win\\data\\flutter_assets",
    };
    for (const wchar_t *suffix : kMarkers) {
      const std::wstring candidate = base + suffix;
      if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
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

    static std::atomic<uint32_t> sShmCounter{0};
    const std::string shmName =
        "Local\\SoLoudVst3Shm_" + std::to_string(GetCurrentProcessId()) + "_" +
        std::to_string(++sShmCounter);

    mShmHandle = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(sizeof(SoLoudVst3EditorShm)), shmName.c_str());
    if (mShmHandle == nullptr) {
      return false;
    }
    void *mapped = MapViewOfFile(mShmHandle, FILE_MAP_ALL_ACCESS, 0, 0,
                                 sizeof(SoLoudVst3EditorShm));
    if (mapped == nullptr) {
      CloseHandle(mShmHandle);
      mShmHandle = nullptr;
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

    const std::wstring helperPath = resolveHelperPath();
    std::wstring cmdLine =
        quoteWinArg(helperPath) + L" " +
        quoteWinArg(utf8ToWide(ctx.bundlePath)) + L" " +
        quoteWinArg(utf8ToWide(ctx.classUid)) + L" " +
        quoteWinArg(utf8ToWide(ctx.title)) + L" " +
        quoteWinArg(utf8ToWide(shmName)) + L" " +
        std::to_wstring(GetCurrentProcessId());

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(helperPath.c_str(), cmdLine.data(), nullptr, nullptr,
                        FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
      UnmapViewOfFile(mShm);
      mShm = nullptr;
      CloseHandle(mShmHandle);
      mShmHandle = nullptr;
      return false;
    }
    CloseHandle(pi.hThread);
    mHelperProcess = pi.hProcess;
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
      if (WaitForSingleObject(mHelperProcess, 0) != WAIT_TIMEOUT) {
        CloseHandle(mHelperProcess);
        mHelperProcess = nullptr;
        break;
      }
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      Sleep(10);
    }

    if (!ready) {
      if (mHelperProcess != nullptr) {
        TerminateProcess(mHelperProcess, 1);
        CloseHandle(mHelperProcess);
        mHelperProcess = nullptr;
      }
      UnmapViewOfFile(mShm);
      mShm = nullptr;
      CloseHandle(mShmHandle);
      mShmHandle = nullptr;
      return false;
    }

    mIsOpen.store(true, std::memory_order_release);
    mRemoteTimerId = SetTimer(nullptr, 0, 8, &onRemoteTimerStatic);
    if (mRemoteTimerId != 0) {
      sRemoteTimers[mRemoteTimerId] = this;
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
    if (plugView->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultTrue) {
      return false;
    }

    close();
    ensureWindowClassRegistered();

    Steinberg::ViewRect rect{0, 0, 800, 600};
    plugView->getSize(&rect);
    const int w = rect.getWidth() > 64 ? rect.getWidth() : 800;
    const int h = rect.getHeight() > 64 ? rect.getHeight() : 600;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                  WS_CLIPCHILDREN;
    bool nativeResize = (plugView->canResize() == Steinberg::kResultTrue);
    if (!nativeResize) {
      Steinberg::ViewRect testRect{0, 0, static_cast<Steinberg::int32>(w * 1.5),
                                   static_cast<Steinberg::int32>(h * 1.5)};
      if (plugView->checkSizeConstraint(&testRect) == Steinberg::kResultTrue &&
          (testRect.getWidth() != w || testRect.getHeight() != h)) {
        nativeResize = true;
      }
    }
    if (nativeResize) {
      style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
    mNativeResize = nativeResize;
    const DWORD exStyle = WS_EX_APPWINDOW;

    RECT winRect{0, 0, w, h};
    AdjustWindowRectEx(&winRect, style, FALSE, exStyle);

    std::wstring wideTitle = L"VST3 Plugin";
    if (title != nullptr && title[0] != '\0') {
      int needed = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
      if (needed > 1) {
        wideTitle.resize(static_cast<size_t>(needed - 1));
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wideTitle.data(), needed);
      }
    }

    mHwnd = CreateWindowExW(
        exStyle, kWindowClassName, wideTitle.c_str(), style, CW_USEDEFAULT,
        CW_USEDEFAULT, winRect.right - winRect.left,
        winRect.bottom - winRect.top, nullptr, nullptr, GetModuleHandleW(nullptr),
        this);
    if (mHwnd == nullptr) {
      return false;
    }

    ShowWindow(mHwnd, SW_SHOW);
    UpdateWindow(mHwnd);

    mPlugView = plugView;
    mPlugView->setFrame(this);

    if (mPlugView->attached(reinterpret_cast<void *>(mHwnd),
                            Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultTrue) {
      mPlugView->setFrame(nullptr);
      mPlugView = nullptr;
      DestroyWindow(mHwnd);
      mHwnd = nullptr;
      return false;
    }

    InvalidateRect(mHwnd, nullptr, FALSE);
    UpdateWindow(mHwnd);
    mIsOpen.store(true, std::memory_order_release);
    return true;
  }

  void close() override {
    mIsOpen.store(false, std::memory_order_release);
    if (mRemoteTimerId != 0) {
      KillTimer(nullptr, mRemoteTimerId);
      sRemoteTimers.erase(mRemoteTimerId);
      mRemoteTimerId = 0;
    }
    if (mShm != nullptr) {
      mShm->hostClosed.store(1, std::memory_order_release);
    }
    if (mHelperProcess != nullptr) {
      if (WaitForSingleObject(mHelperProcess, 150) == WAIT_TIMEOUT) {
        TerminateProcess(mHelperProcess, 0);
        WaitForSingleObject(mHelperProcess, 50);
      }
      CloseHandle(mHelperProcess);
      mHelperProcess = nullptr;
    }
    if (mShm != nullptr) {
      UnmapViewOfFile(mShm);
      mShm = nullptr;
    }
    if (mShmHandle != nullptr) {
      CloseHandle(mShmHandle);
      mShmHandle = nullptr;
    }
    mOnGuiParamEdit = nullptr;

    if (mPlugView != nullptr) {
      mPlugView->removed();
      mPlugView->setFrame(nullptr);
      mPlugView = nullptr;
    }
    if (mHwnd != nullptr) {
      SetWindowLongPtrW(mHwnd, GWLP_USERDATA, 0);
      DestroyWindow(mHwnd);
      mHwnd = nullptr;
    }
  }

  bool isOpen() const noexcept override {
    return mIsOpen.load(std::memory_order_acquire);
  }

  Steinberg::tresult PLUGIN_API
  resizeView(Steinberg::IPlugView *view,
             Steinberg::ViewRect *newSize) override {
    if (view == nullptr || newSize == nullptr || view != mPlugView ||
        mHwnd == nullptr) {
      return Steinberg::kInvalidArgument;
    }
    if (mInResize.load(std::memory_order_acquire)) {
      return Steinberg::kResultFalse;
    }
    Steinberg::ViewRect r{0, 0, 0, 0};
    if (view->getSize(&r) != Steinberg::kResultTrue) {
      return Steinberg::kInternalError;
    }
    RECT clientRect{0, 0, 0, 0};
    GetClientRect(mHwnd, &clientRect);
    if (rectEqual(r, *newSize) &&
        (clientRect.right - clientRect.left) == newSize->getWidth() &&
        (clientRect.bottom - clientRect.top) == newSize->getHeight()) {
      return Steinberg::kResultTrue;
    }

    mInResize.store(true, std::memory_order_release);
    DWORD style = static_cast<DWORD>(GetWindowLongW(mHwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(mHwnd, GWL_EXSTYLE));
    RECT winRect{0, 0, newSize->getWidth(), newSize->getHeight()};
    AdjustWindowRectEx(&winRect, style, FALSE, exStyle);
    SetWindowPos(mHwnd, nullptr, 0, 0, winRect.right - winRect.left,
                 winRect.bottom - winRect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    mInResize.store(false, std::memory_order_release);

    view->onSize(newSize);
    InvalidateRect(mHwnd, nullptr, FALSE);
    UpdateWindow(mHwnd);
    return Steinberg::kResultTrue;
  }

private:
  static constexpr const wchar_t *kWindowClassName =
      L"SoLoudVst3EditorWindowClass";

  static void ensureWindowClassRegistered() {
    static bool registered = false;
    if (registered) {
      return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Vst3EditorWindowWindows::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);
    registered = true;
  }

  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam) {
    if (msg == WM_NCCREATE) {
      auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto *self = reinterpret_cast<Vst3EditorWindowWindows *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_CLOSE:
      self->close();
      return 0;
    case WM_SIZE:
      if (self->mNativeResize &&
          !self->mInResize.load(std::memory_order_acquire) &&
          self->mPlugView != nullptr && wParam != SIZE_MINIMIZED) {
        Steinberg::ViewRect r{0, 0, static_cast<Steinberg::int32>(LOWORD(lParam)),
                              static_cast<Steinberg::int32>(HIWORD(lParam))};
        Steinberg::ViewRect r2{0, 0, 0, 0};
        if (self->mPlugView->getSize(&r2) == Steinberg::kResultTrue &&
            !rectEqual(r, r2)) {
          self->mPlugView->onSize(&r);
        }
      }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
  }

  static inline std::map<UINT_PTR, Vst3EditorWindowWindows *> sRemoteTimers;

  static void CALLBACK onRemoteTimerStatic(HWND /*hwnd*/, UINT /*msg*/,
                                           UINT_PTR id, DWORD /*time*/) {
    auto it = sRemoteTimers.find(id);
    if (it == sRemoteTimers.end() || it->second == nullptr) {
      KillTimer(nullptr, id);
      return;
    }
    auto *self = it->second;
    if (self->mShm == nullptr || self->mHelperProcess == nullptr) {
      KillTimer(nullptr, id);
      sRemoteTimers.erase(it);
      self->mRemoteTimerId = 0;
      return;
    }
    if (WaitForSingleObject(self->mHelperProcess, 0) != WAIT_TIMEOUT ||
        self->mShm->helperClosed.load(std::memory_order_acquire) != 0) {
      KillTimer(nullptr, id);
      sRemoteTimers.erase(it);
      self->mRemoteTimerId = 0;
      CloseHandle(self->mHelperProcess);
      self->mHelperProcess = nullptr;
      UnmapViewOfFile(self->mShm);
      self->mShm = nullptr;
      if (self->mShmHandle != nullptr) {
        CloseHandle(self->mShmHandle);
        self->mShmHandle = nullptr;
      }
      self->mIsOpen.store(false, std::memory_order_release);
      return;
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
  }

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  HWND mHwnd = nullptr;
  UINT_PTR mRemoteTimerId = 0;
  HANDLE mHelperProcess = nullptr;
  HANDLE mShmHandle = nullptr;
  SoLoudVst3EditorShm *mShm = nullptr;
  std::function<void(uint32_t, double)> mOnGuiParamEdit;
  std::atomic<bool> mIsOpen{false};
  std::atomic<bool> mInResize{false};
  bool mNativeResize = false;
};

std::unique_ptr<Vst3EditorWindow> Vst3EditorWindow::create() {
  return std::make_unique<Vst3EditorWindowWindows>();
}

void installVst3TerminationObserver() {}

} // namespace SoLoud

#if defined(_MSC_VER)
// vst3_hosting.lib may reference __std_replace_copy_2 if built with a newer MSVC
// STL that vectorizes std::replace_copy on wchar_t. On toolsets where the CRT
// does not yet provide this symbol, provide a fallback via /alternatename.
extern "C" void __stdcall __std_replace_copy_2_fallback(
    const void* _First,
    const void* _Last,
    void* _Dest,
    uint16_t _Old_val,
    uint16_t _New_val) noexcept {
  const auto* src = static_cast<const uint16_t*>(_First);
  const auto* end = static_cast<const uint16_t*>(_Last);
  auto* dst = static_cast<uint16_t*>(_Dest);
  while (src != end) {
    const uint16_t val = *src++;
    *dst++ = (val == _Old_val) ? _New_val : val;
  }
}
#if defined(_M_X64) || defined(_M_ARM64)
#pragma comment(linker, "/alternatename:__std_replace_copy_2=__std_replace_copy_2_fallback")
#elif defined(_M_IX86)
#pragma comment(linker, "/alternatename:__std_replace_copy_2=_std_replace_copy_2_fallback@16")
#endif
#endif

#endif // SOLOUD_ENABLE_VST3 && _WIN32

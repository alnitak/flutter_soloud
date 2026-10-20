#if defined(SOLOUD_ENABLE_VST3) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vst3_editor_window.h"
#include <atomic>
#include <string>

namespace SoLoud {

static inline bool rectEqual(const Steinberg::ViewRect &a,
                             const Steinberg::ViewRect &b) noexcept {
  return a.left == b.left && a.top == b.top &&
         a.right == b.right && a.bottom == b.bottom;
}

class Vst3EditorWindowWindows final : public Vst3EditorWindow {
public:
  Vst3EditorWindowWindows() = default;
  ~Vst3EditorWindowWindows() override { close(); }

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
    if (rectEqual(r, *newSize)) {
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

    if (view->getSize(&r) != Steinberg::kResultTrue) {
      return Steinberg::kInternalError;
    }
    if (!rectEqual(r, *newSize)) {
      view->onSize(newSize);
    }
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
      if (!self->mInResize.load(std::memory_order_acquire) &&
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

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  HWND mHwnd = nullptr;
  std::atomic<bool> mIsOpen{false};
  std::atomic<bool> mInResize{false};
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

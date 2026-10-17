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
    if (plugView->canResize() == Steinberg::kResultTrue) {
      style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
    const DWORD exStyle = WS_EX_APPWINDOW;

    RECT winRect{0, 0, w, h};
    AdjustWindowRectEx(&winRect, style, FALSE, exStyle);

    std::wstring wideTitle = L"VST3 Plugin";
    if (title != nullptr && title[0] != '\0') {
      int needed = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
      if (needed > 0) {
        wideTitle.resize(static_cast<size_t>(needed));
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

    ShowWindow(mHwnd, SW_SHOW);
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
    if (view == nullptr || newSize == nullptr || mHwnd == nullptr) {
      return Steinberg::kInvalidArgument;
    }
    DWORD style = static_cast<DWORD>(GetWindowLongW(mHwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(mHwnd, GWL_EXSTYLE));
    RECT winRect{0, 0, newSize->getWidth(), newSize->getHeight()};
    AdjustWindowRectEx(&winRect, style, FALSE, exStyle);
    SetWindowPos(mHwnd, nullptr, 0, 0, winRect.right - winRect.left,
                 winRect.bottom - winRect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    view->onSize(newSize);
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
      if (self->mPlugView != nullptr && wParam != SIZE_MINIMIZED) {
        Steinberg::ViewRect r{0, 0, static_cast<Steinberg::int32>(LOWORD(lParam)),
                              static_cast<Steinberg::int32>(HIWORD(lParam))};
        self->mPlugView->onSize(&r);
      }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
  }

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  HWND mHwnd = nullptr;
  std::atomic<bool> mIsOpen{false};
};

std::unique_ptr<Vst3EditorWindow> Vst3EditorWindow::create() {
  return std::make_unique<Vst3EditorWindowWindows>();
}

void installVst3TerminationObserver() {}

} // namespace SoLoud

#endif // SOLOUD_ENABLE_VST3 && _WIN32

// Native test of the in-process Windows VST3 editor window
// (src/vst3/vst3_editor_windows.cpp, Vst3EditorWindowWindows). The plug-in
// side is a fake IPlugView defined here that behaves like a real editor: in
// attached() it creates a child window of the size it reports.
//
// Checks:
//   1. open() creates a visible top-level window whose client area is the
//      view's size, attaches the view (kPlatformTypeHWND, a real HWND) and
//      sets the frame; the view's child window lives inside it.
//   2. resizeView() from the plug-in resizes the client area and calls
//      onSize().
//   3. The window's close button (WM_CLOSE) closes the editor: removed(),
//      window destroyed, isOpen() false.
//   4. open() again (new window), open() with another view (the first one
//      is removed), destroying the window object while open.
//   5. DPI: the editor opened from threads in the DPI-unaware, system-aware
//      and per-monitor-v2 contexts keeps the client area equal to the view
//      size in that context; the window DPI and whether the host called
//      IPlugViewContentScaleSupport::setContentScaleFactor are reported.
//   6. A view that does not support HWND embedding is refused.
//
//   vst3_editor_windows_test.exe [--strict]

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "vst3/vst3_editor_window.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

using namespace Steinberg;

static int gChecks = 0, gFailures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

class FakeView : public IPlugView, public IPlugViewContentScaleSupport
{
public:
    FakeView(bool hwnd = true, int w = 420, int h = 260) : mHwndOk(hwnd), mW(w), mH(h) {}
    virtual ~FakeView() = default;

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IPlugView *>(this);
            addRef();
            return kResultTrue;
        }
        if (FUnknownPrivate::iidEqual(iid, IPlugViewContentScaleSupport::iid))
        {
            *obj = static_cast<IPlugViewContentScaleSupport *>(this);
            addRef();
            return kResultTrue;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++mRefs; }
    uint32 PLUGIN_API release() override
    {
        const uint32 r = --mRefs;
        if (r == 0)
        {
            sDeleted++;
            delete this;
        }
        return r;
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return mHwndOk && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void *parent, FIDString) override
    {
        mAttached++;
        mParent = static_cast<HWND>(parent);
        mChild = CreateWindowExW(0, L"STATIC", L"fake editor", WS_CHILD | WS_VISIBLE, 0, 0, mW, mH, mParent,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        return kResultTrue;
    }
    tresult PLUGIN_API removed() override
    {
        mRemoved++;
        if (mChild != nullptr && IsWindow(mChild)) DestroyWindow(mChild);
        mChild = nullptr;
        return kResultTrue;
    }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect *size) override
    {
        if (!size) return kInvalidArgument;
        *size = ViewRect(0, 0, mW, mH);
        return kResultTrue;
    }
    tresult PLUGIN_API onSize(ViewRect *r) override
    {
        mOnSize++;
        if (r)
        {
            mW = r->getWidth();
            mH = r->getHeight();
            if (mChild) SetWindowPos(mChild, nullptr, 0, 0, mW, mH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return kResultTrue;
    }
    tresult PLUGIN_API onFocus(TBool) override { return kResultTrue; }
    tresult PLUGIN_API setFrame(IPlugFrame *frame) override
    {
        mFrame = frame;
        return kResultTrue;
    }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *) override { return kResultTrue; }

    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override
    {
        mScale = factor;
        mScaleCalls++;
        return kResultTrue;
    }

    tresult requestResize(int w, int h)
    {
        ViewRect r(0, 0, w, h);
        return mFrame ? mFrame->resizeView(this, &r) : kResultFalse;
    }

    std::atomic<uint32> mRefs{1};
    bool mHwndOk;
    int mW, mH;
    HWND mParent = nullptr, mChild = nullptr;
    IPlugFrame *mFrame = nullptr;
    int mAttached = 0, mRemoved = 0, mOnSize = 0, mScaleCalls = 0;
    float mScale = 0;
    static int sDeleted;
};
int FakeView::sDeleted = 0;

static void pump(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

static SIZE clientSize(HWND h)
{
    RECT r{};
    GetClientRect(h, &r);
    return SIZE{r.right - r.left, r.bottom - r.top};
}

static void testOpenResizeClose()
{
    std::printf("open, resize, WM_CLOSE, reopen\n");
    const int deletedBefore = FakeView::sDeleted;
    auto window = SoLoud::Vst3EditorWindow::create();
    FakeView *view = new FakeView(true, 420, 260); // the test's reference
    CHECK(window->open(view, "flutter_soloud editor test \xC3\xA9"), "open()");
    pump(50);
    CHECK(window->isOpen(), "isOpen()");
    CHECK(view->mAttached == 1 && view->mFrame != nullptr, "attached once, frame set");
    HWND top = view->mParent;
    CHECK(top != nullptr && IsWindow(top), "the parent is a real window");
    CHECK(IsWindowVisible(top), "and visible");
    CHECK(view->mChild != nullptr && GetParent(view->mChild) == top, "the editor's child window is inside it");
    SIZE s = clientSize(top);
    CHECK(s.cx == 420 && s.cy == 260, "client area %ldx%ld, view 420x260", s.cx, s.cy);
    wchar_t title[128] = L"";
    GetWindowTextW(top, title, 128);
    CHECK(wcscmp(title, L"flutter_soloud editor test é") == 0, "UTF-8 title converted: '%ls'", title);

    const int sizes = view->mOnSize;
    CHECK(view->requestResize(640, 380) == kResultTrue, "resizeView() accepted");
    pump(50);
    s = clientSize(top);
    CHECK(s.cx == 640 && s.cy == 380, "client area after resizeView: %ldx%ld", s.cx, s.cy);
    CHECK(view->mOnSize > sizes, "onSize() called");

    // The close button.
    PostMessageW(top, WM_CLOSE, 0, 0);
    pump(100);
    CHECK(!window->isOpen(), "WM_CLOSE closes the editor");
    CHECK(view->mRemoved == 1, "WM_CLOSE calls removed() (%d)", view->mRemoved);
    CHECK(!IsWindow(top), "the window is destroyed");

    // Reopen with the same view: a new window.
    CHECK(window->open(view, "again"), "reopen with the same view");
    pump(50);
    CHECK(view->mAttached == 2 && IsWindow(view->mParent), "re-attached to a new window");
    FakeView *second = new FakeView(true, 300, 200);
    CHECK(window->open(second, "second"), "open() with another view");
    pump(50);
    CHECK(view->mRemoved == 2, "the first view was removed (%d)", view->mRemoved);
    s = clientSize(second->mParent);
    CHECK(s.cx == 300 && s.cy == 200, "the window follows the new view: %ldx%ld", s.cx, s.cy);
    HWND secondTop = second->mParent;
    window.reset(); // destroyed while open
    CHECK(second->mRemoved == 1, "destroying the window object removes the view");
    CHECK(!IsWindow(secondTop), "and destroys the window");
    second->release();
    view->release();
    CHECK(FakeView::sDeleted - deletedBefore == 2, "both views released (%d)", FakeView::sDeleted - deletedBefore);
}

static void testDpiContexts()
{
    std::printf("DPI awareness contexts\n");
    using SetCtx = DPI_AWARENESS_CONTEXT(WINAPI *)(DPI_AWARENESS_CONTEXT);
    using GetDpi = UINT(WINAPI *)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setCtx = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(user32, "GetDpiForWindow"));
    if (setCtx == nullptr || getDpi == nullptr)
    {
        std::printf("  not run: needs Windows 10 1607+ (SetThreadDpiAwarenessContext)\n");
        return;
    }
    struct Ctx
    {
        const char *name;
        DPI_AWARENESS_CONTEXT ctx;
    } ctxs[] = {{"unaware", DPI_AWARENESS_CONTEXT_UNAWARE},
                {"system aware", DPI_AWARENESS_CONTEXT_SYSTEM_AWARE},
                {"per-monitor v2", DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}};
    for (const auto &c : ctxs)
    {
        // Each context on its own thread: a window keeps the context of the
        // thread that created it.
        std::thread([&] {
            if (setCtx(c.ctx) == nullptr)
            {
                std::printf("  %s: not supported here\n", c.name);
                return;
            }
            auto window = SoLoud::Vst3EditorWindow::create();
            FakeView *view = new FakeView(true, 500, 300);
            if (!window->open(view, c.name))
            {
                CHECK(false, "%s: open()", c.name);
                view->release();
                return;
            }
            pump(50);
            const SIZE s = clientSize(view->mParent);
            const UINT dpi = getDpi(view->mParent);
            std::printf("  %-15s window DPI %u, client %ldx%ld, setContentScaleFactor %s (%.2f)\n", c.name, dpi,
                        s.cx, s.cy, view->mScaleCalls ? "called" : "not called", view->mScale);
            CHECK(s.cx == 500 && s.cy == 300, "%s: client area %ldx%ld, view 500x300", c.name, s.cx, s.cy);
            CHECK(view->requestResize(600, 400) == kResultTrue, "%s: resizeView()", c.name);
            pump(30);
            const SIZE s2 = clientSize(view->mParent);
            CHECK(s2.cx == 600 && s2.cy == 400, "%s: client after resize %ldx%ld", c.name, s2.cx, s2.cy);
            window.reset();
            view->release();
        }).join();
    }
}

static void testRefusals()
{
    std::printf("open() refusals\n");
    auto window = SoLoud::Vst3EditorWindow::create();
    CHECK(!window->open(nullptr, "null"), "open(nullptr) fails");
    FakeView *noHwnd = new FakeView(false);
    CHECK(!window->open(noHwnd, "no hwnd"), "a view without HWND support is refused");
    CHECK(noHwnd->mAttached == 0 && !window->isOpen(), "never attached, not open");
    window.reset();
    noHwnd->release();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    OleInitialize(nullptr); // an editor normally lives on an STA UI thread
    testOpenResizeClose();
    testDpiContexts();
    testRefusals();
    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

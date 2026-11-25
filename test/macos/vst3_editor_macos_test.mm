// The macOS in-process VST3 editor window (src/vst3/vst3_editor_macos.mm,
// Vst3EditorWindow::create()) with a mock IPlugView that attaches a real
// NSView, the way plug-in editors do:
//   * open: the view is asked for NSView support, gets the window's content
//     view as its parent and the window as its IPlugFrame, the window shows
//     with the title and the view's size;
//   * close from the host: removed() is called once, the frame is cleared,
//     the window goes away, isOpen() turns false;
//   * close from the window (the close button): the same, and a later host
//     close() is harmless;
//   * two editors at once: independent windows; closing one leaves the other
//     attached;
//   * resizeView() from the plug-in (IPlugFrame) resizes the window's
//     content; resizeView() for a view that is not the window's is refused;
//   * open() from a background thread (the Dart thread) runs on the main
//     thread; a view without NSView support is refused;
//   * reopening the same window object replaces the previous view.
// The checks run on a worker thread while the main thread runs the
// application's run loop, as in an app. Windows appear briefly on screen.
//
// Parameter sync with the editor is not covered here: in process it goes
// through the plug-in's own controller, and the out-of-process editor needs
// a plug-in bundle embedding a Flutter app (see vst3_host_checks.h's
// remote-editor test with the stand-in helper).
//
// Build & run with test/macos/vst3_editor_macos_test.sh.

#import <Cocoa/Cocoa.h>

#include "vst3/vst3_editor_window.h"
#include "vst3/vst3_host.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

// vst3_editor_macos.mm's exit-time hook calls the host manager; this test
// links the window alone (the same stubs as vst3_editor_helper_macos.mm).
namespace SoLoud {
Vst3HostManager &Vst3HostManager::instance() {
    static Vst3HostManager sMgr;
    return sMgr;
}
Vst3HostManager::Vst3HostManager() = default;
Vst3HostManager::~Vst3HostManager() = default;
void Vst3HostManager::unloadAll(void (*)(void *), void *) {}
void Vst3HostManager::unloadAllAtExit() {}
} // namespace SoLoud

using namespace Steinberg;

static int gFailures = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  FAIL (line %d): ", __LINE__);                       \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
            ++gFailures;                                                        \
        } else {                                                                \
            std::printf("  ok: ");                                              \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
        }                                                                       \
    } while (0)

/// A plug-in view that adds an NSView to the parent it is attached to.
class MockView : public IPlugView {
public:
    explicit MockView(int w, int h, bool nsview = true) : mW(w), mH(h), mNsView(nsview) {}

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return mNsView && std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void *parent, FIDString type) override {
        attachedCalls++;
        attachedOnMain = [NSThread isMainThread];
        if (std::strcmp(type, kPlatformTypeNSView) != 0 || parent == nullptr) return kResultFalse;
        NSView *p = (__bridge NSView *)parent;
        mView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, mW, mH)];
        [p addSubview:mView];
        parentView = p;
        return kResultTrue;
    }
    tresult PLUGIN_API removed() override {
        removedCalls++;
        [mView removeFromSuperview];
        mView = nil;
        parentView = nil;
        return kResultTrue;
    }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect *size) override {
        if (!size) return kInvalidArgument;
        *size = ViewRect{0, 0, mW, mH};
        return kResultTrue;
    }
    tresult PLUGIN_API onSize(ViewRect *newSize) override {
        if (newSize) {
            mW = newSize->getWidth();
            mH = newSize->getHeight();
        }
        return kResultTrue;
    }
    tresult PLUGIN_API onFocus(TBool) override { return kResultTrue; }
    tresult PLUGIN_API setFrame(IPlugFrame *f) override {
        frame = f;
        return kResultTrue;
    }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *) override { return kResultTrue; }
    tresult PLUGIN_API queryInterface(const TUID, void **obj) override {
        *obj = nullptr;
        return kNoInterface;
    }
    // The window holds the view through an IPtr; this test owns it.
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    /// Asks the host to resize, as a plug-in does when its UI changes size.
    tresult requestResize(int w, int h) {
        mW = w;
        mH = h;
        ViewRect r{0, 0, w, h};
        return frame ? frame->resizeView(this, &r) : kResultFalse;
    }

    std::atomic<int> attachedCalls{0}, removedCalls{0};
    std::atomic<bool> attachedOnMain{false};
    IPlugFrame *frame = nullptr;
    NSView *__strong parentView = nil;

private:
    int mW, mH;
    bool mNsView;
    NSView *__strong mView = nil;
};

template <typename F> static auto onMain(F f) -> decltype(f()) {
    __block decltype(f()) r;
    dispatch_sync(dispatch_get_main_queue(), ^{
        r = f();
    });
    return r;
}

static NSWindow *windowTitled(const char *title) {
    return onMain([&]() -> NSWindow * {
        NSString *t = [NSString stringWithUTF8String:title];
        for (NSWindow *w in [NSApp windows])
            if ([w isVisible] && [[w title] isEqualToString:t]) return w;
        return nil;
    });
}

static NSSize contentSize(NSWindow *w) {
    return onMain([&]() { return [w contentRectForFrameRect:[w frame]].size; });
}

static bool waitFor(const std::function<bool()> &cond, int ms = 2000) {
    for (int i = 0; i < ms / 10; ++i) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

static void openCloseFromHost() {
    std::printf("[case] open and close from the host (open called off the main thread)\n");
    auto win = SoLoud::Vst3EditorWindow::create();
    MockView view(420, 260);
    CHECK(win->open(&view, "Mock Editor A"), "open() returns true");
    CHECK(win->isOpen(), "isOpen()");
    CHECK(view.attachedCalls == 1 && view.attachedOnMain, "attached() once, on the main thread");
    CHECK(view.frame == win.get(), "the window is the view's IPlugFrame");
    NSWindow *w = windowTitled("Mock Editor A");
    CHECK(w != nil, "a visible window with the title");
    if (w) {
        const NSSize s = contentSize(w);
        CHECK(s.width == 420 && s.height == 260, "content size is the view's size (%.0fx%.0f)", s.width, s.height);
        CHECK(onMain([&]() { return view.parentView == [w contentView]; }), "attached to the window's content view");
    }
    win->close();
    CHECK(!win->isOpen() && view.removedCalls == 1, "close(): removed() once, isOpen() false");
    CHECK(view.frame == nullptr, "the view's frame is cleared");
    CHECK(waitFor([] { return windowTitled("Mock Editor A") == nil; }), "the window is gone");
    win->close();
    CHECK(view.removedCalls == 1, "a second close() does nothing");
}

static void closeFromWindow() {
    std::printf("[case] close from the window's close button\n");
    auto win = SoLoud::Vst3EditorWindow::create();
    MockView view(300, 200);
    CHECK(win->open(&view, "Mock Editor B"), "open");
    NSWindow *w = windowTitled("Mock Editor B");
    CHECK(w != nil, "window visible");
    if (w) onMain([&]() {
            [w performClose:nil];
            return true;
        });
    CHECK(waitFor([&] { return !win->isOpen(); }), "isOpen() false after the user closed it");
    CHECK(waitFor([&] { return view.removedCalls.load() == 1; }), "removed() once (%d)", view.removedCalls.load());
    CHECK(waitFor([] { return windowTitled("Mock Editor B") == nil; }), "the window is gone");
    win->close();
    CHECK(view.removedCalls == 1, "a host close() afterwards does nothing more (%d)", view.removedCalls.load());
}

static void twoEditors() {
    std::printf("[case] two editors at once\n");
    auto a = SoLoud::Vst3EditorWindow::create();
    auto b = SoLoud::Vst3EditorWindow::create();
    MockView va(320, 200), vb(500, 300);
    CHECK(a->open(&va, "Mock Editor C1") && b->open(&vb, "Mock Editor C2"), "both open");
    NSWindow *wa = windowTitled("Mock Editor C1");
    NSWindow *wb = windowTitled("Mock Editor C2");
    CHECK(wa != nil && wb != nil && wa != wb, "two windows");
    a->close();
    CHECK(!a->isOpen() && va.removedCalls == 1, "the first closed");
    CHECK(b->isOpen() && vb.removedCalls == 0 && windowTitled("Mock Editor C2") != nil,
          "the second still open and attached");
    if (wb) {
        const NSSize s = contentSize(wb);
        CHECK(s.width == 500 && s.height == 300, "the second keeps its size (%.0fx%.0f)", s.width, s.height);
    }
    b->close();
    CHECK(!b->isOpen() && vb.removedCalls == 1, "the second closed");
}

static void resizeFromPlugin() {
    std::printf("[case] resizeView() from the plug-in\n");
    auto win = SoLoud::Vst3EditorWindow::create();
    MockView view(400, 300);
    CHECK(win->open(&view, "Mock Editor D"), "open");
    NSWindow *w = windowTitled("Mock Editor D");
    CHECK(view.requestResize(640, 360) == kResultTrue, "resizeView() accepted");
    if (w) {
        CHECK(waitFor([&] {
                  const NSSize s = contentSize(w);
                  return s.width == 640 && s.height == 360;
              }),
              "content resized to 640x360 (%.0fx%.0f)", contentSize(w).width, contentSize(w).height);
    }
    MockView other(100, 100);
    ViewRect r{0, 0, 200, 200};
    CHECK(win->resizeView(&other, &r) == kInvalidArgument, "resizeView() for another view is refused");
    CHECK(win->resizeView(&view, nullptr) == kInvalidArgument, "resizeView() without a size is refused");
    win->close();
    ViewRect r2{0, 0, 300, 300};
    CHECK(win->resizeView(&view, &r2) != kResultTrue, "resizeView() after close is refused");
}

static void refusedAndReopen() {
    std::printf("[case] a view without NSView support; reopening a window object\n");
    auto win = SoLoud::Vst3EditorWindow::create();
    MockView noNs(200, 200, false);
    CHECK(!win->open(&noNs, "Mock Editor E") && !win->isOpen() && noNs.attachedCalls == 0,
          "refused without attaching");
    CHECK(!win->open(nullptr, "Mock Editor E"), "a null view is refused");
    MockView v1(300, 200), v2(350, 250);
    CHECK(win->open(&v1, "Mock Editor E1"), "open the first view");
    CHECK(win->open(&v2, "Mock Editor E2"), "open a second view on the same window object");
    CHECK(v1.removedCalls == 1 && v2.attachedCalls == 1, "the first view was removed, the second attached");
    CHECK(waitFor([] { return windowTitled("Mock Editor E1") == nil; }) && windowTitled("Mock Editor E2") != nil,
          "only the second window is shown");
    win.reset();
    CHECK(v2.removedCalls == 1, "destroying the window object detaches the view (%d)", v2.removedCalls.load());
    CHECK(waitFor([] { return windowTitled("Mock Editor E2") == nil; }), "and closes its window");
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        std::thread([] {
            openCloseFromHost();
            closeFromWindow();
            twoEditors();
            resizeFromPlugin();
            refusedAndReopen();
            if (gFailures == 0) {
                std::printf("All VST3 macOS editor checks passed.\n");
                std::exit(0);
            }
            std::printf("%d VST3 macOS editor check(s) failed.\n", gFailures);
            std::exit(1);
        }).detach();
        [NSApp run];
    }
    return 0;
}

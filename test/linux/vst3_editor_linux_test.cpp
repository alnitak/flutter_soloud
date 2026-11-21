// Native test of the in-process Linux VST3 editor window
// (src/vst3/vst3_editor_linux.cpp, Vst3EditorWindowLinux) on a real X server
// (Xvfb in test/docker). The plug-in side is a fake IPlugView defined here
// that behaves like a VSTGUI view: in attached() it asks the frame for the
// Linux IRunLoop and registers a timer and a file-descriptor handler, and it
// removes them again in removed().
//
// Checks:
//   1. open() maps an X11 window of the view's size, attaches the view to it
//      (kPlatformTypeX11EmbedWindowID, a real window id) and sets the frame.
//   2. The IRunLoop timer and fd handler run from the GLib main loop.
//   3. resizeView() from the plug-in resizes the X window and calls onSize().
//   4. close() unmaps the window; open() again with the same view reuses the
//      attachment; open() with a new view detaches the old one.
//   5. The window manager's close button (a WM_PROTOCOLS / WM_DELETE_WINDOW
//      ClientMessage) closes the editor.
//   6. Destroying the window object detaches the view and removes its run
//      loop sources, so no timer fires on a freed handler.
//   7. A view that does not support X11 embedding, and no display at all,
//      make open() fail cleanly.
//
//   vst3_editor_linux_test [--strict]

#include "vst3/vst3_editor_window.h"

#include "pluginterfaces/gui/iplugview.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <glib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <unistd.h>

using namespace Steinberg;

static int gChecks = 0, gFailures = 0, gKnownBugs = 0;
static bool gStrict = false;

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

#define KNOWN_BUG(id, cond, ...)                                               \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            if (gStrict) ++gFailures; else ++gKnownBugs;                       \
            std::printf("  %s %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",\
                        id, __FILE__, __LINE__, #cond);                        \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// The fake plug-in view.

class FakeView : public IPlugView,
                 public Linux::ITimerHandler,
                 public Linux::IEventHandler
{
public:
    explicit FakeView(bool x11 = true, int w = 420, int h = 260)
        : mX11(x11), mW(w), mH(h)
    {
        if (::pipe(mPipe) != 0)
            mPipe[0] = mPipe[1] = -1;
    }
    virtual ~FakeView()
    {
        mDestroyed = true;
        if (mPipe[0] >= 0) ::close(mPipe[0]);
        if (mPipe[1] >= 0) ::close(mPipe[1]);
    }

    // --- FUnknown (heap object, deleted by the last release)
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IPlugView *>(this);
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

    // --- IPlugView
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return mX11 && std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0
                   ? kResultTrue
                   : kResultFalse;
    }
    tresult PLUGIN_API attached(void *parent, FIDString type) override
    {
        mAttached++;
        mParent = reinterpret_cast<unsigned long>(parent);
        mAttachedType = type;
        if (mFrame != nullptr)
        {
            Linux::IRunLoop *loop = nullptr;
            if (mFrame->queryInterface(Linux::IRunLoop::iid,
                                       reinterpret_cast<void **>(&loop)) ==
                    kResultTrue &&
                loop != nullptr)
            {
                mRunLoop = loop;
                mTimerRegistered =
                    loop->registerTimer(this, 20) == kResultTrue;
                mFdRegistered = mPipe[0] >= 0 &&
                                loop->registerEventHandler(this, mPipe[0]) ==
                                    kResultTrue;
            }
        }
        return kResultTrue;
    }
    tresult PLUGIN_API removed() override
    {
        mRemoved++;
        if (mRunLoop != nullptr)
        {
            mTimerUnregistered = mRunLoop->unregisterTimer(this) == kResultTrue;
            mFdUnregistered =
                mRunLoop->unregisterEventHandler(this) == kResultTrue;
            mRunLoop = nullptr;
        }
        return kResultTrue;
    }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect *size) override
    {
        if (size == nullptr) return kInvalidArgument;
        *size = ViewRect(0, 0, mW, mH);
        return kResultTrue;
    }
    tresult PLUGIN_API onSize(ViewRect *newSize) override
    {
        mOnSize++;
        if (newSize != nullptr)
        {
            mW = newSize->getWidth();
            mH = newSize->getHeight();
        }
        return kResultTrue;
    }
    tresult PLUGIN_API onFocus(TBool) override { return kResultTrue; }
    tresult PLUGIN_API setFrame(IPlugFrame *frame) override
    {
        mFrame = frame;
        mSetFrameCalls++;
        return kResultTrue;
    }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *) override { return kResultTrue; }

    // --- run loop handlers
    void PLUGIN_API onTimer() override
    {
        if (mDestroyed) sUseAfterFree++;
        mTimerFired++;
    }
    void PLUGIN_API onFDIsSet(Linux::FileDescriptor fd) override
    {
        if (mDestroyed) sUseAfterFree++;
        char buf[16];
        (void)!::read(fd, buf, sizeof(buf));
        mFdFired++;
    }

    void poke() { (void)!::write(mPipe[1], "x", 1); }

    // The view resizes itself, as a plug-in does from its own UI.
    tresult requestResize(int w, int h)
    {
        ViewRect r(0, 0, w, h);
        return mFrame ? mFrame->resizeView(this, &r) : kResultFalse;
    }

    std::atomic<uint32> mRefs{1};
    bool mX11;
    int mW, mH;
    int mPipe[2] = {-1, -1};
    IPlugFrame *mFrame = nullptr;
    Linux::IRunLoop *mRunLoop = nullptr;
    unsigned long mParent = 0;
    const char *mAttachedType = nullptr;
    int mAttached = 0, mRemoved = 0, mOnSize = 0, mSetFrameCalls = 0;
    int mTimerFired = 0, mFdFired = 0;
    bool mTimerRegistered = false, mFdRegistered = false;
    bool mTimerUnregistered = false, mFdUnregistered = false;
    bool mDestroyed = false;
    static int sDeleted;
    static int sUseAfterFree;
};
int FakeView::sDeleted = 0;
int FakeView::sUseAfterFree = 0;

// ---------------------------------------------------------------------------

static Display *gDpy = nullptr; // the test's own connection, for queries

static void pump(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end)
    {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static bool windowAttrs(unsigned long win, XWindowAttributes &attrs)
{
    XSync(gDpy, False);
    return XGetWindowAttributes(gDpy, static_cast<Window>(win), &attrs) != 0;
}

// The editor talks to the X server over its own connection, and the server
// interleaves clients freely: poll (running the GLib loop) until [pred]
// holds or 2 s pass, leaving the last attributes in [attrs].
template <typename Pred>
static bool waitAttrs(unsigned long win, XWindowAttributes &attrs, Pred pred)
{
    for (int i = 0; i < 100; i++)
    {
        if (windowAttrs(win, attrs) && pred(attrs)) return true;
        pump(20);
    }
    return windowAttrs(win, attrs) && pred(attrs);
}

static int ignoreXErrors(Display *, XErrorEvent *) { return 0; }

// Sends what a window manager sends when the user clicks the close button.
static void sendWmDelete(unsigned long win)
{
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = static_cast<Window>(win);
    ev.xclient.message_type = XInternAtom(gDpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] =
        static_cast<long>(XInternAtom(gDpy, "WM_DELETE_WINDOW", False));
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(gDpy, static_cast<Window>(win), False, NoEventMask, &ev);
    XFlush(gDpy);
}

static void testOpenResizeClose()
{
    std::printf("open, run loop, resize, close, reopen\n");
    auto window = SoLoud::Vst3EditorWindow::create();
    // The test holds the initial reference, as the host's IPtr does; the
    // window takes its own.
    const int deletedBefore = FakeView::sDeleted;
    FakeView *view = new FakeView(true, 420, 260);
    CHECK(window->open(view, "flutter_soloud editor test"), "open()");
    CHECK(window->isOpen(), "isOpen() after open()");
    CHECK(view->mAttached == 1, "attached once (%d)", view->mAttached);
    CHECK(view->mAttachedType != nullptr &&
              std::strcmp(view->mAttachedType, kPlatformTypeX11EmbedWindowID) == 0,
          "attached as X11EmbedWindowID");
    CHECK(view->mFrame != nullptr, "the frame was set");
    const unsigned long win = view->mParent;
    XWindowAttributes a{};
    CHECK(win != 0 && windowAttrs(win, a), "parent 0x%lx is a real X window", win);
    if (win != 0)
        waitAttrs(win, a, [](const XWindowAttributes &x) { return x.map_state != IsUnmapped; });
    if (win != 0 && windowAttrs(win, a))
    {
        CHECK(a.map_state != IsUnmapped, "the window is mapped (%d)", a.map_state);
        CHECK(a.width == 420 && a.height == 260, "window %dx%d, view 420x260",
              a.width, a.height);
        char *name = nullptr;
        if (XFetchName(gDpy, static_cast<Window>(win), &name) && name)
        {
            CHECK(std::strcmp(name, "flutter_soloud editor test") == 0,
                  "title '%s'", name);
            XFree(name);
        }
    }

    // 2. IRunLoop sources run from the GLib main loop.
    CHECK(view->mTimerRegistered, "registerTimer() accepted");
    CHECK(view->mFdRegistered, "registerEventHandler() accepted");
    view->poke();
    pump(200);
    CHECK(view->mTimerFired >= 3, "timer fired %d times in 200 ms", view->mTimerFired);
    CHECK(view->mFdFired >= 1, "fd handler ran %d times", view->mFdFired);

    // 3. Plug-in initiated resize.
    const int sizesBefore = view->mOnSize;
    CHECK(view->requestResize(640, 380) == kResultTrue, "resizeView() accepted");
    pump(50);
    waitAttrs(win, a, [](const XWindowAttributes &x) { return x.width == 640 && x.height == 380; });
    CHECK(a.width == 640 && a.height == 380, "window resized to %dx%d", a.width, a.height);
    CHECK(view->mOnSize > sizesBefore, "onSize() called after resizeView()");
    FakeView *stranger = new FakeView();
    ViewRect r(0, 0, 100, 100);
    CHECK(view->mFrame->resizeView(stranger, &r) != kResultTrue,
          "resizeView() for a view that is not attached is refused");
    stranger->release();

    // 4. close / reopen.
    window->close();
    CHECK(!window->isOpen(), "isOpen() false after close()");
    waitAttrs(win, a, [](const XWindowAttributes &x) { return x.map_state == IsUnmapped; });
    CHECK(a.map_state == IsUnmapped, "window unmapped after close() (%d)", a.map_state);
    CHECK(window->open(view, "again"), "reopen with the same view");
    CHECK(view->mAttached == 1, "the same view is not attached twice (%d)",
          view->mAttached);
    waitAttrs(win, a, [](const XWindowAttributes &x) { return x.map_state != IsUnmapped; });
    CHECK(a.map_state != IsUnmapped, "window mapped again");

    FakeView *second = new FakeView(true, 300, 200);
    CHECK(window->open(second, "second"), "open() with another view");
    CHECK(view->mRemoved == 1, "the first view was removed (%d)", view->mRemoved);
    CHECK(view->mTimerUnregistered && view->mFdUnregistered,
          "the first view's run-loop sources were unregistered");
    CHECK(second->mAttached == 1, "the second view is attached");
    waitAttrs(second->mParent, a, [](const XWindowAttributes &x) { return x.width == 300 && x.height == 200; });
    CHECK(a.width == 300 && a.height == 200, "window follows the new view: %dx%d", a.width, a.height);

    // 6. Destroy the window object while open.
    const int firedBefore = second->mTimerFired;
    window.reset();
    CHECK(second->mRemoved == 1, "destroying the window removes the view");
    CHECK(second->mTimerUnregistered, "and unregisters its timer");
    second->release(); // the test's reference: the window dropped its own
    pump(100);
    CHECK(FakeView::sUseAfterFree == 0,
          "no run-loop callback reached a destroyed view (%d)",
          FakeView::sUseAfterFree);
    (void)firedBefore;
    view->release();
    CHECK(FakeView::sDeleted - deletedBefore == 3,
          "every view was released by the window (%d of 3 deleted)",
          FakeView::sDeleted - deletedBefore);
}

static void testWindowManagerClose()
{
    std::printf("window manager close button (WM_DELETE_WINDOW)\n");
    auto window = SoLoud::Vst3EditorWindow::create();
    FakeView *view = new FakeView();
    if (!window->open(view, "wm close"))
    {
        CHECK(false, "open()");
        view->release();
        return;
    }
    const unsigned long win = view->mParent;
    pump(50);
    sendWmDelete(win);
    pump(300);
    // onEventPollStatic() must read the ClientMessage's data.l[0] from
    // eventBuf[7] (byte 56 on LP64); eventBuf[8] holds data.l[1], the
    // timestamp.
    CHECK(!window->isOpen(),
              "the editor is still open after the window manager's "
              "WM_DELETE_WINDOW message");
    XWindowAttributes a{};
    waitAttrs(win, a, [](const XWindowAttributes &x) { return x.map_state == IsUnmapped; });
    CHECK(a.map_state == IsUnmapped, "the window is still mapped (%d)", a.map_state);
    window.reset();
    view->release();
}

static void testRefusals()
{
    std::printf("open() refusals\n");
    auto window = SoLoud::Vst3EditorWindow::create();
    CHECK(!window->open(nullptr, "null"), "open(nullptr) fails");
    FakeView *noX11 = new FakeView(false);
    CHECK(!window->open(noX11, "no x11"), "a view without X11 embedding is refused");
    CHECK(noX11->mAttached == 0, "and never attached");
    CHECK(!window->isOpen(), "not open");
    window.reset();
    noX11->release();
}

int main(int argc, char **argv)
{
    bool noDisplayMode = false;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
        else if (std::strcmp(argv[i], "--no-display") == 0) noDisplayMode = true;
    }

    if (noDisplayMode)
    {
        // Run by the script with DISPLAY unset: open() must fail cleanly.
        std::printf("no display\n");
        auto window = SoLoud::Vst3EditorWindow::create();
        FakeView *view = new FakeView();
        CHECK(!window->open(view, "no display"), "open() without a display fails");
        CHECK(!window->isOpen(), "not open");
        window.reset();
        view->release();
        std::printf("%d checks, %d failed\n", gChecks, gFailures);
        return gFailures == 0 ? 0 : 1;
    }

    gDpy = XOpenDisplay(nullptr);
    if (gDpy == nullptr)
    {
        std::printf("SKIPPED: no X display (set DISPLAY or install Xvfb)\n");
        return 0;
    }
    XSetErrorHandler(ignoreXErrors);

    testOpenResizeClose();
    testWindowManagerClose();
    testRefusals();

    XCloseDisplay(gDpy);
    std::printf("%d checks, %d failed, %d known bugs\n", gChecks, gFailures,
                gKnownBugs);
    return gFailures == 0 ? 0 : 1;
}

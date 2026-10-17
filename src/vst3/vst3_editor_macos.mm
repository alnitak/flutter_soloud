#if defined(SOLOUD_ENABLE_VST3) && defined(__APPLE__)

#include <TargetConditionals.h>

#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE

#import <Cocoa/Cocoa.h>
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vst3_editor_window.h"
#include "vst3_host.h"
#include <atomic>
#include <string>

namespace SoLoud {
class Vst3EditorWindowMacOS;
}

@interface SoLoudVst3WindowDelegate : NSObject <NSWindowDelegate> {
  SoLoud::Vst3EditorWindowMacOS *_owner;
}
- (instancetype)initWithOwner:(SoLoud::Vst3EditorWindowMacOS *)owner;
- (void)clearOwner;
@end

namespace SoLoud {

static inline bool rectEqual(const Steinberg::ViewRect &a,
                             const Steinberg::ViewRect &b) noexcept {
  return a.left == b.left && a.top == b.top &&
         a.right == b.right && a.bottom == b.bottom;
}

class Vst3EditorWindowMacOS final : public Vst3EditorWindow {
public:
  Vst3EditorWindowMacOS() = default;
  ~Vst3EditorWindowMacOS() override { close(); }

  bool open(Steinberg::IPlugView *plugView, const char *title) override {
    if (plugView == nullptr) {
      return false;
    }
    if (plugView->isPlatformTypeSupported(Steinberg::kPlatformTypeNSView) !=
        Steinberg::kResultTrue) {
      return false;
    }

    close();

    __block bool success = false;
    std::string titleStr = (title != nullptr && title[0] != '\0')
                               ? std::string(title)
                               : std::string("VST3 Plugin");

    void (^openBlock)(void) = ^{
      Steinberg::ViewRect rect{0, 0, 800, 600};
      plugView->getSize(&rect);
      int32_t w = rect.getWidth() > 64 ? rect.getWidth() : 800;
      int32_t h = rect.getHeight() > 64 ? rect.getHeight() : 600;

      // Special handling for HALion Sonic:
      // HALion Sonic reports its compact rack size (900x702) from getSize,
      // but its standard expanded GUI size is 1282x702. Pre-sizing to
      // 1282x702 before attach lets HALion Sonic initialize directly at
      // its preferred size stably without mid-init swapchain destruction.
      if (titleStr.find("HALion") != std::string::npos && w == 900 &&
          h == 702) {
        w = 1282;
        Steinberg::ViewRect targetRect{0, 0, w, h};
        plugView->onSize(&targetRect);
      }

      NSRect contentRect = NSMakeRect(0, 0, w, h);

      NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable;
      if (plugView->canResize() == Steinberg::kResultTrue) {
        style |= NSWindowStyleMaskResizable;
      }

      mWindow = [[NSWindow alloc] initWithContentRect:contentRect
                                            styleMask:style
                                              backing:NSBackingStoreBuffered
                                                defer:YES];
      [mWindow setReleasedWhenClosed:NO];
      NSString *nsTitle = [NSString stringWithUTF8String:titleStr.c_str()];
      [mWindow setTitle:(nsTitle != nil ? nsTitle : @"VST3 Plugin")];
      [mWindow center];

      mDelegate = [[SoLoudVst3WindowDelegate alloc] initWithOwner:this];
      [mWindow setDelegate:mDelegate];
      [[NSNotificationCenter defaultCenter]
          addObserver:mDelegate
             selector:@selector(applicationWillTerminate:)
                 name:NSApplicationWillTerminateNotification
               object:nil];

      mPlugView = plugView;
      mPlugView->setFrame(this);

      NSView *contentView = [mWindow contentView];
#if __has_feature(objc_arc)
      void *nativeParent = (__bridge void *)contentView;
#else
      void *nativeParent = reinterpret_cast<void *>(contentView);
#endif
      if (mPlugView->attached(nativeParent, Steinberg::kPlatformTypeNSView) ==
          Steinberg::kResultTrue) {
        [mWindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        mIsOpen.store(true, std::memory_order_release);
        success = true;
      } else {
        mPlugView->setFrame(nullptr);
        mPlugView = nullptr;
        [mWindow setDelegate:nil];
        [[NSNotificationCenter defaultCenter] removeObserver:mDelegate];
        [mDelegate clearOwner];
#if !__has_feature(objc_arc)
        [mDelegate release];
#endif
        mDelegate = nil;
        [mWindow close];
#if !__has_feature(objc_arc)
        [mWindow release];
#endif
        mWindow = nil;
      }
    };

    if ([NSThread isMainThread]) {
      openBlock();
    } else {
      dispatch_sync(dispatch_get_main_queue(), openBlock);
    }
    return success;
  }

  void close() override {
    if (!mIsOpen.load(std::memory_order_acquire) && mWindow == nil &&
        mPlugView == nullptr) {
      return;
    }

    void (^closeBlock)(void) = ^{
      mIsOpen.store(false, std::memory_order_release);
      if (mDelegate != nil) {
        [[NSNotificationCenter defaultCenter] removeObserver:mDelegate];
        [mDelegate clearOwner];
      }
      if (mPlugView != nullptr) {
        mPlugView->removed();
        mPlugView->setFrame(nullptr);
        mPlugView = nullptr;
      }
      if (mWindow != nil) {
        [mWindow setDelegate:nil];
        [mWindow close];
#if !__has_feature(objc_arc)
        [mWindow release];
#endif
        mWindow = nil;
      }
      if (mDelegate != nil) {
#if !__has_feature(objc_arc)
        [mDelegate release];
#endif
        mDelegate = nil;
      }
    };

    if ([NSThread isMainThread]) {
      closeBlock();
    } else {
      dispatch_sync(dispatch_get_main_queue(), closeBlock);
    }
  }

  bool isOpen() const noexcept override {
    return mIsOpen.load(std::memory_order_acquire);
  }

  Steinberg::tresult PLUGIN_API
  resizeView(Steinberg::IPlugView *view,
             Steinberg::ViewRect *newSize) override {
    if (view == nullptr || newSize == nullptr || view != mPlugView) {
      return Steinberg::kInvalidArgument;
    }
    if (mWindow == nil) {
      return Steinberg::kInternalError;
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

    void (^resizeBlock)(void) = ^{
      if (mWindow == nil) {
        return;
      }
      NSRect contentRect = [mWindow contentRectForFrameRect:mWindow.frame];
      const CGFloat diffY = newSize->getHeight() - contentRect.size.height;
      contentRect.size.width = newSize->getWidth();
      contentRect.size.height = newSize->getHeight();
      contentRect.origin.y -= diffY;
      NSRect frameRect = [mWindow frameRectForContentRect:contentRect];
      [mWindow setFrame:frameRect
                display:[mWindow isVisible]
                animate:NO];
    };

    if ([NSThread isMainThread]) {
      resizeBlock();
    } else {
      dispatch_sync(dispatch_get_main_queue(), resizeBlock);
    }

    mInResize.store(false, std::memory_order_release);

    if (view->getSize(&r) != Steinberg::kResultTrue) {
      return Steinberg::kInternalError;
    }
    if (!rectEqual(r, *newSize)) {
      view->onSize(newSize);
    }
    return Steinberg::kResultTrue;
  }

  void onNativeWindowClosing() {
    mIsOpen.store(false, std::memory_order_release);
    if (mDelegate != nil) {
      [[NSNotificationCenter defaultCenter] removeObserver:mDelegate];
      [mDelegate clearOwner];
#if !__has_feature(objc_arc)
      [mDelegate release];
#endif
      mDelegate = nil;
    }
    if (mPlugView != nullptr) {
      mPlugView->removed();
      mPlugView->setFrame(nullptr);
      mPlugView = nullptr;
    }
    if (mWindow != nil) {
      [mWindow setDelegate:nil];
#if !__has_feature(objc_arc)
      [mWindow release];
#endif
      mWindow = nil;
    }
  }

  NSSize onNativeWindowWillResize(NSSize frameSize) {
    if (mPlugView == nullptr || mWindow == nil) {
      return frameSize;
    }
    NSRect frameRect = NSMakeRect(0, 0, frameSize.width, frameSize.height);
    NSRect contentRect = [mWindow contentRectForFrameRect:frameRect];
    Steinberg::ViewRect r{
        0, 0, static_cast<Steinberg::int32>(contentRect.size.width),
        static_cast<Steinberg::int32>(contentRect.size.height)};
    if (mPlugView->checkSizeConstraint(&r) == Steinberg::kResultTrue) {
      contentRect.size.width = r.getWidth();
      contentRect.size.height = r.getHeight();
      NSRect constrainedFrame =
          [mWindow frameRectForContentRect:contentRect];
      return constrainedFrame.size;
    }
    return frameSize;
  }

  void onNativeWindowDidResize() {
    if (mInResize.load(std::memory_order_acquire)) {
      return;
    }
    if (mPlugView == nullptr || mWindow == nil) {
      return;
    }
    NSView *contentView = [mWindow contentView];
    if (contentView == nil) {
      return;
    }
    NSRect bounds = [contentView bounds];
    Steinberg::ViewRect r{
        0, 0, static_cast<Steinberg::int32>(bounds.size.width),
        static_cast<Steinberg::int32>(bounds.size.height)};
    Steinberg::ViewRect r2{0, 0, 0, 0};
    if (mPlugView->getSize(&r2) == Steinberg::kResultTrue &&
        !rectEqual(r, r2)) {
      mPlugView->onSize(&r);
    }
  }

private:
  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  NSWindow *mWindow = nil;
  SoLoudVst3WindowDelegate *mDelegate = nil;
  std::atomic<bool> mIsOpen{false};
  std::atomic<bool> mInResize{false};
};

std::unique_ptr<Vst3EditorWindow> Vst3EditorWindow::create() {
  return std::make_unique<Vst3EditorWindowMacOS>();
}

} // namespace SoLoud

@implementation SoLoudVst3WindowDelegate
- (instancetype)initWithOwner:(SoLoud::Vst3EditorWindowMacOS *)owner {
  if ((self = [super init])) {
    _owner = owner;
  }
  return self;
}

- (void)clearOwner {
  _owner = nullptr;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  if (_owner != nullptr) {
    _owner->close();
  }
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender;
  if (_owner != nullptr) {
    _owner->onNativeWindowClosing();
  }
  return YES;
}

- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize {
  (void)sender;
  if (_owner != nullptr) {
    return _owner->onNativeWindowWillResize(frameSize);
  }
  return frameSize;
}

- (void)windowDidResize:(NSNotification *)notification {
  (void)notification;
  if (_owner != nullptr) {
    _owner->onNativeWindowDidResize();
  }
}
@end

@interface SoLoudVst3AppTerminateObserver : NSObject
@end

@implementation SoLoudVst3AppTerminateObserver
- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  SoLoud::Vst3HostManager::instance().unloadAll();
}
@end

static SoLoudVst3AppTerminateObserver *gVst3AppTerminateObserver = nil;

namespace SoLoud {

void installVst3TerminationObserver() {
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    void (^installBlock)(void) = ^{
      gVst3AppTerminateObserver = [[SoLoudVst3AppTerminateObserver alloc] init];
      [[NSNotificationCenter defaultCenter]
          addObserver:gVst3AppTerminateObserver
             selector:@selector(applicationWillTerminate:)
                 name:NSApplicationWillTerminateNotification
               object:nil];
    };
    if ([NSThread isMainThread]) {
      installBlock();
    } else {
      dispatch_async(dispatch_get_main_queue(), installBlock);
    }
  });
}

} // namespace SoLoud

#endif // !TARGET_OS_IPHONE
#endif // SOLOUD_ENABLE_VST3 && __APPLE__

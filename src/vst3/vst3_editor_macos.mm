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

@interface NSObject (SoLoudSteinbergCocoaViewSupport)
- (BOOL)useMetalLayer;
- (void)setUseMetalLayer:(BOOL)val;
- (BOOL)isChildWindow;
- (void)setIsChildWindow:(BOOL)val;
- (BOOL)isPathBrowserViewRenderView;
- (void)setIsPathBrowserViewRenderView:(BOOL)val;
- (void)windowOcclusionStateChanged:(BOOL)occluded;
@end

#import <QuartzCore/QuartzCore.h>

@interface SoLoudVst3Window : NSWindow
@property(nonatomic, assign) NSSize baseContentSize;
@property(nonatomic, assign) BOOL supportsHostScaling;
- (void)zoomByFactor:(CGFloat)factor;
- (void)resetZoom;
@end

@implementation SoLoudVst3Window
- (BOOL)performKeyEquivalent:(NSEvent *)event {
  if (self.supportsHostScaling &&
      ([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask) ==
      NSEventModifierFlagCommand) {
    NSString *chars = [event charactersIgnoringModifiers];
    if ([chars isEqualToString:@"+"] || [chars isEqualToString:@"="]) {
      [self zoomByFactor:1.25];
      return YES;
    } else if ([chars isEqualToString:@"-"] || [chars isEqualToString:@"_"]) {
      [self zoomByFactor:0.8];
      return YES;
    } else if ([chars isEqualToString:@"0"]) {
      [self resetZoom];
      return YES;
    }
  }
  return [super performKeyEquivalent:event];
}

- (void)zoomByFactor:(CGFloat)factor {
  NSRect curContentRect = [self contentRectForFrameRect:[self frame]];
  CGFloat newW = std::round(curContentRect.size.width * factor);
  CGFloat newH = std::round(curContentRect.size.height * factor);
  if (self.baseContentSize.width > 0 && self.baseContentSize.height > 0) {
    newH = std::round(newW * (self.baseContentSize.height / self.baseContentSize.width));
  }

  NSSize minSize = [self contentMinSize];
  NSSize maxSize = [self contentMaxSize];
  if (minSize.width > 0 && newW < minSize.width) {
    newW = minSize.width;
    if (self.baseContentSize.width > 0) {
      newH = std::round(newW * (self.baseContentSize.height / self.baseContentSize.width));
    }
  }
  if (minSize.height > 0 && newH < minSize.height) {
    newH = minSize.height;
    if (self.baseContentSize.height > 0) {
      newW = std::round(newH * (self.baseContentSize.width / self.baseContentSize.height));
    }
  }
  if (maxSize.width > 0 && newW > maxSize.width) {
    newW = maxSize.width;
    if (self.baseContentSize.width > 0) {
      newH = std::round(newW * (self.baseContentSize.height / self.baseContentSize.width));
    }
  }
  if (maxSize.height > 0 && newH > maxSize.height) {
    newH = maxSize.height;
    if (self.baseContentSize.height > 0) {
      newW = std::round(newH * (self.baseContentSize.width / self.baseContentSize.height));
    }
  }

  if (std::abs(newW - curContentRect.size.width) < 1.0) return;

  NSRect newContentRect = NSMakeRect(curContentRect.origin.x,
                                     curContentRect.origin.y - (newH - curContentRect.size.height),
                                     newW, newH);
  NSRect newFrame = [self frameRectForContentRect:newContentRect];
  NSScreen *screen = [self screen];
  if (screen != nil) {
    NSRect screenRect = [screen visibleFrame];
    CGFloat top = [self frame].origin.y + [self frame].size.height;
    newFrame.origin.y = top - newFrame.size.height;
    if (newFrame.origin.y < screenRect.origin.y) {
      newFrame.origin.y = screenRect.origin.y;
    }
    if (NSMaxX(newFrame) > NSMaxX(screenRect)) {
      newFrame.origin.x = NSMaxX(screenRect) - newFrame.size.width;
    }
    if (newFrame.origin.x < screenRect.origin.x) {
      newFrame.origin.x = screenRect.origin.x;
    }
  }
  [self setFrame:newFrame display:YES animate:YES];
}

- (void)resetZoom {
  if (self.baseContentSize.width <= 0 || self.baseContentSize.height <= 0) return;
  NSRect curContentRect = [self contentRectForFrameRect:[self frame]];
  CGFloat newW = self.baseContentSize.width;
  CGFloat newH = self.baseContentSize.height;
  NSRect newContentRect = NSMakeRect(curContentRect.origin.x,
                                     curContentRect.origin.y - (newH - curContentRect.size.height),
                                     newW, newH);
  NSRect newFrame = [self frameRectForContentRect:newContentRect];
  NSScreen *screen = [self screen];
  if (screen != nil) {
    NSRect screenRect = [screen visibleFrame];
    CGFloat top = [self frame].origin.y + [self frame].size.height;
    newFrame.origin.y = top - newFrame.size.height;
    if (newFrame.origin.y < screenRect.origin.y) {
      newFrame.origin.y = screenRect.origin.y;
    }
    if (NSMaxX(newFrame) > NSMaxX(screenRect)) {
      newFrame.origin.x = NSMaxX(screenRect) - newFrame.size.width;
    }
    if (newFrame.origin.x < screenRect.origin.x) {
      newFrame.origin.x = screenRect.origin.x;
    }
  }
  [self setFrame:newFrame display:YES animate:YES];
}
@end

namespace SoLoud {

static inline bool rectEqual(const Steinberg::ViewRect &a,
                             const Steinberg::ViewRect &b) noexcept {
  return a.left == b.left && a.top == b.top &&
         a.right == b.right && a.bottom == b.bottom;
}

static void ensurePluginSubviewsVisible(NSView *view) {
  if (view == nil) {
    return;
  }
  SEL setPathBrowserSel = @selector(setIsPathBrowserViewRenderView:);
  if ([view respondsToSelector:setPathBrowserSel]) {
    [view setIsPathBrowserViewRenderView:YES];
  }
  SEL occlusionSel = @selector(windowOcclusionStateChanged:);
  if ([view respondsToSelector:occlusionSel]) {
    [view windowOcclusionStateChanged:NO];
  }
  [view setNeedsDisplay:YES];
  CALayer *layer = [view layer];
  if (layer != nil) {
    [layer setNeedsDisplay];
  }
  for (NSView *subview in [view subviews]) {
    ensurePluginSubviewsVisible(subview);
  }
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
      const int32_t w = rect.getWidth() > 64 ? rect.getWidth() : 800;
      const int32_t h = rect.getHeight() > 64 ? rect.getHeight() : 600;
      mBaseWidth = w;
      mBaseHeight = h;

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
      mUseHostScaling = false;

      NSRect contentRect = NSMakeRect(0, 0, w, h);

      NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable;
      if (mNativeResize) {
        style |= NSWindowStyleMaskResizable;
      }

      mWindow = [[SoLoudVst3Window alloc] initWithContentRect:contentRect
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
      mWindow.baseContentSize = NSMakeSize(w, h);
      mWindow.supportsHostScaling = NO;
      [mWindow setReleasedWhenClosed:NO];
      [mWindow setHidesOnDeactivate:NO];
      if (mNativeResize) {
        [mWindow setContentMinSize:NSMakeSize(std::max<int>(100, w / 2),
                                              std::max<int>(100, h / 2))];
        [mWindow setContentMaxSize:NSMakeSize(w * 4, h * 4)];
      } else {
        [mWindow setContentMinSize:NSMakeSize(w, h)];
        [mWindow setContentMaxSize:NSMakeSize(w, h)];
      }
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
        NSView *pluginView = [[contentView subviews] count] > 0
                                 ? [[contentView subviews] firstObject]
                                 : nil;

        if (!mNativeResize) {
          // Check if the attached plugin subview is JUCE-based (or supports Cocoa view
          // coordinate transforms). JUCE plugins return canResize() == kResultFalse
          // but safely scale via host contentView bounds scaling. Non-JUCE plugins
          // (such as Voxengo's CWindowCocoaContent) bypass Cocoa coordinate transformations
          // and directly query hardware display mouse positions via CGEventGetLocation,
          // breaking mouse hit-testing when host scaling is applied.
          bool canHostScale = false;
          if (pluginView != nil) {
            NSString *className = NSStringFromClass([pluginView class]);
            if ([className hasPrefix:@"JUCEView"]) {
              canHostScale = true;
            }
          }

          if (canHostScale) {
            mUseHostScaling = true;
            mWindow.supportsHostScaling = YES;
            [mWindow setStyleMask:([mWindow styleMask] | NSWindowStyleMaskResizable)];
            [mWindow setContentAspectRatio:NSMakeSize(w, h)];
            [mWindow setContentMinSize:NSMakeSize(std::max<int>(100, w / 2),
                                                  std::max<int>(100, h / 2))];
            [mWindow setContentMaxSize:NSMakeSize(w * 4, h * 4)];
            [contentView setFrameSize:NSMakeSize(w, h)];
            [contentView setBoundsSize:NSMakeSize(mBaseWidth, mBaseHeight)];
            if (pluginView != nil) {
              [pluginView setFrame:NSMakeRect(0, 0, mBaseWidth, mBaseHeight)];
            }
          } else {
            mUseHostScaling = false;
            mWindow.supportsHostScaling = NO;
            [mWindow setStyleMask:([mWindow styleMask] & ~NSWindowStyleMaskResizable)];
            [mWindow setContentAspectRatio:NSMakeSize(0, 0)];
            [mWindow setContentMinSize:NSMakeSize(w, h)];
            [mWindow setContentMaxSize:NSMakeSize(w, h)];
            [contentView setFrame:NSMakeRect(0, 0, w, h)];
            [contentView setBounds:NSMakeRect(0, 0, w, h)];
            if (pluginView != nil) {
              [pluginView setFrame:NSMakeRect(0, 0, w, h)];
            }
          }
        } else {
          mUseHostScaling = false;
          mWindow.supportsHostScaling = NO;
          [contentView setFrame:NSMakeRect(0, 0, w, h)];
          [contentView setBounds:NSMakeRect(0, 0, w, h)];
          if (pluginView != nil) {
            [pluginView setFrame:NSMakeRect(0, 0, w, h)];
          }
        }
        ensurePluginSubviewsVisible(contentView);
        [mWindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        if ([[contentView subviews] count] > 0) {
          NSView *pluginView = [[contentView subviews] firstObject];
          [mWindow makeFirstResponder:pluginView];
        }
        [[NSNotificationCenter defaultCenter]
            postNotificationName:NSWindowDidBecomeKeyNotification
                          object:mWindow];
        [[NSNotificationCenter defaultCenter]
            postNotificationName:NSWindowDidBecomeMainNotification
                          object:mWindow];
        [mWindow displayIfNeeded];
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
      mBaseWidth = newSize->getWidth();
      mBaseHeight = newSize->getHeight();
      mWindow.baseContentSize = NSMakeSize(mBaseWidth, mBaseHeight);
      if (mUseHostScaling) {
        [mWindow setContentAspectRatio:NSMakeSize(mBaseWidth, mBaseHeight)];
        [mWindow setContentMinSize:NSMakeSize(std::max<int>(100, mBaseWidth / 2),
                                              std::max<int>(100, mBaseHeight / 2))];
        [mWindow setContentMaxSize:NSMakeSize(mBaseWidth * 4, mBaseHeight * 4)];
      } else if (!mNativeResize) {
        [mWindow setContentMinSize:NSMakeSize(mBaseWidth, mBaseHeight)];
        [mWindow setContentMaxSize:NSMakeSize(mBaseWidth, mBaseHeight)];
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
      NSView *contentView = [mWindow contentView];
      if (contentView != nil) {
        if (mUseHostScaling) {
          [contentView setBoundsSize:NSMakeSize(mBaseWidth, mBaseHeight)];
        } else {
          [contentView setFrame:NSMakeRect(0, 0, mBaseWidth, mBaseHeight)];
          [contentView setBounds:NSMakeRect(0, 0, mBaseWidth, mBaseHeight)];
        }
        if ([[contentView subviews] count] > 0) {
          NSView *pluginView = [[contentView subviews] firstObject];
          [pluginView setFrame:NSMakeRect(0, 0, mBaseWidth, mBaseHeight)];
        }
      }
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

    void (^refreshBlock)(void) = ^{
      if (mWindow != nil) {
        ensurePluginSubviewsVisible([mWindow contentView]);
        [mWindow displayIfNeeded];
      }
    };
    if ([NSThread isMainThread]) {
      refreshBlock();
    } else {
      dispatch_sync(dispatch_get_main_queue(), refreshBlock);
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
    if (!mNativeResize && !mUseHostScaling) {
      NSRect contentRect = NSMakeRect(0, 0, mBaseWidth, mBaseHeight);
      NSRect lockedFrame = [mWindow frameRectForContentRect:contentRect];
      return lockedFrame.size;
    }
    if (mUseHostScaling) {
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
    if (mUseHostScaling) {
      [contentView setBoundsSize:NSMakeSize(mBaseWidth, mBaseHeight)];
      if ([[contentView subviews] count] > 0) {
        NSView *pluginView = [[contentView subviews] firstObject];
        [pluginView setFrame:NSMakeRect(0, 0, mBaseWidth, mBaseHeight)];
      }
    } else if (mNativeResize) {
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
    ensurePluginSubviewsVisible(contentView);
    [mWindow displayIfNeeded];
  }

  void onNativeWindowOcclusionChanged() {
    if (mWindow == nil) {
      return;
    }
    if (([mWindow occlusionState] & NSWindowOcclusionStateVisible) != 0) {
      ensurePluginSubviewsVisible([mWindow contentView]);
    }
  }

  void onNativeWindowDidBecomeKey() {
    if (mWindow == nil) {
      return;
    }
    NSView *contentView = [mWindow contentView];
    if (contentView != nil) {
      if ([[contentView subviews] count] > 0) {
        NSView *pluginView = [[contentView subviews] firstObject];
        if ([mWindow firstResponder] != pluginView) {
          [mWindow makeFirstResponder:pluginView];
        }
      }
      ensurePluginSubviewsVisible(contentView);
      [mWindow displayIfNeeded];
    }
  }

private:
  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  SoLoudVst3Window *mWindow = nil;
  SoLoudVst3WindowDelegate *mDelegate = nil;
  int32_t mBaseWidth = 800;
  int32_t mBaseHeight = 600;
  bool mNativeResize = false;
  bool mUseHostScaling = false;
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

- (void)windowDidBecomeKey:(NSNotification *)notification {
  (void)notification;
  if (_owner != nullptr) {
    _owner->onNativeWindowDidBecomeKey();
  }
}

- (void)windowDidBecomeMain:(NSNotification *)notification {
  (void)notification;
  if (_owner != nullptr) {
    _owner->onNativeWindowDidBecomeKey();
  }
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

- (void)windowDidChangeOcclusionState:(NSNotification *)notification {
  (void)notification;
  if (_owner != nullptr) {
    _owner->onNativeWindowOcclusionChanged();
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

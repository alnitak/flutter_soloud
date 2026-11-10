#if defined(SOLOUD_ENABLE_VST3) && defined(__APPLE__)

#include <TargetConditionals.h>

#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE

#import <Cocoa/Cocoa.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/memorystream.h"
#include "vst3_editor_shm.h"
#include "vst3_editor_window.h"
#include "vst3_host.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

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

  [self applyContentSize:NSMakeSize(newW, newH) animate:YES];
}

- (void)applyContentSize:(NSSize)newSize animate:(BOOL)animate {
  NSRect curContentRect = [self contentRectForFrameRect:[self frame]];
  NSRect newContentRect = NSMakeRect(curContentRect.origin.x,
                                     curContentRect.origin.y - (newSize.height - curContentRect.size.height),
                                     newSize.width, newSize.height);
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
  [self setFrame:newFrame display:YES animate:animate];
}

- (void)resetZoom {
  if (self.baseContentSize.width <= 0 || self.baseContentSize.height <= 0) return;
  [self applyContentSize:self.baseContentSize animate:YES];
}
@end

namespace SoLoud {

namespace {
// Bridge for VST3 plugins (such as flutter_vst3) that embed their own
// precompiled App.framework and create a secondary FlutterEngine inside a
// Flutter macOS host application.
//
// 1. On macOS, FlutterDartProject has no aotLibraryPath property, so a secondary
//    FlutterEngine falls back to dlsym(RTLD_DEFAULT, "_kDartSnapshotData"),
//    which resolves to the host app's App.framework instead of the plugin's
//    App.framework. We intercept FlutterEngine initialization for precompiled
//    plugin bundles and append --aot-shared-library-name=<plugin App binary>.
// 2. When the host app runs in Debug (JIT) mode, the loaded FlutterMacOS
//    framework cannot execute a precompiled AOT plugin bundle. We detect this
//    before FlutterEngineInitialize is called, unregister the temporary engine
//    from FlutterAppDelegate (preventing 'flutter/lifecycle' platform-message
//    errors), and fail fast so the plugin cleanly falls back to its Cocoa UI.
const char kVst3PluginBundleKey = 0;

using Vst3EmbedderInitFn = int (*)(size_t, const void *, const void *, void *,
                                   void **);
Vst3EmbedderInitFn sOrigEmbedderInit = nullptr;
std::string sPendingAotSwitch;
std::atomic<bool> sAppTerminating{false};

id (*sOrigInitWithBundle)(id, SEL, NSBundle *) = nullptr;
id (*sOrigInitWithNameProject)(id, SEL, NSString *, id, BOOL) = nullptr;
BOOL (*sOrigRunWithEntrypoint)(id, SEL, NSString *) = nullptr;
void (*sOrigShutDownEngine)(id, SEL) = nullptr;

int vst3EmbedderInitWithAotLibrary(size_t version, const void *config,
                                   const void *args, void *userData,
                                   void **engineOut) {
  if (args == nullptr || sPendingAotSwitch.empty()) {
    return sOrigEmbedderInit(version, config, args, userData, engineOut);
  }
  const size_t structSize = *static_cast<const size_t *>(args);
  std::vector<char> copy(static_cast<const char *>(args),
                         static_cast<const char *>(args) + structSize);
  // In FlutterProjectArgs (64-bit), command_line_argc is at +0x28 and
  // command_line_argv is at +0x30.
  const int argc = *reinterpret_cast<const int *>(copy.data() + 0x28);
  const auto *argv =
      *reinterpret_cast<const char *const *const *>(copy.data() + 0x30);
  std::vector<const char *> vec(argv, argv + (argv && argc > 0 ? argc : 0));
  vec.push_back(sPendingAotSwitch.c_str());
  *reinterpret_cast<int *>(copy.data() + 0x28) = static_cast<int>(vec.size());
  *reinterpret_cast<const char ***>(copy.data() + 0x30) = vec.data();
  return sOrigEmbedderInit(version, config, copy.data(), userData, engineOut);
}

void unregisterPluginEngineLifecycle(id engine) {
  if (engine == nil) {
    return;
  }
  id appDel = [[NSApplication sharedApplication] delegate];
  SEL removeSel = sel_registerName("removeApplicationLifecycleDelegate:");
  if (appDel != nil && [appDel respondsToSelector:removeSel]) {
    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(appDel, removeSel,
                                                          engine);
  }
}

id vst3InitWithPrecompiledDartBundle(id self, SEL _cmd, NSBundle *bundle) {
  id proj = sOrigInitWithBundle(self, _cmd, bundle);
  if (proj != nil && bundle != nil) {
    objc_setAssociatedObject(proj, &kVst3PluginBundleKey, bundle,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  return proj;
}

id vst3InitWithNameProject(id self, SEL _cmd, NSString *name, id project,
                           BOOL allowHeadless) {
  id eng = sOrigInitWithNameProject(self, _cmd, name, project, allowHeadless);
  if (eng != nil && project != nil) {
    NSBundle *bundle = static_cast<NSBundle *>(
        objc_getAssociatedObject(project, &kVst3PluginBundleKey));
    if (bundle != nil) {
      objc_setAssociatedObject(eng, &kVst3PluginBundleKey, bundle,
                               OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
  }
  return eng;
}

BOOL vst3RunWithEntrypoint(id self, SEL _cmd, NSString *entrypoint) {
  NSBundle *appBundle = static_cast<NSBundle *>(
      objc_getAssociatedObject(self, &kVst3PluginBundleKey));
  if (appBundle == nil) {
    return sOrigRunWithEntrypoint(self, _cmd, entrypoint);
  }

  SEL apiSel = sel_registerName("embedderAPI");
  void **api = [self respondsToSelector:apiSel]
                   ? reinterpret_cast<void **(*)(id, SEL)>(objc_msgSend)(
                         self, apiSel)
                   : nullptr;
  // FlutterEngineProcTable::RunsAOTCompiledDartCode is at index 30 (+0xf0).
  auto runsAot = reinterpret_cast<bool (*)(void)>(api ? api[30] : nullptr);
  if (!runsAot || !runsAot()) {
    NSString *kernelPath = [appBundle pathForResource:@"kernel_blob"
                                               ofType:@"bin"
                                          inDirectory:@"flutter_assets"];
    if (kernelPath == nil) {
      unregisterPluginEngineLifecycle(self);
      return NO;
    }
  } else if (appBundle.executablePath != nil) {
    // FlutterEngineProcTable::Initialize is at index 5 (+0x28).
    sPendingAotSwitch = std::string("--aot-shared-library-name=") +
                        appBundle.executablePath.fileSystemRepresentation;
    if (reinterpret_cast<Vst3EmbedderInitFn>(api[5]) !=
        &vst3EmbedderInitWithAotLibrary) {
      sOrigEmbedderInit = reinterpret_cast<Vst3EmbedderInitFn>(api[5]);
      api[5] = reinterpret_cast<void *>(&vst3EmbedderInitWithAotLibrary);
    }
  }

  const BOOL ok = sOrigRunWithEntrypoint(self, _cmd, entrypoint);
  sPendingAotSwitch.clear();
  if (!ok) {
    unregisterPluginEngineLifecycle(self);
  }
  return ok;
}

void vst3ShutDownEngine(id self, SEL _cmd) {
  NSBundle *appBundle = static_cast<NSBundle *>(
      objc_getAssociatedObject(self, &kVst3PluginBundleKey));
  if (appBundle != nil) {
    objc_setAssociatedObject(self, &kVst3PluginBundleKey, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    unregisterPluginEngineLifecycle(self);
    if (!sAppTerminating.load(std::memory_order_acquire)) {
      // When closeEditor() is invoked from Dart FFI on the main thread, the
      // host app's Dart isolate is still bound to Thread 0. Calling
      // FlutterEngineDeinitialize synchronously would invoke
      // DartIsolate::Shutdown() -> Dart_EnterIsolate() on the same thread and
      // abort with "Dart_EnterIsolate expects there to be no current isolate".
      // Deferring shutDownEngine to the next main-queue turn runs it after the
      // FFI call returns and the host Dart isolate has exited.
#if !__has_feature(objc_arc)
      [self retain];
      [appBundle retain];
#endif
      dispatch_async(dispatch_get_main_queue(), ^{
        (void)appBundle;
        sOrigShutDownEngine(self, _cmd);
#if !__has_feature(objc_arc)
        [self release];
        [appBundle release];
#endif
      });
      return;
    }
  }
  sOrigShutDownEngine(self, _cmd);
}

void installFlutterVst3HostBridgeIfNeeded() {
  static bool sInstalled = false;
  if (sInstalled) {
    return;
  }
  Class projCls = NSClassFromString(@"FlutterDartProject");
  Class engCls = NSClassFromString(@"FlutterEngine");
  if (projCls == nil || engCls == nil) {
    return;
  }
  Method mBundle = class_getInstanceMethod(
      projCls, sel_registerName("initWithPrecompiledDartBundle:"));
  Method mInit = class_getInstanceMethod(
      engCls, sel_registerName("initWithName:project:allowHeadlessExecution:"));
  Method mRun =
      class_getInstanceMethod(engCls, sel_registerName("runWithEntrypoint:"));
  Method mShut =
      class_getInstanceMethod(engCls, sel_registerName("shutDownEngine"));
  if (mBundle == nullptr || mInit == nullptr || mRun == nullptr ||
      mShut == nullptr) {
    return;
  }
  sOrigInitWithBundle = reinterpret_cast<decltype(sOrigInitWithBundle)>(
      method_setImplementation(
          mBundle, reinterpret_cast<IMP>(&vst3InitWithPrecompiledDartBundle)));
  sOrigInitWithNameProject =
      reinterpret_cast<decltype(sOrigInitWithNameProject)>(
          method_setImplementation(
              mInit, reinterpret_cast<IMP>(&vst3InitWithNameProject)));
  sOrigRunWithEntrypoint = reinterpret_cast<decltype(sOrigRunWithEntrypoint)>(
      method_setImplementation(mRun,
                               reinterpret_cast<IMP>(&vst3RunWithEntrypoint)));
  sOrigShutDownEngine = reinterpret_cast<decltype(sOrigShutDownEngine)>(
      method_setImplementation(mShut,
                               reinterpret_cast<IMP>(&vst3ShutDownEngine)));
  sInstalled = true;
}
} // namespace

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

  bool supportsRemoteEditor(const std::string &bundlePath) const override {
#if defined(SOLOUD_VST3_HELPER_MAIN) || !defined(SOLOUD_VST3_HELPER_PATH)
    (void)bundlePath;
    return false;
#else
    if (bundlePath.empty()) {
      return false;
    }
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *helperPath =
        [NSString stringWithUTF8String:SOLOUD_VST3_HELPER_PATH];
    if (helperPath == nil || ![fm isExecutableFileAtPath:helperPath]) {
      return false;
    }
    NSString *nsBundle = [NSString stringWithUTF8String:bundlePath.c_str()];
    if (nsBundle == nil) {
      return false;
    }
    NSString *appFw = [nsBundle
        stringByAppendingPathComponent:@"Contents/Frameworks/App.framework"];
    NSString *flFw = [nsBundle stringByAppendingPathComponent:
                                   @"Contents/Frameworks/FlutterMacOS.framework"];
    return [fm fileExistsAtPath:appFw] || [fm fileExistsAtPath:flFw];
#endif
  }

  bool openRemote(const Vst3RemoteEditorContext &ctx) override {
#if defined(SOLOUD_VST3_HELPER_MAIN) || !defined(SOLOUD_VST3_HELPER_PATH)
    (void)ctx;
    return false;
#else
    close();
    if (!supportsRemoteEditor(ctx.bundlePath)) {
      return false;
    }

    static std::atomic<uint32_t> sShmCounter{0};
    NSString *shmFile = [NSTemporaryDirectory()
        stringByAppendingPathComponent:
            [NSString stringWithFormat:@"soloud_vst3_shm_%d_%u.bin", getpid(),
                                       ++sShmCounter]];
    const char *shmPath = [shmFile fileSystemRepresentation];
    const int fd = ::open(shmPath, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) {
      return false;
    }
    if (ftruncate(fd, sizeof(SoLoudVst3EditorShm)) != 0) {
      ::close(fd);
      unlink(shmPath);
      return false;
    }
    void *mapped =
        mmap(nullptr, sizeof(SoLoudVst3EditorShm), PROT_READ | PROT_WRITE,
             MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED || mapped == nullptr) {
      unlink(shmPath);
      return false;
    }

    mShm = static_cast<SoLoudVst3EditorShm *>(mapped);
    initVst3EditorShmHeader(mShm);

    if (ctx.component != nullptr) {
      Steinberg::MemoryStream stream;
      if (ctx.component->getState(&stream) == Steinberg::kResultTrue &&
          stream.getSize() > 0 &&
          static_cast<size_t>(stream.getSize()) <= kVst3EditorMaxStateBytes &&
          stream.getData() != nullptr) {
        mShm->initialStateSize = static_cast<uint32_t>(stream.getSize());
        std::memcpy(mShm->initialState, stream.getData(),
                    mShm->initialStateSize);
      }
    }

    if (ctx.controller != nullptr) {
      const int32_t count = ctx.controller->getParameterCount();
      uint32_t written = 0;
      for (int32_t i = 0;
           i < count && written < kVst3EditorMaxInitialParams; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        if (ctx.controller->getParameterInfo(i, info) == Steinberg::kResultOk) {
          mShm->initialParams[written].paramId = info.id;
          mShm->initialParams[written].reserved = 0;
          mShm->initialParams[written].value =
              ctx.controller->getParamNormalized(info.id);
          ++written;
        }
      }
      mShm->initialParamCount = written;
    }

    mOnGuiParamEdit = ctx.onGuiParamEdit;

    const std::string parentPidStr = std::to_string(getpid());
    const std::string titleStr =
        ctx.title.empty() ? std::string("VST3 Plugin") : ctx.title;
    const std::string shmPathStr(shmPath);

    const pid_t pid = fork();
    if (pid < 0) {
      unlink(shmPath);
      munmap(mShm, sizeof(SoLoudVst3EditorShm));
      mShm = nullptr;
      return false;
    }
    if (pid == 0) {
      const int maxFd = getdtablesize();
      for (int d = 3; d < maxFd; ++d) {
        ::close(d);
      }
      execl(SOLOUD_VST3_HELPER_PATH, SOLOUD_VST3_HELPER_PATH,
            ctx.bundlePath.c_str(), ctx.classUid.c_str(), titleStr.c_str(),
            shmPathStr.c_str(), parentPidStr.c_str(),
            static_cast<char *>(nullptr));
      _exit(127);
    }

    mHelperPid = pid;
    mShmPath = shmPathStr;

    // Wait up to 3 seconds for the helper window to finish mounting.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    uint32_t ready = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      ready = mShm->helperReady.load(std::memory_order_acquire);
      if (ready != 0) {
        break;
      }
      int status = 0;
      if (waitpid(mHelperPid, &status, WNOHANG) != 0) {
        mHelperPid = 0;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (ready != 1) {
      close();
      return false;
    }

    mIsOpen.store(true, std::memory_order_release);
    publishRemoteShm(mShm);

    void (^startTimerBlock)(void) = ^{
      NSTimer *timer = [NSTimer
          timerWithTimeInterval:0.008
                        repeats:YES
                          block:^(NSTimer *t) {
                            (void)t;
                            pollRemoteHelper();
                          }];
      [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
#if !__has_feature(objc_arc)
      [timer retain];
#endif
      mPollTimer = timer;
    };
    if ([NSThread isMainThread]) {
      startTimerBlock();
    } else {
      dispatch_sync(dispatch_get_main_queue(), startTimerBlock);
    }

    return true;
#endif
  }

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
      installFlutterVst3HostBridgeIfNeeded();

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
        if (pluginView != nil) {
          [mWindow makeFirstResponder:pluginView];
        } else {
          // Some VST3 plugins (such as flutter_vst3 EmbeddedFlutterView) mount
          // their NSView asynchronously on the next main-queue turn.
          SoLoudVst3Window *winRef = mWindow;
          dispatch_async(dispatch_get_main_queue(), ^{
            if (!mIsOpen.load(std::memory_order_acquire) || mWindow != winRef ||
                winRef == nil) {
              return;
            }
            if (mPlugView != nullptr) {
              Steinberg::ViewRect asyncRect{0, 0, 0, 0};
              if (mPlugView->getSize(&asyncRect) == Steinberg::kResultTrue &&
                  asyncRect.getWidth() > 64 && asyncRect.getHeight() > 64 &&
                  (asyncRect.getWidth() != mBaseWidth ||
                   asyncRect.getHeight() != mBaseHeight)) {
                resizeView(mPlugView, &asyncRect);
              }
            }
            NSView *cv = [winRef contentView];
            if (cv != nil && [[cv subviews] count] > 0) {
              ensurePluginSubviewsVisible(cv);
              NSView *asyncSub = [[cv subviews] firstObject];
              if (asyncSub != nil &&
                  ([winRef firstResponder] == nil ||
                   [winRef firstResponder] == winRef)) {
                [winRef makeFirstResponder:asyncSub];
              }
              [winRef displayIfNeeded];
            }
          });
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
        mPlugView == nullptr && mHelperPid == 0 && mShm == nullptr) {
      return;
    }

    void (^closeBlock)(void) = ^{
      mIsOpen.store(false, std::memory_order_release);
      if (mPollTimer != nil) {
        [mPollTimer invalidate];
#if !__has_feature(objc_arc)
        [mPollTimer release];
#endif
        mPollTimer = nil;
      }
      if (mShm != nullptr) {
        mShm->hostClosed.store(1, std::memory_order_release);
      }
      if (mHelperPid > 0) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        int status = 0;
        bool exited = false;
        while (std::chrono::steady_clock::now() < deadline) {
          if (waitpid(mHelperPid, &status, WNOHANG) != 0) {
            exited = true;
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!exited) {
          kill(mHelperPid, SIGKILL);
          waitpid(mHelperPid, &status, 0);
        }
        mHelperPid = 0;
      }
      if (mShm != nullptr) {
        retireRemoteShm();
        munmap(mShm, sizeof(SoLoudVst3EditorShm));
        mShm = nullptr;
      }
      if (!mShmPath.empty()) {
        unlink(mShmPath.c_str());
        mShmPath.clear();
      }
      mOnGuiParamEdit = nullptr;
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
    if (rectEqual(r, *newSize) &&
        mBaseWidth == newSize->getWidth() &&
        mBaseHeight == newSize->getHeight()) {
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

    view->onSize(newSize);

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
  void pollRemoteHelper() {
    if (mShm == nullptr) {
      return;
    }
    uint32_t r = mShm->guiToHostRead.load(std::memory_order_relaxed);
    const uint32_t w = mShm->guiToHostWrite.load(std::memory_order_acquire);
    while (r != w) {
      const auto msg = mShm->guiToHostQueue[r % kVst3EditorParamQueueSize];
      if (mOnGuiParamEdit) {
        mOnGuiParamEdit(msg.paramId, msg.value);
      }
      ++r;
    }
    mShm->guiToHostRead.store(r, std::memory_order_release);

    bool helperExited =
        (mShm->helperClosed.load(std::memory_order_acquire) != 0);
    if (!helperExited && mHelperPid > 0) {
      int status = 0;
      if (waitpid(mHelperPid, &status, WNOHANG) != 0) {
        mHelperPid = 0;
        helperExited = true;
      }
    }
    if (helperExited) {
      close();
    }
  }

  Steinberg::IPtr<Steinberg::IPlugView> mPlugView;
  SoLoudVst3Window *mWindow = nil;
  SoLoudVst3WindowDelegate *mDelegate = nil;
  SoLoudVst3EditorShm *mShm = nullptr;
  std::string mShmPath;
  pid_t mHelperPid = 0;
  NSTimer *mPollTimer = nil;
  std::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue)>
      mOnGuiParamEdit;
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
  SoLoud::sAppTerminating.store(true, std::memory_order_release);
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
  SoLoud::sAppTerminating.store(true, std::memory_order_release);
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

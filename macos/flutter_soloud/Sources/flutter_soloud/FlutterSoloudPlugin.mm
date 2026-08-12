#import "FlutterSoloudPlugin.h"

#include "engine_lifecycle.h"

/// The engine-local channel Dart uses to hand this plugin its engine id.
///
/// macOS does not publicly expose the owning FlutterEngine's id to a plugin —
/// unlike Android, where `FlutterEngine.getEngineId()` is public — so the id
/// has to come from the isolate, which reads it from
/// `PlatformDispatcher.instance.engineId`.
static NSString *const kEngineLifecycleChannel =
    @"flutter_soloud/engine_lifecycle";

/// Matches `kNoEngineId` in the shared C++ implementation.
static const int64_t kNoEngineId = -1;

@implementation FlutterSoloudPlugin {
  /// Touched only from the platform thread: method calls are delivered there,
  /// and so is the engine's deallocation. No synchronization needed, and none
  /// should be added without revisiting that.
  int64_t _engineId;
  BOOL _hasEngineId;
}

+ (void)registerWithRegistrar:(id<FlutterPluginRegistrar>)registrar {
  // Deliberately does no native work. This runs at engine startup for every app
  // that depends on the plugin, whether or not it ever plays a sound: it must
  // stay a channel registration and nothing more.
  FlutterSoloudPlugin *instance = [[FlutterSoloudPlugin alloc] init];
  FlutterMethodChannel *channel =
      [FlutterMethodChannel methodChannelWithName:kEngineLifecycleChannel
                                  binaryMessenger:registrar.messenger];
  [registrar addMethodCallDelegate:instance channel:channel];

  // Published so the registrar holds this object for the engine's lifetime.
  // That is what makes -dealloc a usable teardown signal below.
  [registrar publish:instance];

  // The channel is deliberately not retained here. Nothing needs it after
  // registration -- the handler block lives in the engine's messenger, and it
  // is that block, released when the engine deallocates, which lets this object
  // go. Holding the channel would add a strong reference with no purpose.
}

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _engineId = kNoEngineId;
    _hasEngineId = NO;
  }
  return self;
}

- (void)handleMethodCall:(FlutterMethodCall *)call
                  result:(FlutterResult)result {
  if (![call.method isEqualToString:@"prepareEngineInit"]) {
    result(FlutterMethodNotImplemented);
    return;
  }

  NSDictionary *arguments = call.arguments;
  if (![arguments isKindOfClass:[NSDictionary class]]) {
    result([FlutterError errorWithCode:@"invalid_arguments"
                               message:@"Expected a map of prepare arguments."
                               details:nil]);
    return;
  }

  NSNumber *engineIdArgument = arguments[@"engineId"];
  NSNumber *epochArgument = arguments[@"shutdownEpoch"];
  if (![engineIdArgument isKindOfClass:[NSNumber class]] ||
      ![epochArgument isKindOfClass:[NSNumber class]]) {
    result([FlutterError
        errorWithCode:@"invalid_arguments"
              message:@"Expected an engine id and a shutdown epoch."
              details:nil]);
    return;
  }

  const int64_t engineId = [engineIdArgument longLongValue];
  if (engineId == kNoEngineId) {
    result([FlutterError
        errorWithCode:@"invalid_engine_id"
              message:@"The engine id is the no-engine sentinel."
              details:nil]);
    return;
  }

  const uint64_t shutdownEpoch =
      (uint64_t)[epochArgument unsignedLongLongValue];

  // Claimed here, before the reply, so the claim exists before Dart dispatches
  // the initialization. Conditional on the epoch Dart read before it sent this:
  // `deinit()` can run while Dart is suspended waiting for the reply, and a
  // claim landing after that teardown would lower the shutdown flag and record
  // ownership for an engine that is already gone.
  if (!prepareEngineInitForRequest(engineId, shutdownEpoch)) {
    result([FlutterError
        errorWithCode:@"stale_prepare"
              message:@"This initialization was superseded by a shutdown."
              details:nil]);
    return;
  }

  // Only now is this plugin associated with the engine: a refused request must
  // not arm a teardown, because it took no claim to tear down.
  _engineId = engineId;
  _hasEngineId = YES;

  // Hot-restart recovery. A restart leaves the previous isolate's callables
  // registered under this same engine id, and this is the first point a
  // supported API can retire them. Scoped to this engine, and the new isolate
  // has not registered its own callables yet, so it can only ever retire stale
  // same-engine ones.
  clearDartCallbackRegistrationsForEngine(engineId);

  result(@(YES));
}

- (void)dealloc {
  // macOS's stand-in for iOS's detachFromEngineForRegistrar:, which does not
  // exist here -- the macOS FlutterPlugin protocol is only
  // registerWithRegistrar: and handleMethodCall:result:.
  //
  // Two strong references hold this object, and FlutterEngine releases both
  // when it deallocates: the registrar's published value, which its dealloc
  // explicitly replaces with NSNull, and the method-call handler block stored
  // in the engine's messenger. So this runs when the engine goes, which is the
  // signal we need.
  //
  // It is a weaker guarantee than iOS's: it is reference-count timing rather
  // than a documented contract, and an application that retains this object --
  // through `valuePublishedByPlugin:`, say -- delays or prevents it. When that
  // happens the result is simply that the engine is not torn down
  // automatically, exactly as before this existed; nothing is left in a worse
  // state.
  if (!_hasEngineId) {
    // No handshake ever completed, so this plugin does not know which engine it
    // belongs to. Tearing down "whatever is current" would let a dying engine
    // destroy a replacement, so do nothing at all.
    return;
  }

  const int64_t engineId = _engineId;
  _hasEngineId = NO;
  _engineId = kNoEngineId;

  // Returns immediately: it retires this engine's callables, and hands the
  // blocking part of the teardown -- stopping the device, joining the scheduler
  // -- to a native worker thread. Nothing here may block, least of all in
  // dealloc.
  requestEngineTeardownForEngine(engineId);
}

@end

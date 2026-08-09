#import <FlutterMacOS/FlutterMacOS.h>

/**
 * Keeps flutter_soloud's process-global native state in step with the lifetime
 * of the FlutterEngine that owns it.
 *
 * The native engine — the SoLoud player, its output device, its lifecycle
 * scheduler and the registered Dart callables — lives for the whole process,
 * while the Dart isolate driving it belongs to a single FlutterEngine. When
 * that engine goes away but the process keeps running, native code would
 * otherwise keep an output device running with nothing left able to control it.
 *
 * This plugin only observes the engine's lifetime. Ownership and teardown
 * decisions belong to the shared C++ implementation, which the Android and iOS
 * plugins drive through the same entry points.
 *
 * macOS has no `detachFromEngineForRegistrar:` — its FlutterPlugin protocol is
 * only `registerWithRegistrar:` and `handleMethodCall:result:` — so this class
 * takes its own deallocation as the signal instead. FlutterEngine's `dealloc`
 * republishes `NSNull` over every registrar and releases the message handlers
 * holding this object, which is what makes that work. See the implementation.
 *
 * It does no native work at registration: an app that depends on flutter_soloud
 * but never plays a sound pays nothing for this.
 */
@interface FlutterSoloudPlugin : NSObject <FlutterPlugin>
@end

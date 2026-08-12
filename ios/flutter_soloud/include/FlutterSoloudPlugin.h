#import <Flutter/Flutter.h>

/**
 * Keeps flutter_soloud's process-global native state in step with the lifetime
 * of the FlutterEngine that owns it.
 *
 * The native engine — the SoLoud player, its output device, its lifecycle
 * scheduler and the registered Dart callables — lives for the whole process,
 * while the Dart isolate driving it belongs to a single FlutterEngine. When
 * that engine goes away but the process keeps running (add-to-app hosts, an
 * engine created and released around a screen, a FlutterEngineGroup), native
 * code would otherwise keep an output device running with nothing left able to
 * control it.
 *
 * This plugin only observes the engine's lifetime. Ownership and teardown
 * decisions belong to the shared C++ implementation, which the Android plugin
 * drives through the same entry points.
 *
 * It does no native work at registration: an app that depends on flutter_soloud
 * but never plays a sound pays nothing for this.
 *
 * Lives in the public headers directory because the SwiftPM target has no
 * explicit `publicHeadersPath` and so uses its default `include` directory —
 * which is where GeneratedPluginRegistrant looks for
 * `<flutter_soloud/FlutterSoloudPlugin.h>`.
 */
@interface FlutterSoloudPlugin : NSObject <FlutterPlugin>
@end

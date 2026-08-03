// ignore_for_file: avoid_print, document_ignores

import 'dart:async';
import 'dart:js_interop';

import 'package:web/web.dart' as web;

/// Initialize the WASM module before the app starts.
/// It must be compiled with
/// `dart compile js -O3 -o init_module.dart.js ./init_module.dart`
/// and the resulting `init_module.dart.js` must be added as a script
/// in the `index.html`:
/// ```html
/// <script src="assets/packages/flutter_soloud/web/init_module.dart.js" defer></script>
/// ```
///
/// This script picks the right WASM build flavor at runtime and loads its
/// JS glue dynamically:
/// - `libflutter_soloud_plugin_mt.js` (multi-threaded, SharedArrayBuffer)
///   when the page is cross-origin isolated (COOP/COEP headers present);
/// - `libflutter_soloud_plugin.js` (single-threaded) otherwise.
///
/// For backward compatibility, if the page already loads a glue script
/// explicitly, e.g.:
/// ```html
/// <script src="assets/packages/flutter_soloud/web/libflutter_soloud_plugin.js" defer></script>
/// ```
/// that module is used as-is and no flavor selection happens.
///
/// Setting `self.flutter_soloud_force_single_threaded = true` before this
/// script runs forces the single-threaded flavor (useful for debugging).

@JS('Module_soloud')
external JSFunction? get moduleFactory; // null when the glue is not loaded

@JS('Module_soloud')
external JSObject moduleConstructor(); // Represents the IIFE

@JS('self.Module_soloud') // Attach Module_soloud to the global scope
external set globalModule(JSObject module);

/// Records which build flavor is in use (`mt`, `st` or `manual`).
@JS('self.flutter_soloud_build')
external set buildFlavor(JSString flavor);

@JS('globalThis.crossOriginIsolated')
external bool? get isCrossOriginIsolated;

@JS('globalThis.SharedArrayBuffer')
external JSObject? get sharedArrayBuffer;

@JS('self.flutter_soloud_force_single_threaded')
external bool? get forceSingleThreaded;

const _assetsBase = 'assets/packages/flutter_soloud/web/';

/// Dynamically loads a JS file and waits for it to execute.
Future<void> _loadScript(String src) {
  final completer = Completer<void>();
  final script = web.HTMLScriptElement()..src = src;
  script.onload = ((web.Event _) => completer.complete()).toJS;
  script.onerror = ((web.Event _) {
    completer.completeError(StateError('Failed to load script: $src'));
  }).toJS;
  web.document.head!.appendChild(script);
  return completer.future;
}

Future<void> initializeModule() async {
  try {
    if (moduleFactory == null) {
      // No glue script loaded by the page: choose the best flavor supported
      // by this browsing context and load it dynamically.
      final useMt = forceSingleThreaded != true &&
          isCrossOriginIsolated == true &&
          sharedArrayBuffer != null;
      final flavor = useMt ? 'mt' : 'st';
      buildFlavor = flavor.toJS;
      print('flutter_soloud: loading $flavor WASM build '
          '(crossOriginIsolated: $isCrossOriginIsolated)');
      await _loadScript(
        '$_assetsBase/libflutter_soloud_plugin${useMt ? '_mt' : ''}.js',
      );
      if (moduleFactory == null) {
        throw StateError('Module_soloud not found after loading the glue.');
      }
    } else {
      // The page loaded a glue script explicitly (old-style index.html).
      buildFlavor = 'manual'.toJS;
    }

    // Convert JavaScript Promise to Dart Future
    final modulePromise = moduleConstructor() as JSPromise;
    final module = await JSPromiseToFuture<JSAny?>(modulePromise).toDart;
    if (module == null) {
      throw Exception('Module initialization failed: Module is null');
    }
    globalModule = module as JSObject; // Make it globally accessible
    print('Module_soloud initialized and set globally.');
  } catch (e) {
    print('Failed to initialize Module_soloud: $e');
    rethrow;
  }
}

/// The main Web Worker
void main() async {
  await initializeModule();
}

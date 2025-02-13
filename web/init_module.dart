// ignore_for_file: avoid_print, document_ignores

import 'dart:js_interop';

/// Initialize the WASM module before the app starts.
/// It must be compiled with
/// `dart compile js -O3 -o init_module.dart.js ./init_module.dart`
/// and the resulting `init_module.dart.js` must be added as a script
/// in the `index.html` with also `libflutter_soloud_plugin.js`:
/// ```hmtl
/// <script src="assets/packages/flutter_soloud/web/libflutter_soloud_plugin.js" defer></script>
/// <script src="assets/packages/flutter_soloud/web/init_module.dart.js" defer></script>
/// ```

@JS('Module_soloud')
external JSObject getModule();

@JS('Module_soloud')
external JSObject moduleConstructor(); // Represents the IIFE

@JS('self.Module_soloud') // Attach Module_soloud to the global scope
external set globalModule(JSObject module);

Future<void> initializeModule() async {
  try {
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

// ignore_for_file: avoid_print

import 'dart:js_interop';
import 'dart:js_util';

/// Initialize the WASM ogg and opus modules before the app starts.
/// It must be compiled with
/// `dart compile js -O3 -o init_xiph_modules.dart.js ./init_xiph_modules.dart`
/// and the resulting `init_xiph_modules.dart.js` must be added as a script
/// in the `index.html` with also `libflutter_soloud_plugin.js`:
/// ```hmtl
/// <script src="assets/packages/flutter_soloud/web/libflutter_soloud_plugin.js" defer></script>
/// <script src="assets/packages/flutter_soloud/web/init_xiph_modules.dart.js" defer></script>
/// ```

@JS('Module_ogg')
external JSObject getOggModule();

@JS('Module_ogg')
external JSObject oggModuleConstructor(); // Represents the IIFE

@JS('self.Module_ogg') // Attach Module_ogg to the global scope
external set globalOggModule(JSObject module);

@JS('Module_opus')
external JSObject getOpusModule();

@JS('Module_opus')
external JSObject opusModuleConstructor(); // Represents the IIFE

@JS('self.Module_opus') // Attach Module_opus to the global scope
external set globalOpusModule(JSObject module);

Future<void> initializeXiphModules() async {
  try {
    // Convert JavaScript Promise to Dart Future
    final oggPromise = oggModuleConstructor();
    final oggModule = await promiseToFuture<JSObject>(oggPromise);
    globalOggModule = oggModule; // Make it globally accessible
    print('Module_ogg initialized and set globally.');
  } catch (e) {
    print('Failed to initialize Module_ogg: $e');
    rethrow;
  }
  try {
    // Convert JavaScript Promise to Dart Future
    final opusPromise = opusModuleConstructor();
    final opusModule = await promiseToFuture<JSObject>(opusPromise);
    globalOpusModule = opusModule; // Make it globally accessible
    print('Module_opus initialized and set globally.');
  } catch (e) {
    print('Failed to initialize Module_opus: $e');
    rethrow;
  }
}

/// The main Web Worker
void main() async {
  await initializeXiphModules();
}

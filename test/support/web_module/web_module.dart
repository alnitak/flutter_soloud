// Loads the flutter_soloud WASM module before browser tests that start the
// real engine (see test/common/flutter_test_config.dart). A no-op on the VM.
export 'web_module_stub.dart'
    if (dart.library.js_interop) 'web_module_web.dart';

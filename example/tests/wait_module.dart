/// Waits for the flutter_soloud WASM module to finish instantiating.
///
/// On web the module loads asynchronously (see init_soloud.js), so a
/// test that touches the raw bindings before `SoLoud.init()` would hit a
/// not-yet-instantiated `Module_soloud` (still the MODULARIZE factory
/// function, without any exports). On native this is a no-op.
library;

export 'wait_module_web.dart' if (dart.library.io) 'wait_module_stub.dart';

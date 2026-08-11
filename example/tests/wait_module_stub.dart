/// No-op on platforms without `dart:js_interop` (native): the FFI library
/// is loaded eagerly, so there is nothing to wait for.
Future<void> waitForModule() async {}

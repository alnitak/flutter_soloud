// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/bindings/bindings_player_ffi.dart';

/// Controller that expose FFI.
class SoLoudController {
  factory SoLoudController() => _instance ??= SoLoudController._();

  SoLoudController._() {
    // With code assets the @Native bindings are resolved by the VM from the
    // hooked-in native library, so there is no DynamicLibrary to open.
    soLoudFFI = FlutterSoLoudFfi();
  }

  static SoLoudController? _instance;

  late final FlutterSoLoudFfi soLoudFFI;
}

// ignore_for_file: public_member_api_docs

import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:flutter_soloud/src/bindings/bindings_player_ffi.dart';

/// Controller that expose FFI.
class SoLoudController {
  factory SoLoudController() => _instance ??= SoLoudController._();

  SoLoudController._() {
    /// Initialize lib
    if (Platform.isLinux || Platform.isAndroid) {
      nativeLib = ffi.DynamicLibrary.open('libflutter_soloud_plugin.so');
    } else if (Platform.isWindows) {
      nativeLib = ffi.DynamicLibrary.open('flutter_soloud_plugin.dll');
    } else if (Platform.isMacOS) {
      try {
        final lib = ffi.DynamicLibrary.process();
        lib.lookup('isInited');
        nativeLib = lib;
      } catch (_) {
        final candidatePaths = [
          '../flutter_soloud/macos/cmake_build/macosx/libflutter_soloud_plugin.dylib',
          '../../flutter_soloud/macos/cmake_build/macosx/libflutter_soloud_plugin.dylib',
          '/Volumes/NVME/workspace/libs/flutter_soloud/macos/cmake_build/macosx/libflutter_soloud_plugin.dylib',
        ];
        ffi.DynamicLibrary? foundLib;
        for (final p in candidatePaths) {
          if (File(p).existsSync()) {
            try {
              foundLib = ffi.DynamicLibrary.open(p);
              break;
            } catch (_) {}
          }
        }
        nativeLib = foundLib ?? ffi.DynamicLibrary.process();
      }
    } else {
      nativeLib = ffi.DynamicLibrary.process();
    }
    soLoudFFI = FlutterSoLoudFfi.fromLookup(nativeLib.lookup);
  }

  static SoLoudController? _instance;

  late ffi.DynamicLibrary nativeLib;

  late final FlutterSoLoudFfi soLoudFFI;
}

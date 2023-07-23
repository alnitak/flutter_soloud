/// header file used by ffiGen to generate
/// lib/flutter_soloud_bindings_ffi_TMP.dart

// copy here the definition you would like to generate.
// go into "flutter_soloud" dir from the root project dir
//
// flutter pub run ffigen --config ffigen_player.yaml
// to generate [flutter_soloud_bindings_ffi_TMP.dart] or
//
// flutter pub run ffigen --config ffigen_capture.yaml
// to generate [lib/bindings_capture_ffi_TMP.dart]
//
// the generated code will be in flutter_soloud_bindings_ffi_TMP.dart
// copy the generated definition  into flutter_soloud_bindings_ffi.dart


#include "enums.h"

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate



FFI_PLUGIN_EXPORT enum CaptureErrors getCaptureAudioTexture2D(float** samples);

FFI_PLUGIN_EXPORT enum CaptureErrors setCaptureFftSmoothing(float smooth);
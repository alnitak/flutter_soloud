/// header file used by ffiGen to generate
/// lib/flutter_soloud_bindings_ffi_TMP.dart

// copy here the definition you would like to generate.
// go into "flutter_soloud" dir from the root project dir
//
// dart run ffigen
//
// the generated code will be in flutter_soloud_bindings_ffi_TMP.dart
// copy the generated definition  into flutter_soloud_bindings_ffi.dart


#include "enums.h"

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate

/// @brief Set a dart function to call when the sound with [handle] handle ends
/// @param callback the dart function. Must be global or a static class member:
///     ```@pragma('vm:entry-point')
///        void playEndedCallback(int handle) {
///             // here the sound with [handle] has ended.
///             // you can play again
///             soLoudController.soLoudFFI.play(handle);
///             // or dispose it
///             soLoudController.soLoudFFI.stop(handle);
///        } 
///     ```
/// @param handle the handle to the sound
/// @return true if success;
FFI_PLUGIN_EXPORT bool setPlayEndedCallback(void (*callback)(unsigned int), unsigned int handle);

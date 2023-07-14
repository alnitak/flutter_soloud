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

/// @brief Pause or unpause already loaded sound identified by [handle]
/// @param handle the sound handle
FFI_PLUGIN_EXPORT void pauseSwitch(unsigned int handle);

/// @brief Gets the pause state
/// @param handle the sound handle
/// @return true if paused
FFI_PLUGIN_EXPORT bool getPause(unsigned int handle);


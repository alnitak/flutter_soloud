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

/// @brief Pause already loaded sound identified by [handle]
/// @param handle 
FFI_PLUGIN_EXPORT void pause(unsigned int handle);

/// @brief Play already loaded sound identified by [handle]
/// @param handle 
FFI_PLUGIN_EXPORT void play(unsigned int handle);

/// @brief Stop already loaded sound identified by [handle] and clear it
/// @param handle
FFI_PLUGIN_EXPORT void stop(unsigned int handle);

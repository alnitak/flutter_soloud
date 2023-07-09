/// header file used by ffiGen to generate
/// lib/flutter_soloud_bindings_ffi_TMP.dart

// copy here the definition you would like to generate.
// go into "flutter_soloud" dir from the root project dir
//
// dart run ffigen -v all
//
// the generated code will be in flutter_soloud_bindings_ffi_TMP.dart
// copy the generated definition  into flutter_soloud_bindings_ffi.dart


#ifndef BINDINGS_H
#define BINDINGS_H

#include "enums.h"

#ifndef FFI_PLUGIN_EXPORT
    #ifdef __ANDROID__
        #define FFI_PLUGIN_EXPORT __attribute__((visibility("default"))) __attribute__((used))
    #elif __linux__
        #define FFI_PLUGIN_EXPORT __attribute__((visibility("default"))) __attribute__((used))
    #elif _WIN32 | WIN32 | __WIN32 | _WIN64
        #define FFI_PLUGIN_EXPORT __declspec(dllexport)
    #endif
#endif


//--------------------- copy here the new functions to generate
/// header file used by ffiGen to generate
/// lib/flutter_soloud_bindings_ffi_TMP.dart

// copy here the definition you would like to generate.
// go into "flutter_soloud" dir from the root project dir
// and run:
//
// flutter pub run ffigen --config ffigen.yaml
// to generate [flutter_soloud_FFIGEN.dart], or if it complains for some reason:
// export CPATH="$(clang -v 2>&1 | grep "Selected GCC installation" | rev | cut -d' ' -f1 | rev)/include";  dart run ffigen --config ffigen.yaml
//
// the generated code will be placed into flutter_soloud_FFIGEN.dart
// copy the generated definition into flutter_soloud_bindings_ffi.dart

#include "enums.h"
// #include <stdbool.h>

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate

FFI_PLUGIN_EXPORT void readSamplesFromFile(
    const char *filePath,
    float startTime,
    float endTime,
    unsigned long numSamplesNeeded,
    bool average,
    float *pSamples);

FFI_PLUGIN_EXPORT void readSamplesFromMem(
    const unsigned char *buffer,
    unsigned long dataSize,
    float startTime,
    float endTime,
    unsigned long numSamplesNeeded,
    bool average,
    float *pSamples);

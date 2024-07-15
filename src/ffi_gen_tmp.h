/// header file used by ffiGen to generate
/// lib/flutter_soloud_bindings_ffi_TMP.dart

// copy here the definition you would like to generate.
// go into "flutter_soloud" dir from the root project dir
// and run:
//
// flutter pub run ffigen --config ffigen.yaml
// to generate [flutter_soloud_FFIGEN.dart]
//
// the generated code will be placed into flutter_soloud_FFIGEN.dart
// copy the generated definition into flutter_soloud_bindings_ffi.dart

#include "enums.h"

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate

FFI_PLUGIN_EXPORT enum PlayerErrors getFilterParams(
    unsigned int handle,
    enum FilterType filterType,
    int attributeId,
    float *filterValue);

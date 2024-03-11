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

struct CaptureDevice
{
    char *name;
    unsigned int isDefault;
};

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate



/// Play already loaded sound identified by [hash]
///
/// [hash] the unique sound hash of a sound
/// [volume] 1.0f full volume
/// [pan] 0.0f centered
/// [paused] 0 not paused
/// [newHandle] the handle for this sound
/// Return the error if any and the [newHandle] of the sound
FFI_PLUGIN_EXPORT enum PlayerErrors play(
    unsigned int hash,
    float volume,
    float pan,
    bool paused,
    unsigned int *newHandle);

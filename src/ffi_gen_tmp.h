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


/// play3d() is the 3d version of the play() call
///
/// [posX], [posY], [posZ] are the audio source position coordinates.
/// [velX], [velY], [velZ] are the audio source velocity.
/// [looping] whether to start the sound in looping state.
/// [loopingStartAt] If looping is enabled, the loop point is, by default, 
/// the start of the stream. The loop start point can be set with this parameter, and 
/// current loop point can be queried with [getLoopingPoint] and 
/// changed by [setLoopingPoint].
/// Returns the handle of the sound, 0 if error
FFI_PLUGIN_EXPORT unsigned int play3d(
    unsigned int soundHash,
    float posX,
    float posY,
    float posZ,
    float velX,
    float velY,
    float velZ,
    float volume,
    bool paused,
    bool looping,
    double loopingStartAt,
    unsigned int *handle);
    
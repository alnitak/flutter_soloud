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



    /// Set a sound's relative play speed.
    /// Setting the value to 0 will cause undefined behavior, likely a crash.
    /// Change the relative play speed of a sample. This changes the effective 
    /// sample rate while leaving the base sample rate alone.
    ///
    /// Note that playing a sound at a higher sample rate will require SoLoud 
    /// to request more samples from the sound source, which will require more 
    /// memory and more processing power. Playing at a slower sample rate is cheaper.
    ///
    /// [handle] the sound handle
    /// [speed] the new speed
    FFI_PLUGIN_EXPORT void setRelativePlaySpeed(unsigned int handle, float speed);

    /// Get a sound's relative play speed.
    /// If an invalid handle is given to getRelativePlaySpeed, it will return 1.

    /// Return the current play speed.
    FFI_PLUGIN_EXPORT float getRelativePlaySpeed(unsigned int handle);

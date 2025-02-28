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
#include <stdbool.h>

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate

/// Set the inaudible behavior of a live sound. By default,
/// if a sound is inaudible, it's paused, and will resume when it
/// becomes audible again. With this function you can tell SoLoud
/// to either kill the sound if it becomes inaudible, or to keep
/// ticking the sound even if it's inaudible.
///
/// [handle]  handle to check.
/// [mustTick] whether to keep ticking or not when the sound becomes inaudible.
/// [kill] whether to kill the sound or not when the sound becomes inaudible.
FFI_PLUGIN_EXPORT void setInaudibleBehavior(unsigned int handle, bool mustTick, bool kill);

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


typedef void (*dartVoiceEndedCallback_t)(unsigned int *);
typedef void (*dartFileLoadedCallback_t)(enum PlayerErrors *, char *completeFileName, unsigned int *);
typedef void (*dartStateChangedCallback_t)(enum PlayerStateEvents *);

/// Set a Dart functions to call when an event occurs.
///
FFI_PLUGIN_EXPORT void setDartEventCallback(
        dartVoiceEndedCallback_t voice_ended_callback,
        dartFileLoadedCallback_t file_loaded_callback,
        dartStateChangedCallback_t state_changed_callback);

FFI_PLUGIN_EXPORT void nativeFree(void *pointer);
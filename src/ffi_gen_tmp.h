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

/// Used to create a new voice group. Returns 0 if not successful.
FFI_PLUGIN_EXPORT unsigned int createVoiceGroup();

/// Deallocates the voice group. Does not stop the voices attached to the voice group.
///
/// [handle] the group handle to destroy.
FFI_PLUGIN_EXPORT void destroyVoiceGroup(unsigned int handle);

/// Adds voice handle to the voice group. The voice handles can still be used separate from the group.
/// [voiceGroupHandle] the group handle to add the new [handle].
/// [voiceHandle] voice handle to add to the [voiceGroupHandle].
FFI_PLUGIN_EXPORT void addVoiceToGroup(unsigned int voiceGroupHandle, unsigned int voiceHandle);

/// Checks if the handle is a valid voice group. Does not care if the voice group is empty.
///
/// [handle] the group handle to check.
/// Return true if [handle] is a group handle.
FFI_PLUGIN_EXPORT int isVoiceGroup(unsigned int handle);

/// Checks whether a voice group is empty. SoLoud automatically trims the voice groups of
/// voices that have ended, so the group may be empty even though you've added valid voice handles to it.
///
/// [handle] group handle to check.
/// Return true if the group handle doesn't have any voices.
FFI_PLUGIN_EXPORT int isVoiceGroupEmpty(unsigned int handle);

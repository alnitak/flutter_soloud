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


/// Returns the number of concurrent sounds that are playing at the moment.
FFI_PLUGIN_EXPORT unsigned int getActiveVoiceCount();

/// Returns the number of concurrent sounds that are playing a specific audio source.
FFI_PLUGIN_EXPORT int countAudioSource(unsigned int soundHash);

/// Returns the number of voices the application has told SoLoud to play.
FFI_PLUGIN_EXPORT unsigned int getVoiceCount();

/// Get a sound's protection state.
FFI_PLUGIN_EXPORT bool getProtectVoice(unsigned int handle);

/// Set a sound's protection state.
///
/// Normally, if you try to play more sounds than there are voices,
/// SoLoud will kill off the oldest playing sound to make room.
/// This will most likely be your background music. This can be worked
/// around by protecting the sound.
/// If all voices are protected, the result will be undefined.
///
/// [handle]  handle to check.
/// [protect] whether to protect or not.
FFI_PLUGIN_EXPORT void setProtectVoice(unsigned int handle, bool protect);

/// Get the current maximum active voice count.
FFI_PLUGIN_EXPORT unsigned int getMaxActiveVoiceCount();

/// Set the current maximum active voice count.
/// If voice count is higher than the maximum active voice count,
/// SoLoud will pick the ones with the highest volume to actually play.
/// [maxVoiceCount] the max concurrent sounds that can be played.
///
/// NOTE: The number of concurrent voices is limited, as having unlimited
/// voices would cause performance issues, as well as lead to unnecessary clipping.
/// The default number of concurrent voices is 16, but this can be adjusted at runtime.
/// The hard maximum number is 4095, but if more are required, SoLoud can be modified to
/// support more. But seriously, if you need more than 4095 sounds at once, you're
/// probably going to make some serious changes in any case.
FFI_PLUGIN_EXPORT void setMaxActiveVoiceCount(unsigned int maxVoiceCount);
    
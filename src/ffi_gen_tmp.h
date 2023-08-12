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


/// Pause or unpause already loaded sound identified by [handle]
///
/// [handle] the sound handle
/// [pause] the sound handle
FFI_PLUGIN_EXPORT void setPause(unsigned int handle, bool pause);

    /// Smoothly change the global volume over specified time.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors fadeGlobalVolume(float to, float time);
    /// Smoothly change a channel's volume over specified time.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors fadeVolume(unsigned int handle, float to, float time);
    /// Smoothly change a channel's pan setting over specified time.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors fadePan(unsigned int handle, float to, float time);
    /// Smoothly change a channel's relative play speed over specified time.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors fadeRelativePlaySpeed(unsigned int handle, float to, float time);
    /// After specified time, pause the channel.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors schedulePause(unsigned int handle, float time);
    /// After specified time, stop the channel.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors scheduleStop(unsigned int handle, float time);
    /// Set fader to oscillate the volume at specified frequency.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors oscillateVolume(unsigned int handle, float from, float to, float time);
    /// Set fader to oscillate the panning at specified frequency.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors oscillatePan(unsigned int handle, float from, float to, float time);
    /// Set fader to oscillate the relative play speed at specified frequency.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors oscillateRelativePlaySpeed(unsigned int handle, float from, float to, float time);
    /// Set fader to oscillate the global volume at specified frequency.
    ///
FFI_PLUGIN_EXPORT enum PlayerErrors oscillateGlobalVolume(float from, float to, float time);
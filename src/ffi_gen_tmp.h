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

/// The callback to monitor.
    /// 
    /// It is called by SoLoud when a voice ends
FFI_PLUGIN_EXPORT void voiceEndedCallback(unsigned int *handle);

/// Set a Dart function to call when a sound ends.
///
/// [callback] Dart function that will be called when the sound ends to play.
///  Must be a global or a static class member.
///  If using a global function, this must be declared with `@pragma('vm:entry-point')`
///  ```
///  @pragma('vm:entry-point')
///  void playEndedCallback(int handle) {
///       // here the sound with [handle] has ended.
///       // you can play again
///       soLoudController.soLoudFFI.play(handle);
///       // or dispose it
///       soLoudController.soLoudFFI.stop(handle);
///  }
///  ```
FFI_PLUGIN_EXPORT void setDartPlayEndedCallback(void (*callback)(unsigned int *));

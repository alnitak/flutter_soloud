// Native side of custom_dsp.dart.

/// The `webJsFunctionName` to pass to `createCustomAudioSource`: on native
/// platforms, the name of the test synth compiled into the plugin.
String installTestDsp() => 'soloudCustomSynthCallback';

/// Whether the source made with [installTestDsp] produces sound. Always on
/// native platforms.
bool get testDspIsAudible => true;

/// Why [testDspIsAudible] is false, for a skip reason.
String get testDspSilentReason => '';

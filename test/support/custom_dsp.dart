// A DSP callback for `SoLoud.createCustomAudioSource` that test files can use
// without importing dart:ffi (which would break their web compile).
//
// On native platforms this names the test synth built into the plugin
// (`soloudCustomSynthCallback`, see src/synth/custom_source.cpp: a stereo
// A-minor pad with its fundamental at 110 Hz). In the browser it installs a
// JavaScript function on `globalThis` that writes the same kind of signal (a
// 110 Hz sine) into the WASM heap, and returns its name.
export 'custom_dsp_io.dart' if (dart.library.js_interop) 'custom_dsp_web.dart';

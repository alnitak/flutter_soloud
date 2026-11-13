import 'dart:ffi' as ffi;
import 'dart:io';

import 'custom_dsp.dart';
import 'src/custom_dsp.g.dart' as bindings;

/// Returns native DSP callback addresses via FFIGen-generated bindings.
CustomDspCallbacks getPlatformDspCallbacks() {
  int sourceAddress = 0;
  int filterAddress = 0;

  try {
    // Using auto-generated @ffi.Native bindings from package:ffigen:
    sourceAddress = bindings.getCustomSourceCallbackAddress().address;
    filterAddress = bindings.getCustomFilterCallbackAddress().address;
  } catch (_) {
    // Dynamic fallback when statically linked or loaded from a shared library:
    final ffi.DynamicLibrary lib = Platform.isWindows
        ? ffi.DynamicLibrary.open('custom_dsp_callbacks.dll')
        : ffi.DynamicLibrary.process();

    sourceAddress = lib.lookup('myCustomSourceDspCallback').address;
    filterAddress = lib.lookup('myCustomFilterDspCallback').address;
  }

  return CustomDspCallbacks(
    sourceCallbackAddress: sourceAddress,
    filterCallbackAddress: filterAddress,
  );
}

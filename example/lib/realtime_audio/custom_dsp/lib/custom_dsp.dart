// ignore_for_file: experimental_member_use

import 'package:flutter_soloud/flutter_soloud.dart';

import 'custom_dsp_stub.dart'
    if (dart.library.ffi) 'custom_dsp_ffi.dart'
    if (dart.library.js_interop) 'custom_dsp_web.dart';

/// Container for resolved custom DSP callbacks across Native FFI and Web.
class CustomDspCallbacks {
  const CustomDspCallbacks({
    this.sourceCallbackAddress = 0,
    this.filterCallbackAddress = 0,
    this.sourceJsFunctionName,
    this.filterJsFunctionName,
  });

  /// Function pointer address for `myCustomSourceDspCallback` (Native).
  final int sourceCallbackAddress;

  /// Function pointer address for `myCustomFilterDspCallback` (Native).
  final int filterCallbackAddress;

  /// JavaScript function name registered on `globalThis` (Web).
  final String? sourceJsFunctionName;

  /// JavaScript function name registered on `globalThis` (Web).
  final String? filterJsFunctionName;

  /// Whether running on Web using JavaScript callbacks.
  bool get isWeb => sourceJsFunctionName != null;

  /// Creates a SoLoud [AudioSource] using this procedural DSP source callback.
  AudioSource createSource(
    SoLoud soloud, {
    int sampleRate = 48000,
    Channels channels = Channels.stereo,
  }) {
    return soloud.createCustomAudioSource(
      sampleRate: sampleRate,
      channels: channels,
      dspCallbackAddress: sourceCallbackAddress,
      webJsFunctionName: sourceJsFunctionName,
    );
  }

  /// Configures and binds this custom filter callback onto [slot].
  void configureFilter(
    CustomFilterSlot slot, {
    double drive = 2.5,
    double wet = 1.0,
  }) {
    slot
      ..configureParameters(const <CustomFilterParamSpec>[
        CustomFilterParamSpec(
          name: 'Drive',
          min: 1,
          max: 10,
          defaultValue: 2.5,
        ),
      ])
      ..activate()
      ..setNativeCallback(
        callbackAddress: filterCallbackAddress,
        webJsFunctionName: filterJsFunctionName,
      );

    if (slot is CustomFilterGlobal) {
      slot.wet.value = wet;
      slot.param(1, min: 1, max: 10).value = drive;
    } else if (slot is CustomFilterSingle) {
      slot.wet().value = wet;
      slot.param(1, min: 1, max: 10).value = drive;
    }
  }
}

/// Resolves and initializes custom DSP callbacks for the current platform.
CustomDspCallbacks loadCustomDspCallbacks() => getPlatformDspCallbacks();

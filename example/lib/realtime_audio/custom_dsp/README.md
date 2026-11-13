# Custom DSP Callbacks with Native Assets & Web Interop

This standalone subproject demonstrates how to create, compile, and use your own **Custom Audio Source Callback** and **Custom Filter Callback** in `flutter_soloud` without modifying the plugin's source code.

---

## Table of Contents
1. [Why Native/Web Callbacks?](#1-why-nativeweb-callbacks)
2. [C Callback Implementation (`src/custom_dsp.c`)](#2-c-callback-implementation)
   - [Custom Audio Source Callback](#custom-audio-source-callback)
   - [Custom Filter Callback](#custom-filter-callback)
   - [Exporting Function Pointer Addresses](#exporting-function-pointer-addresses)
3. [Automated Compilation with Native Assets (`hook/build.dart`)](#3-automated-compilation-with-native-assets)
4. [Generating FFI Bindings with `package:ffigen`](#4-generating-ffi-bindings-with-packageffigen)
5. [Using the Callbacks in Flutter / Dart](#5-using-the-callbacks-in-flutter--dart)
   - [Importing Generated Bindings](#importing-the-generated-bindings)
   - [Creating a Custom Audio Source](#creating-a-custom-audio-source)
   - [Configuring a Custom Filter](#configuring-a-custom-filter)
6. [Web Support (`dart:js_interop`)](#6-web-support-dartjs_interop)
7. [How to Run This Example](#7-how-to-run-this-example)

---

## 1. Why Native/Web Callbacks?

SoLoud is a low-latency, real-time audio engine. Its audio processing loop runs on a high-priority, dedicated **OS real-time audio thread** (managed by `miniaudio`).

### The Dart `NativeCallable` Limitation
In Dart FFI:
- `NativeCallable.isolateLocal` can **only** be invoked from the specific OS thread that hosts the Dart isolate. If called from the native audio thread, the Dart VM terminates the process immediately.
- `NativeCallable.listener` posts asynchronous events to the Dart event loop, meaning it cannot return synchronous audio samples or process buffers in real-time before the hardware deadline.

Because of this, custom real-time audio synthesis and DSP filtering must be implemented:
- In **Native C, C++, or Rust** on Desktop and Mobile (Android, iOS, macOS, Windows, Linux).
- In **JavaScript or WASM** on Web.

`flutter_soloud` is designed from the ground up to support this by allowing you to pass raw native function pointer memory addresses (`dspCallbackAddress` and `callbackAddress`) or a Web JavaScript function name (`webJsFunctionName`).

---

## 2. C Callback Implementation

### Custom Audio Source Callback
Matches SoLoud's `SoLoudSourceDspCallback_t` signature:

```c
#include <stdint.h>
#include <math.h>

#if defined(_WIN32)
  #define MY_EXPORT __declspec(dllexport)
#else
  #define MY_EXPORT __attribute__((visibility("default")))
#endif

MY_EXPORT unsigned int myCustomSourceDspCallback(
    float *aBuffer,
    unsigned int aSamplesToRead,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    double aStreamTime,
    double aStreamPosition,
    uint32_t *aOutHasEnded,
    void *aUserData)
{
    // Buffer layout is de-interleaved:
    // Channel 0: aBuffer + 0 * aBufferSize
    // Channel 1: aBuffer + 1 * aBufferSize
    for (unsigned int i = 0; i < aSamplesToRead; ++i) {
        double t = aStreamPosition + (double)i / (double)aSamplerate;
        float sampleL = 0.25f * sinf(2.0f * 3.14159265f * 220.0f * (float)t);
        float sampleR = 0.25f * sinf(2.0f * 3.14159265f * 330.0f * (float)t);

        aBuffer[i] = sampleL;
        if (aChannels > 1) {
            aBuffer[aBufferSize + i] = sampleR;
        }
    }

    // Set *aOutHasEnded = 1 when your source has finished playing.
    if (aOutHasEnded != (uint32_t*)0) {
        *aOutHasEnded = 0;
    }

    return aSamplesToRead; // Return number of samples written
}
```

### Custom Filter Callback
Matches SoLoud's `SoLoudFilterDspCallback_t` signature:

```c
MY_EXPORT void myCustomFilterDspCallback(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    double aTime,
    const float *aParams,
    uint64_t aParamChangedMask,
    unsigned int aParamCount,
    void *aUserData)
{
    // In SoLoud:
    // aParams[0] is hardcoded as "Wet" (0.0 to 1.0).
    // aParams[1..7] correspond to your custom parameters configured in Flutter.
    float drive = (aParamCount > 1) ? aParams[1] : 2.5f;

    for (unsigned int ch = 0; ch < aChannels; ++ch) {
        float *dst = aBuffer + ch * aBufferSize;
        for (unsigned int i = 0; i < aSamples; ++i) {
            dst[i] = tanhf(dst[i] * drive); // Soft-clipping saturation
        }
    }
}
```

### Exporting Function Pointer Addresses
To pass these callbacks to Dart without fragile dynamic string lookups across different platforms, export helper functions returning their raw pointer addresses:

```c
MY_EXPORT void *getCustomSourceCallbackAddress(void) {
    return (void*)&myCustomSourceDspCallback;
}

MY_EXPORT void *getCustomFilterCallbackAddress(void) {
    return (void*)&myCustomFilterDspCallback;
}
```

---

## 3. Automated Compilation with Native Assets

With Dart 3's **Native Assets** feature, you do not need manual Xcode, CMake, or NDK configurations. Dart compiles your C code automatically for the target device whenever you run `flutter run`.

### `pubspec.yaml`
Declare the hook toolchain dependencies:
```yaml
dependencies:
  code_assets: ^2.0.0
  native_toolchain_c: ^0.19.4
  hooks: ^2.2.0
  flutter_soloud:
    path: ../../../../
```

### `hook/build.dart`
The build hook invokes `package:native_toolchain_c`'s `CBuilder.library`:
```dart
import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final builder = CBuilder.library(
      name: 'custom_dsp_callbacks',
      assetName: 'src/custom_dsp.c',
      sources: ['src/custom_dsp.c'],
      includes: ['src'],
    );

    await builder.run(input: input, output: output);
  });
}
```

---

## 4. Generating FFI Bindings with `package:ffigen`

Instead of writing manual `@ffi.Native` declarations, bindings are automatically generated from `src/custom_dsp.h` using `package:ffigen`.

### Configuration (`pubspec.yaml`)
```yaml
dev_dependencies:
  ffigen: ^23.0.0

ffigen:
  name: CustomDspBindings
  description: 'Automatically generated FFI bindings for custom DSP callbacks'
  output: 'lib/src/custom_dsp.g.dart'
  headers:
    entry-points:
      - 'src/custom_dsp.h'
    include-directives:
      - '**custom_dsp.h'
  ffi-native:
    asset-id: 'package:custom_dsp/src/custom_dsp.c'
  preamble: |
    // AUTO-GENERATED FILE - DO NOT MODIFY.
    // Generated via package:ffigen.
    // ignore_for_file: type=lint, unused_import, unused_element, camel_case_types, non_constant_identifier_names
```

### Running FFIgen
To generate or regenerate the FFI bindings, simply run:
```bash
dart run ffigen
```

This generates `lib/src/custom_dsp.g.dart` containing type-safe `@ffi.Native` declarations matching your C functions:
```dart
@ffi.Native<ffi.Pointer<ffi.Void> Function()>()
external ffi.Pointer<ffi.Void> getCustomFilterCallbackAddress();

@ffi.Native<ffi.Pointer<ffi.Void> Function()>()
external ffi.Pointer<ffi.Void> getCustomSourceCallbackAddress();
```

---

## 5. Using the Callbacks in Flutter / Dart

### Importing the Generated Bindings
In `lib/custom_dsp_ffi.dart`:

```dart
import 'src/custom_dsp.g.dart' as bindings;

final int sourceAddress = bindings.getCustomSourceCallbackAddress().address;
final int filterAddress = bindings.getCustomFilterCallbackAddress().address;
```

### Creating a Custom Audio Source
Pass the pointer address as `dspCallbackAddress`:

```dart
final source = SoLoud.instance.createCustomAudioSource(
  sampleRate: 48000,
  channels: Channels.stereo,
  dspCallbackAddress: sourceAddress,
);

// Play it just like any regular AudioSource:
final handle = SoLoud.instance.play(source, volume: 0.8);
```

### Configuring a Custom Filter
Bind the filter pointer address as `callbackAddress`:

```dart
final slot0 = SoLoud.instance.filters.customFilter(0)
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
    callbackAddress: getCustomFilterCallbackAddress().address,
  );

// Tweak parameters in real-time:
slot0.wet.value = 1.0;
slot0.param(1, min: 1, max: 10).value = 4.0;
```

---

## 5. Web Support (`dart:js_interop`)

On Web (WASM), C function pointers across memory spaces do not apply in the same way. Instead, attach your callback functions directly to `globalThis` using `dart:js_interop`:

```dart
import 'dart:js_interop';

@JS('globalThis')
external JSObject get _globalThis;

@JS('Module_soloud.HEAPF32')
external JSFloat32Array get _heapF32;

void registerWebCallbacks() {
  _globalThis['customDspSynth'] = ((
    JSNumber bufferPtr,
    JSNumber samplesToRead,
    JSNumber bufferSize,
    JSNumber channels,
    JSNumber sampleRate,
    JSNumber streamTime,
    JSNumber streamPosition,
    JSNumber outHasEndedPtr,
    JSAny? userData,
  ) {
    final int count = samplesToRead.toDartInt;
    final int f32Offset = bufferPtr.toDartInt >> 2;

    for (int i = 0; i < count; i++) {
      _heapF32[f32Offset + i] = 0.25.toJS; // Write samples into WASM heap
    }
    return samplesToRead;
  }).toJS;
}
```

Then configure SoLoud using `webJsFunctionName`:
```dart
final source = SoLoud.instance.createCustomAudioSource(
  webJsFunctionName: 'customDspSynth',
);

slot0.setNativeCallback(
  webJsFunctionName: 'customDspFilter',
);
```

---

## 6. How to Run This Example

1. Open a terminal in this directory:
   ```bash
   cd example/lib/realtime_audio/custom_dsp
   ```
2. Get dependencies:
   ```bash
   flutter pub get
   ```
3. Run on your desired target platform:
   ```bash
   # Desktop / Mobile (compiles src/custom_dsp.c automatically via hook/build.dart)
   flutter run -d macos
   flutter run -d windows
   flutter run -d linux
   flutter run -d ios
   flutter run -d android

   # Web (uses dart:js_interop)
   flutter run -d chrome
   ```

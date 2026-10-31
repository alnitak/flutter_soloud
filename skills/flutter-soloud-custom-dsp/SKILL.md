---
name: flutter-soloud-custom-dsp
version: 1
description: Teaches how to build, compile, and bind custom synchronous C/C++/Rust audio generators (SoLoudSourceDspCallback_t) and real-time DSP filter effects (SoLoudFilterDspCallback_t) in flutter_soloud using Dart Native Assets (hook/build.dart), manual CMake/Cargo compilation, package:ffigen, and Web dart:js_interop. Use when the user asks to write custom DSP algorithms, custom synthesizers/oscillators in C or Rust, compile native audio callbacks with build hooks, generate FFI bindings with ffigen, or create custom audio sources or custom filter slots (createCustomAudioSource, filters.customFilter).
---

# Custom Audio Sources & Custom DSP Filters in flutter_soloud

`flutter_soloud` allows developers to implement synchronous, zero-latency **Custom Audio Sources** (audio generators/synthesizers) and **Custom DSP Filter Slots** (effects like distortion, saturation, equalizers, reverbs, or delays) in native code (C, C++, Rust) or Web (`dart:js_interop`) without modifying `flutter_soloud`'s engine source code.

---

## 1. Core Architecture & Thread-Safety Rules

### Why Callbacks Run in Native / Web Code (Not Dart)
SoLoud's audio rendering loop executes on a dedicated, high-priority **OS real-time audio thread** (managed by `miniaudio`).

In Dart FFI:
- `NativeCallable.isolateLocal` can **only** be called from the OS thread hosting that Dart isolate. Calling it from the real-time audio thread causes the Dart VM to immediately abort/terminate the process.
- `NativeCallable.listener` posts asynchronous events back to the Dart event loop; it cannot return synchronous audio buffers before the hardware DMA buffer deadline.

Therefore:
- **Native Platforms (macOS, iOS, Android, Windows, Linux)**: Audio callbacks are written in C, C++, or Rust, compiled into a native library, and passed to SoLoud as raw 64-bit function pointer memory addresses (`int callbackAddress`).
- **Web (WASM)**: Audio callbacks are written in JavaScript or Dart via `dart:js_interop` attached to `globalThis`, and passed to SoLoud by function name (`String webJsFunctionName`).

### Hard Real-Time Audio Constraints
Inside your audio callbacks:
1. **Never allocate or deallocate heap memory** (`malloc`, `free`, `new`, `delete`, `std::vector::push_back`, etc.). All persistent state must be pre-allocated inside a custom state struct passed via `userData`.
2. **Never lock blocking synchronization primitives** (`std::mutex`, `pthread_mutex`, condition variables, semaphore waits). Use lock-free atomics (`std::atomic` in C++, `stdatomic.h` in C, `std::sync::atomic` in Rust) or single-producer single-consumer (SPSC) ring buffers.
3. **Never perform I/O** (no `printf`, no file reads/writes, no network sockets, no system calls).
4. **Never call into the Dart VM or invoke Dart isolate ports**.

---

## 2. Implementing the Callbacks (C/C++ & Rust)

### A. Custom Audio Source Callback (`SoLoudSourceDspCallback_t`)
Used to generate audio procedurally (oscillators, physical modeling, custom samplers).

#### C / C++
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
    // Buffer layout is planar (de-interleaved):
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

    // Set *aOutHasEnded = 1 when the audio source finishes playing (e.g. sampler finished)
    if (aOutHasEnded != (uint32_t*)0) {
        *aOutHasEnded = 0; // 0 = keep playing, 1 = ended
    }

    return aSamplesToRead; // Return number of frames actually rendered
}

// Export a helper returning the raw function pointer address:
MY_EXPORT void *getCustomSourceCallbackAddress(void) {
    return (void*)&myCustomSourceDspCallback;
}
```

#### Rust (`extern "C"`)
```rust
#[no_mangle]
pub unsafe extern "C" fn my_custom_source_callback(
    buffer: *mut f32,
    samples_to_read: u32,
    buffer_size: u32,
    channels: u32,
    samplerate: f32,
    _stream_time: f64,
    stream_position: f64,
    out_has_ended: *mut u32,
    _user_data: *mut std::ffi::c_void,
) -> u32 {
    let buf_stride = buffer_size as usize;
    for i in 0..(samples_to_read as usize) {
        let t = stream_position + (i as f64) / (samplerate as f64);
        let s_l = 0.25 * (2.0 * std::f64::consts::PI * 220.0 * t).sin() as f32;
        let s_r = 0.25 * (2.0 * std::f64::consts::PI * 330.0 * t).sin() as f32;

        *buffer.add(i) = s_l;
        if channels > 1 {
            *buffer.add(buf_stride + i) = s_r;
        }
    }
    if !out_has_ended.is_null() {
        *out_has_ended = 0;
    }
    samples_to_read
}

#[no_mangle]
pub extern "C" fn get_custom_source_callback_address() -> *const std::ffi::c_void {
    my_custom_source_callback as *const std::ffi::c_void
}
```

---

### B. Custom DSP Filter Callback (`SoLoudFilterDspCallback_t`)
Used to process audio in real-time (distortion, saturation, custom biquads, ring mod, delays).

#### C / C++
```c
#include <stdint.h>
#include <math.h>

#if defined(_WIN32)
  #define MY_EXPORT __declspec(dllexport)
#else
  #define MY_EXPORT __attribute__((visibility("default")))
#endif

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
    // aParams[0] is always "Wet" (0.0 to 1.0) - SoLoud handles wet/dry bypass automatically.
    // aParams[1..7] are your user-defined parameters configured via Dart CustomFilterParamSpec.
    float drive = (aParamCount > 1) ? aParams[1] : 2.5f;

    // Check if parameter 1 changed since the last buffer:
    // int driveChanged = (aParamChangedMask & (1ULL << 1)) != 0;

    // In-place processing across all channels:
    for (unsigned int ch = 0; ch < aChannels; ++ch) {
        float *dst = aBuffer + ch * aBufferSize;
        for (unsigned int i = 0; i < aSamples; ++i) {
            dst[i] = tanhf(dst[i] * drive); // Soft-clipping saturation
        }
    }
}

// Export helper returning the raw function pointer address:
MY_EXPORT void *getCustomFilterCallbackAddress(void) {
    return (void*)&myCustomFilterDspCallback;
}
```

---

## 3. Compilation Workflows

### Option A: Automated Cross-Compilation with Dart Native Assets (`hook/build.dart`)
*(Recommended for Dart 3 and Flutter 3.24+)*

Native Assets automatically compiles your C code for the active build target (Android NDK, iOS Clang, macOS, Windows MSVC, Linux GCC) whenever `flutter run` or `flutter test` executes.

1. Add dependencies to your project's `pubspec.yaml`:
```yaml
dependencies:
  code_assets: ^2.0.0
  hooks: ^2.2.0
  native_toolchain_c: ^0.19.4 # Or native_toolchain_rust for Rust
  flutter_soloud: ^5.3.0
```

2. Create `hook/build.dart`:
```dart
import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final builder = CBuilder.library(
      name: 'my_custom_dsp',
      assetName: 'src/custom_dsp.c',
      sources: ['src/custom_dsp.c'],
      includes: ['src'],
    );

    await builder.run(input: input, output: output);
  });
}
```

3. Place your C sources in `src/custom_dsp.c` and header in `src/custom_dsp.h`.

---

### Option B: Manual / Precompiled Dynamic Library
If you already have a build system (CMake, Makefile, Cargo):

1. **Compile your code into a shared library**:
   - macOS: `clang -O3 -shared -fPIC src/custom_dsp.c -o libcustom_dsp.dylib`
   - Linux: `gcc -O3 -shared -fPIC src/custom_dsp.c -o libcustom_dsp.so`
   - Windows: `cl /O2 /LD src/custom_dsp.c /Fe:custom_dsp.dll`
   - Rust: set `crate-type = ["cdylib"]` in `Cargo.toml` and run `cargo build --release`.

2. **Load the library and look up the pointer in Dart**:
```dart
import 'dart:ffi' as ffi;

final dylib = ffi.DynamicLibrary.open('libcustom_dsp.dylib'); // Or .so / .dll

final getSourceAddr = dylib.lookupFunction<
    ffi.Pointer<ffi.Void> Function(),
    ffi.Pointer<ffi.Void> Function()
>('getCustomSourceCallbackAddress');

final int sourceAddress = getSourceAddr().address;
```

---

### Option C: Web Platform Support (`dart:js_interop`)
On Web (Wasm), C pointers cannot be passed across memory spaces. Instead, register functions on `globalThis` using `dart:js_interop`:

```dart
import 'dart:js_interop';

@JS('globalThis')
external JSObject get _globalThis;

@JS('Module_soloud.HEAPF32')
external JSFloat32Array get _heapF32;

void registerWebDspCallbacks() {
  // 1. Audio Source Generator
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
      _heapF32[f32Offset + i] = 0.25.toJS;
    }
    return samplesToRead;
  }).toJS;

  // 2. Audio Filter Effect
  _globalThis['customDspFilter'] = ((
    JSNumber bufferPtr,
    JSNumber samples,
    JSNumber bufferSize,
    JSNumber channels,
    JSNumber sampleRate,
    JSNumber time,
    JSNumber paramsPtr,
    JSNumber numParams,
  ) {
    // Process buffer in-place on _heapF32
  }).toJS;
}
```

---

## 4. Generating FFI Bindings with `package:ffigen`

Instead of manually writing `@ffi.Native` bindings, use `package:ffigen`:

1. In `src/custom_dsp.h`:
```c
#pragma once
#include <stddef.h>

void *getCustomSourceCallbackAddress(void);
void *getCustomFilterCallbackAddress(void);
```

2. Add configuration to `pubspec.yaml`:
```yaml
dev_dependencies:
  ffigen: ^23.0.0

ffigen:
  name: CustomDspBindings
  description: 'Auto-generated FFI bindings for custom DSP callbacks'
  output: 'lib/src/custom_dsp.g.dart'
  headers:
    entry-points:
      - 'src/custom_dsp.h'
    include-directives:
      - '**custom_dsp.h'
  ffi-native:
    asset-id: 'package:my_app/src/custom_dsp.c' # Matches Native Assets assetName
  preamble: |
    // AUTO-GENERATED FILE - DO NOT MODIFY.
    // ignore_for_file: type=lint
```

3. Generate bindings:
```bash
dart run ffigen
```

This generates type-safe Dart functions:
```dart
import 'src/custom_dsp.g.dart' as bindings;

final int sourceAddress = bindings.getCustomSourceCallbackAddress().address;
final int filterAddress = bindings.getCustomFilterCallbackAddress().address;
```

---

## 5. Wiring into `flutter_soloud`

### A. Playing a Custom Audio Source
```dart
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> startSynth() async {
  final soloud = SoLoud.instance;

  final source = soloud.createCustomAudioSource(
    sampleRate: 48000,
    channels: Channels.stereo,
    dspCallbackAddress: kIsWeb ? 0 : sourceAddress,
    webJsFunctionName: kIsWeb ? 'customDspSynth' : null,
  );

  final handle = soloud.play(source, volume: 0.8);

  // When finished:
  // soloud.stop(handle);
  // soloud.disposeSource(source);
}
```

### B. Configuring a Custom Filter Slot
`flutter_soloud` supports up to **8 independent custom filter slots** (`slotIndex: 0..7`) per scope:
- Global: `SoLoud.instance.filters.customFilter(slotIndex)`
- Mixing Bus: `bus.filters.customFilter(slotIndex)`
- Single Sound: `audioSource.filters.customFilter(slotIndex)` *(must be activated before `play(sound)`)*

```dart
Future<void> setupFilter() async {
  final slot0 = SoLoud.instance.filters.customFilter(0)
    // 1. Configure up to 7 user parameters (index 0 is reserved for 'Wet')
    ..configureParameters(const [
      CustomFilterParamSpec(
        name: 'Drive',
        min: 1.0,
        max: 20.0,
        defaultValue: 2.5,
      ),
    ])
    // 2. Activate the filter slot
    ..activate()
    // 3. Attach the native callback pointer or web JS function
    ..setNativeCallback(
      callbackAddress: kIsWeb ? 0 : filterAddress,
      webJsFunctionName: kIsWeb ? 'customDspFilter' : null,
    );

  // 4. Modulate parameters live:
  slot0.wet.value = 1.0; // 0.0 = hard bypass, 1.0 = 100% wet
  slot0.param(1, min: 1.0, max: 20.0).value = 5.0;

  // Fade or oscillate parameter values:
  slot0.param(1).fadeFilterParameter(
    to: 12.0,
    time: const Duration(seconds: 3),
  );
}
```

---

## 6. Complete Checklist for Custom DSP

1. [ ] **Write C/C++/Rust callback**: planar buffer indexing (`aBuffer + ch * aBufferSize`), no allocations, no locks.
2. [ ] **Export address helper**: `void *getCallbackAddress(void) { return (void*)&callback; }`.
3. [ ] **Configure build**:
   - Automated: `hook/build.dart` + `package:native_toolchain_c`.
   - Manual: build `.dylib` / `.so` / `.dll` with CMake/Cargo.
4. [ ] **Run ffigen**: add `ffigen:` config to `pubspec.yaml` and run `dart run ffigen`.
5. [ ] **Web fallback**: implement `dart:js_interop` function on `globalThis` if supporting Flutter Web.
6. [ ] **Bind in SoLoud**:
   - Source: `soloud.createCustomAudioSource(dspCallbackAddress: addr)`.
   - Filter: `slot.configureParameters(specs) -> slot.activate() -> slot.setNativeCallback(callbackAddress: addr)`.
7. [ ] **Clean up**: call `soloud.disposeSource(source)` or `slot.deactivate()` when done.

---

## Reference Implementation
A complete, runnable Flutter desktop and web project demonstrating this full workflow is located at:
`example/lib/realtime_audio/custom_dsp/`

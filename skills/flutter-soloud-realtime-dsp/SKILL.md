---
name: flutter-soloud-realtime-dsp
version: 4
description: Teaches how to use flutter_soloud's real-time audio mixer injection, synchronous custom DSP callbacks, and desktop VST3 plugin hosting: setRealtimeStream (lock-free SPSC ring buffer with instantaneous retroactive mixer injection, pushFloat32, pushInt16, pushClockedFloat32, pushScheduledFloat32, startFloat32Feeder, RealtimeStreamFeeder, RealtimeNativeBuffer), createCustomSource (zero-latency C/Wasm/JS audio generator), customFilter(id) (up to 8 multi-instance custom DSP filter slots per scope with 16 modulatable params and automatic wet/dry bypass), and Vst3Host / Vst3EffectPlugin / Vst3InstrumentPlugin (desktop .vst3 effect & synth hosting with native GUI editor windows). Use when the user asks to feed low-latency live PCM directly into the mixer, build a custom synthesizer or drum machine, write custom audio DSP effects or generators, host native C++/Rust/FAUST/Cmajor/VST3 plugins, or process audio in a background Dart Isolate.
---

# Real-Time Audio Mixer Injection, Custom Synchronous DSP & Desktop VST3 Hosting in flutter_soloud

`flutter_soloud` provides three mechanisms for real-time audio synthesis, custom DSP effects, and external plugin hosting:

1. **`RealtimeStreamHandle` (`SoLoud.instance.setRealtimeStream`)** — Lock-free Single-Producer Single-Consumer (SPSC) planar float ring buffer with **instantaneous retroactive mixer injection** into the currently playing hardware buffer window (`mRenderRing`), sub-buffer sample-offset scheduling (`pushClockedFloat32`, `pushScheduledFloat32`), and managed cross-platform Isolate/Web feeding (`stream.startFloat32Feeder<TState>` / `RealtimeStreamFeeder`).
2. **`CustomAudioSource` & `CustomFilterSlot` (`createCustomSource` & `filters.customFilter(id)`)** — Zero-latency synchronous callbacks invoked directly inside SoLoud's audio thread (or Web AudioWorklet via Emscripten `EM_ASM` / Wasm function table) for custom generators and effects, supporting up to 8 independent `CustomFilterSlot` instances per scope (Global, Bus, or Single Sound).
3. **`Vst3Host` (`Vst3EffectPlugin` & `Vst3InstrumentPlugin`)** — Opt-in desktop (`macOS`, `Windows`, `Linux`) 64-bit native `.vst3` plugin host built on top of `CustomFilterSlot` and `CustomAudioSource`, supporting effect slots, playable 3D instrument voices, real-time MIDI (`noteOn`/`noteOff`), native desktop editor GUI windows (`openEditorWindow`), parameter automation, and preset state save/restore.

## Choosing Between `setBufferStream`, `setRealtimeStream`, `createCustomSource`, and `Vst3Host`

| Use Case | Recommended API | Why |
|---|---|---|
| Internet radio, HTTP/WebSocket chunks, MP3/OGG/FLAC/AAC/Opus decoding | `setBufferStream` | Dynamic buffer growth, format auto-detection, ICY metadata, automatic pause/resume on underrun |
| Interactive Dart synthesizers, live microphone FX, VoIP, MIDI instruments, drum machines | `setRealtimeStream` | Fixed pre-allocated lock-free ring buffer, instantaneous retroactive injection into the active hardware window, `stream.startFloat32Feeder` zero-GC Isolate/Web support |
| Sample-accurate oscillators, physical modeling, custom DSP filters, C++/Rust/Wasm/JS DSP | `createCustomSource` / `filters.customFilter(id)` | Zero ring-buffer latency; runs synchronously inside the audio thread callback |
| Hosting commercial or open-source `.vst3` effects (`Fx`) & synths (`Instrument\|Synth`) on Desktop | `Vst3Host` (`loadEffect` / `loadInstrument`) | Full Steinberg VST3 host with native GUI editor windows (`NSWindow`/`HWND`/`X11`), MIDI event queue, and C++ fader automation |

---

## 1. Real-Time PCM Mixer Injection (`RealtimeStreamHandle`)

### Minimal Example (Main Isolate)

```dart
import 'dart:math' as math;
import 'dart:typed_data';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init(sampleRate: 48000, bufferSize: 512);

  final stream = SoLoud.instance.setRealtimeStream(
    sampleRate: 48000,
    channels: Channels.stereo,
    format: BufferType.f32le,
    bufferFrames: 8192,
  );

  final handle = SoLoud.instance.play(stream.source);
  stream.bindActiveVoice(handle);

  // Generate 256 stereo interleaved frames of 440 Hz sine wave
  const frames = 256;
  final pcm = Float32List(frames * 2);
  for (var i = 0; i < frames; i++) {
    final s = math.sin(2.0 * math.pi * 440.0 * i / 48000.0) * 0.5;
    pcm[i * 2] = s;
    pcm[i * 2 + 1] = s;
  }

  // Immediately injects into the currently playing hardware mixer window!
  final written = stream.pushFloat32(pcm);
}
```

### Clocked & Sample-Accurate Scheduled Pushes

Like `playClocked` and `playScheduled`, `RealtimeStreamHandle` can offset or schedule pushed PCM frames within the active hardware mixer buffer:

- `stream.pushClockedFloat32(Duration soundTime, Float32List samples)` — Anchors rapid pushes to an app/physics clock so sub-buffer timing offsets are preserved.
- `stream.pushScheduledFloat32(Duration atTime, Float32List samples)` — Schedules consumption of the pushed frames at an exact engine time (`SoLoud.instance.getPlayheadTime()`).
- `stream.pushNativeFloat32(RealtimeNativeBuffer buffer, int frameCount)` — Zero-copy push from a pre-allocated native/WASM buffer.

### Managed Background Isolate & Web Feeding (`stream.startFloat32Feeder`)

Use `stream.startFloat32Feeder<TState>(...)` (or `stream.startInt16Feeder<TState>(...)`) to automatically run your top-level synthesis function inside a dedicated background Dart `Isolate` on native platforms and automatically fall back to a zero-GC timer loop on Web (`kIsWeb`), without writing any `Isolate.spawn`, `ReceivePort`/`SendPort`, or `RealtimeNativeBuffer` boilerplate:

```dart
final feeder = await stream.startFloat32Feeder<SynthState>(
  initialState: SynthState(running: true, frequency: 440),
  onFeed: _generateChunk, // Top-level or static function
  onStateUpdate: _updateState,
  chunkFrames: 480,
);

// Update state from UI sliders/buttons at any time:
feeder.updateState(SynthState(running: true, frequency: 523.25));

// Dispose when finished:
await feeder.dispose();
```

If `onFeed` returns `0` (e.g. when paused) or `< framesRequested`, the feeder yields until the next tick, and if the C++ ring buffer runs dry, the mixer smoothly applies a 64-frame raised-cosine anti-click decay ramp to silence.

---

## 2. Synchronous Custom DSP (`CustomAudioSource` & `CustomFilter`)

### Multi-Instance `CustomFilter` (Up to 8 Slots per Scope)

Unlike built-in filters (which allow one instance per `FilterType`), you can activate up to **8 independent `CustomFilter` slots** (`id: 0..7`) on the same scope (`SoLoud.instance.filters.customFilter(id)`, `sound.filters.customFilter(id)`, or `bus.filters.customFilter(id)`).

Each `CustomFilter` provides:
- `wet` (`param[0]`, range `0.0..1.0`, default `1.0`)
- `p1`..`p15` or `param(int index)` (`index` in `1..15`) — 15 user-definable parameters supporting live `.value`, `.fadeFilterParameter(...)`, and `.oscillateFilterParameter(...)`.

#### Automatic 3-Branch `wet` Bypass & Blending
The C++ engine (`SoLoud::CustomFilterInstance::filter()`) automatically handles `param[0]` (`wet`):
1. **`wet <= 0.0`**: Immediately returns without invoking the callback (**hard bypass, 0% CPU overhead**).
2. **`wet >= 1.0`**: Invokes the callback directly in-place on `aBuffer` (**zero memory copy overhead**).
3. **`0.0 < wet < 1.0`**: Copies the dry signal into a pre-allocated scratch buffer, runs your callback in-place, and blends `dry * (1 - wet) + processed * wet`.

Your callback should always write the **100% wet processed signal** into the buffer.

### Native (FFI) vs. Web (`jsCallbackName`) Setup

On **Native platforms** (iOS, Android, macOS, Windows, Linux):
- Use `Pointer.fromFunction<NativeCustomFilterDspCallback>(myStaticCallback)` (or pass a C/C++/Rust function pointer directly).
- The Dart function must be a **top-level or `static` function** because it is invoked on the real-time audio thread (`NativeCallable.listener` cannot be used for synchronous audio manipulation).

On **Web (Wasm / JS)**:
- Register a global JavaScript function `globalThis.myCustomFilter = function(bufferPtr, samples, channels, sampleRate, time, paramsPtr, numParams, userDataPtr) { ... }` using `HEAPF32` views.
- Pass `jsCallbackName: 'myCustomFilter'` to `setCallback(...)`.

```dart
import 'dart:ffi' as ffi;
import 'dart:math' as math;
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

void _softClipCallback(
  ffi.Pointer<ffi.Float> buffer,
  int samples,
  int channels,
  double sampleRate,
  double time,
  ffi.Pointer<ffi.Float> params,
  int numParams,
  ffi.Pointer<ffi.Void> userData,
) {
  final drive = params[1]; // p1
  final total = samples * channels;
  for (var i = 0; i < total; i++) {
    final x = buffer[i] * drive;
    // Fast rational tanh approximation
    final x2 = x * x;
    buffer[i] = (x * (27.0 + x2) / (27.0 + 9.0 * x2)).clamp(-1.0, 1.0);
  }
}

void setupCustomEffects() {
  final slot0 = SoLoud.instance.filters.customFilter(0);
  slot0.defineParam(1, name: 'Drive', min: 1.0, max: 10.0, def: 2.5);
  if (kIsWeb) {
    slot0.setCallback(jsCallbackName: 'mySoftClipFilter');
  } else {
    slot0.setCallback(
      nativeCallback: ffi.Pointer.fromFunction<NativeCustomFilterDspCallback>(
        _softClipCallback,
      ),
    );
  }
  slot0.activate();
  slot0.p1.value = 4.0;
}
```

### Custom Audio Source (`createCustomSource`)

```dart
final source = SoLoud.instance.createCustomSource(
  channels: Channels.stereo,
  sampleRate: 48000,
  nativeAudioCallback: ffi.Pointer.fromFunction<NativeCustomSourceAudioCallback>(
    _myOscillatorCallback,
    0,
  ),
);
final handle = SoLoud.instance.play(source);
```

---

## 3. Desktop VST3 Plugin Hosting (`Vst3Host`, `Vst3EffectPlugin`, `Vst3InstrumentPlugin`)

On **macOS**, **Windows**, and **Linux**, `flutter_soloud` can host 64-bit native `.vst3` audio effects (`Fx`) and virtual instruments (`Instrument|Synth`), including opening their native desktop editor GUI windows (`NSWindow`, `HWND`, `X11 Window`).

### Enabling VST3 in `pubspec.yaml` & macOS Entitlements

VST3 support is **opt-in** (`enable_vst3: false` by default so the package remains 100% MIT/Zlib unless opted in). Enable it in the app's `pubspec.yaml`:

```yaml
hooks:
  user_defines:
    flutter_soloud:
      enable_vst3: true # Downloads prebuilt Steinberg VST3 SDK bridge from flutter_soloud_prebuilds
```

On **macOS**:
1. Add `<key>com.apple.security.cs.disable-library-validation</key><true/>` and `<key>com.apple.security.app-sandbox</key><false/>` to `macos/Runner/DebugProfile.entitlements` and `macos/Runner/Release.entitlements` so macOS allows `dlopen`ing third-party signed `.vst3` bundles and reading preset directories.
2. Add `<key>NSDocumentsFolderUsageDescription</key>` to `macos/Runner/Info.plist` (many plugins like Surge XT create user preset folders in `~/Documents/`). If macOS TCC blocks `~/Documents/` with `Operation not permitted`, enable **Documents Folder** access for the app (or the parent Terminal/IDE when running `flutter test` / `flutter run`) in **System Settings → Privacy & Security → Files and Folders**.

On **Linux**:
VST3 plugin editor GUIs standardly use the X11 desktop backend. Flutter applications hosting VST3 plugins with a GUI must be launched with the `GDK_BACKEND=x11` environment variable (e.g. `GDK_BACKEND=x11 flutter run -d linux`), otherwise the Flutter engine defaults to Wayland (if available) and the native VST3 plugin GUI will not be displayed.

### Scanning, Hosting an Effect, Opening Native GUI, & Automating Parameters

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> setupVst3Rack() async {
  final soloud = SoLoud.instance;
  await soloud.init(sampleRate: 48000, bufferSize: 512);

  final vstHost = Vst3Host();
  if (!vstHost.isSupported) return;

  // 1. Scan a .vst3 bundle
  const fxBundle = '/Library/Audio/Plug-Ins/VST3/Surge XT Effects.vst3';
  final classes = vstHost.scanBundle(fxBundle);

  // 2. Load a VST3 Effect into Global CustomFilterSlot 0 (or a Mixing Bus slot)
  final slot0 = soloud.filters.customFilters[0];
  final effect = vstHost.loadEffect(
    bundlePath: fxBundle,
    classUid: classes.first.classUid,
    slot: slot0,
  );
  effect.setTransport(bpm: 124.0, isPlaying: true);

  // 3. Open the plugin's native desktop GUI window (if available)
  if (effect.hasEditor) {
    effect.openEditorWindow(title: classes.first.name);
  }
  effect.onGuiParameterChanged.listen((edit) {
    final text = effect.getParameterDisplay(edit.paramId);
    print('GUI changed param ${edit.paramId} -> $text');
  });

  // Listen to dynamic parameter updates (e.g. multi-effects switching sub-effects):
  effect.onRestartComponent.listen((evt) {
    print('Plugin updated parameters (${effect.parameters.length} total)');
  });

  // 4. Map a VST3 ParamID to CustomFilterSlot param[1] for C++ Fader / LFO modulation
  if (effect.parameters.isNotEmpty) {
    final p = effect.mapToSlotParameter(
      slotParamIndex: 1,
      vst3ParamId: effect.parameters.first.paramId,
    );
    p.fadeFilterParameter(to: 0.85, time: const Duration(seconds: 3));
  }

  // 5. Load a VST3 Virtual Instrument (Instrument|Synth) & play MIDI notes
  final synth = vstHost.loadInstrument(
    bundlePath: '/Library/Audio/Plug-Ins/VST3/Surge XT.vst3',
  );
  final voice = soloud.play3d(synth.audioSource, 1.5, 0.0, -1.0);
  synth.noteOn(pitch: 60, velocity: 0.85);
}
```

---

## Traps & Real-Time Audio Safety Rules

- **Never allocate, lock mutexes, or perform I/O inside `nativeCallback` / `nativeAudioCallback`.** These callbacks run synchronously on the OS real-time audio thread. Avoid `print()`, `List` allocations, `Future`, or `Stream` inside the callback.
- **`customFilter(id)` asserts `0 <= id < 8`.** Calling `customFilter(8)` throws an `AssertionError` / `ArgumentError`. Also remember SoLoud's total limit of 8 active filters per scope (`FILTERS_PER_STREAM = 8`) across all built-in + custom filters combined.
- **VST3 effects (`Vst3Host.loadEffect`) only support Global or Mixing Bus filter slots.** Passing a single-sound `CustomFilterSlot` throws an `ArgumentError` because stateful VST3 effect instances cannot be cloned per voice. Route specific voices through a `Bus` (`soloud.createBus()`) to apply a VST3 effect to a subset of sounds.
- **VST3 is desktop-only (`macOS`, `Windows`, `Linux`) and opt-in (`enable_vst3: true`).** On iOS, Android, and Web (or when `enable_vst3` is not `true`), `Vst3Host().isSupported` is `false`. Also note that enabling `enable_vst3: true` links the prebuilt Steinberg VST3 Hosting SDK (dual-licensed under the Steinberg VST3 License or GPLv3).
- **Linux VST3 GUI hosting requires `GDK_BACKEND=x11`.** The standard VST3 windowing backend on Linux uses X11 (`Steinberg::kPlatformTypeX11EmbedWindowID`). On Linux Wayland sessions, the app must be run with `GDK_BACKEND=x11 flutter run -d linux`, otherwise the native editor window will not be displayed.
- **Check `.vst3` bundle existence with `Directory(path).existsSync() || File(path).existsSync()`.** Standard `.vst3` plugins on macOS, Windows, and Linux are directory bundles, so `File(path).existsSync()` alone returns `false` for directory bundles.
- **Multi-effects & dynamic VST3 plugins change parameters at runtime.** When a user selects a different sub-effect inside a multi-effect plugin (such as Surge XT Effects switching from EQ to Delay), the plugin calls Steinberg's `restartComponent`. `Vst3PluginBase` automatically updates its `parameters` list and emits `onRestartComponent` so UI widgets can re-render sliders. You can also re-query parameter metadata manually with `effect.refreshParameters()` or `synth.refreshParameters()` at any time.
- **Per-sound `customFilter(id)` must be activated before `play(sound)`.** Just like built-in per-sound filters, voices started before `activate()` do not carry the filter.
- **SoLoud's internal buffer layout is planar (`[ch0_frames..., ch1_frames...]`).** Inside `NativeCustomFilterDspCallback` and `NativeCustomSourceAudioCallback`, channel `c` starts at `buffer + c * samples`.
- **Always call `stream.dispose()`, `effect.dispose()`, or `synth.dispose()` when finished** to free native ring buffers, `RealtimeNativeBuffer` allocations, and VST3 plugin instances.

## More Depth

- Runnable interactive examples:
  - `example/lib/realtime_audio/realtime_pcm_synth.dart` — XY pad polyphonic synth with instantaneous retroactive mixer injection.
  - `example/lib/realtime_audio/isolate_drum_sequencer.dart` — Zero-GC background `SoLoudIsolate` 4-track drum machine.
  - `example/lib/realtime_audio/custom_dsp_workbench.dart` — Custom audio source + chained multi-instance `customFilter(0)` and `customFilter(1)` with live modulation.
  - `example/lib/realtime_audio/vst3_plugin_rack.dart` — Desktop VST3 effect & virtual instrument rack with bundle scanning, native GUI editor windows, C++ fader automation, preset save/restore, and MIDI keyboard.


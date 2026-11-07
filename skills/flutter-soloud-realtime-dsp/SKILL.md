---
name: flutter-soloud-realtime-dsp
version: 5
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

## 2. Synchronous Custom DSP (`CustomAudioSource` & `CustomFilterSlot`)

### Multi-Instance `CustomFilterSlot` (Up to 8 Slots per Scope)

Unlike built-in filters (which allow one instance per `FilterType`), you can activate up to **8 independent `CustomFilterSlot` instances** (`id: 0..7`) on the same scope (`SoLoud.instance.filters.customFilter(id)`, `sound.filters.customFilter(id)`, or `bus.filters.customFilter(id)`).

Each `CustomFilterSlot` provides:
- `wet` (`param[0]`, range `0.0..1.0`, default `1.0`): hardcoded bypass when `0.0`, zero-copy in-place processing when `1.0`, and smooth blending when `0.0 < wet < 1.0`.
- Up to 7 user-definable parameters (`param(1)`..`param(7)`) configured with `configureParameters(const [CustomFilterParamSpec(name: '...', min: ..., max: ..., defaultValue: ...)])` supporting live `.value`, `.fadeFilterParameter(...)`, and `.oscillateFilterParameter(...)`.

### Thread-Safety & Compilation (C/Rust vs. Web)
Because SoLoud executes DSP callbacks on a dedicated high-priority **OS real-time audio thread**, Dart functions cannot be safely invoked synchronously via `Pointer.fromFunction` or `NativeCallable`. Callbacks must be implemented:
- In **Native C/C++ or Rust** for Android, iOS, macOS, Windows, Linux (passed as raw memory addresses via `callbackAddress` / `dspCallbackAddress`).
- In **JavaScript or WebAssembly** for Web (attached to `globalThis` and passed via `webJsFunctionName`).

> For complete instructions on compiling C/Rust callbacks with Dart Native Assets (`hook/build.dart`), manual CMake/Cargo compilation, generating bindings with `package:ffigen`, and Web interop, see the companion skill **flutter-soloud-custom-dsp** (`skills/flutter-soloud-custom-dsp/SKILL.md`).

```dart
// 1. Configure and activate a Custom Filter Slot (0..7):
final slot0 = SoLoud.instance.filters.customFilter(0)
  ..configureParameters(const [
    CustomFilterParamSpec(
      name: 'Drive',
      min: 1.0,
      max: 20.0,
      defaultValue: 2.5,
    ),
  ])
  ..activate()
  ..setNativeCallback(
    callbackAddress: kIsWeb ? 0 : nativeFilterCallbackAddress,
    webJsFunctionName: kIsWeb ? 'customDspFilter' : null,
  );

// Modulate parameters:
slot0.wet.value = 1.0;
slot0.param(1, min: 1.0, max: 20.0).value = 4.0;
slot0.param(1).fadeFilterParameter(to: 10.0, time: const Duration(seconds: 2));

// 2. Create and play a Custom Audio Source:
final source = SoLoud.instance.createCustomAudioSource(
  channels: Channels.stereo,
  sampleRate: 48000,
  dspCallbackAddress: kIsWeb ? 0 : nativeSourceCallbackAddress,
  webJsFunctionName: kIsWeb ? 'customDspSynth' : null,
);
final handle = SoLoud.instance.play(source, volume: 0.8);
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


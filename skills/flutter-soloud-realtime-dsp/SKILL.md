---
name: flutter-soloud-realtime-dsp
version: 7
description: Teaches flutter_soloud's real-time audio mixer injection, synchronous custom DSP callbacks, and desktop VST3 plugin hosting - setRealtimeStream (lock-free ring buffer with retroactive mixer injection, pushFloat32, pushInt16, pushClockedFloat32, pushScheduledFloat32, startFloat32Feeder, RealtimeStreamFeeder, RealtimeNativeBuffer), createCustomAudioSource (zero-latency C/Wasm/JS audio generator), customFilter(id) (up to 8 custom DSP filter slots per scope, each with a wet parameter and up to 15 modulatable user parameters), and Vst3Host / Vst3EffectPlugin / Vst3InstrumentPlugin (desktop .vst3 effect and synth hosting with native editor windows). Use when the user asks to feed low-latency live PCM into the mixer, build a custom synthesizer, piano/MIDI instrument, drum machine or emulator audio output, play voice-chat frames, write custom audio DSP effects or generators, host native C++/Rust/FAUST/Cmajor/VST3 plugins, or generate audio in a background Dart Isolate.
---

# Real-Time Audio Mixer Injection, Custom Synchronous DSP & Desktop VST3 Hosting in flutter_soloud

`flutter_soloud` provides three mechanisms for real-time audio synthesis, custom DSP effects, and external plugin hosting:

1. **`RealtimeStreamHandle` (`SoLoud.instance.setRealtimeStream`)** — Lock-free Single-Producer Single-Consumer (SPSC) planar float ring buffer with **instantaneous retroactive mixer injection** into the window being played (when `init()` enables the render-ahead ring with `renderAheadFrames`), sub-buffer sample-offset scheduling (`pushClockedFloat32`, `pushScheduledFloat32`), and managed cross-platform Isolate/Web feeding (`stream.startFloat32Feeder<TState>` / `RealtimeStreamFeeder`).
2. **`CustomAudioSource` & `CustomFilterSlot` (`createCustomAudioSource` & `filters.customFilter(id)`)** — Zero-latency synchronous callbacks invoked directly inside SoLoud's audio thread (or Web AudioWorklet via Emscripten `EM_ASM` / Wasm function table) for custom generators and effects, supporting up to 8 independent `CustomFilterSlot` instances per scope (Global, Bus, or Single Sound).
3. **`Vst3Host` (`Vst3EffectPlugin` & `Vst3InstrumentPlugin`)** — Opt-in desktop (`macOS`, `Windows`, `Linux`) 64-bit native `.vst3` plugin host built on top of `CustomFilterSlot` and `CustomAudioSource`, supporting effect slots, playable 3D instrument voices, real-time MIDI (`noteOn`/`noteOff`), native desktop editor GUI windows (`openEditorWindow`), parameter automation, and preset state save/restore.

## Choosing Between `setBufferStream`, `setRealtimeStream`, `createCustomAudioSource`, and `Vst3Host`

| Use Case | Recommended API | Why |
|---|---|---|
| Internet radio, HTTP/WebSocket chunks, MP3/OGG/FLAC/AAC/Opus decoding | `setBufferStream` | Dynamic buffer growth, format auto-detection, ICY metadata, automatic pause/resume on underrun |
| Interactive Dart synthesizers, live microphone FX, VoIP, MIDI instruments, drum machines | `setRealtimeStream` | Fixed pre-allocated lock-free ring buffer, instantaneous retroactive injection into the active hardware window, `stream.startFloat32Feeder` zero-GC Isolate/Web support |
| Sample-accurate oscillators, physical modeling, custom DSP filters, C++/Rust/Wasm/JS DSP | `createCustomAudioSource` / `filters.customFilter(id)` | Zero ring-buffer latency; runs synchronously inside the audio thread callback |
| Hosting commercial or open-source `.vst3` effects (`Fx`) & synths (`Instrument\|Synth`) on Desktop | `Vst3Host` (`loadEffect` / `loadInstrument`) | Full Steinberg VST3 host with native GUI editor windows (`NSWindow`/`HWND`/`X11`), MIDI event queue, and C++ fader automation |

---

## 1. Real-Time PCM Mixer Injection (`RealtimeStreamHandle`)

### Minimal Example (Main Isolate)

```dart
import 'dart:math' as math;
import 'dart:typed_data';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init(
    sampleRate: 48000,
    // Native only: enables splicing pushed audio into the window being
    // played. Without it a push is heard from the next mix buffer.
    renderAheadFrames: 2048,
    devicePeriodFrames: 256,
  );

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

  // Returns the frames actually written (less than requested when the ring
  // is full).
  final written = stream.pushFloat32(pcm);

  // When done: stop feeding and dispose the source.
  // await SoLoud.instance.disposeSource(stream.source);
}
```

`setRealtimeStream({int sampleRate = 48000, Channels channels = Channels.stereo, BufferType format = BufferType.f32le, int bufferFrames = 2048, int historyFrames = 4096, bool autoDispose = false})` accepts only `BufferType.f32le`, `s16le` and `f32lePlanar` (anything else throws `ArgumentError`). `bufferFrames` is rounded up to a power of two and raised to at least two engine mix buffers; the ring is limited to 2^24 frames, and negative or absurd sizes throw. Use the engine sample rate to avoid resampling. `stream.availableToRead`, `availableToWrite` and `underrunCount` report the ring state; `stream.reset()` drops what is queued.

### Clocked & Sample-Accurate Scheduled Pushes

Like `playClocked` and `playScheduled`, `RealtimeStreamHandle` can offset or schedule pushed PCM frames within the active hardware mixer buffer:

- `stream.pushClockedFloat32(Duration soundTime, Float32List samples)` — Anchors rapid pushes to an app/physics clock so sub-buffer timing offsets are preserved.
- `stream.pushScheduledFloat32(Duration atTime, Float32List samples)` — Schedules the pushed frames at an absolute engine time (`SoLoud.instance.getEngineTime()` / `getPlayheadTime()` plus your offset). Chunks of one stream never overlap: a chunk scheduled before the end of the queued audio starts right after it.
- `stream.pushNativeFloat32(RealtimeNativeBuffer buffer, int frameCount)` — Zero-copy push from a pre-allocated native/WASM buffer (`stream.allocateNativeBuffer(maxFrames: ...)`, write into `buffer.float32View`). `pushNativeInt16`, `pushNativeClockedFloat32` and `pushNativeScheduledFloat32` are the matching variants.

All push methods return the number of frames written. Splicing into the window being played needs `renderAheadFrames` in `init()` and the playing voice bound with `stream.bindActiveVoice(handle)` (or passed as `voiceHandle:`).

### Managed Background Isolate & Web Feeding (`stream.startFloat32Feeder`)

Use `stream.startFloat32Feeder<TState>(...)` (or `stream.startInt16Feeder<TState>(...)`) to automatically run your top-level synthesis function inside a dedicated background Dart `Isolate` on native platforms and automatically fall back to a zero-GC timer loop on Web (`kIsWeb`), without writing any `Isolate.spawn`, `ReceivePort`/`SendPort`, or `RealtimeNativeBuffer` boilerplate:

```dart
class SynthState {
  SynthState({required this.running, required this.frequency});
  bool running;
  double frequency;
  double phase = 0; // DSP state, kept across updateState() calls
}

// Top-level (or static): it is sent to the worker isolate.
int _generateChunk(
  Float32List out, // interleaved, framesRequested * channels samples
  int framesRequested,
  SynthState state,
  void Function(Object? event) emitToUi,
) {
  if (!state.running) return 0;
  final inc = 2 * math.pi * state.frequency / 48000;
  for (var i = 0; i < framesRequested; i++) {
    final s = math.sin(state.phase) * 0.5;
    state.phase += inc;
    out[i * 2] = s;
    out[i * 2 + 1] = s;
  }
  return framesRequested; // frames written
}

// Copy the UI-controlled fields, keep the phase.
void _updateState(SynthState current, SynthState next) {
  current
    ..running = next.running
    ..frequency = next.frequency;
}

final feeder = await stream.startFloat32Feeder<SynthState>(
  initialState: SynthState(running: true, frequency: 440),
  onFeed: _generateChunk,
  onStateUpdate: _updateState,
  chunkFrames: 480, // 10 ms at 48 kHz; must be <= bufferFrames
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
- Up to 15 user-definable parameters (`param(1)`..`param(15)`, `CustomFilterSlot.maxUserParams`) configured with `configureParameters(const [CustomFilterParamSpec(name: '...', min: ..., max: ..., defaultValue: ...)])` supporting live `.value`, `.fadeFilterParameter(...)`, and `.oscillateFilterParameter(...)`. More than 7 specs throw `ArgumentError`.
- On a sound or bus slot `wet` and `param` take the voice: `sound.filters.customFilter(0).wet(soundHandle: handle)`, `.param(1, soundHandle: handle)`; on the global slot `wet` is a getter.

### Thread-Safety & Compilation (C/Rust vs. Web)
Because SoLoud executes DSP callbacks on a dedicated high-priority **OS real-time audio thread**, Dart functions cannot be safely invoked synchronously via `Pointer.fromFunction` or `NativeCallable`. Callbacks must be implemented:
- In **Native C/C++ or Rust** for Android, iOS, macOS, Windows, Linux (passed as raw memory addresses via `callbackAddress` / `dspCallbackAddress`).
- In **JavaScript or WebAssembly** for Web (attached to `globalThis` and passed via `webJsFunctionName`). A `webJsFunctionName` callback runs only with the single-threaded WASM build: the multi-threaded build (used when the page is cross-origin isolated) renders in an AudioWorklet that cannot see the page's `globalThis`, so the source is silent and the filter does nothing (a warning is logged). Put `self.flutter_soloud_force_single_threaded = true` in a script before `init_soloud.js` to force the single-threaded build.

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

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| A Dart synthesizer, drum machine, emulator audio or voice-chat frames pushed through `setBufferStream`/`addAudioDataStream` | `setRealtimeStream` + `pushFloat32`/`pushInt16` | A buffer stream pauses the voice to rebuffer when it runs dry; a realtime stream is a fixed ring that never pauses (it fades to silence on underrun and back in) and, with the render-ahead ring, splices new audio at the playhead. Raw PCM only |
| A `Timer.periodic` or a hand-written `Isolate.spawn` + ports loop that generates and pushes PCM | `stream.startFloat32Feeder(...)` / `startInt16Feeder(...)` | The feeder runs the generator in a background isolate on native (the same code on a timer on web), keeps the ring above a watermark and reuses one native buffer, so UI jank and GC pauses do not starve the audio. `onFeed` must be a top-level or static function |
| Notes or hits pushed with `pushFloat32` that sound late or uneven, with `init()` called without `renderAheadFrames` | `init(renderAheadFrames:, devicePeriodFrames:)` (**flutter-soloud-setup**), then `pushClockedFloat32` for hits timed by the app clock or `pushScheduledFloat32` for a timeline | Without the render-ahead ring a push is only heard from the next mix buffer; with it the samples are spliced into the window being played, and the clocked/scheduled variants keep sub-buffer timing. Native only, ignored on web |
| A drum machine, metronome or sequencer whose sounds are plain samples, built by copying sample data into a realtime stream | Load the samples and trigger them with `playClocked` / `playScheduled` (**flutter-soloud-scheduling**) | Sample-accurate with no PCM code at all; keep the realtime stream for audio that is really computed at runtime |
| A new `Float32List` allocated for every pushed chunk in a hand-written feeding loop | `stream.allocateNativeBuffer(maxFrames:)` once, write into `buffer.float32View`, then `pushNativeFloat32(buffer, frames)` | No allocation and no copy per chunk, so no garbage-collector pauses in the feeding loop. Call `buffer.dispose()` at the end |
| A custom distortion, echo, reverb, EQ, compressor, limiter or pitch shifter planned as native DSP code | The built-in filter first: `waveShaperFilter`, `echoFilter`, `freeverbFilter`, `parametricEqFilter`, `compressorFilter`, `limiterFilter`, `pitchShiftFilter` (**flutter-soloud-filters**) | No C toolchain and no per-platform build, and the built-ins also work on the multi-threaded web build. Write a `customFilter` only for DSP the built-ins do not cover (**flutter-soloud-custom-dsp**) |
| A custom effect switched on and off with `deactivate()` / `activate()` | `slot.wet.value = 0` to bypass and `1` to enable (or `wet.fadeFilterParameter` for a smooth change) | At `wet == 0` the engine skips the callback completely; `deactivate()` also clears the bound callback, so `setNativeCallback` must be called again after the next `activate()` |
| A VST3 effect wanted on some sounds only (music but not UI, one instrument) | A mixing bus: `SoLoud.instance.createMixingBus()`, `vstHost.loadEffect(slot: bus.filters.customFilter(0), ...)`, and play those sounds on the bus (**flutter-soloud-mixing-bus**) | One plugin instance processes the whole bus once per mix; a VST3 effect belongs on a global or bus slot, not on a single sound |

## Traps & Real-Time Audio Safety Rules

- **Never allocate, lock mutexes, or perform I/O inside `nativeCallback` / `nativeAudioCallback`.** These callbacks run synchronously on the OS real-time audio thread. Avoid `print()`, `List` allocations, `Future`, or `Stream` inside the callback.
- **`customFilter(id)` asserts `0 <= id < 8`** (an `AssertionError` in debug builds). `configureParameters` with more than 15 specs and `mapToSlotParameter` with an index outside `1..15` throw `ArgumentError`. Also remember SoLoud's total limit of 8 active filters per scope (`FILTERS_PER_STREAM = 8`) across all built-in + custom filters combined.
- **Load VST3 effects (`Vst3Host.loadEffect`) only into Global or Mixing Bus filter slots.** A VST3 effect is one stateful instance and cannot be cloned per voice, so a single-sound slot (`sound.filters.customFilter(...)`) throws `ArgumentError`. Route specific voices through a `Bus` (`soloud.createMixingBus()`) to apply a VST3 effect to a subset of sounds.
- **VST3 is desktop-only (`macOS`, `Windows`, `Linux`) and opt-in (`enable_vst3: true`).** On iOS, Android, and Web (or when `enable_vst3` is not `true`), `Vst3Host().isSupported` is `false`. Also note that enabling `enable_vst3: true` links the prebuilt Steinberg VST3 Hosting SDK (dual-licensed under the Steinberg VST3 License or GPLv3).
- **Linux VST3 GUI hosting requires `GDK_BACKEND=x11`.** The standard VST3 windowing backend on Linux uses X11 (`Steinberg::kPlatformTypeX11EmbedWindowID`). On Linux Wayland sessions, the app must be run with `GDK_BACKEND=x11 flutter run -d linux`, otherwise the native editor window will not be displayed.
- **Check `.vst3` bundle existence with `Directory(path).existsSync() || File(path).existsSync()`.** Standard `.vst3` plugins on macOS, Windows, and Linux are directory bundles, so `File(path).existsSync()` alone returns `false` for directory bundles.
- **Multi-effects & dynamic VST3 plugins change parameters at runtime.** When a user selects a different sub-effect inside a multi-effect plugin (such as Surge XT Effects switching from EQ to Delay), the plugin calls Steinberg's `restartComponent`. `Vst3PluginBase` automatically updates its `parameters` list and emits `onRestartComponent` so UI widgets can re-render sliders. You can also re-query parameter metadata manually with `effect.refreshParameters()` or `synth.refreshParameters()` at any time.
- **Per-sound `customFilter(id)` must be activated before `play(sound)`.** Just like built-in per-sound filters, voices started before `activate()` do not carry the filter. Per-sound filters are not available on Web: every call throws `SoLoudFilterForSingleSoundOnWebDartException` (use a global or bus slot).
- **`activate()` first, then `setNativeCallback`.** `deactivate()` keeps the parameter definitions but clears the callback, so bind it again after re-activating. Reading or setting a parameter of a slot that is not active throws.
- **Parameter values must be finite.** `fadeFilterParameter`/`oscillateFilterParameter` throw `ArgumentError` for NaN or infinite values; a NaN or out-of-range `.value` is ignored with a logged warning.
- **A VST3 instrument is one voice.** `loadInstrument` creates a single-instance source: playing `synth.audioSource` again stops the voice already playing it. Polyphony comes from `noteOn`/`noteOff`, not from several `play()` calls.
- **SoLoud's internal buffer layout is planar (`[ch0_frames..., ch1_frames...]`).** Inside the native callbacks (`SoLoudFilterDspCallback_t`, `SoLoudSourceDspCallback_t`), channel `c` starts at `aBuffer + c * aBufferSize` (the channel stride, which can be larger than the number of frames to process).
- **Free what you created.** `await feeder.dispose()` for a feeder, `buffer.dispose()` for a `RealtimeNativeBuffer`, `SoLoud.instance.disposeSource(stream.source)` for a realtime stream (`RealtimeStreamHandle` has no `dispose()`), and `effect.dispose()` / `synth.dispose()` for VST3 plugins.

## More Depth

- Runnable interactive examples:
  - `example/lib/realtime_audio/realtime_pcm_synth.dart` — XY pad polyphonic synth with instantaneous retroactive mixer injection.
  - `example/lib/realtime_audio/isolate_drum_sequencer.dart` — Zero-GC background-isolate 4-track drum machine.
  - `example/lib/realtime_audio/custom_dsp/` — Stand-alone project: custom audio source + `customFilter(0)` with native C callbacks (build hook + ffigen) and Web JS callbacks.
  - `example/lib/realtime_audio/vst3_plugin_rack.dart` — Desktop VST3 effect & virtual instrument rack with bundle scanning, native GUI editor windows, C++ fader automation, preset save/restore, and MIDI keyboard.


## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

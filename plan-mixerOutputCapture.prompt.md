# Plan: Mixer Output Capture for flutter_soloud

## TL;DR
Expose the SoLoud master mix output to Dart as a stream. A new `src/mixeroutput/` module captures interleaved float samples from the miniaudio callback, writes them into a lock-free circular buffer, optionally encodes them to PCM/Opus/Vorbis/FLAC on a worker thread, and notifies Dart via a `NativeCallable` (FFI) or Web Worker message (web). Dart reads the data either as zero-copy `TypedData` views into shared native memory (FFI) or from a `SharedArrayBuffer` (web).

## Decisions from discussion
- **Capture scope**: master mix only.
- **Output formats**: `PCM_F32LE` (default), `PCM_S8`, `PCM_S16LE`, `PCM_S32LE`, `OPUS`, `VORBIS`, `FLAC`. If NO_XIPH_LIBS is not defined, the Xiph libraries are not linked and the OPUS, VORBIS, and FLAC output formats are not available.
- **Data transfer**: FFI pulls from shared native circular buffer; web uses `SharedArrayBuffer` shared memory.
- **Notification threshold**: bytes. Callback fires when at least `N` bytes are available, passing a pointer/offset and the actual contiguous byte length (which may be slightly less than `N` near a buffer wrap).
- **Compressed formats**: encoded on a separate C++ encoder thread so the audio thread never blocks.

## Key Discovery Findings
- Natural hook point: `soloud_miniaudio_audiomixer` in `src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp`, which calls `soloud->mix((float *)pOutput, frameCount)`.
- SoLoud `mix()` emits interleaved float samples at the configured sample rate/channels.
- Existing callback model uses `NativeCallable.listener` (FFI) and Web Worker messages (web).
- Existing `BufferType` enum already covers `PCM_F32LE`, `PCM_S8`, `PCM_S16LE`, `PCM_S32LE`, plus `AUTO`/`OPUS` for *input* streams; a new `MixerOutputFormat` enum is cleaner.
- New C++ code should live under `src/mixeroutput/` and be added to `src/src.cmake`.
- FFI additions require `src/ffi_gen_tmp.h` + `dart run ffigen --config ffigen.yaml`, then copying generated bindings into `lib/src/bindings/bindings_player_ffi.dart` and mirroring in `bindings_player_web.dart`.

## Architecture

### C++ capture pipeline (`src/mixeroutput/`)

```
miniaudio callback
       │
       ▼
soloud->mix(float buffer) ──► MixerOutput::onAudioData()
                                   │
            ┌──────────────────────┼──────────────────────┐
            │                      │                      │
            ▼                      ▼                      ▼
    PCM formats            raw PCM ring buffer      encoder thread
    (in-place convert)            │                (Opus/Vorbis/FLAC)
                                  │                      │
                                  ▼                      ▼
                    output ring buffer ◄─────── encoded output ring buffer
                                  │
                                  ▼
              Dart notification callback
              (pointer/offset + length)
```

#### New C++ files
- `src/mixeroutput/mixer_output_format.h` — `MixerOutputFormat` enum mirroring Dart enum.
- `src/mixeroutput/mixer_output.h/.cpp` — `MixerOutput` singleton. Owns the lock-free circular buffer, atomic read/write indices, the notification thread, and the callback pointer.
- `src/mixeroutput/pcm_converter.h/.cpp` — SIMD-friendly float → `s8`/`s16le`/`s32le`/`f32le` conversion.
- `src/mixeroutput/mixer_output_encoder.h` — abstract encoder interface.
- `src/mixeroutput/opus_output_encoder.h/.cpp` — Opus encoder (Xiph `libopus`).
- `src/mixeroutput/vorbis_output_encoder.h/.cpp` — Vorbis encoder (Xiph `libvorbisenc`).
- `src/mixeroutput/flac_output_encoder.h/.cpp` — FLAC encoder (Xiph `libFLAC`).

#### Circular buffer design
- Single writer: audio thread (PCM formats) or encoder thread (compressed formats).
- Single reader: Dart / consumer thread.
- Atomic `std::atomic<size_t>` write offset and read offset.
- Total size configurable, default ~1 MB (tunable).
- Notification thread (or encoder thread) sleeps on a condition variable / atomic wait and fires the Dart callback when `availableBytes >= notificationThresholdBytes`.
- Callback passes: `(uint8_t* data, size_t length)`. The contiguous length may be capped at the wrap point, so it can be slightly less than `N`.

#### Hook point
- Modify `src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp` in `soloud_miniaudio_audiomixer`, after `soloud->mix((float *)pOutput, frameCount)`:
  ```cpp
  if (MixerOutput::instance().isRunning()) {
      MixerOutput::instance().onAudioData((float *)pOutput, frameCount);
  }
  ```
- Guard with `#ifdef WITH_MINIAUDIO` / `#ifndef __EMSCRIPTEN__` as needed.

#### FFI exports (added in `src/bindings.cpp` and `src/ffi_gen_tmp.h`)
- `startMixerCapture(int format, int sampleRate, int channels, size_t bufferSizeBytes, size_t notificationThresholdBytes)` → returns `PlayerErrors` also if has been choosen OPUS, VORBIS, or FLAC encoder format and NO_XIPH_LIBS is not defined.
- `stopMixerCapture()`.
- `isMixerCaptureRunning()`.
- `getMixerCaptureBufferPointer()` → `uint8_t*`.
- `getMixerCaptureBufferSize()` → `size_t`.
- `getMixerCaptureAvailableBytes()` → `size_t`.
- `advanceMixerCaptureReadPosition(size_t bytes)`.
- `setMixerOutputCallback(dartMixerOutputCallback_t callback)`.

### Dart side

#### New enum
- Add `MixerOutputFormat` to `lib/src/enums.dart` and `src/enums.h`.

#### Abstract binding layer (`lib/src/bindings/bindings_player.dart`)
- Add `StreamController<MixerOutputChunk> mixerOutputController`.
- Add abstract methods: `startMixerCapture(...)`, `stopMixerCapture()`, `isMixerCaptureRunning()`, `getMixerCaptureBufferPointer()`, etc.

#### FFI implementation (`lib/src/bindings/bindings_player_ffi.dart`)
- Create `ffi.NativeCallable<MixerOutputCallbackTFunction>.listener(_mixerOutputCallback)`.
- `_mixerOutputCallback(uint8_t* data, size_t length)` builds a `Uint8List.view(data, length)` and adds a `MixerOutputChunk` to the stream controller.
- Implement start/stop/read helpers using the generated FFI bindings.

#### Web implementation (`lib/src/bindings/bindings_player_web.dart`)
- Use `SharedArrayBuffer` for the audio output buffer.
- Register a JS callback or worker listener for `mixerOutputData` messages containing `{offset, length}`.
- Wrap the `SharedArrayBuffer` region in a Dart `Uint8List` view.
- Send a worker message to advance the read position.

#### Public API (`lib/src/soloud.dart`)
```dart
Stream<MixerOutputChunk> startMixerOutputStream({
  MixerOutputFormat format = MixerOutputFormat.pcmF32le,
  int sampleRate = -1, // -1 means same as source. Means the same sampleRate the player is currerntly using
  int channels = -1,  // -1 means same as source. Means the same sampleRate the player is currerntly using
  int bufferSizeBytes = 1024 * 1024,
  int notificationThresholdBytes = 4096,
});

void stopMixerOutputStream();
bool get isMixerOutputStreamRunning;

/// Direct shared-memory read for advanced users.
({Pointer<Uint8> pointer, int availableBytes}) getMixerOutputBuffer();
```

### Web build changes
- Add `-s SHARED_MEMORY=1` to `web/compile_wasm.sh`.
- Allocate the mixer-output `SharedArrayBuffer` from JS and pass its address/reference to WASM.
- Update `web/worker.dart` to forward mixer-output messages.
- Document required COOP/COEP headers for web deployment if the user wants to use the mixer output stream feature.

## Implementation Steps

### Phase 1 — Foundation (no encoding yet)
*Goal: PCM-only capture working on desktop/mobile.*

1. **Enums & FFI scaffolding**
   - Add `MixerOutputFormat` to `lib/src/enums.dart` and `src/enums.h`.
   - Add the bindings into `lib/src/bindings/bindings_player_ffi.dart`.

2. **C++ circular buffer**
   - Create `src/mixeroutput/mixer_output.h/.cpp` with atomic offsets and a PCM converter.
   - Create `src/mixeroutput/pcm_converter.h/.cpp`.
   - Hook into `soloud_miniaudio_audiomixer`.
   - Add `src/mixeroutput/` files to `src/src.cmake`.

3. **C FFI exports**
   - Implement `startMixerCapture`, `stopMixerCapture`, `getMixerCaptureBufferPointer`, `getMixerCaptureAvailableBytes`, `advanceMixerCaptureReadPosition`, `setMixerOutputCallback` in `src/bindings.cpp`.

4. **Dart FFI implementation**
   - Add abstract methods to `FlutterSoLoud`.
   - Implement in `FlutterSoLoudFfi` with a `NativeCallable` listener.
   - Wire into `SoLoud.instance` public API.

5. **Verification**
   - `dart analyze`, `dart format`, `dart test`.
   - Build Linux/macOS/Windows example and confirm PCM data flows.

### Phase 2 — Compressed encoders
*Goal: Opus/Vorbis/FLAC output on desktop/mobile.*

6. **Encoder thread & interface**
   - Add `MixerOutputEncoder` interface.
   - Refactor `MixerOutput` to feed raw PCM into a worker thread for compressed formats.

7. **Opus encoder**
   - Implement `OpusOutputEncoder` using `libopus`.

8. **Vorbis encoder**
   - Implement `VorbisOutputEncoder` using `libvorbisenc` + `libogg`.

9. **FLAC encoder**
   - Implement `FlacOutputEncoder` using `libFLAC`.

10. **Verification**
    - Encode to each format, validate headers/streams with external tools.

### Phase 3 — Web support
*Goal: Web capture via SharedArrayBuffer.*

11. **WASM shared memory**
    - Update `web/compile_wasm.sh` with `-s SHARED_MEMORY=1`.
    - Allocate `SharedArrayBuffer` and expose to WASM.

12. **Web callback plumbing**
    - Implement web bindings in `bindings_player_web.dart`.
    - Update `web/worker.dart` to forward `mixerOutputData` messages.

13. **Verification**
    - Build web WASM and run example with required headers.

### Phase 4 — Polish
*Goal: API stability and tests.*

14. Add Dart unit tests for enum sync and buffer math.
15. Add example page for mixer output capture in example/lib/mixer_capture/mixer_capture.dart. This example should play "example/assets/audio/8_bit_mentality.mp3", let the user start the pitchshift filter and customize its parameters with sliders. A button to start and stop capturing the mixer output and a dropdown button to choose the output format.
16. Add error handling / new `PlayerErrors` values if needed.
17. Format, analyze, and update `CHANGELOG.md`.

## Relevant files
- `src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp` — hook `soloud_miniaudio_audiomixer`.
- `src/mixeroutput/*` — new capture module.
- `src/bindings.cpp` — new C FFI exports.
- `src/ffi_gen_tmp.h` — ffigen input.
- `src/enums.h` — new C++ `MixerOutputFormat`.
- `src/src.cmake` — add new C++ sources.
- `lib/src/enums.dart` — new Dart `MixerOutputFormat`.
- `lib/src/bindings/bindings_player.dart` — abstract interface and stream controller.
- `lib/src/bindings/bindings_player_ffi.dart` — FFI implementation and `NativeCallable`.
- `lib/src/bindings/bindings_player_web.dart` — web implementation.
- `lib/src/soloud.dart` — public `SoLoud.instance` API.
- `web/compile_wasm.sh` — shared-memory flags.
- `web/worker.dart` — worker message forwarding.

## Verification
1. `dart analyze` passes with no warnings/errors.
2. `dart format -o none --set-exit-if-changed .` passes.
3. `dart test` passes.
4. Build example on Linux/macOS/Windows and verify PCM stream bytes match expected sample rate × channels × bytes-per-sample.
5. Verify Opus/Vorbis/FLAC output streams start with valid codec headers.
6. Build web WASM with `web/compile_wasm.sh` and confirm `SharedArrayBuffer` data flows.

## Scope boundaries
- **Included**: master mix capture, PCM + Opus/Vorbis/FLAC output, FFI shared-memory read, web SharedArrayBuffer support, start/stop/notification API.
- **Excluded**: per-bus capture, real-time encoding on the audio thread (encoder runs on a worker thread), automatic file saving, visualization/FFT from captured data.

## Further considerations
1. **Web SharedArrayBuffer requirement**: COOP/COEP headers are required in the hosting web server. This should be documented.
2. **Encoder latency**: compressed formats add buffering latency (encoder must accumulate frames). The notification threshold and buffer sizes should be tuned per format.
3. **Memory lifetime**: because data is read as zero-copy views, Dart consumers must process the chunk promptly or copy it; otherwise the circular buffer may overwrite unconsumed data.

---
name: flutter-soloud-output-capture
version: 2
description: Teaches how to record the flutter_soloud engine's mixed master output as a Stream<Uint8List> (raw PCM or Opus/Vorbis/FLAC/WAV) using startMixerOutputStream/stopMixerOutputStream, including the WAV header fix-up and capturing from a worker isolate via SoLoudIsolate. Use when the user wants to record, save, export, share or stream what the app is playing (the final mix, NOT microphone input — flutter_soloud does not capture mic), e.g. "save the mix to a .wav file", "record the game audio", "export my beat/loop/synth performance", "stream the engine output over the network", "feed the app's audio to an encoder or a WebRTC/voice pipeline", or needs the mixed PCM for analysis or tests.
---

# Mixer output capture

flutter_soloud taps the master mixer output: everything the engine mixes (all voices, buses, and active global filters) is copied into a native circular buffer and delivered to Dart as a broadcast `Stream<Uint8List>` of audio chunks. This is a capture of the *output* the engine produces — it is not an input device. flutter_soloud has no microphone recording; for mic input use another package. Capture runs on all platforms; on web it requires the WebAssembly build (`--wasm`).

## Minimal example

Record 5 seconds of the mix to a raw PCM file:

```dart
import 'dart:io';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> recordMix(String outputPath) async {
  await SoLoud.instance.init();

  final sink = File(outputPath).openWrite();
  final stream = SoLoud.instance.startMixerOutputStream(
    format: MixerOutputFormat.pcmS16le,
  );
  final sub = stream.listen(sink.add);

  final sound = await SoLoud.instance.loadAsset('assets/music.mp3');
  SoLoud.instance.play(sound);
  await Future<void>.delayed(const Duration(seconds: 5));

  // Stop first: the tail of the buffer is flushed into the stream
  // synchronously on stop, so listeners still attached get the last bytes.
  SoLoud.instance.stopMixerOutputStream();
  await sub.cancel();
  await sink.close();
}
```

## The API shape

All on `SoLoud.instance` (and mirrored on `SoLoudIsolate.instance`, see below):

- `Stream<Uint8List> startMixerOutputStream({MixerOutputFormat format = MixerOutputFormat.pcmF32le, int sampleRate = -1, int channels = -1, int bufferSizeBytes = 1024 * 1024, int notificationThresholdBytes = 4096, int chunkPCMFrames = -1})` — starts capture and returns a **broadcast** stream. Capture neither resamples nor remixes: `sampleRate`/`channels` must be `-1` (follow the engine) or equal to the engine's values, anything else throws `SoLoudInvalidParameterException`. Throws `SoLoudNotInitializedException` if the engine is not initialized, and keep the sizes positive (an invalid size fails with a `SoLoudCppException`). Calling it while already capturing returns the existing stream (the new arguments are ignored). Only one capture runs at a time.
- `void stopMixerOutputStream()` — stops capture, flushes remaining buffer bytes into the stream synchronously, then closes the stream. It stops only a capture started by the same isolate.
- `bool get isMixerOutputStreamRunning` — whether capture is active.
- `Uint8List getMixerOutputWavHeader()` — the finalized 44-byte WAV header of the last `MixerOutputFormat.wav` capture of this engine session, available after stopping (an empty list during the capture, after a capture in another format, or before any capture). Throws `SoLoudNotInitializedException` without the engine.
- `enum MixerOutputFormat` — `pcmF32le` (default; no conversion on the native side, the cheapest), `pcmS8`, `pcmS16le`, `pcmS32le`, `opus`, `vorbis`, `flac`, `wav` (16-bit PCM in a RIFF/WAVE container). Helpers: `isPcm`, `bytesPerSample` (0 for compressed and `wav`), `bytesPerFrame(channels)`.
- The data is interleaved at the engine's rate and channel count: `SoLoud.instance.sampleRate` and the `channels` given to `init()` (defaults 44100 Hz, stereo). Store them with a raw PCM file, it has no header.

How this differs from what models assume from other packages:

- There is no `record()`/`startRecording()` and no `AudioRecorder` — the API is named after the *mixer output*. You are recording playback, not a device.
- No `onAudio`/`dataAvailable` callback like the Web Audio `ScriptProcessorNode`/`AudioWorklet` — the result is an ordinary Dart `Stream`; you `listen` to it.
- Unlike just_audio/audioplayers there is no file-target convenience API ("record to path"). You always write the chunks yourself.
- The chunk size is driven by `notificationThresholdBytes` (compressed formats, and PCM when `chunkPCMFrames` is -1) or by `chunkPCMFrames` (PCM only, fixed-size chunks of exactly `chunkPCMFrames * channels * bytesPerSample` bytes; must be -1 or >= 2048). There is no per-chunk timestamp or duration metadata — just bytes.
- Compressed formats (`opus`, `vorbis`, `flac`) require the plugin to be built with the Xiph libraries; `wav` and the PCM formats are always available.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| "Record/export what the app plays" (a beat maker, a synth performance, game audio for a replay or share) planned with a screen recorder, a microphone/recording plugin, or by re-rendering the sounds in Dart | `startMixerOutputStream(format: ...)` written to a file | A digital copy of the exact mix (all voices, buses, global filters), no microphone permission, no room noise. It records in real time while the audio plays; it is not a faster-than-realtime offline render. |
| A playable file wanted, built by collecting raw PCM and writing a WAV header by hand | `MixerOutputFormat.wav` + `getMixerOutputWavHeader()` patched over the first 44 bytes after stop; `flac`/`opus`/`vorbis` when size matters | The engine writes the container; only the header sizes need the patch. WAV is 16-bit PCM and always available; the compressed formats need the Xiph build. |
| PCM captured as `pcmS16le`/`wav` and then converted to floats in Dart for analysis, DSP or an encoder that takes floats | `MixerOutputFormat.pcmF32le` (the default) and view the chunks as `Float32List` | It is the engine's own sample format: no conversion on the native side and none in Dart. |
| An encoder, WebSocket or other consumer that needs fixed-size frames, fed through a hand-written re-chunking buffer | `chunkPCMFrames: n` (PCM formats, `n >= 2048`) | Every chunk is exactly `n * channels * bytesPerSample` bytes (the flush on stop can be shorter), so no accumulation code in Dart. |
| Heavy work in the stream listener (encoding, sync file writes, network) with gaps or lost audio when the UI is busy | Capture from a worker isolate with `SoLoudIsolate.instance.startMixerOutputStream`, or at least a larger `bufferSizeBytes` | The capture keeps flowing while the main isolate is blocked; the engine itself stays on the main isolate. |
| A level meter, spectrum or waveform display built on top of the capture stream | `setVisualizationEnabled` + `audioVisualizationEvents` / `getApproximateVolume` (**flutter-soloud-visualization**) | The engine already computes wave, FFT and peaks natively; the capture stream is for recording and streaming, not for drawing. |

## Traps

- **Not microphone input.** If the user asks to record the mic, this API is the wrong tool regardless of how it is named. flutter_soloud has no input capture.
- **WAV header is a placeholder until stop.** With `MixerOutputFormat.wav` the stream starts with a 44-byte header whose `RIFF`/`data` size fields are zero. After `stopMixerOutputStream()`, call `getMixerOutputWavHeader()` and overwrite the first 44 bytes of the file, or players report duration 0 / refuse to play:
  ```dart
  SoLoud.instance.stopMixerOutputStream();
  await sub.cancel();
  await sink.close();
  final header = SoLoud.instance.getMixerOutputWavHeader();
  if (header.length == 44) {
    final raf = File(path).openSync(mode: FileMode.writeOnlyAppend)
      ..setPositionSync(0)
      ..writeFromSync(header);
    await raf.close();
  }
  ```
  Read the header before the next capture starts and before `deinit()`.
- **Cancel the subscription after stop, not before.** `stop()` flushes the buffer tail into the stream with synchronous delivery; cancelling first loses the final chunk (which for compressed formats carries the encoder tail).
- **Rate and channels are the engine's.** Passing another `sampleRate` or `channels` throws `SoLoudInvalidParameterException`; to record at 48 kHz or in mono, `init()` the engine that way or convert the captured data yourself.
- **`chunkPCMFrames` is PCM-only.** Passing it with opus/vorbis/flac/wav, or a value below 2048, trips an assert in debug builds. Compressed formats always use `notificationThresholdBytes`.
- **Silence is not recorded while the output device is stopped.** The capture is fed by the device callback, and the device stops after an idle period when nothing plays (about 500 ms by default). For a recording with real-time gaps of silence, call `SoLoud.instance.setAudioDeviceIdleTimeout(null)` while recording (and restore a `Duration` afterwards), or keep a voice playing.
- **Main-isolate stalls create capture gaps.** The native side waits for Dart to advance the read position; a 500 ms synchronous block on the listening isolate produces a ~500 ms gap between chunks. Don't do heavy synchronous work (file sync writes on huge chunks, JSON parsing) in the listener; enlarge `bufferSizeBytes` to absorb bursts, or capture from a worker isolate (below).
- **Init ordering.** `startMixerOutputStream` before `await SoLoud.instance.init()` throws `SoLoudNotInitializedException`. `deinit()` auto-stops capture and closes the stream.
- **Filters and volume are baked in.** The capture is what goes to the device: post-mix, after global filters (e.g. `limiterFilter`) and the global volume. A user turning the app volume down in your settings also turns the recording down.

## Capturing from a worker isolate

The engine is a C++ singleton that can only be initialized on the main isolate. For heavy consumers (encoding, network streaming), run the capture loop in a spawned isolate via `SoLoudIsolate.instance`, which exposes `startMixerOutputStream`, `stopMixerOutputStream`, `isMixerOutputStreamRunning`, `getMixerOutputWavHeader`, plus `readSamplesFromFile`/`readSamplesFromMem`. `SoLoudIsolate` has no `init`/`deinit`, no loading, no playback — those stay on the main isolate.

```dart
// Main isolate first:
await SoLoud.instance.init();
// ... then inside the spawned isolate (entry point needs
// @pragma('vm:entry-point')):
final stream = SoLoudIsolate.instance.startMixerOutputStream(
  format: MixerOutputFormat.pcmS16le,
  chunkPCMFrames: 2048,
);
final sub = stream.listen((chunk) { /* encode / send */ });
// on shutdown message: stop first (it flushes the tail), then cancel.
SoLoudIsolate.instance.stopMixerOutputStream();
await sub.cancel();
```

Only one capture runs at a time, engine-wide: while a worker's capture runs, `SoLoud.instance.stopMixerOutputStream()` on the main isolate does not stop it (each isolate stops only its own).

`SoLoudIsolate` is marked `@experimental`; silence lints with `// ignore_for_file: experimental_member_use` where needed.

## More depth

- Demo recording the mix to a file with format picker and WAV header patching: `example/lib/mixer_capture/mixer_capture.dart` in the flutter_soloud repo.
- Demo of capture in a spawned isolate (including surviving a blocked main isolate): `example/lib/mixer_capture/isolate_capture_test.dart`.
- Doc page: "Mixer output capture" under Advanced in the online docs.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

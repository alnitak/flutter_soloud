---
name: flutter_soloud-output-capture
version: 1
description: Teaches how to record the flutter_soloud engine's mixed master output as a Stream<Uint8List> (raw PCM or Opus/Vorbis/FLAC/WAV) using startMixerOutputStream/stopMixerOutputStream, including the WAV header fix-up and capturing from a worker isolate via SoLoudIsolate. Use when the user wants to record, save, or stream what the app is playing (the final mix, NOT microphone input — flutter_soloud does not capture mic), e.g. "save the mix to a .wav file", "record the game audio", "stream the engine output over the network".
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

- `Stream<Uint8List> startMixerOutputStream({MixerOutputFormat format = MixerOutputFormat.pcmF32le, int sampleRate = -1, int channels = -1, int bufferSizeBytes = 1024 * 1024, int notificationThresholdBytes = 4096, int chunkPCMFrames = -1})` — starts capture and returns a **broadcast** stream. `sampleRate`/`channels` of `-1` follow the engine config. Throws `SoLoudNotInitializedException` if the engine is not initialized. Calling it while already capturing returns the existing stream.
- `void stopMixerOutputStream()` — stops capture, flushes remaining buffer bytes into the stream synchronously, then closes the stream.
- `bool get isMixerOutputStreamRunning` — whether capture is active.
- `Uint8List getMixerOutputWavHeader()` — the finalized 44-byte WAV header; only meaningful with `MixerOutputFormat.wav` after stopping (returns an empty list otherwise).
- `enum MixerOutputFormat` — `pcmF32le` (default, `pcmS8`, `pcmS16le`, `pcmS32le`, `opus`, `vorbis`, `flac`, `wav`. Helpers: `isPcm`, `bytesPerSample` (0 for compressed), `bytesPerFrame(channels)`.

How this differs from what models assume from other packages:

- There is no `record()`/`startRecording()` and no `AudioRecorder` — the API is named after the *mixer output*. You are recording playback, not a device.
- No `onAudio`/`dataAvailable` callback like the Web Audio `ScriptProcessorNode`/`AudioWorklet` — the result is an ordinary Dart `Stream`; you `listen` to it.
- Unlike just_audio/audioplayers there is no file-target convenience API ("record to path"). You always write the chunks yourself.
- The chunk size is driven by `notificationThresholdBytes` (compressed formats, and PCM when `chunkPCMFrames` is -1) or by `chunkPCMFrames` (PCM only, fixed-size chunks of exactly `chunkPCMFrames * channels * bytesPerSample` bytes; must be -1 or >= 2048). There is no per-chunk timestamp or duration metadata — just bytes.
- Compressed formats (`opus`, `vorbis`, `flac`) require the plugin to be built with the Xiph libraries; `wav` and the PCM formats are always available.

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
- **Cancel the subscription after stop, not before.** `stop()` flushes the buffer tail into the stream with synchronous delivery; cancelling first loses the final chunk (which for compressed formats carries the encoder tail).
- **`chunkPCMFrames` is PCM-only.** Passing it with opus/vorbis/flac/wav trips an assert in debug builds. Compressed formats always use `notificationThresholdBytes`.
- **Main-isolate stalls create capture gaps.** The native side waits for Dart to advance the read position; a 500 ms synchronous block on the listening isolate produces a ~500 ms gap between chunks. Don't do heavy synchronous work (file sync writes on huge chunks, JSON parsing) in the listener; enlarge `bufferSizeBytes` to absorb bursts, or capture from a worker isolate (below).
- **Init ordering.** `startMixerOutputStream` before `await SoLoud.instance.init()` throws `SoLoudNotInitializedException`. `deinit()` auto-stops capture and closes the stream.
- **Filters are baked in.** Active global filters (e.g. `pitchShiftFilter`) apply to the captured output — the capture is post-mix, post-filter.

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
// on shutdown message:
await sub.cancel();
SoLoudIsolate.instance.stopMixerOutputStream();
```

`SoLoudIsolate` is marked `@experimental`; silence lints with `// ignore_for_file: experimental_member_use` where needed.

## More depth

- Demo recording the mix to a file with format picker and WAV header patching: `example/lib/mixer_capture/mixer_capture.dart` in the flutter_soloud repo.
- Demo of capture in a spawned isolate (including surviving a blocked main isolate): `example/lib/mixer_capture/isolate_capture_test.dart`.
- Doc page: `docs/advanced/mixer_output_capture.mdx` (mixer output capture guide).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

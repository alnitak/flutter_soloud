---
name: flutter_soloud-streaming
version: 1
description: Teaches the push buffer-stream API of the flutter_soloud package (setBufferStream, addAudioDataStream, setDataIsEnded, BufferingType, BufferType, icy metadata). Use when the user wants to play audio that arrives in chunks — internet/icecast radio, WebSocket PCM feeds, TTS/LLM streaming APIs, or procedurally generated PCM — instead of loading a complete file or asset.
---

# Push buffer streaming in flutter_soloud

Push streaming plays audio while it is still arriving: you create an `AudioSource` with `SoLoud.instance.setBufferStream(...)`, push chunks with `addAudioDataStream(source, bytes)`, and mark the end with `setDataIsEnded(source)`. The engine decodes and buffers internally, auto-pausing when the buffer runs dry and resuming once `bufferingTimeNeeds` seconds are buffered again. This is the API for icecast/web radio, WebSocket audio, streaming TTS, and generated PCM — not for complete files on disk (use `loadFile`/`loadMem`/`loadUrl` for those).

## Minimal example

```dart
import 'dart:typed_data';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  // Synchronous — no await. Returns an AudioSource immediately.
  final stream = SoLoud.instance.setBufferStream(
    maxBufferSizeBytes: 1024 * 1024 * 100, // default: 100 MB cap, not pre-allocated
    bufferingType: BufferingType.released, // free played data (radio-style)
    bufferingTimeNeeds: 2,                 // seconds buffered before unpause
    sampleRate: 44100,
    channels: Channels.stereo,
    format: BufferType.auto,               // detect MP3 / Ogg-Opus / Ogg-Vorbis
    onBuffering: (isBuffering, handle, time) {
      // isBuffering=true: engine paused, waiting for data
      // isBuffering=false: resumed
    },
    onMetadata: (metadata) {
      // AudioMetadata; fires on first data and on mid-stream changes
    },
  );

  // Synchronous too. Safe to call before any data arrives — playback
  // starts once bufferingTimeNeeds seconds are buffered.
  final handle = SoLoud.instance.play(stream);

  // ... as chunks arrive (http stream, WebSocket, generator):
  SoLoud.instance.addAudioDataStream(stream, someUint8ListChunk);

  // When the feed ends (or never, for live radio):
  SoLoud.instance.setDataIsEnded(stream);

  // Optional: know when playback actually finished. Only fires if
  // setDataIsEnded was called.
  stream.soundEvents.listen((event) {
    if (event.event == SoundEventType.handleIsNoMoreValid &&
        event.handle == handle) {
      // finished playing
    }
  });
}
```

## The API shape

All of these live on `SoLoud.instance` and operate on the `AudioSource` returned by `setBufferStream`. Verified against `lib/src/soloud.dart` and `lib/src/enums.dart`:

- `AudioSource setBufferStream({int? maxBufferSizeBytes, Duration? maxBufferSizeDuration, BufferingType bufferingType = BufferingType.preserved, double bufferingTimeNeeds = 2, int sampleRate = 24000, Channels channels = Channels.mono, BufferType format = BufferType.s16le, bool autoDispose = false, void Function(bool isBuffering, int handle, double time)? onBuffering, void Function(AudioMetadata)? onMetadata})` — synchronous factory. `maxBufferSizeBytes` and `maxBufferSizeDuration` are mutually exclusive (asserted); neither pre-allocates memory, they only cap how much data may be added.
- `void addAudioDataStream(AudioSource source, Uint8List audioChunk)` — push one chunk. Synchronous, callable from any isolate (`@pragma('vm:entry-point')` functions in the example use it).
- `void setDataIsEnded(AudioSource sound)` — marks the stream complete. Without this, `soundEvents` never reports playback completion.
- `void resetBufferStream(AudioSource sound)` — drops buffered data so you can retune the same source to new content.
- `int getBufferSize(AudioSource sound)` — current buffered size in bytes.
- `Duration getStreamTimeConsumed(AudioSource sound)` — elapsed play time; **only** for `BufferingType.released` streams (throws for other sounds).
- `void setBufferIcyMetaInt(AudioSource sound, int icyMetaInt)` — pass the `icy-metaint` response header of an icecast stream so the engine can strip/extract MP3 or Ogg-FLAC metadata. Must be called once, **before the first** `addAudioDataStream`. `setMp3BufferIcyMetaInt` is a deprecated alias of this.

Key enums (`lib/src/enums.dart`):

- `BufferType` — `f32le`, `s8`, `s16le`, `s32le` are raw interleaved PCM (you must set `sampleRate`/`channels` correctly). `auto` detects MP3, Ogg-Opus, and Ogg-Vorbis containers and **ignores** `sampleRate`/`channels`. `opus` still exists but is deprecated — it is silently rewritten to `auto` with a debugPrint.
- `BufferingType` — `preserved`: keeps all data in memory, allows multiple simultaneous handles, seeking and looping. `released`: frees played data (bounded memory for endless feeds), but plays once, cannot seek, and `getPosition` always returns 0 — use `getStreamTimeConsumed` instead.

Divergences from audioplayers/just_audio habits:

- No `setUrl`/`AudioSource.uri` streaming; you do the HTTP/WebSocket I/O yourself and push raw bytes.
- `setBufferStream` and `play` are **synchronous** — `await SoLoud.instance.play(stream)` compiles but is pointless; the docs snippet showing that is stale.
- There is no built-in position stream for `released` streams; poll `getStreamTimeConsumed` on a timer (the example's `buffer_widget.dart` does exactly this).
- Buffer underruns are not errors: the engine pauses and fires `onBuffering(isBuffering: true, ...)`, then resumes automatically.

## Traps

- **`BufferingType.released` plays exactly once.** A second `play` on the same source throws `SoLoudBufferStreamCanBePlayedOnlyOnceCppException`. When it finishes, the buffer is empty and you must dispose the source yourself (or pass `autoDispose: true`).
- **`getPosition` lies for released streams** — always `Duration.zero`. Use `getStreamTimeConsumed(sound)`; calling it on a non-released sound throws.
- **Reaching the max buffer size ends the stream.** Once the cap is hit, the stream is treated as ended and further `addAudioDataStream` calls throw `SoLoudStreamEndedAlreadyCppException` (the web_radio example catches it and reconnects). Size the cap for the feed, or use `released`.
- **Icy metadata ordering.** Request the header with `Icy-MetaData: 1` on the HTTP request, then call `setBufferIcyMetaInt(source, int.parse(headers['icy-metaint'] ?? '0'))` on the first audio chunk, before any `addAudioDataStream`. Later calls are ignored.
- **`BufferType.opus` is deprecated.** Passing it still works (rewritten to `auto` with a debugPrint) but new code should use `auto`.
- **With `format: BufferType.auto`, `sampleRate` and `channels` are ignored** — the decoder takes them from the container. They only matter for raw PCM formats.
- **Raw PCM must match the declared format exactly**: interleaved, little-endian, correct sample rate and channel count; the engine resamples to `sampleRate` but does not channel-convert or fix wrong formats — mismatch means noise or wrong speed, not an error.
- **Compressed streaming needs the bundled Xiph libs.** If the app was built with them excluded, `addAudioDataStream` of compressed data throws `SoLoudXiphLibsNotAvailableException`.
- **Forgetting `setDataIsEnded`** leaves the stream "live" forever: `soundEvents` never emits `handleIsNoMoreValid` and (with `preserved`) the buffer keeps growing.
- **`maxBufferSizeDuration` is computed from `sampleRate`/`channels`** as `ms * sampleRate * channels * 4 bytes` (internal float storage) — for `auto` sources this conversion is approximate.

## More depth

- Full recipes (icecast radio with icy metadata, WebSocket PCM, procedural PCM in an isolate): [references/recipes.md](references/recipes.md)
- Pull-model streaming (engine requests data via callback; for huge files and byte-range fetching) is covered by the sibling skill **flutter_soloud-pull-streaming** (`skills/flutter_soloud-pull-streaming/SKILL.md`).
- Runnable demos in this repo: `example/lib/buffer_stream/web_radio.dart`, `websocket.dart`, `generate.dart`, `simple_noise_stream.dart` (the last one is the smallest complete example).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

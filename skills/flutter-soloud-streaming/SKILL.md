---
name: flutter-soloud-streaming
version: 5
description: Teaches the push buffer-stream API of the flutter_soloud package (setBufferStream, addAudioDataStream, setDataIsEnded, BufferingType, BufferType, icy metadata). Use when the user wants to play audio that arrives in chunks — internet/icecast radio, WebSocket PCM feeds, TTS/LLM streaming APIs (voice assistants, game dialogue generated at runtime), or procedurally generated PCM — instead of loading a complete file or asset, or when a stream stutters, never ends, or grows in memory.
---

# Push buffer streaming in flutter_soloud

Push streaming plays audio while it is still arriving: you create an `AudioSource` with `SoLoud.instance.setBufferStream(...)`, push chunks with `addAudioDataStream(source, bytes)`, and mark the end with `setDataIsEnded(source)`. The engine decodes and buffers internally, auto-pausing when the buffer runs dry and resuming once `bufferingTimeNeeds` seconds are buffered again. This is the API for icecast/web radio, WebSocket audio, streaming TTS, and generated PCM — not for complete files on disk (use `loadFile`/`loadMem`/`loadUrl` for those). For low-latency live synthesis or MIDI instruments fed from Dart, use `SoLoud.instance.setRealtimeStream(...)` instead (see **flutter-soloud-realtime-dsp**).

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
    format: BufferType.auto,               // detect MP3, WAV, FLAC, Ogg, AAC, AC-3, E-AC-3
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
- `void addAudioDataStream(AudioSource source, Uint8List audioChunk)` — push one chunk. Synchronous; an empty chunk is ignored. Callable from a worker isolate too (with `setDataIsEnded`); every other stream method belongs to the isolate that called `init()`. Throws `SoLoudStreamEndedAlreadyCppException` (stream ended by you or by reaching the size cap), `SoLoudPcmBufferFullCppException`, `SoLoudHashIsNotABufferStreamCppException`, `SoLoudAudioFormatNotSupportedCppException` (no decoder for this format on this platform) and `SoLoudXiphLibsNotAvailableException`.
- `void setDataIsEnded(AudioSource sound)` — marks the stream complete. Without this, `soundEvents` never reports playback completion.
- `void resetBufferStream(AudioSource sound)` — drops buffered data so you can retune the same source to new content.
- `int getBufferSize(AudioSource sound)` — current buffered size in bytes.
- `Duration getStreamTimeConsumed(AudioSource sound)` — elapsed play time; **only** for `BufferingType.released` streams (throws for other sounds).
- `void setBufferIcyMetaInt(AudioSource sound, int icyMetaInt)` — pass the `icy-metaint` response header of an icecast stream so the engine can strip/extract MP3 or Ogg-FLAC metadata. Must be called once, **before the first** `addAudioDataStream`. `setMp3BufferIcyMetaInt` is a deprecated alias of this.

All of them throw `SoLoudNotInitializedException` before `init()`.

Key enums (`lib/src/enums.dart`):

- `BufferType` — `f32le`, `s8`, `s16le`, `s32le` are raw interleaved PCM, and `f32lePlanar` is raw planar 32-bit float PCM (`[ch0..., ch1...]`); you must set `sampleRate`/`channels` correctly. `auto` detects MP3, WAV, FLAC, Ogg (Opus, Vorbis, FLAC), AAC (ADTS), AC-3, and E-AC-3 streams and **ignores** `sampleRate`/`channels` (on Linux, decoding AAC/AC-3/E-AC-3 requires FFmpeg `libavcodec`/`libavformat` runtime libraries; on Web, AC-3 and E-AC-3 streaming is not supported by any browser; core formats work out of the box). Ogg Opus streams with 3 to 8 channels are downmixed to stereo.
- `BufferingType` — `preserved`: keeps all data in memory, allows multiple simultaneous handles, seeking and looping. `released`: frees played data (bounded memory for endless feeds), but plays once, cannot seek or loop, and `getPosition` always returns 0 — use `getStreamTimeConsumed` instead.

Divergences from audioplayers/just_audio habits:

- No `setUrl`/`AudioSource.uri` streaming; you do the HTTP/WebSocket I/O yourself and push raw bytes.
- `setBufferStream` and `play` are **synchronous** — `await SoLoud.instance.play(stream)` compiles but is pointless.
- There is no built-in position stream for `released` streams; poll `getStreamTimeConsumed` on a timer (the example's `buffer_widget.dart` does exactly this).
- Buffer underruns are not errors: the engine pauses and fires `onBuffering(isBuffering: true, ...)`, then resumes automatically.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| A complete file or a finite URL downloaded and pushed chunk by chunk with `addAudioDataStream` | `loadUrl`/`loadFile`/`loadAsset` for normal sizes (**flutter-soloud-loading**); a pull-buffer stream when it is big and must seek (**flutter-soloud-pull-streaming**) | The loaders are one call and give a fully seekable source; a pull stream fetches only the byte ranges it needs and keeps memory bounded. Push streaming is for data without random access |
| Web radio or another endless feed created with the default `BufferingType.preserved` | `bufferingType: BufferingType.released` | `preserved` keeps every decoded sample until `maxBufferSizeBytes` is reached, and then the stream ends; `released` frees what was played. Cost: one `play()` only, no seek or loop, and `getPosition` is 0 (use `getStreamTimeConsumed`) |
| MP3/Opus/AAC chunks (TTS or LLM audio APIs, radio) decoded to PCM in Dart or by another package before pushing | `format: BufferType.auto` and push the encoded bytes as they arrive | The engine detects and decodes the stream natively and reports `onMetadata`; no decoder dependency and no extra copies in Dart. MP4/M4A cannot be streamed; AAC/AC-3 need FFmpeg on Linux |
| A Dart synth, emulator audio, voice-chat frames or a drum machine pushed with `addAudioDataStream` and a tiny `bufferingTimeNeeds` | `setRealtimeStream` (**flutter-soloud-realtime-dsp**) | A buffer stream pauses the voice to rebuffer `bufferingTimeNeeds` seconds whenever it runs dry; a realtime stream never pauses (it fades to silence and back) and has lower latency. Raw PCM only (`f32le`, `s16le`, `f32lePlanar`), experimental API |
| A `Timer` polling `getPosition`/`getBufferSize` to guess when the stream stalled, or a hand-made "buffering..." flag | The `onBuffering` callback of `setBufferStream` | The engine reports the exact moment it pauses for data and when it resumes, with the handle and the time |
| ICY metadata parsed by hand from the radio bytes to show the track title | `setBufferIcyMetaInt(source, metaInt)` before the first chunk, plus `onMetadata` | The engine takes the metadata blocks out of the audio and delivers station and title as `AudioMetadata` |
| PCM bytes built with `Uint8List.fromList(floatOrInt16List)` or a manual byte loop | `typedList.buffer.asUint8List()` with the matching `BufferType` | `fromList` truncates every sample to one byte (noise); the view passes the real bytes without copying |
| Long PCM generation loops running in the UI isolate before `addAudioDataStream` | Generate and push inside `compute()`/`Isolate.run`: `addAudioDataStream` and `setDataIsEnded` work from a worker isolate once `init()` and `setBufferStream` ran in the main one (see [references/recipes.md](references/recipes.md)); for continuous generation use `stream.startFloat32Feeder` of a realtime stream (**flutter-soloud-realtime-dsp**) | The UI stays smooth while samples are computed and no bytes are copied back to the main isolate; the feeder runs the generator in its own isolate with no port code |

## Traps

- **MP4 and M4A containers are NOT supported for streaming.** Container files with ISO BMFF / MP4 atom structures (`moov`, `mdat`) require random access to read header atom offsets and cannot be streamed chunk-by-chunk with `setBufferStream` or `setPullBufferStream`. For real-time streaming, use elementary streams (AAC ADTS, AC-3, E-AC-3, OGG, MP3, WAV, or raw PCM). MP4 and M4A are only supported for full-file/asset/memory loading (`loadFile`, `loadAsset`, `loadMem`, `loadUrl`).
- **AC-3 and E-AC-3 streaming is NOT supported on the Web platform in any browser.** Google Chrome, Chromium browsers, and Mozilla Firefox do not support AC-3 or E-AC-3 due to proprietary Dolby Digital licensing restrictions. Safari's WebCodecs engine rejects raw AC-3 elementary stream frames. AC-3 and E-AC-3 streaming is native-only (iOS, macOS native, Android, Windows, and Linux with FFmpeg). On Web, feeding AC-3/E-AC-3 stream data makes `addAudioDataStream` throw `SoLoudAudioFormatNotSupportedCppException`; so does feeding AAC/AC-3/E-AC-3 on Linux without FFmpeg. For cross-browser web streaming, use AAC (ADTS), Opus, MP3, FLAC, or raw PCM.
- **`BufferingType.released` plays exactly once.** A second `play` on the same source throws `SoLoudBufferStreamCanBePlayedOnlyOnceCppException`. When it finishes, the buffer is empty and you must dispose the source yourself (or pass `autoDispose: true`).
- **`getPosition` lies for released streams** — always `Duration.zero`. Use `getStreamTimeConsumed(sound)`; calling it on a non-released sound throws.
- **Reaching the max buffer size ends the stream.** Once the cap is hit, the stream is treated as ended and further `addAudioDataStream` calls throw `SoLoudStreamEndedAlreadyCppException` (the web_radio example catches it and reconnects). Size the cap for the feed, or use `released`.
- **Icy metadata ordering.** Request the header with `Icy-MetaData: 1` on the HTTP request, then call `setBufferIcyMetaInt(source, int.parse(headers['icy-metaint'] ?? '0'))` on the first audio chunk, before any `addAudioDataStream`. Later calls are ignored.
- **With `format: BufferType.auto`, `sampleRate` and `channels` are ignored** — the decoder takes them from the container. They only matter for raw PCM formats.
- **Raw PCM must match the declared format exactly**: interleaved (or planar for `f32lePlanar`), little-endian, correct sample rate and channel count; the engine resamples to `sampleRate` but does not channel-convert or fix wrong formats — mismatch means noise or wrong speed, not an error.
- **Ogg, Vorbis, Opus and FLAC streams need the Xiph libs.** If the app was built with them excluded (`no_xiph_libs: true`), `addAudioDataStream` of data in those formats throws `SoLoudXiphLibsNotAvailableException`. MP3, WAV and raw PCM streams still work.
- **Forgetting `setDataIsEnded`** leaves the stream "live" forever: `soundEvents` never emits `handleIsNoMoreValid` and (with `preserved`) the buffer keeps growing.
- **`maxBufferSizeDuration` is computed from `sampleRate`/`channels`** as `ms * sampleRate * channels * 4 bytes` (internal float storage) — for `auto` sources this conversion is approximate.

## More depth

- Full recipes (icecast radio with icy metadata, WebSocket PCM, procedural PCM in an isolate): [references/recipes.md](references/recipes.md)
- Pull-model streaming (engine requests data via callback; for huge files and byte-range fetching) is covered by the sibling skill **flutter-soloud-pull-streaming** (`skills/flutter-soloud-pull-streaming/SKILL.md`).
- Low-latency realtime PCM streaming (`setRealtimeStream`, with isolate feeders) is covered by **flutter-soloud-realtime-dsp** (`skills/flutter-soloud-realtime-dsp/SKILL.md`).
- Runnable demos in this repo: `example/lib/buffer_stream/web_radio.dart`, `websocket.dart`, `generate.dart`, `simple_noise_stream.dart` (the last one is the smallest complete example).

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

---
name: flutter-soloud-pull-streaming
version: 3
description: Teaches the pull-buffer streaming API of the flutter_soloud audio plugin — setPullBufferStream with its onMoreDataIsNeeded callback, addPullBufferDataStream with byte offsets, seek via engine re-requests, and bounded-memory playback of huge seekable sources (HTTP range requests, large files). Use when the user asks to stream a large remote/local audio file with seeking (podcast, audiobook, long music track or game soundtrack, encrypted audio), play multi-GB audio without loading it into memory, cut the memory or start-up delay of a big `loadUrl`/`loadMem`, or is deciding between push (setBufferStream) and pull streaming.
---

# flutter_soloud pull-buffer streaming

In flutter_soloud, "pull" streaming means the engine owns the data demand: you create an `AudioSource` with `setPullBufferStream(...)` declaring the total encoded size, and the engine calls your `onMoreDataIsNeeded(offset)` callback whenever it wants the next chunk of *encoded* bytes at a specific byte offset. You fetch the bytes (HTTP Range request, file read, decryptor, custom protocol) and hand them back via `addPullBufferDataStream`. Decoded audio lives in a fixed-size circular buffer, so memory stays bounded regardless of source size — a 10 GB file can play with a 5 MB buffer. Seeking works: after `SoLoud.instance.seek(handle, pos)`, the engine re-issues `onMoreDataIsNeeded` at the byte offset for the new position. One pull stream = one playback voice.

## Minimal example

```dart
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> playHugeFile(
  Uint8List Function(int offset, int length) fetchRange,
  int totalBytes,
) async {
  await SoLoud.instance.init(); // must complete before any other call

  // Declare first: the callback closure must reference the source, and a
  // local can't be referenced inside its own initializer.
  late final AudioSource source;
  source = SoLoud.instance.setPullBufferStream(
    audioSizeBytes: totalBytes, // REQUIRED, non-zero, known upfront
    bufferSizeBytes: 5 * 1024 * 1024, // decoded circular buffer, ~14 s stereo f32
    bufferTriggerPosition: 0.8, // default; ask for more when 20% ahead remains
    format: BufferType.auto, // default; detects MP3, WAV, FLAC, OGG, AAC, AC-3, E-AC-3
    onAudioDuration: (seconds) {/* total duration is now known */},
    onMetadata: (metadata) {/* detected format, sample rate, channels */},
    onMoreDataIsNeeded: (offset) {
      const chunkSize = 64 * 1024;
      if (offset < 0 || offset >= totalBytes) return;
      final remaining = totalBytes - offset;
      final length = remaining < chunkSize ? remaining : chunkSize;
      SoLoud.instance.addPullBufferDataStream(
        source,
        fetchRange(offset, length),
        offset: offset,
      );
    },
  );

  final handle = SoLoud.instance.play(source);

  // Seeking is free: the engine re-requests data at the new offset.
  SoLoud.instance.seek(handle, const Duration(minutes: 2));

  // No end-of-stream call exists: when sequential data reaches
  // audioSizeBytes the engine ends the stream itself. A seek to or past the
  // end also ends the sound.
  // Dispose on teardown: SoLoud.instance.deinit();
}
```

## The API shape

All on `SoLoud.instance` (`lib/src/soloud.dart`). Real signatures:

```dart
AudioSource setPullBufferStream({
  int bufferSizeBytes = 1024 * 1024 * 10, // 10 MB
  double bufferTriggerPosition = 0.8,
  int sampleRate = 44100,
  Channels channels = Channels.stereo,
  BufferType format = BufferType.auto,
  int audioSizeBytes = 0,        // 0 throws SoLoudCppException(invalidParameter)
  bool autoDispose = false,
  void Function(bool isBuffering, int handle, double time)? onBuffering,
  void Function(AudioMetadata)? onMetadata,
  void Function(double duration)? onAudioDuration,
  void Function(int offset)? onMoreDataIsNeeded,
});

void resetPullBufferStream(AudioSource sound);

PlayerErrors addPullBufferDataStream(
  AudioSource source,
  Uint8List audioChunk, {
  int offset = 0, // byte offset in the ENCODED stream
});

({PlayerErrors error, Duration startTime, Duration endTime})
  getPullBufferTimeRange(AudioSource source);
```

- `setPullBufferStream` returns an `AudioSource` synchronously (unlike `loadAsset`/`loadFile`, which are async) and throws `SoLoudCppException` on error (also for a negative or absurd `bufferSizeBytes`). Play it with the usual synchronous `SoLoud.instance.play(source)`.
- `audioSizeBytes` is the total size of the source as you will feed it (encoded bytes, or PCM bytes for the raw formats) and is mandatory upfront (server `Content-Length`, file length). It drives duration calculation and end-of-stream detection; for Ogg formats the engine also uses it to request the tail chunk for duration probing.
- `bufferTriggerPosition` is about how much decoded audio remains **ahead of the playhead**, not absolute fill level: `0.8` fires the request when the playhead is within the last 20% of the decoded window. Values outside `[0.0, 1.0]` are clamped.
- `sampleRate`/`channels` are **ignored** when `format` is `BufferType.auto` (the common case); they only matter for raw PCM formats (`f32le`, `s8`, `s16le`, `s32le`). `onAudioDuration` is called for PCM streams too.
- `addPullBufferDataStream` takes a named `offset`. `offset: 0` means "append the next sequential chunk" — always pass the real requested offset from the callback instead. Empty chunks are a no-op returning `PlayerErrors.noError`. Any error throws a `SoLoudCppException` subclass, so the return value is always `noError`. On Web, when it is called synchronously inside `onMoreDataIsNeeded`, the data is added right after the callback returns and an error is only logged.
- `getPullBufferTimeRange` returns the decoded window currently in the circular buffer as `Duration`s; the playhead normally sits near `startTime`. Use it to render a buffered-range indicator and to detect "smart" seeks (target inside the window = no refetch needed).
- Divergence from audioplayers/just_audio: there is no `setUrl`, no `player.durationStream`, no `AudioSource.uri`. Duration arrives asynchronously through the `onAudioDuration` callback (seconds as `double`), and position is polled with `SoLoud.instance.getPosition(handle)`. There is no `Source` object per URL — you are the transport.

## Pull vs push

flutter_soloud has two streaming APIs; pick by source shape:

- **Pull (`setPullBufferStream`)** — the source is seekable/addressable and you know its size: HTTP servers with Range support, local files, encrypted containers. Supports arbitrary seek and bounded memory. This skill.
- **Push (`setBufferStream` + `addAudioDataStream` + `setDataIsEnded`)** — data arrives sequentially with no random access: live radio, microphone feeds, procedurally generated audio. See the sibling skill `flutter-soloud-streaming`.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| A long remote track (podcast, audiobook, DJ mix, long game soundtrack) loaded with `loadUrl`, or a huge file with `loadMem`/`LoadMode.memory`, that must seek | `setPullBufferStream` + HTTP Range requests in `onMoreDataIsNeeded` | Playback starts without downloading the whole file, memory stays at `bufferSizeBytes`, and a seek fetches only the new range. Costs: you write the transport, the total size must be known, one voice per stream, no MP4/M4A |
| A seekable file fed sequentially into a push stream (`setBufferStream` + `addAudioDataStream`) | `setPullBufferStream` | A `preserved` push stream keeps everything it decoded in RAM and a `released` one cannot seek; a pull stream does both: bounded memory and seek |
| Live radio, a microphone feed or any source with no total size or no random access forced into the pull API | Push streaming (**flutter-soloud-streaming**) | Pull needs `audioSizeBytes` and addressable offsets; a live feed has neither |
| A pull stream written only to play a normal local music file on a native platform (no encryption, no custom transport) | `loadFile(path, mode: LoadMode.disk)` (**flutter-soloud-loading**) | One call, streams from disk with low RAM and allows several voices; keep the pull stream for web (no `loadFile` there), for multi-GB or encrypted files and for remote sources |
| Encrypted or packed audio (game data archive, DRM-like container) decrypted completely into memory before `loadMem` | A pull stream that decrypts or unpacks the requested range inside `onMoreDataIsNeeded` | Only the part being played is ever decrypted and held in memory; the engine tells you the byte offset it needs |
| Every `seek()` followed by a manual refetch, a stream reset or a new stream | Just call `SoLoud.instance.seek(handle, pos)`; check `getPullBufferTimeRange(source)` to know if the target is already decoded | The engine re-requests the right offset by itself; a seek inside the decoded window needs no network at all |
| A buffered/progress bar computed from downloaded bytes, or the duration guessed from the bitrate | `getPullBufferTimeRange` for the buffered window and `onAudioDuration` for the length (see `example/lib/pull_buffer/seek_bar.dart`) | Both values come from the engine's decoder; no byte or bitrate arithmetic in Dart |

## Traps

- **MP4 and M4A containers are NOT supported for streaming.** While MP4/M4A audio files are supported for whole-file loading (`loadFile`/`loadAsset`/`loadMem`), streaming chunks from MP4/M4A containers is not supported. Use streamable elementary formats (AAC ADTS, AC-3, E-AC-3, OGG, MP3, WAV, or raw PCM).
- **Linux requirement for AAC / AC-3 / E-AC-3.** On Linux, streaming these formats requires FFmpeg shared libraries (`libavcodec` and `libavformat`) installed on the host system (`sudo apt install ffmpeg libavcodec-extra`, `sudo pacman -S ffmpeg`, etc.). Without FFmpeg, feeding them throws `SoLoudAudioFormatNotSupportedCppException`. Core formats (MP3, WAV, FLAC, OGG) work without FFmpeg.
- **Web browser limitation for AC-3 / E-AC-3.** AC-3 and E-AC-3 streaming is **not supported on the Web platform in any browser**. Chrome and Firefox lack Dolby licensing, and Safari's WebCodecs engine rejects raw AC-3 elementary stream frames. AC-3 and E-AC-3 streaming is supported only on native platforms (iOS, macOS native, Android, Windows, and Linux with FFmpeg). On Web, feeding AC-3/E-AC-3 stream data fails with `PlayerErrors.audioFormatNotSupported` (`SoLoudAudioFormatNotSupportedCppException`). For cross-browser web streaming, use AAC (ADTS), Opus, MP3, FLAC, or raw PCM.
- **`audioSizeBytes` of 0 throws.** `setPullBufferStream` calls `SoLoudCppException.fromPlayerError(PlayerErrors.invalidParameter)` when it is 0. Do a HEAD request / stat the file first. A server that omits `Content-Length` and `Accept-Ranges: bytes` is a poor fit for pull.
- **Do not invent an end-of-stream call.** There is no `setDataIsEnded` for pull streams (that belongs to the push API). The stream ends automatically once sequential data reaches `audioSizeBytes`.
- **The callback can re-fire for the same offset and fire out of order.** Deduplicate in-flight requests (`_pendingOffsets.add(offset)` pattern) and guard `offset < 0 || offset >= audioSizeBytes` — the engine may probe the tail for Ogg duration. `addPullBufferDataStream` accepts out-of-order chunks precisely for this.
- **Seek invalidates your dedup state.** After an out-of-buffer `seek(handle, pos)`, the engine re-requests offsets you may have already served. Clear your "already fetched" set on such seeks, or the re-requested bytes never arrive and playback stalls silently. Seeks *inside* the current `getPullBufferTimeRange` window are free and need no refetch.
- **One voice per stream.** A pull `AudioSource` is tied to a single playing instance; create a new stream for a new voice. `SoLoudInvalidPullBufferStateCppException` / `SoLoudHashIsNotAPullBufferStreamCppException` mean you called a pull method on the wrong source or at the wrong time.
- **Web: replacing a stream.** Disposing the old pull source while the audio thread still processes it can crash/deadlock. The example works around this by leaving the old source in the engine and letting callbacks capture their own `currentSource`, dropping callbacks whose source is stale (`currentSource != _source`). Web also requires the server to send CORS headers for Range requests.
- **Chunk size is yours to choose.** The engine only tells you *where*, not *how much*. ~64 KB keeps HTTP overhead reasonable and the trigger firing; chunks larger than the whole file defeat the pull model (everything decodes in the first callback).
- **Playhead lives at the buffer's edge.** The decoded window slides forward; the playhead stays near `startTime`, not centered in a large pre-buffered region. Don't design UX around a just_audio-style bufferedPosition far ahead of playback.
- **Errors from `addPullBufferDataStream` throw.** It returns `PlayerErrors` but throws `SoLoudCppException` on any non-`noError` result (on Web, a call made synchronously inside `onMoreDataIsNeeded` only logs the error) — wrap feeds in try/catch and stop feeding a broken stream.
- **Ogg Opus with 3 to 8 channels is downmixed to stereo.**

## More depth

Full working demos in the plugin repo under `example/lib/pull_buffer/`:

- `http_range_stream.dart` — HTTP Range-request streaming with HEAD-based size discovery, offset dedup, smart-seek detection.
- `file_stream.dart` — streaming a huge local file/asset in chunks; shows the stale-callback guard pattern for web.
- `seek_bar.dart` — `PullBufferSeekBar` widget visualizing the sliding decoded window from `getPullBufferTimeRange`.

Reference doc: `docs/advanced/pull_buffer_streaming.mdx` (flutter_soloud_docs repo). Tests worth reading for edge cases: `example/tests/tests/pull_buffer_seek_test.dart`, `pull_buffer_in_buffer_seek_test.dart`, `pull_buffer_range_test.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

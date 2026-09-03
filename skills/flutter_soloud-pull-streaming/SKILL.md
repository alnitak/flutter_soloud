---
name: flutter_soloud-pull-streaming
version: 1
description: Teaches the pull-buffer streaming API of the flutter_soloud audio plugin — setPullBufferStream with its onMoreDataIsNeeded callback, addPullBufferDataStream with byte offsets, seek via engine re-requests, and bounded-memory playback of huge seekable sources (HTTP range requests, large files). Use when the user asks to stream a large remote/local audio file with seeking, play multi-GB audio without loading it into memory, or is deciding between push (setBufferStream) and pull streaming.
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
    format: BufferType.auto, // default; detects MP3/OGG Opus/OGG Vorbis/FLAC/WAV
    onAudioDuration: (seconds) {/* total duration is now known */},
    onMetadata: (metadata) {/* detected format, sample rate, channels */},
    onMoreDataIsNeeded: (offset) {
      const chunkSize = 64 * 1024;
      final end = (offset + chunkSize).clamp(0, totalBytes);
      if (offset < 0 || offset >= totalBytes) return;
      SoLoud.instance.addPullBufferDataStream(
        source,
        fetchRange(offset, end - offset),
        offset: offset,
      );
    },
  );

  final handle = SoLoud.instance.play(source);

  // Seeking is free: the engine re-requests data at the new offset.
  SoLoud.instance.seek(handle, const Duration(minutes: 2));

  // No end-of-stream call exists: when sequential data reaches
  // audioSizeBytes the engine ends the stream itself.
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

- `setPullBufferStream` returns an `AudioSource` synchronously (unlike `loadAsset`/`loadFile`, which are async) and throws `SoLoudCppException` on error. Play it with the usual synchronous `SoLoud.instance.play(source)`.
- `audioSizeBytes` is the total size of the **encoded** source and is mandatory upfront (server `Content-Length`, file length). It drives duration calculation and end-of-stream detection; for Ogg formats the engine also uses it to request the tail chunk for duration probing.
- `bufferTriggerPosition` is about how much decoded audio remains **ahead of the playhead**, not absolute fill level: `0.8` fires the request when the playhead is within the last 20% of the decoded window. Values outside `[0.0, 1.0]` are clamped.
- `sampleRate`/`channels` are **ignored** when `format` is `BufferType.auto` (the common case); they only matter for raw PCM formats (`f32le`, `s8`, `s16le`, `s32le`). `BufferType.opus` is deprecated — use `auto`.
- `addPullBufferDataStream` takes a named `offset`. `offset: 0` means "append the next sequential chunk" — always pass the real requested offset from the callback instead. Empty chunks are a no-op returning `PlayerErrors.noError`.
- `getPullBufferTimeRange` returns the decoded window currently in the circular buffer as `Duration`s; the playhead normally sits near `startTime`. Use it to render a buffered-range indicator and to detect "smart" seeks (target inside the window = no refetch needed).
- Divergence from audioplayers/just_audio: there is no `setUrl`, no `player.durationStream`, no `AudioSource.uri`. Duration arrives asynchronously through the `onAudioDuration` callback (seconds as `double`), and position is polled with `SoLoud.instance.getPosition(handle)`. There is no `Source` object per URL — you are the transport.

## Pull vs push

flutter_soloud has two streaming APIs; pick by source shape:

- **Pull (`setPullBufferStream`)** — the source is seekable/addressable and you know its size: HTTP servers with Range support, local files, encrypted containers. Supports arbitrary seek and bounded memory. This skill.
- **Push (`setBufferStream` + `addAudioDataStream` + `setDataIsEnded`)** — data arrives sequentially with no random access: live radio, microphone feeds, procedurally generated audio. See the sibling skill `flutter_soloud-streaming`.

## Traps

- **`audioSizeBytes` of 0 throws.** `setPullBufferStream` calls `SoLoudCppException.fromPlayerError(PlayerErrors.invalidParameter)` when it is 0. Do a HEAD request / stat the file first. A server that omits `Content-Length` and `Accept-Ranges: bytes` is a poor fit for pull.
- **Do not invent an end-of-stream call.** There is no `setDataIsEnded` for pull streams (that belongs to the push API). The stream ends automatically once sequential data reaches `audioSizeBytes`.
- **The callback can re-fire for the same offset and fire out of order.** Deduplicate in-flight requests (`_pendingOffsets.add(offset)` pattern) and guard `offset < 0 || offset >= audioSizeBytes` — the engine may probe the tail for Ogg duration. `addPullBufferDataStream` accepts out-of-order chunks precisely for this.
- **Seek invalidates your dedup state.** After an out-of-buffer `seek(handle, pos)`, the engine re-requests offsets you may have already served. Clear your "already fetched" set on such seeks, or the re-requested bytes never arrive and playback stalls silently. Seeks *inside* the current `getPullBufferTimeRange` window are free and need no refetch.
- **One voice per stream.** A pull `AudioSource` is tied to a single playing instance; create a new stream for a new voice. `PlayerErrors.invalidPullBufferState` / `hashIsNotAPullBufferStream` mean you called a pull method on the wrong source or at the wrong time.
- **Web: replacing a stream.** Disposing the old pull source while the audio thread still processes it can crash/deadlock. The example works around this by leaving the old source in the engine and letting callbacks capture their own `currentSource`, dropping callbacks whose source is stale (`currentSource != _source`). Web also requires the server to send CORS headers for Range requests.
- **Chunk size is yours to choose.** The engine only tells you *where*, not *how much*. ~64 KB keeps HTTP overhead reasonable and the trigger firing; chunks larger than the whole file defeat the pull model (everything decodes in the first callback).
- **Playhead lives at the buffer's edge.** The decoded window slides forward; the playhead stays near `startTime`, not centered in a large pre-buffered region. Don't design UX around a just_audio-style bufferedPosition far ahead of playback.
- **Errors from `addPullBufferDataStream` throw.** It returns `PlayerErrors` but throws `SoLoudCppException` on any non-`noError` result — wrap feeds in try/catch and stop feeding a broken stream.

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

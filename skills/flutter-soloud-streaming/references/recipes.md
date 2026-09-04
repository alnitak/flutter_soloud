# Push-stream recipes

Condensed from `example/lib/buffer_stream/` in this repo. All snippets assume
`await SoLoud.instance.init()` has already run.

## Web radio (icecast/shoutcast MP3 or Ogg)

Source: `example/lib/buffer_stream/web_radio.dart`. Uses `package:http` and
`BufferingType.released` so memory stays bounded on an endless feed.

```dart
import 'package:http/http.dart' as http;

AudioSource? source;
bool icyMetaIntSent = false;

Future<void> playUrl(String url) async {
  source = SoLoud.instance.setBufferStream(
    maxBufferSizeBytes: 1024 * 1024 * 200,
    bufferingType: BufferingType.released,
    bufferingTimeNeeds: 3,
    format: BufferType.auto,          // detects MP3 / Ogg-Opus / Ogg-Vorbis
    channels: Channels.stereo,
    onBuffering: (isBuffering, handle, time) {
      // update a "buffering..." indicator
    },
    onMetadata: (metadata) {
      // AudioMetadata — station name, current track, etc.
    },
  );
  SoLoud.instance.play(source!);      // synchronous

  final client = http.Client();
  final request = http.Request('GET', Uri.parse(url));
  // Ask the server to embed icy metadata in the stream.
  request.headers.addAll({'Icy-MetaData': '1'});
  final response = await client.send(request);
  // Note: follow 301/302 redirects yourself — package:http does not for
  // streamed requests.
  icyMetaIntSent = false;

  response.stream.listen(
    (data) {
      if (!icyMetaIntSent) {
        icyMetaIntSent = true;
        // Must happen before the first addAudioDataStream.
        SoLoud.instance.setBufferIcyMetaInt(
          source!,
          int.parse(response.headers['icy-metaint'] ?? '0'),
        );
      }
      try {
        SoLoud.instance.addAudioDataStream(source!, Uint8List.fromList(data));
      } on SoLoudStreamEndedAlreadyCppException {
        // Max buffer size was reached; stream is now ended. Reconnect or
        // reset instead of pushing more data.
      }
    },
    onDone: () => SoLoud.instance.setDataIsEnded(source!),
  );
}
```

Points that matter:

- `setBufferIcyMetaInt` needs the raw `icy-metaint` integer from the response
  headers and is only honored before the first chunk is added.
- Handle `SoLoudStreamEndedAlreadyCppException` around every
  `addAudioDataStream` — a stalled network plus a full buffer ends the stream.

## WebSocket PCM

Source: `example/lib/buffer_stream/websocket.dart` (needs the matching
`websocketd`+ffmpeg server, see https://github.com/alnitak/websocketd).
The format/sampleRate/channels must be agreed with the server out of band —
raw PCM carries no header.

```dart
final sound = SoLoud.instance.setBufferStream(
  maxBufferSizeBytes: 1024 * 1024 * 200,
  bufferingType: BufferingType.preserved,
  bufferingTimeNeeds: 3,
  sampleRate: 48000,
  channels: Channels.mono,
  format: BufferType.s16le,           // must match what the server sends
  onBuffering: (isBuffering, handle, time) { /* ... */ },
);

SoundHandle? handle;
var chunkCount = 0;

channel.stream.listen(
  (message) {
    SoLoud.instance.addAudioDataStream(
      sound,
      Uint8List.fromList(message as List<int>),
    );
    // Start playback on the first chunk; play() before any data also works —
    // the engine just buffers until bufferingTimeNeeds is satisfied.
    if (++chunkCount == 1) {
      handle = SoLoud.instance.play(sound);
    }
  },
  onDone: () => SoLoud.instance.setDataIsEnded(sound),
);
```

Useful controls shown in the demo:

- `SoLoud.instance.resetBufferStream(sound)` — drop everything buffered
  (e.g. when switching feeds on the same source).
- `SoLoud.instance.getBufferSize(sound)` — bytes currently buffered; the
  demo's `ui/buffer_widget.dart` polls it together with position
  (`getPosition(handle)` for `preserved`, `getStreamTimeConsumed(sound)`
  for `released`) on a `Timer.periodic`.

## Procedural PCM generation

Source: `example/lib/buffer_stream/generate.dart`. Generate samples (optionally
in an isolate — the demo marks the generator `@pragma('vm:entry-point')` and
passes the `AudioSource` in), push once, end immediately.

```dart
import 'dart:math';

Future<void> makeTone() async {
  final sound = SoLoud.instance.setBufferStream(
    maxBufferSizeBytes: 1024 * 1024,
    format: BufferType.s8,            // matches Int8List below
    channels: Channels.mono,
    sampleRate: 44100,
  );

  const frequency = 440.0;
  const duration = 2.0;               // seconds
  const sampleRate = 44100;
  final sampleCount = (sampleRate * duration).ceil();
  final audioData = Int8List(sampleCount);
  for (var i = 0; i < sampleCount; i++) {
    audioData[i] = (127 * sin(2 * pi * frequency * i / sampleRate)).toInt();
  }

  // Typed-data view: .buffer.asUint8List() matches the declared format.
  SoLoud.instance.addAudioDataStream(sound, audioData.buffer.asUint8List());
  SoLoud.instance.setDataIsEnded(sound);
  SoLoud.instance.play(sound);
}
```

Format/typed-data pairings used in the demo: `BufferType.s8` ↔ `Int8List`,
`s16le` ↔ `Int16List`, `f32le` ↔ `Float32List`. Always push
`typedList.buffer.asUint8List()` so the byte layout matches the declared
`BufferType`.

## Choosing the buffering type

| Need | Use |
|---|---|
| Endless feed (radio, live), bounded RAM | `BufferingType.released` |
| Seeking, looping, or replaying the stream | `BufferingType.preserved` |
| Multiple simultaneous handles of one stream | `BufferingType.preserved` |

Remember the released-mode consequences: single playback, no `seek`,
`getPosition` always returns 0 (use `getStreamTimeConsumed`), and the source
must be disposed manually when done (`autoDispose: true` on
`setBufferStream` handles this automatically once all handles finish).

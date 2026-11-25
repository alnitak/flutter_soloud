// ICY (Shoutcast) metadata in MP3 buffer streams, with the real engine. The
// stream is generated here: silent MPEG-1 Layer III frames with an ICY
// metadata block inserted every `icy-metaint` bytes. With
// setBufferIcyMetaInt the titles reach onMetadata and the metadata bytes are
// kept out of the audio (all the frames decode); setMp3BufferIcyMetaInt does
// the same; the value can be set after the first chunk; without it the
// titles are not parsed.
@Tags(['device'])
library;

import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// Bytes of one 128 kbps, 44.1 kHz MPEG-1 Layer III frame without padding.
const _frameBytes = 144 * 128000 ~/ 44100; // 417

/// Samples per MPEG-1 Layer III frame.
const _frameSamples = 1152;

/// [count] mono MP3 frames. Side info and main data are all zero, which
/// decodes as silence.
Uint8List _mp3Frames(int count) {
  final out = Uint8List(_frameBytes * count);
  for (var f = 0; f < count; f++) {
    out.setAll(f * _frameBytes, const [0xFF, 0xFB, 0x90, 0xC0]);
  }
  return out;
}

/// Expects [length] to be the duration of [count] frames (to the
/// microsecond rounding).
void _expectFrames(Duration length, int count) => expect(
  length.inMicroseconds,
  closeTo(count * _frameSamples * 1000000 / 44100, 2),
  reason: 'decoded $length, $count frames expected',
);

/// [audio] with an ICY metadata block after every [metaInt] bytes; block i
/// carries `metadata[i % metadata.length]` (an empty string gives a block of
/// length 0).
Uint8List _withIcy(Uint8List audio, int metaInt, List<String> metadata) {
  final out = BytesBuilder(copy: false);
  var block = 0;
  for (var p = 0; p < audio.length; p += metaInt) {
    if (p + metaInt > audio.length) {
      out.add(Uint8List.sublistView(audio, p));
      break;
    }
    out.add(Uint8List.sublistView(audio, p, p + metaInt));
    final text = utf8.encode(metadata[block++ % metadata.length]);
    final blocks = (text.length + 15) ~/ 16;
    out
      ..addByte(blocks)
      ..add(Uint8List(blocks * 16)..setAll(0, text));
  }
  return out.takeBytes();
}

String _title(String title) => "StreamTitle='$title';";

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  /// An auto-detecting buffer stream recording its metadata events.
  (AudioSource, List<Mp3Metadata>) newStream() {
    final events = <Mp3Metadata>[];
    final source = soloud.setBufferStream(
      format: BufferType.auto,
      bufferingTimeNeeds: 0.1,
      onMetadata: (m) => events.add(m.mp3Metadata!),
    );
    return (source, events);
  }

  /// Adds [bytes] in [chunk]-byte pieces.
  void feed(AudioSource source, Uint8List bytes, {int chunk = 4096}) {
    for (var p = 0; p < bytes.length; p += chunk) {
      soloud.addAudioDataStream(
        source,
        Uint8List.sublistView(bytes, p, math.min(p + chunk, bytes.length)),
      );
    }
  }

  /// [events] without consecutive repeats of the same title.
  List<String> titles(List<Mp3Metadata> events) {
    final out = <String>[];
    for (final e in events) {
      final t = e.artist.isEmpty ? e.title : '${e.artist} - ${e.title}';
      if (out.isEmpty || out.last != t) out.add(t);
    }
    return out;
  }

  /// Waits for the decoded length to settle after the end of the data.
  Future<Duration> settledLength(AudioSource source) async {
    var last = const Duration(seconds: -1);
    for (var i = 0; i < 50; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
      final now = soloud.getLength(source);
      if (now == last && now > Duration.zero) break;
      last = now;
    }
    return soloud.getLength(source);
  }

  for (final chunk in [97, 700, 16384]) {
    test('titles reach onMetadata and every frame decodes '
        '($chunk-byte chunks)', () async {
      final (source, events) = newStream();
      soloud.setBufferIcyMetaInt(source, 1000);
      feed(
        source,
        _withIcy(_mp3Frames(200), 1000, [
          _title('Artist A - One'),
          _title('Artist B - Two'),
        ]),
        chunk: chunk,
      );
      soloud.setDataIsEnded(source);

      _expectFrames(await settledLength(source), 200);
      expect(await waitUntil(() => titles(events).length >= 10), isTrue);
      final seen = titles(events);
      expect(seen.take(4), [
        'Artist A - One',
        'Artist B - Two',
        'Artist A - One',
        'Artist B - Two',
      ]);
      // 83 blocks: the last one (index 82) is the first title.
      expect(events.last.artist, 'Artist A');
      expect(events.last.title, 'One');
    });
  }

  test('a title without " - " is all title; StreamUrl is parsed', () async {
    final (source, events) = newStream();
    soloud.setBufferIcyMetaInt(source, 2000);
    feed(
      source,
      _withIcy(_mp3Frames(20), 2000, [
        "${_title('Just a title')}StreamUrl='https://example.com/x';",
      ]),
    );
    soloud.setDataIsEnded(source);
    expect(await waitUntil(() => events.isNotEmpty), isTrue);
    final m = events.firstWhere((e) => e.title.isNotEmpty);
    expect(m.title, 'Just a title');
    expect(m.artist, isEmpty);
    expect(m.streamUrl, 'https://example.com/x');
  });

  test('a repeated title is reported once', () async {
    final (source, events) = newStream();
    soloud.setBufferIcyMetaInt(source, 1000);
    feed(source, _withIcy(_mp3Frames(100), 1000, [_title('Same')]));
    soloud.setDataIsEnded(source);
    _expectFrames(await settledLength(source), 100);
    // One event for the title; the decoder may report its format once more
    // when it initializes, with the same title.
    expect(
      events.where((e) => e.title == 'Same').length,
      inInclusiveRange(1, 2),
    );
    expect(titles(events), ['Same']);
  });

  test('empty metadata blocks change nothing and are not audio', () async {
    final (source, events) = newStream();
    soloud.setBufferIcyMetaInt(source, 1000);
    feed(source, _withIcy(_mp3Frames(100), 1000, [_title('First'), '', '']));
    soloud.setDataIsEnded(source);
    _expectFrames(await settledLength(source), 100);
    expect(titles(events), ['First']);
  });

  test('UTF-8 titles are decoded', () async {
    final (source, events) = newStream();
    soloud.setBufferIcyMetaInt(source, 1000);
    feed(source, _withIcy(_mp3Frames(20), 1000, [_title('Café - Ñandú ♪')]));
    soloud.setDataIsEnded(source);
    expect(await waitUntil(() => events.isNotEmpty), isTrue);
    final m = events.firstWhere((e) => e.title.isNotEmpty);
    expect(m.artist, 'Café');
    expect(m.title, 'Ñandú ♪');
  });

  test('setMp3BufferIcyMetaInt (deprecated) does the same', () async {
    final (source, events) = newStream();
    // ignore: deprecated_member_use_from_same_package, deprecated_member_use
    soloud.setMp3BufferIcyMetaInt(source, 1000);
    feed(source, _withIcy(_mp3Frames(50), 1000, [_title('Old API')]));
    soloud.setDataIsEnded(source);
    _expectFrames(await settledLength(source), 50);
    expect(titles(events), ['Old API']);
  });

  test('can be set after the first chunk, before the first block', () async {
    final (source, events) = newStream();
    final bytes = _withIcy(_mp3Frames(200), 4000, [
      _title('A - One'),
      _title('B - Two'),
    ]);
    soloud.addAudioDataStream(source, Uint8List.sublistView(bytes, 0, 2000));
    await Future<void>.delayed(const Duration(milliseconds: 100));
    soloud.setBufferIcyMetaInt(source, 4000);
    feed(source, Uint8List.sublistView(bytes, 2000));
    soloud.setDataIsEnded(source);
    _expectFrames(await settledLength(source), 200);
    expect(titles(events).take(3), ['A - One', 'B - Two', 'A - One']);
  });

  test('without it the titles are not parsed', () async {
    final (source, events) = newStream();
    feed(source, _withIcy(_mp3Frames(50), 1000, [_title('Hidden')]));
    soloud.setDataIsEnded(source);
    expect(await waitUntil(() => events.isNotEmpty), isTrue);
    await Future<void>.delayed(const Duration(milliseconds: 200));
    expect(events.every((e) => e.title.isEmpty), isTrue);
  });

  test('a plain MP3 stream decodes fully after resetBufferStream', () async {
    final (source, _) = newStream();
    soloud.addAudioDataStream(source, _mp3Frames(10));
    await Future<void>.delayed(const Duration(milliseconds: 100));
    soloud.resetBufferStream(source);
    // 200 frames are 83400 bytes: past the 16000-byte default metaint.
    feed(source, _mp3Frames(200));
    soloud.setDataIsEnded(source);
    _expectFrames(await settledLength(source), 200);
  });

  test('a plain MP3 stream decodes fully without a reset', () async {
    final (source, _) = newStream();
    feed(source, _mp3Frames(200));
    soloud.setDataIsEnded(source);
    _expectFrames(await settledLength(source), 200);
  });
}

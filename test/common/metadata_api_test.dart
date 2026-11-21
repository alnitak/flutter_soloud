// The `onMetadata` callbacks of setBufferStream / setPullBufferStream, fed
// with streams generated here (WAV, MP3 with and without an ID3v2 tag), plus
// one Ogg Vorbis file from example/assets for the Ogg comment path. Runs the
// real engine.
@TestOn('vm')
@Tags(['device'])
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

/// A mono 16-bit WAV with [seconds] of a 440 Hz sine.
Uint8List _wav({double seconds = 1, int sampleRate = 22050}) {
  final frames = (seconds * sampleRate).round();
  final data = frames * 2;
  final b = ByteData(44 + data);
  void str(int o, String s) {
    for (var i = 0; i < s.length; i++) {
      b.setUint8(o + i, s.codeUnitAt(i));
    }
  }

  str(0, 'RIFF');
  b.setUint32(4, 36 + data, Endian.little);
  str(8, 'WAVE');
  str(12, 'fmt ');
  b
    ..setUint32(16, 16, Endian.little)
    ..setUint16(20, 1, Endian.little) // PCM
    ..setUint16(22, 1, Endian.little) // mono
    ..setUint32(24, sampleRate, Endian.little)
    ..setUint32(28, sampleRate * 2, Endian.little)
    ..setUint16(32, 2, Endian.little)
    ..setUint16(34, 16, Endian.little);
  str(36, 'data');
  b.setUint32(40, data, Endian.little);
  for (var i = 0; i < frames; i++) {
    b.setInt16(
      44 + i * 2,
      (sin(2 * pi * 440 * i / sampleRate) * 8000).round(),
      Endian.little,
    );
  }
  return b.buffer.asUint8List();
}

/// [count] MPEG-1 Layer III frames, mono, 44.1 kHz, 128 kbps (417 bytes
/// each). Side info and main data are all zero, which decodes as silence.
Uint8List _mp3Frames({int count = 120}) {
  const frameSize = 144 * 128000 ~/ 44100; // 417, no padding
  final out = Uint8List(frameSize * count);
  for (var f = 0; f < count; f++) {
    out.setAll(f * frameSize, const [0xFF, 0xFB, 0x90, 0xC0]);
  }
  return out;
}

/// An ID3v2.3 tag with ISO-8859-1 text frames.
Uint8List _id3v23(Map<String, String> frames) {
  final body = BytesBuilder();
  frames.forEach((id, text) {
    final payload = [0x00, ...latin1.encode(text)];
    body
      ..add(ascii.encode(id))
      ..add([
        (payload.length >> 24) & 0xff,
        (payload.length >> 16) & 0xff,
        (payload.length >> 8) & 0xff,
        payload.length & 0xff,
        0,
        0,
      ])
      ..add(payload);
  });
  final size = body.length;
  return Uint8List.fromList([
    ...ascii.encode('ID3'),
    3,
    0,
    0,
    // synchsafe size
    (size >> 21) & 0x7f,
    (size >> 14) & 0x7f,
    (size >> 7) & 0x7f,
    size & 0x7f,
    ...body.takeBytes(),
  ]);
}

/// Creates an auto-detecting buffer stream, feeds it [bytes] (in chunks), ends
/// it, and returns the first metadata event.
Future<AudioMetadata> _firstMetadata(Uint8List bytes) async {
  final events = <AudioMetadata>[];
  final first = Completer<AudioMetadata>();
  final source = SoLoud.instance.setBufferStream(
    format: BufferType.auto,
    bufferingTimeNeeds: 0.1,
    onMetadata: (m) {
      events.add(m);
      if (!first.isCompleted) first.complete(m);
    },
  );
  const chunk = 16 * 1024;
  for (var i = 0; i < bytes.length; i += chunk) {
    SoLoud.instance.addAudioDataStream(
      source,
      Uint8List.sublistView(bytes, i, min(i + chunk, bytes.length)),
    );
  }
  SoLoud.instance.setDataIsEnded(source);
  final metadata = await first.future.timeout(
    const Duration(seconds: 5),
    onTimeout: () => fail('onMetadata never fired'),
  );
  // Let any duplicate event arrive before disposing.
  await Future<void>.delayed(const Duration(milliseconds: 100));
  expect(events, hasLength(1), reason: 'metadata is reported once');
  await SoLoud.instance.disposeSource(source);
  return metadata;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final temp = Directory.systemTemp.createTempSync('soloud_metadata_api');
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => temp.path,
      );

  setUpAll(SoLoud.instance.init);
  tearDownAll(() {
    SoLoud.instance.deinit();
    temp.deleteSync(recursive: true);
  });
  tearDown(SoLoud.instance.disposeAllSources);

  test('WAV is detected and reported as such', () async {
    final m = await _firstMetadata(_wav());
    expect(m.detectedType, DetectedType.wav);
    // The FFI struct carries every format's block, so the others are present
    // but empty.
    expect(m.mp3Metadata!.title, isEmpty);
    expect(m.oggMetadata!.comments, isEmpty);
    expect(m.toString(), contains('WAV audio format'));
  });

  test('MP3 without tags reports the stream format', () async {
    final m = await _firstMetadata(_mp3Frames());
    expect(m.detectedType, DetectedType.mp3Stream);
    final mp3 = m.mp3Metadata!;
    expect(mp3.sampleRate, 44100);
    expect(mp3.channels, 1);
    expect(mp3.bitrate, 128000);
    expect(mp3.title, isEmpty);
    expect(mp3.artist, isEmpty);
  });

  test('MP3 with an ID3v2.3 tag reports its text frames', () async {
    final tag = _id3v23({
      'TIT2': 'Generated Title',
      'TPE1': 'Generated Artist',
      'TALB': 'Generated Album',
      'TYER': '2026',
      'TCON': 'Test',
      'TRCK': '3/9',
    });
    final m = await _firstMetadata(
      Uint8List.fromList([...tag, ..._mp3Frames()]),
    );
    expect(m.detectedType, DetectedType.mp3WithId3);
    final mp3 = m.mp3Metadata!;
    expect(mp3.title, 'Generated Title');
    expect(mp3.artist, 'Generated Artist');
    expect(mp3.album, 'Generated Album');
    expect(mp3.date, '2026');
    expect(mp3.genre, 'Test');
    expect(mp3.track, '3/9');
    expect(mp3.sampleRate, 44100);
    expect(mp3.channels, 1);
    expect(m.toString(), contains('Title: Generated Title'));
  });

  Uint8List? asset(String name) {
    final file = File('example/assets/audio/$name');
    return file.existsSync() ? file.readAsBytesSync() : null;
  }

  test('Ogg Vorbis reports comments and stream info', () async {
    final bytes = asset('sample-vorbis.ogg');
    if (bytes == null) return markTestSkipped('sample-vorbis.ogg not found');
    final m = await _firstMetadata(bytes);
    expect(m.detectedType, DetectedType.oggVorbis);
    final ogg = m.oggMetadata!;
    expect(ogg.commentsCount, ogg.comments.length);
    expect(ogg.comments.keys.map((k) => k.toLowerCase()), contains('encoder'));
    expect(ogg.vorbisInfo.rate, 44100);
    expect(ogg.vorbisInfo.channels, 2);
    expect(m.toString(), contains('Vorbis Info'));
  });

  // Regression for S8: getMetadata() did not copy vc.vendor, so the vendor
  // was always empty for Vorbis (Opus set it).
  test('Ogg Vorbis reports the vendor string', () async {
    final bytes = asset('sample-vorbis.ogg');
    if (bytes == null) return markTestSkipped('sample-vorbis.ogg not found');
    final m = await _firstMetadata(bytes);
    expect(m.oggMetadata!.vendor, isNotEmpty);
  });

  test('Ogg Opus reports vendor, comments and stream info', () async {
    final bytes = asset('sample-OPUS.opus');
    if (bytes == null) return markTestSkipped('sample-OPUS.opus not found');
    final m = await _firstMetadata(bytes);
    expect(m.detectedType, DetectedType.oggOpus);
    final ogg = m.oggMetadata!;
    expect(ogg.vendor, isNotEmpty);
    expect(ogg.commentsCount, ogg.comments.length);
    expect(ogg.comments.keys.map((k) => k.toLowerCase()), contains('encoder'));
    expect(ogg.opusInfo.channels, 2);
    expect(ogg.opusInfo.inputSampleRate, 48000);
  });

  test('a PCM buffer stream never reports metadata', () async {
    var called = false;
    final source = SoLoud.instance.setBufferStream(
      sampleRate: 22050,
      onMetadata: (_) => called = true,
    );
    final pcm = Uint8List.sublistView(_wav(), 44);
    SoLoud.instance
      ..addAudioDataStream(source, pcm)
      ..setDataIsEnded(source);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    expect(called, isFalse);
  });

  test('a stream without onMetadata still decodes', () async {
    final source = SoLoud.instance.setBufferStream(format: BufferType.auto);
    SoLoud.instance
      ..addAudioDataStream(source, _wav())
      ..setDataIsEnded(source);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    expect(SoLoud.instance.getLength(source), greaterThan(Duration.zero));
  });

  test('pull buffer stream reports metadata of the data it pulls', () async {
    final wav = _wav(seconds: 2);
    final metadata = Completer<AudioMetadata>();
    final source = SoLoud.instance.setPullBufferStream(
      audioSizeBytes: wav.length,
      onMetadata: (m) {
        if (!metadata.isCompleted) metadata.complete(m);
      },
    );
    SoLoud.instance.addPullBufferDataStream(source, wav);
    final m = await metadata.future.timeout(
      const Duration(seconds: 5),
      onTimeout: () => fail('onMetadata never fired'),
    );
    expect(m.detectedType, DetectedType.wav);
  });

  test('the callback is dropped with its source', () async {
    // After disposeSource the native side must not call into a closed
    // NativeCallable; feeding a new stream afterwards still works.
    final first = SoLoud.instance.setBufferStream(
      format: BufferType.auto,
      onMetadata: (_) {},
    );
    await SoLoud.instance.disposeSource(first);
    final m = await _firstMetadata(_wav());
    expect(m.detectedType, DetectedType.wav);
  });
}

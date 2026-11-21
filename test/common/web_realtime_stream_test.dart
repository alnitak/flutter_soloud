// Realtime streams and native metadata on the web, with the real WASM module:
//   * realtime_stream_isolate_web.dart: there are no isolates, so a feeder
//     started with useIsolate: true runs on the event loop
//     (isUsingIsolate == false), fills the ring, its emitToUi events arrive,
//     and what it feeds is heard on the mixer output; pushFloat32 and
//     pushInt16 from Dart write into the WASM ring and are consumed;
//   * native_metadata_web.dart: metadata strings the engine parsed into WASM
//     memory (an ID3v2.4 tag in front of an MP3 fed to a buffer stream) come
//     back as UTF-8, also outside ASCII and at the field length limit
//     (MAX_STRING_LENGTH = 1024 bytes in metadata_ffi.h, NUL included).
//
// Browser only: ./test/support/web_tests.sh test/common/web_realtime_stream_test.dart [--wasm]
@TestOn('browser')
@Tags(['device'])
library;

import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

const _rate = 44100;

Float32List _sine(int frames, {double hz = 440, int channels = 2}) {
  final out = Float32List(frames * channels);
  for (var f = 0; f < frames; f++) {
    final v = 0.5 * math.sin(2 * math.pi * hz * f / _rate);
    for (var c = 0; c < channels; c++) {
      out[f * channels + c] = v;
    }
  }
  return out;
}

/// Phase of the feeder's sine, kept across calls.
var _phase = 0;

int _feed(Float32List out, int frames, int hz, void Function(Object?) emit) {
  for (var f = 0; f < frames; f++) {
    final v = 0.5 * math.sin(2 * math.pi * hz * (_phase + f) / _rate);
    out[f * 2] = v;
    out[f * 2 + 1] = v;
  }
  _phase += frames;
  emit(frames);
  return frames;
}

List<int> _syncsafe(int v) => [
  (v >> 21) & 0x7f,
  (v >> 14) & 0x7f,
  (v >> 7) & 0x7f,
  v & 0x7f,
];

/// An ID3v2.4 tag with UTF-8 TIT2 / TPE1 frames.
Uint8List _id3(String title, String artist) {
  List<int> frame(String id, String text) {
    final body = [3, ...utf8.encode(text)];
    return [...ascii.encode(id), ..._syncsafe(body.length), 0, 0, ...body];
  }

  final frames = [...frame('TIT2', title), ...frame('TPE1', artist)];
  return Uint8List.fromList([
    ...ascii.encode('ID3'),
    4,
    0,
    0,
    ..._syncsafe(frames.length),
    ...frames,
  ]);
}

void main() {
  setUpEngineTests(globalVolume: 1);
  final soloud = SoLoud.instance;
  late Uint8List mp3;

  setUpAll(() async {
    final channel = spawnHybridUri('../support/web_hybrid/asset_bytes.dart');
    channel.sink.add(['sample-MP3.mp3']);
    final map = (await channel.stream.first as Map).cast<String, Object?>();
    mp3 = Uint8List.fromList([
      // Numbers cross the channel as doubles under dart2wasm.
      for (final v in map['sample-MP3.mp3']! as List) (v as num).toInt(),
    ]);
    await channel.sink.close();
  });

  group('realtime', () {
    test(
      'pushFloat32 and pushInt16 write into the ring and are consumed',
      () async {
        final rt = soloud.setRealtimeStream(
          sampleRate: _rate,
          bufferFrames: 8192,
        );
        final h = soloud.play(rt.source);
        rt.bindActiveVoice(h);
        final pushed = rt.pushFloat32(_sine(4096));
        expect(pushed, 4096);
        final ints = Int16List.fromList([
          for (final s in _sine(2048)) (s * 32767).round(),
        ]);
        expect(rt.pushInt16(ints), 2048);
        expect(rt.availableToRead, greaterThan(0));
        await waitUntil(() => rt.availableToRead == 0);
        expect(rt.availableToRead, 0, reason: 'the engine consumed the ring');
      },
    );

    test('the feeder runs on the event loop and is heard', () async {
      final rt = soloud.setRealtimeStream(sampleRate: _rate);
      final h = soloud.play(rt.source);
      rt.bindActiveVoice(h);
      var uiEvents = 0;
      _phase = 0;
      final feeder = await rt.startFloat32Feeder<int>(
        initialState: 1000,
        onFeed: _feed,
        onUiEvent: (_) => uiEvents++,
        chunkFrames: 512,
      );
      expect(feeder.isUsingIsolate, isFalse);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.2), reason: 'the fed sine is heard');
      expect(out.frequency(0), closeTo(1000, 20));
      expect(uiEvents, greaterThan(0), reason: 'emitToUi reaches the UI');
      final underruns = rt.underrunCount;
      await Future<void>.delayed(const Duration(milliseconds: 500));
      expect(rt.underrunCount - underruns, lessThan(5));
      await feeder.dispose();
    });
  });

  group('metadata strings from WASM memory', () {
    Future<Mp3Metadata> metadataOf(String title, String artist) async {
      final events = <Mp3Metadata>[];
      final source = soloud.setBufferStream(
        format: BufferType.auto,
        bufferingTimeNeeds: 0.1,
        onMetadata: (m) {
          if (m.mp3Metadata != null) events.add(m.mp3Metadata!);
        },
      );
      final bytes = Uint8List.fromList([..._id3(title, artist), ...mp3]);
      for (var p = 0; p < bytes.length; p += 8192) {
        soloud.addAudioDataStream(
          source,
          Uint8List.sublistView(bytes, p, math.min(p + 8192, bytes.length)),
        );
      }
      soloud.setDataIsEnded(source);
      await waitUntil(() => events.isNotEmpty);
      expect(events, isNotEmpty, reason: 'onMetadata was called');
      return events.first;
    }

    test('non-ASCII title and artist decode as UTF-8', () async {
      final m = await metadataOf('Café del Mar – Ünïcödé', 'Sigur Rós 🎵 東京');
      expect(m.title, 'Café del Mar – Ünïcödé');
      expect(m.artist, 'Sigur Rós 🎵 東京');
    });

    test('a title of exactly 1023 bytes comes back whole', () async {
      // 341 three-byte characters = 1023 bytes, the field holds 1023 + NUL.
      final title = '東' * 341;
      final m = await metadataOf(title, 'a');
      expect(utf8.encode(m.title).length, 1023);
      expect(m.title, title);
    });

    test('a longer title is cut without breaking the string', () async {
      final title = 'é' * 700; // 1400 bytes
      final m = await metadataOf(title, 'b');
      final n = utf8.encode(m.title).length;
      expect(n, inInclusiveRange(1, 1023));
      expect(
        m.title.contains('�'),
        isFalse,
        reason: 'cut in the middle of a character',
      );
      expect(title.startsWith(m.title), isTrue);
    });

    test('an empty title is empty', () async {
      final m = await metadataOf('', 'c');
      expect(m.title, isEmpty);
      expect(m.artist, 'c');
    });
  });
}

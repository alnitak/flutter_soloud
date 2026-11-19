// SoundHash: the extension type over int that identifies a loaded sound.
// The pure-Dart cases need no device; the "engine" group starts SoLoud to
// check that the hashes it hands out are unique, and is tagged `device`.
@TestOn('vm')
library;

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart'
    show TestDefaultBinaryMessengerBinding, TestWidgetsFlutterBinding;
import 'package:test/test.dart';

/// A minimal mono 16-bit WAV of [frames] silent frames.
Uint8List _wav({int frames = 441, int sampleRate = 44100}) {
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
    ..setUint16(20, 1, Endian.little)
    ..setUint16(22, 1, Endian.little)
    ..setUint32(24, sampleRate, Endian.little)
    ..setUint32(28, sampleRate * 2, Endian.little)
    ..setUint16(32, 2, Endian.little)
    ..setUint16(34, 16, Endian.little);
  str(36, 'data');
  b.setUint32(40, data, Endian.little);
  return b.buffer.asUint8List();
}

void main() {
  group('SoundHash value semantics', () {
    test('equality and hashCode follow the wrapped int', () {
      const a = SoundHash(42);
      const b = SoundHash(42);
      const c = SoundHash(43);
      expect(a, b);
      expect(a == b, isTrue);
      expect(a == c, isFalse);
      expect(a.hashCode, b.hashCode);
      expect(a.hashCode, 42.hashCode);
      expect(a.hash, 42);
      expect(<SoundHash>{a, c, SoundHash.fromValue(42)}, hasLength(2));
      expect(<SoundHash, String>{a: 'x'}[b], 'x');
    });

    test('a constructed hash is valid', () {
      expect(const SoundHash(1).isValid, isTrue);
      expect(const SoundHash(0x7fffffff).isValid, isTrue);
    });

    test('SoundHash(0) is rejected by an assert', () {
      // ignore: prefer_const_constructors
      expect(() => SoundHash(0), throwsA(isA<AssertionError>()));
    });

    test('SoundHash.invalid wraps 0 and is not valid', () {
      const invalid = SoundHash.invalid();
      expect(invalid.hash, 0);
      expect(invalid.isValid, isFalse);
      expect(invalid, const SoundHash.invalid());
      expect(invalid == const SoundHash(1), isFalse);
    });

    test('SoundHash.fromValue maps 0 and negatives to invalid', () {
      expect(SoundHash.fromValue(0), const SoundHash.invalid());
      expect(SoundHash.fromValue(-1), const SoundHash.invalid());
      expect(SoundHash.fromValue(-0x80000000).isValid, isFalse);
      expect(SoundHash.fromValue(7), const SoundHash(7));
      expect(SoundHash.fromValue(7).isValid, isTrue);
    });
  });

  group('SoundHash.random', () {
    test('is always valid and fits a signed 32-bit int', () {
      for (var i = 0; i < 10000; i++) {
        final hash = SoundHash.random();
        expect(hash.isValid, isTrue);
        expect(hash.hash, inInclusiveRange(1, (1 << 31) - 1));
      }
    });

    test('is practically unique', () {
      // 2000 draws from ~2^31 values: the chance of even one collision is
      // about 0.1%, so allow a couple instead of being flaky.
      final hashes = {for (var i = 0; i < 2000; i++) SoundHash.random()};
      expect(hashes.length, greaterThanOrEqualTo(1998));
    });
  });

  group('engine-issued hashes', tags: ['device'], () {
    late Directory temp;
    setUpAll(() async {
      TestWidgetsFlutterBinding.ensureInitialized();
      temp = Directory.systemTemp.createTempSync('soloud_sound_hash');
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(
            const MethodChannel('plugins.flutter.io/path_provider'),
            (call) async => temp.path,
          );
      await SoLoud.instance.init();
    });
    tearDownAll(() {
      SoLoud.instance.deinit();
      temp.deleteSync(recursive: true);
    });
    tearDown(SoLoud.instance.disposeAllSources);

    test('generated sources each get a distinct valid hash', () async {
      final sources = [
        for (var i = 0; i < 20; i++)
          await SoLoud.instance.loadWaveform(WaveForm.sin, false, 1, 0),
      ];
      final hashes = sources.map((s) => s.soundHash).toSet();
      expect(hashes, hasLength(sources.length));
      expect(hashes.every((h) => h.isValid), isTrue);
    });

    test('loadMem with different names gives distinct hashes', () async {
      final bytes = _wav();
      final a = await SoLoud.instance.loadMem('hash-a.wav', bytes);
      final b = await SoLoud.instance.loadMem('hash-b.wav', bytes);
      expect(a.soundHash.isValid, isTrue);
      expect(b.soundHash.isValid, isTrue);
      expect(a.soundHash, isNot(b.soundHash));
    });

    test('loading the same name twice still gives a new hash', () async {
      // Player::registerSound: on a name-hash collision it loads a new
      // instance under a fresh random hash (and reports fileAlreadyLoaded,
      // which Dart only logs), so the two sources stay independent.
      final bytes = _wav();
      final a = await SoLoud.instance.loadMem('hash-same.wav', bytes);
      final b = await SoLoud.instance.loadMem('hash-same.wav', bytes);
      expect(b.soundHash.isValid, isTrue);
      expect(b.soundHash, isNot(a.soundHash));
      await SoLoud.instance.disposeSource(a);
      expect(SoLoud.instance.activeSounds, contains(b));
      expect(SoLoud.instance.activeSounds, isNot(contains(a)));
    });

    test('a loaded hash differs from generated ones', () async {
      final loaded = await SoLoud.instance.loadMem('hash-c.wav', _wav());
      final generated = await SoLoud.instance.loadWaveform(
        WaveForm.saw,
        false,
        1,
        0,
      );
      expect(loaded.soundHash, isNot(generated.soundHash));
    });
  });
}

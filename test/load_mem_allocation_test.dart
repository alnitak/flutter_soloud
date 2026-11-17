// loadMem() copies the encoded bytes into native memory for the native
// loader, which decodes them (memory mode) or keeps a copy of its own to
// stream from (disk mode). The copy Dart makes must not outlive the call:
// repeated loads, each disposed, must not grow the process.
//
// This calls the real native exports through the @Native bindings (the
// package's build hook builds the library for the host) on an initialized
// engine (miniaudio falls back to its null backend without a device), and
// measures the resident set. test/load_mem_ownership_test.cpp checks the
// native side of the contract.
@TestOn('vm')
library;

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/bindings_player_ffi.dart';
import 'package:flutter_soloud/src/bindings/flutter_soloud_ffigen.dart'
    as native;
import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

const int _mb = 1024 * 1024;

/// A 16-bit mono 44.1 kHz PCM WAV of [frames] frames of a tone.
Uint8List _wav(int frames) {
  final bytes = Uint8List(44 + frames * 2);
  final header = ByteData.sublistView(bytes, 0, 44);
  void ascii(int at, String s) {
    for (var i = 0; i < s.length; i++) {
      header.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  ascii(0, 'RIFF');
  header.setUint32(4, 36 + frames * 2, Endian.little);
  ascii(8, 'WAVEfmt ');
  header
    ..setUint32(16, 16, Endian.little)
    ..setUint16(20, 1, Endian.little)
    ..setUint16(22, 1, Endian.little)
    ..setUint32(24, 44100, Endian.little)
    ..setUint32(28, 44100 * 2, Endian.little)
    ..setUint16(32, 2, Endian.little)
    ..setUint16(34, 16, Endian.little);
  ascii(36, 'data');
  header.setUint32(40, frames * 2, Endian.little);
  final samples = Int16List.sublistView(bytes, 44);
  for (var i = 0; i < frames; i++) {
    final phase = i % 200;
    samples[i] = (phase < 100 ? phase : 200 - phase) * 100 - 5000;
  }
  return bytes;
}

void main() {
  final bindings = FlutterSoLoudFfi();

  setUpAll(() {
    native.prepareEngineInit(FlutterSoLoudFfi.noEngineId);
    final result = native.initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    expect(result, native.PlayerErrors.noError, reason: 'engine init');
  });

  tearDownAll(native.dispose);

  for (final mode in LoadMode.values) {
    test('loadMem in $mode mode frees its copy of the input', () {
      final wav = _wav(8 * _mb); // 16 MB of PCM
      var n = 0;
      void loadAndDispose() {
        final r = bindings.loadMem('$mode-${n++}', wav, mode);
        expect(r.error, PlayerErrors.noError);
        bindings.disposeSound(r.soundHash);
      }

      loadAndDispose();
      final before = ProcessInfo.currentRss;
      for (var i = 0; i < 12; i++) {
        loadAndDispose();
      }
      final growth = ProcessInfo.currentRss - before;
      expect(
        growth,
        lessThan(64 * _mb),
        reason:
            '12 loads of a 16 MB file, each disposed, grew the process by '
            '${growth ~/ _mb} MB: a copy of the input leaks',
      );
    });
  }
}

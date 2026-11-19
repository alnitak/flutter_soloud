// FlutterSoLoudFfi.readSamplesFromMem() and readSamplesFromFile() must
// free the native memory they use: the copy of the input file, the file
// name and the samples buffer.
@TestOn('vm')
library;

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_test/flutter_test.dart';

/// A mono 16-bit 44.1 kHz WAV file of [frames] frames of a ramp.
Uint8List _wav(int frames) {
  final dataBytes = frames * 2;
  final b = ByteData(44 + dataBytes);
  void tag(int at, String s) {
    for (var i = 0; i < 4; i++) {
      b.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  tag(0, 'RIFF');
  b.setUint32(4, 36 + dataBytes, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  b
    ..setUint32(16, 16, Endian.little)
    ..setUint16(20, 1, Endian.little)
    ..setUint16(22, 1, Endian.little)
    ..setUint32(24, 44100, Endian.little)
    ..setUint32(28, 88200, Endian.little)
    ..setUint16(32, 2, Endian.little)
    ..setUint16(34, 16, Endian.little);
  tag(36, 'data');
  b.setUint32(40, dataBytes, Endian.little);
  for (var i = 0; i < frames; i++) {
    b.setInt16(44 + i * 2, (i % 200) * 100 - 10000, Endian.little);
  }
  return b.buffer.asUint8List();
}

/// How much the resident set grows, in MiB, over [calls] calls of [read].
double _rssGrowthMiB(int calls, void Function() read) {
  read(); // Warm up the allocators.
  final before = ProcessInfo.currentRss;
  for (var i = 0; i < calls; i++) {
    read();
  }
  return (ProcessInfo.currentRss - before) / (1024 * 1024);
}

void main() {
  // An 8 MiB file.
  final wav = _wav(4 * 1024 * 1024);

  test('readSamplesFromMem frees the copy of the file', () {
    final ffi = SoLoudController().soLoudFFI;
    final samples = ffi.readSamplesFromMem(wav, 256);
    expect(samples, hasLength(256));
    expect(samples.any((s) => s != 0), isTrue);

    // Leaking the copy would take 8 MiB a call.
    final grown = _rssGrowthMiB(40, () => ffi.readSamplesFromMem(wav, 256));
    expect(grown, lessThan(32), reason: 'native memory is leaked');
  });

  test('readSamplesFromFile frees the samples buffer', () {
    final ffi = SoLoudController().soLoudFFI;
    final dir = Directory.systemTemp.createTempSync('read_samples_test');
    addTearDown(() => dir.deleteSync(recursive: true));
    final file = File('${dir.path}/ramp.wav')..writeAsBytesSync(wav);

    final samples = ffi.readSamplesFromFile(file.path, 256);
    expect(samples, hasLength(256));
    expect(samples.any((s) => s != 0), isTrue);

    // Leaking the samples buffer would take 256 KiB a call. The copy
    // returned is small enough not to pile up before it is collected.
    const n = 16 * 1024;
    final grown = _rssGrowthMiB(
      400,
      () => ffi.readSamplesFromFile(file.path, n),
    );
    expect(grown, lessThan(32), reason: 'native memory is leaked');
  });
}

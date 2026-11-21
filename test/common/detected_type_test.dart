import 'dart:io';

import 'package:flutter_soloud/src/bindings/native_metadata_ffi.dart';
import 'package:flutter_soloud/src/metadata.dart';
import 'package:test/test.dart';

/// Parses the `DetectedTypeFFI` enumerators of `src/audiobuffer/metadata_ffi.h`
/// into an ordered list so the Dart enum can be verified against the C one.
List<String> _nativeDetectedTypes() {
  final source = File('src/audiobuffer/metadata_ffi.h').readAsStringSync();
  final body = RegExp(
    r'typedef enum\s*\{(.*?)\}\s*DetectedTypeFFI;',
    dotAll: true,
  ).firstMatch(source);
  expect(
    body,
    isNotNull,
    reason: 'Could not find the DetectedTypeFFI enum in metadata_ffi.h',
  );

  final lines = body!.group(1)!.split('\n');
  final entries = <String>[];
  for (final rawLine in lines) {
    final line = rawLine.trim().replaceAll(',', '');
    if (line.isNotEmpty && !line.startsWith('//')) {
      entries.add(line);
    }
  }
  return entries;
}

void main() {
  group('DetectedType and DetectedTypeFFI synchronization', () {
    test('C++ DetectedTypeFFI matches Dart NativeDetectedType indices', () {
      final nativeTypes = _nativeDetectedTypes();
      expect(nativeTypes.length, NativeDetectedType.values.length);

      for (var i = 0; i < nativeTypes.length; i++) {
        final cName = nativeTypes[i];
        final dartNative = NativeDetectedType.values[i];
        expect(dartNative.name, cName);
        expect(dartNative.value, i);
      }
    });

    test('Dart DetectedType fromInt and toString coverage', () {
      expect(DetectedType.fromInt(0), DetectedType.unknown);
      expect(DetectedType.fromInt(1), DetectedType.oggOpus);
      expect(DetectedType.fromInt(2), DetectedType.oggVorbis);
      expect(DetectedType.fromInt(3), DetectedType.oggFlac);
      expect(DetectedType.fromInt(4), DetectedType.mp3WithId3);
      expect(DetectedType.fromInt(5), DetectedType.mp3Stream);
      expect(DetectedType.fromInt(6), DetectedType.wav);
      expect(DetectedType.fromInt(7), DetectedType.m4a);
      expect(DetectedType.fromInt(8), DetectedType.aac);
      expect(DetectedType.fromInt(9), DetectedType.ac3);
      expect(DetectedType.fromInt(10), DetectedType.eac3);
      expect(DetectedType.fromInt(99), DetectedType.unknown);

      expect(DetectedType.unknown.toString(), 'Unknown');
      expect(DetectedType.oggOpus.toString(), 'Ogg Opus');
      expect(DetectedType.oggVorbis.toString(), 'Ogg Vorbis');
      expect(DetectedType.oggFlac.toString(), 'Ogg FLAC');
      expect(DetectedType.mp3WithId3.toString(), 'MP3 with ID3');
      expect(DetectedType.mp3Stream.toString(), 'MP3 Stream');
      expect(DetectedType.wav.toString(), 'WAV');
      expect(DetectedType.m4a.toString(), 'M4A');
      expect(DetectedType.aac.toString(), 'AAC');
      expect(DetectedType.ac3.toString(), 'AC3');
      expect(DetectedType.eac3.toString(), 'EAC3');
    });

    test('NativeDetectedType toDart() maps all values correctly', () {
      expect(NativeDetectedType.UNKNOWN.toDart(), DetectedType.unknown);
      expect(NativeDetectedType.OGG_OPUS.toDart(), DetectedType.oggOpus);
      expect(NativeDetectedType.OGG_VORBIS.toDart(), DetectedType.oggVorbis);
      expect(NativeDetectedType.OGG_FLAC.toDart(), DetectedType.oggFlac);
      expect(NativeDetectedType.MP3_WITH_ID3.toDart(), DetectedType.mp3WithId3);
      expect(NativeDetectedType.MP3_STREAM.toDart(), DetectedType.mp3Stream);
      expect(NativeDetectedType.WAV.toDart(), DetectedType.wav);
      expect(NativeDetectedType.M4A.toDart(), DetectedType.m4a);
      expect(NativeDetectedType.AAC.toDart(), DetectedType.aac);
      expect(NativeDetectedType.AC3.toDart(), DetectedType.ac3);
      expect(NativeDetectedType.EAC3.toDart(), DetectedType.eac3);
    });
  });
}

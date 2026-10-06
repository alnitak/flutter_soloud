import 'dart:io';

import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

/// Parses the `BufferType` enum values of `src/enums.h`
/// into an ordered list so the Dart enum can be verified against the C one.
List<String> _nativeBufferTypes() {
  final source = File('src/enums.h').readAsStringSync();
  final body = RegExp(
    r'typedef enum BufferType\s*\{(.*?)\}\s*BufferType_t;',
    dotAll: true,
  ).firstMatch(source);
  expect(
    body,
    isNotNull,
    reason: 'Could not find the BufferType enum in src/enums.h',
  );

  final lines = body!.group(1)!.split('\n');
  final entries = <String>[];
  for (final rawLine in lines) {
    final line = rawLine.trim().split('=').first.trim().replaceAll(',', '');
    if (line.isNotEmpty && !line.startsWith('//')) {
      entries.add(line);
    }
  }
  return entries;
}

void main() {
  group('BufferType synchronization', () {
    test('BufferType value corresponds to its position', () {
      expect(BufferType.values.length, 5);
      for (final type in BufferType.values) {
        expect(
          type.value,
          BufferType.values.indexOf(type),
          reason:
              'The value of $type is ${type.value} '
              'but its position in the BufferType enum is '
              '${BufferType.values.indexOf(type)}.',
        );
      }
    });

    test('C++ BufferType matches Dart BufferType indices', () {
      final nativeTypes = _nativeBufferTypes();
      expect(nativeTypes.length, BufferType.values.length);
      expect(nativeTypes, [
        'PCM_F32LE',
        'PCM_S8',
        'PCM_S16LE',
        'PCM_S32LE',
        'AUTO',
      ]);
      expect(BufferType.auto.value, 4);
    });

    test('BufferType toString representation', () {
      expect(BufferType.f32le.toString(), 'Little Endian Float 32-bit');
      expect(BufferType.s8.toString(), 'Signed 8-bit');
      expect(BufferType.s16le.toString(), 'Little Endian Signed 16-bit');
      expect(BufferType.s32le.toString(), 'Little Endian Signed 32-bit');
      expect(BufferType.auto.toString(), 'Auto Detect Audio Format');
    });
  });
}

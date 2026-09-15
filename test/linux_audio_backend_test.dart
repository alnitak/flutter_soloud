import 'dart:io';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:test/test.dart';

void main() {
  test('LinuxAudioBackend value corresponds to its position', () {
    for (final backend in LinuxAudioBackend.values) {
      expect(
        backend.value,
        LinuxAudioBackend.values.indexOf(backend),
        reason:
            'The value of $backend is ${backend.value} but its position in the '
            'LinuxAudioBackend enum is '
            '${LinuxAudioBackend.values.indexOf(backend)}. The native side '
            'expects the C enum ordinal, so the two must line up.',
      );
    }
  });

  test('LinuxAudioBackend fromValue maps correctly', () {
    expect(LinuxAudioBackend.fromValue(0), LinuxAudioBackend.auto_);
    expect(LinuxAudioBackend.fromValue(1), LinuxAudioBackend.alsa);
    expect(LinuxAudioBackend.fromValue(2), LinuxAudioBackend.pulseAudio);
    expect(LinuxAudioBackend.fromValue(3), LinuxAudioBackend.jack);
    expect(LinuxAudioBackend.fromValue(99), LinuxAudioBackend.auto_);
  });

  test('LinuxAudioBackend is in sync with C++ LinuxAudioBackend in src/enums.h', () {
    final header = File('src/enums.h').readAsStringSync();
    final block = RegExp(
      r'typedef enum LinuxAudioBackend \{(.*?)\}',
      dotAll: true,
    ).firstMatch(header);
    expect(
      block,
      isNotNull,
      reason: 'could not find `typedef enum LinuxAudioBackend` in src/enums.h',
    );

    final names = RegExp(
      r'^\s*(linuxBackend[A-Za-z]+)\s*=\s*(\d+)',
      multiLine: true,
    ).allMatches(block!.group(1)!).toList();

    expect(
      names.length,
      LinuxAudioBackend.values.length,
      reason:
          'src/enums.h declares ${names.length} LinuxAudioBackend values but '
          'lib/src/enums.dart declares ${LinuxAudioBackend.values.length}.',
    );

    const expectedOrder = <String, LinuxAudioBackend>{
      'linuxBackendAuto': LinuxAudioBackend.auto_,
      'linuxBackendAlsa': LinuxAudioBackend.alsa,
      'linuxBackendPulseAudio': LinuxAudioBackend.pulseAudio,
      'linuxBackendJack': LinuxAudioBackend.jack,
    };

    for (final match in names) {
      final cName = match.group(1)!;
      final cValue = int.parse(match.group(2)!);
      final dartBackend = expectedOrder[cName];
      expect(
        dartBackend,
        isNotNull,
        reason: 'Unexpected C enum value $cName not found in expected mapping',
      );
      expect(
        cValue,
        dartBackend!.value,
        reason:
            'Value mismatch for $cName: C has $cValue but Dart has '
            '${dartBackend.value}',
      );
    }
  });

  test('setLinuxAudioBackend before init succeeds without error', () async {
    await expectLater(
      SoLoud.instance.setLinuxAudioBackend(LinuxAudioBackend.pulseAudio),
      completes,
    );
    await expectLater(
      SoLoud.instance.setLinuxAudioBackend(LinuxAudioBackend.alsa),
      completes,
    );
    await expectLater(
      SoLoud.instance.setLinuxAudioBackend(LinuxAudioBackend.auto_),
      completes,
    );
  });
}

import 'dart:io';

import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

/// `getAudioDeviceState()` returns the native enum as a bare int, which
/// `AudioDeviceState.fromValue` maps back by matching `value`. Nothing checks
/// that mapping at compile time, so a state inserted into the C++ enum and not
/// into the Dart one silently misreports every state after the insertion
/// point — reporting `started` as `stopped`, say, which reads as a device that
/// failed to start.
void main() {
  test('AudioDeviceState value corresponds to its position', () {
    for (final state in AudioDeviceState.values) {
      expect(
        state.value,
        AudioDeviceState.values.indexOf(state),
        reason:
            'The value of $state is ${state.value} but its position in the '
            'AudioDeviceState enum is '
            '${AudioDeviceState.values.indexOf(state)}. The native side sends '
            'the C enum ordinal, so the two must line up.',
      );
    }
  });

  test('AudioDeviceState is in sync with the C++ AudioDeviceState', () {
    final header = File('src/enums.h').readAsStringSync();
    final block = RegExp(
      r'typedef enum AudioDeviceState \{(.*?)\}',
      dotAll: true,
    ).firstMatch(header);
    expect(
      block,
      isNotNull,
      reason: 'could not find `typedef enum AudioDeviceState` in src/enums.h',
    );

    final names = RegExp(
      r'^\s*(audioDevice[A-Za-z]+)\s*=\s*(\d+)',
      multiLine: true,
    ).allMatches(block!.group(1)!).toList();

    expect(
      names.length,
      AudioDeviceState.values.length,
      reason:
          'src/enums.h declares ${names.length} AudioDeviceState values but '
          'lib/src/enums.dart declares ${AudioDeviceState.values.length}. '
          'They are matched by value across the FFI boundary, so both must be '
          'extended together.',
    );

    // Names differ in style (`audioDeviceStarting` vs `starting`), so the
    // explicit C values are checked against the Dart ones by position, which
    // is exactly what the mapping depends on.
    const expectedOrder = <String, AudioDeviceState>{
      'audioDeviceUninitialized': AudioDeviceState.uninitialized,
      'audioDeviceStopped': AudioDeviceState.stopped,
      'audioDeviceStarted': AudioDeviceState.started,
      'audioDeviceStarting': AudioDeviceState.starting,
      'audioDeviceStopping': AudioDeviceState.stopping,
    };

    expect(
      names.map((m) => m.group(1)).toList(),
      expectedOrder.keys.toList(),
      reason:
          'the C++ AudioDeviceState members changed; update this test and '
          'lib/src/enums.dart together',
    );

    for (final match in names) {
      final cName = match.group(1)!;
      final cValue = int.parse(match.group(2)!);
      expect(
        cValue,
        expectedOrder[cName]!.value,
        reason:
            '$cName is $cValue in src/enums.h but '
            '${expectedOrder[cName]} is ${expectedOrder[cName]!.value} in '
            'lib/src/enums.dart.',
      );
    }
  });

  test('AudioDeviceState.fromValue falls back rather than throwing', () {
    // The native value is trusted input today, but a mismatch must degrade to
    // a benign reading rather than take down the caller of a cheap getter.
    expect(AudioDeviceState.fromValue(-1), AudioDeviceState.uninitialized);
    expect(AudioDeviceState.fromValue(99), AudioDeviceState.uninitialized);
    for (final state in AudioDeviceState.values) {
      expect(AudioDeviceState.fromValue(state.value), state);
    }
  });
}

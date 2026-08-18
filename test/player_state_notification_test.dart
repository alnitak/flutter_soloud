import 'dart:io';

import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

/// The native side hands these across the FFI boundary as a bare int, which
/// Dart turns straight back into an enum with
/// `PlayerStateNotification.values[state.value]`. Nothing checks that at
/// compile time, so a value added to one enum and not the other silently
/// misreports every event after the insertion point — or throws a range error.
void main() {
  test('PlayerStateNotification is in sync with the C++ PlayerStateEvents', () {
    final header = File('src/enums.h').readAsStringSync();
    final block = RegExp(
      r'typedef enum PlayerStateEvents \{(.*?)\}',
      dotAll: true,
    ).firstMatch(header);
    expect(
      block,
      isNotNull,
      reason: 'could not find `typedef enum PlayerStateEvents` in src/enums.h',
    );

    final names = RegExp(r'^\s*(event_[a-z_]+)', multiLine: true)
        .allMatches(block!.group(1)!)
        .map((m) => m.group(1)!)
        .toList();

    expect(
      names.length,
      PlayerStateNotification.values.length,
      reason:
          'src/enums.h declares ${names.length} PlayerStateEvents '
          '($names) but lib/src/enums.dart declares '
          '${PlayerStateNotification.values.length} PlayerStateNotification '
          'values. They are matched by ordinal across the FFI boundary, so '
          'both must be extended together.',
    );

    // Ordinals must line up one for one; the names differ in style
    // (`event_interruption_began` vs `interruptionBegan`) so only the order is
    // checked, which is exactly what the ordinal mapping depends on.
    const expectedOrder = <String, PlayerStateNotification>{
      'event_started': PlayerStateNotification.started,
      'event_stopped': PlayerStateNotification.stopped,
      'event_rerouted': PlayerStateNotification.rerouted,
      'event_interruption_began': PlayerStateNotification.interruptionBegan,
      'event_interruption_ended': PlayerStateNotification.interruptionEnded,
      'event_unlocked': PlayerStateNotification.unlocked,
      'event_audio_device_start_failed':
          PlayerStateNotification.audioDeviceStartFailed,
    };

    for (var i = 0; i < names.length; i++) {
      final nativeName = names[i];
      expect(
        expectedOrder.containsKey(nativeName),
        isTrue,
        reason:
            '`$nativeName` was added to src/enums.h without a matching entry '
            'in this test or in PlayerStateNotification.',
      );
      expect(
        expectedOrder[nativeName]!.index,
        i,
        reason:
            '`$nativeName` is at position $i in src/enums.h but its Dart '
            'counterpart ${expectedOrder[nativeName]} is at position '
            '${expectedOrder[nativeName]!.index}.',
      );
    }
  });
}

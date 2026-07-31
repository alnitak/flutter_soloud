import 'dart:io';

import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:test/test.dart';

/// Parses the `PlayerErrors` enumerators of `src/enums.h` into a
/// `name -> value` map, so the Dart enum can be checked against the C one.
Map<String, int> _nativePlayerErrors() {
  final source = File('src/enums.h').readAsStringSync();
  final body = RegExp(
    r'typedef enum PlayerErrors \{(.*?)\} PlayerErrors_t;',
    dotAll: true,
  ).firstMatch(source);
  expect(
    body,
    isNotNull,
    reason: 'Could not find the PlayerErrors enum in src/enums.h',
  );

  final entries = <String, int>{};
  final entry = RegExp(r'^\s*(\w+)\s*=\s*(\d+)\s*,?\s*$', multiLine: true);
  for (final match in entry.allMatches(body!.group(1)!)) {
    entries[match.group(1)!] = int.parse(match.group(2)!);
  }
  return entries;
}

void main() {
  test('PlayerErrors value corresponds to its position', () {
    for (final error in PlayerErrors.values) {
      expect(
        error.value,
        PlayerErrors.values.indexOf(error),
        reason:
            'The value of $error is ${error.value} '
            'but its position in the PlayerErrors enum is '
            '${PlayerErrors.values.indexOf(error)}. '
            'This makes code such as `final error = PlayerErrors.values[ret];` '
            'invalid.',
      );
    }
  });

  test('PlayerErrors is numerically in sync with the C++ enum', () {
    final native = _nativePlayerErrors();

    expect(
      native.length,
      PlayerErrors.values.length,
      reason:
          'The C++ `PlayerErrors` enum in `src/enums.h` has '
          '${native.length} values while the Dart one in `lib/src/enums.dart` '
          'has ${PlayerErrors.values.length}. They must be kept in sync: '
          'errors are passed over the ABI as plain integers.',
    );

    for (final error in PlayerErrors.values) {
      expect(
        native[error.name],
        error.value,
        reason:
            'The C++ value of `${error.name}` is ${native[error.name]} '
            'but the Dart one is ${error.value}.',
      );
    }
  });

  test('PlayerErrors is NOT interchangeable with SoLoud SOLOUD_ERRORS', () {
    // The C++ side must translate SoLoud results into PlayerErrors rather
    // than cast them (see `fromSoLoudError()` in src/player.cpp). This test
    // documents why: the two enums only agree up to `fileLoadFailed`, because
    // PlayerErrors inserts `fileAlreadyLoaded` at 4. A cast used to turn
    // SoLoud's NOT_IMPLEMENTED (6) into `outOfMemory` (6).
    final source = File('src/soloud/include/soloud_error.h').readAsStringSync();
    final soloudErrors = <String, int>{};
    final entry = RegExp(r'^\s*(\w+)\s*=\s*(\d+)', multiLine: true);
    for (final match in entry.allMatches(source)) {
      soloudErrors[match.group(1)!] = int.parse(match.group(2)!);
    }

    expect(
      soloudErrors,
      containsPair('SO_NO_ERROR', 0),
      reason: 'Could not parse SOLOUD_ERRORS from soloud_error.h',
    );

    // The first four agree, which is exactly what makes the mismatch easy to
    // miss.
    expect(
      soloudErrors['INVALID_PARAMETER'],
      PlayerErrors.invalidParameter.value,
    );
    expect(soloudErrors['FILE_NOT_FOUND'], PlayerErrors.fileNotFound.value);
    expect(soloudErrors['FILE_LOAD_FAILED'], PlayerErrors.fileLoadFailed.value);

    // From here on they diverge, so casting is wrong.
    expect(
      soloudErrors['DLL_NOT_FOUND'],
      isNot(PlayerErrors.dllNotFound.value),
      reason:
          'If these ever line up, the mapping in `fromSoLoudError()` '
          'should be revisited, but it must never become a cast again.',
    );
    expect(
      soloudErrors['OUT_OF_MEMORY'],
      isNot(PlayerErrors.outOfMemory.value),
    );
    expect(
      soloudErrors['NOT_IMPLEMENTED'],
      isNot(PlayerErrors.notImplemented.value),
    );
    // The specific collision that made seek() report "out of memory".
    expect(soloudErrors['NOT_IMPLEMENTED'], PlayerErrors.outOfMemory.value);
  });

  test('the new device/playback errors keep their documented values', () {
    // These are appended at the end so that no existing value is renumbered.
    expect(PlayerErrors.audioDeviceFailedToStart.value, 34);
    expect(PlayerErrors.failedToStartPlayback.value, 35);
  });

  group('SoLoudCppException.fromPlayerError', () {
    // These are not failures (or, for maxActiveVoiceCountReached, are
    // non-blocking) and must never be turned into an exception.
    const notThrowable = {
      PlayerErrors.noError,
      PlayerErrors.fileAlreadyLoaded,
      PlayerErrors.maxActiveVoiceCountReached,
    };

    test('maps every error but the non-throwable ones', () {
      for (final error in PlayerErrors.values) {
        if (notThrowable.contains(error)) {
          expect(
            () => SoLoudCppException.fromPlayerError(error),
            throwsArgumentError,
            reason: '$error should not be thrown as an exception',
          );
          continue;
        }

        expect(
          SoLoudCppException.fromPlayerError(error),
          isA<SoLoudCppException>(),
          reason: '$error has no matching exception',
        );
      }
    });

    test('maps the audio device and playback failures', () {
      expect(
        SoLoudCppException.fromPlayerError(
          PlayerErrors.audioDeviceFailedToStart,
        ),
        isA<SoLoudAudioDeviceFailedToStartCppException>(),
      );
      expect(
        SoLoudCppException.fromPlayerError(PlayerErrors.failedToStartPlayback),
        isA<SoLoudFailedToStartPlaybackCppException>(),
      );
    });

    test('maxActiveVoiceCountReached stays non-blocking', () {
      // The sound does not play, but this is a warning rather than a failure:
      // the playback methods log it and return a zeroed handle instead of
      // throwing, so it must have no exception mapping.
      expect(
        () => SoLoudCppException.fromPlayerError(
          PlayerErrors.maxActiveVoiceCountReached,
        ),
        throwsArgumentError,
      );
    });

    test('every error has a non-empty description', () {
      for (final error in PlayerErrors.values) {
        if (notThrowable.contains(error)) {
          continue;
        }
        expect(
          SoLoudCppException.fromPlayerError(error).description,
          isNotEmpty,
          reason: '$error has no description',
        );
      }
    });
  });
}

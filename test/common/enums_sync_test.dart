// The Dart enums in `lib/src/enums.dart` cross the FFI (and WASM) boundary as
// plain integers, so each one must keep the names, order and values of its C
// counterpart in `src/enums.h` (or `src/soloud/include/soloud.h` for the
// waveforms). This parses the headers, so no device is needed.
//
// PlayerErrors, FilterType and DetectedType have dedicated tests
// (player_errors_test, filter_type_test, detected_type_test); PlayerErrors is
// checked here too by name, the others are not repeated.
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter_soloud/src/bindings/flutter_soloud_ffigen.dart'
    as native;
import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

final String _enumsH = File('src/enums.h').readAsStringSync();

/// Strips `//` comments, so commented-out enumerators are not counted.
String _stripComments(String source) => source
    .replaceAll(RegExp('//.*'), '')
    .replaceAll(RegExp(r'/\*.*?\*/', dotAll: true), '');

/// Parses the body of a C enum into its enumerators, in declaration order,
/// with their values (implicit ones continue from the previous value).
List<(String, int)> _parseEnumBody(String body) {
  final result = <(String, int)>[];
  var next = 0;
  for (final raw in _stripComments(body).split(',')) {
    final entry = raw.trim();
    if (entry.isEmpty) continue;
    final match = RegExp(r'^(\w+)\s*(?:=\s*(-?\d+))?$').firstMatch(entry);
    expect(match, isNotNull, reason: 'Unparsable enumerator `$entry`');
    final value = match!.group(2) == null ? next : int.parse(match.group(2)!);
    result.add((match.group(1)!, value));
    next = value + 1;
  }
  return result;
}

/// Parses `typedef enum [name] { ... } <name>_t;` from `src/enums.h`.
List<(String, int)> _cEnum(String name) {
  final match = RegExp(
    'typedef enum $name'
    r'\s*\{(.*?)\}\s*\w+;',
    dotAll: true,
  ).firstMatch(_enumsH);
  expect(match, isNotNull, reason: 'Enum $name not found in src/enums.h');
  return _parseEnumBody(match!.group(1)!);
}

/// `SOME_SNAKE_NAME` or `some_snake_name` -> `someSnakeName`.
String _camel(String snake) {
  final parts = snake.toLowerCase().split('_').where((p) => p.isNotEmpty);
  final buffer = StringBuffer();
  for (final (i, part) in parts.indexed) {
    buffer.write(i == 0 ? part : part[0].toUpperCase() + part.substring(1));
  }
  return buffer.toString();
}

String _lowerFirst(String s) => s[0].toLowerCase() + s.substring(1);

/// Asserts that [dart] (name -> value, in declaration order) matches [c]
/// after mapping each C name with [rename].
void _expectInSync(
  String enumName,
  List<(String, int)> c,
  List<(String, int)> dart, {
  String Function(String cName) rename = _identity,
}) {
  final cMapped = [for (final (name, value) in c) (rename(name), value)];
  expect(
    dart,
    cMapped,
    reason:
        'Dart `$enumName` (left: actual) is out of sync with the C enum '
        '(right: expected) after renaming the C enumerators. Keep '
        'lib/src/enums.dart and src/enums.h in sync.',
  );
}

String _identity(String s) => s;

void main() {
  group('lib/src/enums.dart is in sync with src/enums.h', () {
    test('PlayerErrors: same names and values', () {
      _expectInSync('PlayerErrors', _cEnum('PlayerErrors'), [
        for (final e in PlayerErrors.values) (e.name, e.value),
      ]);
    });

    test('ReadSamplesErrors: same names and values', () {
      _expectInSync('ReadSamplesErrors', _cEnum('ReadSamplesErrors'), [
        for (final e in ReadSamplesErrors.values) (e.name, e.value),
      ]);
      for (final e in ReadSamplesErrors.values) {
        expect(ReadSamplesErrors.fromValue(e.value), e);
      }
      expect(
        () => ReadSamplesErrors.fromValue(ReadSamplesErrors.values.length),
        throwsArgumentError,
      );
    });

    test('AudioDeviceState: audioDeviceX -> x, same values', () {
      _expectInSync('AudioDeviceState', _cEnum('AudioDeviceState'), [
        for (final e in AudioDeviceState.values) (e.name, e.value),
      ], rename: (n) => _lowerFirst(n.replaceFirst('audioDevice', '')));
      for (final e in AudioDeviceState.values) {
        expect(AudioDeviceState.fromValue(e.value), e);
      }
      expect(AudioDeviceState.fromValue(99), AudioDeviceState.uninitialized);
    });

    test('BufferType: PCM_X -> x, same values', () {
      _expectInSync('BufferType', _cEnum('BufferType'), [
        for (final e in BufferType.values) (e.name, e.value),
      ], rename: (n) => _camel(n.replaceFirst('PCM_', '')));
    });

    test('MixerOutputFormat: MIXER_OUTPUT_X -> x, same values', () {
      _expectInSync('MixerOutputFormat', _cEnum('MixerOutputFormat'), [
        for (final e in MixerOutputFormat.values) (e.name, e.value),
      ], rename: (n) => _camel(n.replaceFirst('MIXER_OUTPUT_', '')));
    });

    test('VisualizationKind: VISUALIZATION_X -> x, same values', () {
      _expectInSync('VisualizationKind', _cEnum('VisualizationKind'), [
        for (final e in VisualizationKind.values) (e.name, e.value),
      ], rename: (n) => _camel(n.replaceFirst('VISUALIZATION_', '')));
    });

    test('VisualizationChannel constants match the #defines', () {
      int define(String name) {
        final match = RegExp(
          '#define $name'
          r'\s+(-?\d+)',
        ).firstMatch(_enumsH);
        expect(match, isNotNull, reason: '#define $name not found');
        return int.parse(match!.group(1)!);
      }

      expect(
        VisualizationChannel.merged,
        define('VISUALIZATION_CHANNEL_MERGED'),
      );
      expect(VisualizationChannel.all, define('VISUALIZATION_CHANNEL_ALL'));
    });

    test('LinuxAudioBackend: linuxBackendX -> x, same values', () {
      _expectInSync('LinuxAudioBackend', _cEnum('LinuxAudioBackend'), [
        for (final e in LinuxAudioBackend.values) (e.name, e.value),
      ], rename: (n) => _lowerFirst(n.replaceFirst('linuxBackend', '')));
      for (final e in LinuxAudioBackend.values) {
        expect(LinuxAudioBackend.fromValue(e.value), e);
      }
    });

    test('PlayerStateNotification is indexed by PlayerStateEvents ordinal', () {
      // bindings_player_ffi.dart does `PlayerStateNotification.values[state]`.
      _expectInSync('PlayerStateNotification', _cEnum('PlayerStateEvents'), [
        for (final e in PlayerStateNotification.values) (e.name, e.index),
      ], rename: (n) => _camel(n.replaceFirst('event_', '')));
    });
  });

  test('WaveForm matches Soloud::WAVEFORM by ordinal', () {
    // loadWaveform passes `WaveForm.index` to SoLoud.
    final soloudH = File('src/soloud/include/soloud.h').readAsStringSync();
    final match = RegExp(
      r'enum WAVEFORM\s*\{(.*?)\}',
      dotAll: true,
    ).firstMatch(soloudH);
    expect(match, isNotNull, reason: 'enum WAVEFORM not found in soloud.h');
    final c = _parseEnumBody(match!.group(1)!);
    // `WAVE_FSQUARE` -> `fSquare`: compare case-insensitively.
    _expectInSync('WaveForm', c, [
      for (final e in WaveForm.values) (e.name.toLowerCase(), e.index),
    ], rename: (n) => n.replaceFirst('WAVE_', '').toLowerCase());
  });

  group('the ffigen enums agree with the hand-written Dart ones', () {
    test('PlayerErrors', () {
      expect(
        [for (final e in native.PlayerErrors.values) (e.name, e.value)],
        [for (final e in PlayerErrors.values) (e.name, e.value)],
      );
    });

    test('ReadSamplesErrors', () {
      expect(
        [for (final e in native.ReadSamplesErrors.values) (e.name, e.value)],
        [for (final e in ReadSamplesErrors.values) (e.name, e.value)],
      );
    });

    test('AudioDeviceState', () {
      expect(
        [for (final e in native.AudioDeviceState.values) e.value],
        [for (final e in AudioDeviceState.values) e.value],
      );
    });

    test('PlayerStateEvents', () {
      expect(
        [for (final e in native.PlayerStateEvents.values) e.value],
        [for (final e in PlayerStateNotification.values) e.index],
      );
    });
  });
}

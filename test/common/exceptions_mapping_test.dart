// Every error code the C++ side can return (`PlayerErrors` and
// `ReadSamplesErrors` in `src/enums.h`) must map to its own Dart exception in
// `lib/src/exceptions/exceptions_from_cpp.dart`. The expected table below is
// keyed by the C enumerator names parsed from the header, so a code added to
// the header without a Dart mapping (or without an entry here) fails.
//
// player_errors_test.dart checks the numeric sync and that each code maps to
// *some* SoLoudCppException; this checks that it maps to the *right* one.
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:test/test.dart';

/// Codes that are not failures, or are non-blocking warnings, and therefore
/// must make `fromPlayerError` throw an ArgumentError instead.
const _notThrowable = <String>{
  'noError',
  'fileAlreadyLoaded',
  'maxActiveVoiceCountReached',
};

/// C++ `PlayerErrors` enumerator name -> expected Dart exception type.
const _expectedPlayerErrors = <String, Type>{
  'invalidParameter': SoLoudInvalidParameterException,
  'fileNotFound': SoLoudFileNotFoundException,
  'fileLoadFailed': SoLoudFileLoadFailedException,
  'dllNotFound': SoLoudDllNotFoundException,
  'outOfMemory': SoLoudOutOfMemoryException,
  'notImplemented': SoLoudNotImplementedException,
  'unknownError': SoLoudUnknownErrorException,
  'nullPointer': SoLoudNullPointerException,
  'soundHashNotFound': SoLoudSoundHashNotFoundCppException,
  'backendNotInited': SoLoudBackendNotInitedException,
  'filterNotFound': SoLoudFilterNotFoundException,
  'visualizationNotEnabled': SoLoudVisualizationNotEnabledException,
  'maxNumberOfFiltersReached': SoLoudMaxFilterNumberReachedException,
  'filterAlreadyAdded': SoLoudFilterAlreadyAddedException,
  'playerAlreadyInited': SoLoudPlayerAlreadyInitializedException,
  'soundHandleNotFound': SoLoudSoundHandleNotFoundCppException,
  'filterParameterGetError': SoLoudFilterParameterGetErrorCppException,
  'noPlaybackDevicesFound': SoLoudNoPlaybackDevicesFoundCppException,
  'pcmBufferFull': SoLoudPcmBufferFullCppException,
  'hashIsNotABufferStream': SoLoudHashIsNotABufferStreamCppException,
  'streamEndedAlready': SoLoudStreamEndedAlreadyCppException,
  'failedToCreateOpusDecoder': SoLoudFailedToCreateDecoderCppException,
  'failedToDecodeOpusPacket': SoLoudFailedToDecodeOpusPacketCppException,
  'bufferStreamCanBePlayedOnlyOnce':
      SoLoudBufferStreamCanBePlayedOnlyOnceCppException,
  'wrongBufferTypeToAskForTimeConsumed':
      SoLoudWrongBufferTypeToAskForTimeConsumedCppException,
  'bufferStreamWithReleasedBufferTypeCannotBeSeeked':
      SoLoudBufferStreamWithReleasedBufferTypeCannotBeSeekedCppException,
  'audioFormatNotSupported': SoLoudAudioFormatNotSupportedCppException,
  'xiphLibsNotFound': SoLoudXiphLibsNotFoundCppException,
  'busIdNotFound': SoLoudBusIdNotFoundCppException,
  'hashIsNotAPullBufferStream': SoLoudHashIsNotAPullBufferStreamCppException,
  'invalidPullBufferState': SoLoudInvalidPullBufferStateCppException,
  'audioDeviceFailedToStart': SoLoudAudioDeviceFailedToStartCppException,
  'failedToStartPlayback': SoLoudFailedToStartPlaybackCppException,
  'hashIsNotARealtimeStream': SoLoudHashIsNotARealtimeStreamCppException,
};

/// C++ `ReadSamplesErrors` enumerator name -> expected Dart exception type.
const _expectedReadSamplesErrors = <String, Type>{
  'noBackend': SoLoudReadSamplesNoBackendCppException,
  'failedToGetDataFormat': SoLoudReadSamplesFailedToGetDataFormatCppException,
  'failedToSeekPcm': SoLoudReadSamplesFailedToSeekPcmCppException,
  'failedToReadPcmFrames': SoLoudReadSamplesFailedToReadPcmFramesCppException,
};

/// The enumerator names of `typedef enum <name> { ... }` in `src/enums.h`.
List<String> _cEnumNames(String name) {
  final source = File('src/enums.h').readAsStringSync();
  final body = RegExp(
    'typedef enum $name'
    r'\s*\{(.*?)\}',
    dotAll: true,
  ).firstMatch(source);
  if (body == null) throw StateError('Enum $name not found in src/enums.h');
  return [
    for (final raw in body.group(1)!.replaceAll(RegExp('//.*'), '').split(','))
      if (raw.trim().isNotEmpty) raw.trim().split(RegExp(r'\s|=')).first,
  ];
}

void main() {
  group('PlayerErrors -> SoLoudCppException', () {
    final cNames = _cEnumNames('PlayerErrors');

    test('every C++ code is accounted for, and nothing else', () {
      expect(
        {..._expectedPlayerErrors.keys, ..._notThrowable},
        unorderedEquals(cNames),
        reason:
            'A PlayerErrors code in src/enums.h has no expected mapping here '
            '(or this table lists a code that no longer exists). Map it in '
            'SoLoudCppException.fromPlayerError and add it to this table.',
      );
      expect(
        _expectedPlayerErrors.keys.toSet().intersection(_notThrowable),
        isEmpty,
      );
    });

    for (final name in cNames) {
      if (_notThrowable.contains(name)) {
        test('$name is not turned into an exception', () {
          expect(
            () => SoLoudCppException.fromPlayerError(
              PlayerErrors.values.byName(name),
            ),
            throwsArgumentError,
          );
        });
        continue;
      }
      test('$name -> ${_expectedPlayerErrors[name]}', () {
        final exception = SoLoudCppException.fromPlayerError(
          PlayerErrors.values.byName(name),
        );
        expect(exception.runtimeType, _expectedPlayerErrors[name]);
        expect(exception, isA<SoLoudCppException>());
        expect(exception, isNot(isA<SoLoudDartException>()));
        expect(exception.message, isNull);
        expect(exception.description, isNotEmpty);
        expect(
          exception.toString(),
          '${exception.runtimeType}: ${exception.description}',
        );
      });
    }

    test('no two codes share an exception type', () {
      final types = [
        for (final error in PlayerErrors.values)
          if (!_notThrowable.contains(error.name))
            SoLoudCppException.fromPlayerError(error).runtimeType,
      ];
      expect(types.toSet(), hasLength(types.length));
    });

    test('no two exceptions share a description', () {
      final descriptions = [
        for (final error in PlayerErrors.values)
          if (!_notThrowable.contains(error.name))
            SoLoudCppException.fromPlayerError(error).description,
      ];
      expect(descriptions.toSet(), hasLength(descriptions.length));
    });

    test('every PlayerErrors has its own human-readable sentence', () {
      final sentences = [for (final e in PlayerErrors.values) '$e'];
      final unique = {
        for (final e in PlayerErrors.values)
          e.toString().replaceFirst('PlayerErrors.${e.name} ', ''),
      };
      expect(unique, hasLength(sentences.length));
    });
  });

  group('ReadSamplesErrors -> SoLoudCppException', () {
    final cNames = _cEnumNames('ReadSamplesErrors');

    test('every C++ code is accounted for, and nothing else', () {
      expect({
        ..._expectedReadSamplesErrors.keys,
        'readSamplesNoError',
      }, unorderedEquals(cNames));
    });

    test('readSamplesNoError is not turned into an exception', () {
      expect(
        () => SoLoudCppException.fromReadSampleError(
          ReadSamplesErrors.readSamplesNoError,
        ),
        throwsArgumentError,
      );
    });

    for (final MapEntry(key: name, value: type)
        in _expectedReadSamplesErrors.entries) {
      test('$name -> $type', () {
        final exception = SoLoudCppException.fromReadSampleError(
          ReadSamplesErrors.values.byName(name),
        );
        expect(exception.runtimeType, type);
        expect(exception.description, isNotEmpty);
      });
    }

    test('read-samples exceptions do not reuse a PlayerErrors type', () {
      final playerTypes = _expectedPlayerErrors.values.toSet();
      for (final type in _expectedReadSamplesErrors.values) {
        expect(playerTypes, isNot(contains(type)));
      }
    });
  });
}

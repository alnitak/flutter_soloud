import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/sound_hash.dart';

part 'exceptions_from_cpp.dart';
part 'exceptions_from_dart.dart';

/// A base class for all SoLoud exceptions.
sealed class SoLoudException implements Exception {
  /// Creates a new SoLoud exception.
  const SoLoudException([this.message]);

  /// A message that explains what exactly went wrong in that particular case.
  final String? message;

  /// A verbose description of what this exception means, in general.
  /// Don't confuse with the optional [message] parameter, which is there
  /// to explain what exactly went wrong in that particular case.
  String get description;

  @override
  String toString() {
    final buffer = StringBuffer()
      // ignore: no_runtimeType_toString
      ..write(runtimeType)
      ..write(': ')
      ..write(description);

    if (message != null) {
      buffer
        ..write(' (')
        ..write(message)
        ..write(')');
    }

    return buffer.toString();
  }
}

/// A base class for all SoLoud exceptions that are thrown from the Dart side.
abstract class SoLoudDartException extends SoLoudException {
  /// Creates a new SoLoud exception that is thrown from the Dart side.
  const SoLoudDartException([super.message]);
}

/// A base class for all SoLoud exceptions that are thrown from the C++ side.
///
/// These exceptions correspond to the errors define in the [PlayerErrors] enum.
abstract class SoLoudCppException extends SoLoudException {
  /// Creates a new SoLoud exception that is thrown from the C++ side.
  const SoLoudCppException([super.message]);

  /// Takes a [PlayerErrors] enum value and returns a corresponding exception.
  /// This is useful when we need to convert a C++ error to a Dart exception.
  ///
  /// If [error] is [PlayerErrors.noError], this constructor throws
  /// an [ArgumentError].
  factory SoLoudCppException.fromPlayerError(PlayerErrors error) {
    switch (error) {
      case PlayerErrors.noError:
        throw ArgumentError(
          'Trying to create an exception from PlayerErrors.noError. '
              'This is a bug in the library. Please report it.',
          'error',
        );
      case PlayerErrors.invalidParameter:
        return const SoLoudInvalidParameterException();
      case PlayerErrors.fileNotFound:
        return const SoLoudFileNotFoundException();
      case PlayerErrors.fileLoadFailed:
        return const SoLoudFileLoadFailedException();
      case PlayerErrors.fileAlreadyLoaded:
        throw ArgumentError(
          'The fileAlreadyLoaded return from C++ should not be thrown '
              'as an error. It has no effect on functionality: the sound '
              'is already loaded, and we get the correct sound back, too.',
          'error',
        );
      case PlayerErrors.dllNotFound:
        return const SoLoudDllNotFoundException();
      case PlayerErrors.outOfMemory:
        return const SoLoudOutOfMemoryException();
      case PlayerErrors.notImplemented:
        return const SoLoudNotImplementedException();
      case PlayerErrors.unknownError:
        return const SoLoudUnknownErrorException();
      case PlayerErrors.nullPointer:
        return const SoLoudNullPointerException();
      case PlayerErrors.soundHashNotFound:
        return const SoLoudSoundHashNotFoundCppException();
      case PlayerErrors.backendNotInited:
        return const SoLoudBackendNotInitedException();
      case PlayerErrors.filterNotFound:
        return const SoLoudFilterNotFoundException();
      case PlayerErrors.visualizationNotEnabled:
        return const SoLoudVisualizationNotEnabledException();
      case PlayerErrors.maxNumberOfFiltersReached:
        return const SoLoudMaxFilterNumberReachedException();
      case PlayerErrors.filterAlreadyAdded:
        return const SoLoudFilterAlreadyAddedException();
      case PlayerErrors.playerAlreadyInited:
        return const SoLoudPlayerAlreadyInitializedException();
      case PlayerErrors.soundHandleNotFound:
        return const SoLoudSoundHandleNotFoundCppException();
      case PlayerErrors.filterParameterGetError:
        return const SoLoudFilterParameterGetErrorCppException();
      case PlayerErrors.noPlaybackDevicesFound:
        return const SoLoudNoPlaybackDevicesFoundCppException();
      case PlayerErrors.pcmBufferFull:
        return const SoLoudPcmBufferFullCppException();
      case PlayerErrors.hashIsNotABufferStream:
        return const SoLoudHashIsNotABufferStreamCppException();
      case PlayerErrors.streamEndedAlready:
        return const SoLoudStreamEndedAlreadyCppException();
      case PlayerErrors.failedToCreateOpusDecoder:
        return const SoLoudFailedToCreateOpusDecoderCppException();
      case PlayerErrors.failedToDecodeOpusPacket:
        return const SoLoudFailedToDecodeOpusPacketCppException();
      case PlayerErrors.bufferStreamCanBePlayedOnlyOnce:
        return const SoLoudBufferStreamCanBePlayedOnlyOnceCppException();
      case PlayerErrors.maxActiveVoiceCountReached:
        throw ArgumentError(
          'Trying to create an exception from '
              'PlayerErrors.maxActiveVoiceCountReached. This is a non-blocking '
              'error, so it should not be thrown as an exception. '
              'This is a bug in the library. Please report it.',
          'error',
        );
    }
  }

  /// Takes a [ReadSamplesErrors] enum value and returns a corresponding
  /// exception.
  /// This is useful when we need to convert a C++ error to a Dart exception.
  ///
  /// If [error] is [ReadSamplesErrors.readSamplesNoError], this constructor
  /// throws an [ArgumentError].
  factory SoLoudCppException.fromReadSampleError(ReadSamplesErrors error) {
    switch (error) {
      case ReadSamplesErrors.readSamplesNoError:
        throw ArgumentError(
          'Trying to create an exception from ReadSamplesErrors.noError. '
              'This is a bug in the library. Please report it.',
          'error',
        );
      case ReadSamplesErrors.noBackend:
        return const SoLoudReadSamplesNoBackendCppException();
      case ReadSamplesErrors.failedToGetDataFormat:
        return const SoLoudReadSamplesFailedToGetDataFormatCppException();
      case ReadSamplesErrors.failedToSeekPcm:
        return const SoLoudReadSamplesFailedToSeekPcmCppException();
      case ReadSamplesErrors.failedToReadPcmFrames:
        return const SoLoudReadSamplesFailedToReadPcmFramesCppException();
    }
  }
}

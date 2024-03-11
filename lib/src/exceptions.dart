import 'package:flutter_soloud/src/sound_hash.dart';

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
}

/// An exception that is thrown when the SoLoud engine fails to shutdown.
/// This is not thrown during normal shutdown, but it _can_ be thrown if
/// `initialize()` was called at a time of shutdown, and that shutdown failed.
class ShutdownFailedException extends SoLoudException {
  /// Creates a new [ShutdownFailedException].
  const ShutdownFailedException([super.message]);

  @override
  String get description => 'Failed to shut down SoLoud.';
}

class SoLoudInvalidParameterException extends SoLoudException {
  const SoLoudInvalidParameterException([super.message]);

  @override
  String get description => 'An invalid parameter was passed to SoLoud.';
}

class SoLoudFileNotFoundException extends SoLoudException {
  const SoLoudFileNotFoundException([super.message]);

  @override
  String get description => 'The file was not found.';
}

class SoLoudFileLoadFailedException extends SoLoudException {
  const SoLoudFileLoadFailedException([super.message]);

  @override
  String get description => 'The file was found, but could not be loaded.';
}

class SoLoudFileAlreadyLoadedException extends SoLoudException {
  const SoLoudFileAlreadyLoadedException([super.message]);

  @override
  String get description => 'The sound file has already been loaded.';
}

class SoLoudDllNotFoundException extends SoLoudException {
  const SoLoudDllNotFoundException([super.message]);

  @override
  String get description => 'The DLL was not found, or the wrong DLL was used.';
}

class SoLoudOutOfMemoryException extends SoLoudException {
  const SoLoudOutOfMemoryException([super.message]);

  @override
  String get description => 'Out of memory.';
}

class SoLoudNotImplementedException extends SoLoudException {
  const SoLoudNotImplementedException([super.message]);

  @override
  String get description => 'The feature is not implemented.';
}

class SoLoudUnknownErrorException extends SoLoudException {
  const SoLoudUnknownErrorException([super.message]);

  @override
  String get description => 'An unknown error occurred.';
}

class SoLoudNullPointerException extends SoLoudException {
  const SoLoudNullPointerException([super.message]);

  @override
  String get description =>
      'A null pointer was captured. This could happen when passing a non '
      'initialized pointer (with calloc()) to retrieve FFT or wave data. '
      'Or, setVisualization has not been enabled.';
}

class SoLoudSoundHashNotFoundException extends SoLoudException {
  const SoLoudSoundHashNotFoundException(this.soundHash, [String? message])
      : super(message);

  final SoundHash? soundHash;

  @override
  String get description => 'The sound with specified hash is not found.';
}

class SoLoudBackendNotInitedException extends SoLoudException {
  const SoLoudBackendNotInitedException([super.message]);

  @override
  String get description => 'The player is not initialized.';
}

class SoLoudIsolateAlreadyStartedException extends SoLoudException {
  const SoLoudIsolateAlreadyStartedException([super.message]);

  @override
  String get description => 'The audio isolate has already started.';
}

class SoLoudMultipleInitializationException extends SoLoudException {
  const SoLoudMultipleInitializationException([super.message]);

  @override
  String get description =>
      'The engine has already been initialized successfully, '
      'but a new call to initialize() was made.';
}

class SoLoudConcurrentInitializationException extends SoLoudException {
  const SoLoudConcurrentInitializationException([super.message]);

  @override
  String get description => 'The engine is currently being initialized, '
      'and another call to initialize() was made.';
}

class SoLoudIsolateNotStartedException extends SoLoudException {
  const SoLoudIsolateNotStartedException([super.message]);

  @override
  String get description => 'The audio isolate failed to spawn.';
}

class SoLoudEngineNotInitedException extends SoLoudException {
  const SoLoudEngineNotInitedException([super.message]);

  @override
  String get description => 'The engine is not yet started.';
}

class SoLoudEngineInitializationTimedOutException extends SoLoudException {
  const SoLoudEngineInitializationTimedOutException([super.message]);

  @override
  String get description => 'The engine took too long to initialize.';
}

class SoLoudFilterNotFoundException extends SoLoudException {
  const SoLoudFilterNotFoundException([super.message]);

  @override
  String get description => 'The filter was not found.';
}

class SoLoudVisualizationNotEnabledException extends SoLoudException {
  const SoLoudVisualizationNotEnabledException([super.message]);

  @override
  String get description => 'Asking for audio data is not enabled! Please use '
      '`setVisualizationEnabled(true);` to enable!';
}

class SoLoudAssetLoadFailedException extends SoLoudException {
  const SoLoudAssetLoadFailedException([super.message]);

  @override
  String get description => 'The asset was found but for some reason '
      "couldn't be loaded.";
}

class SoLoudTemporaryFolderFailedException extends SoLoudException {
  const SoLoudTemporaryFolderFailedException([super.message]);

  @override
  String get description => 'There was a problem creating or opening the '
      'temporary folder that is used to hold audio files.';
}

class SoLoudNetworkStatusCodeException extends SoLoudException {
  const SoLoudNetworkStatusCodeException(this.statusCode, [String? message])
      : super(message);

  /// The status code returned from the network request.
  final int statusCode;

  @override
  String get description => 'The request failed with status code $statusCode.';
}

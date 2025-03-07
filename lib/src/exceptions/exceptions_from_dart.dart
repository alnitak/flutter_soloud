part of 'exceptions.dart';

// ////////////////////////
// / Dart-side exceptions /
// ////////////////////////

/// An exception that is thrown when the SoLoud isolate fails to spawn.
class SoLoudIsolateSpawnFailedException extends SoLoudDartException {
  /// Creates a new [SoLoudIsolateSpawnFailedException].
  const SoLoudIsolateSpawnFailedException([super.message]);

  @override
  String get description => 'The audio isolate failed to spawn.';
}

/// An exception that is thrown when a SoLoud method is accessed before
/// the engine is initialized (by calling `SoLoud.initialize()` and either
/// awaiting that method call, or awaiting the `SoLoud.initialized` Future
/// elsewhere, or by checking the synchronous `SoLoud.isInitialized` bool).
class SoLoudNotInitializedException extends SoLoudDartException {
  /// Creates a new [SoLoudNotInitializedException].
  const SoLoudNotInitializedException([super.message]);

  @override
  String get description => 'SoLoud has not been initialized yet. '
      'Call `SoLoud.initialize()` first and await it, or await the '
      '`SoLoud.initialized` Future elsewhere. Alternately, you can check '
      'the synchronous `SoLoud.isInitialized` bool just before calling '
      'any SoLoud method.';
}

/// An exception that is thrown when the SoLoud engine initialization times out.
class SoLoudInitializationTimedOutException extends SoLoudDartException {
  /// Creates a new [SoLoudInitializationTimedOutException].
  const SoLoudInitializationTimedOutException([super.message]);

  @override
  String get description => 'The engine took too long to initialize.';
}

/// An exception that is thrown when the SoLoud engine initialization
/// is cut short by a call to `SoLoud.deinit()`.
class SoLoudInitializationStoppedByDeinitException extends SoLoudDartException {
  /// Creates a new [SoLoudInitializationStoppedByDeinitException].
  const SoLoudInitializationStoppedByDeinitException([super.message]);

  @override
  String get description => 'SoLoud.deinit() was called during initialization.';
}

/// An exception that is thrown when the temporary folder fails to be created
/// or opened.
class SoLoudTemporaryFolderFailedException extends SoLoudDartException {
  /// Creates a new [SoLoudTemporaryFolderFailedException].
  const SoLoudTemporaryFolderFailedException([super.message]);

  @override
  String get description => 'There was a problem creating or opening the '
      'temporary folder that is used to hold audio files.';
}

/// An exception that is thrown when the network request returns
/// a non-200 status code.
class SoLoudNetworkStatusCodeException extends SoLoudDartException {
  /// Creates a new [SoLoudNetworkStatusCodeException].
  const SoLoudNetworkStatusCodeException(this.statusCode, [String? message])
      : super(message);

  /// The status code returned from the network request.
  final int statusCode;

  @override
  String get description => 'The request failed with status code $statusCode.';
}

/// An exception that is thrown when SoLoud (Dart) receives a sound hash
/// that is not found.
class SoLoudSoundHashNotFoundDartException extends SoLoudDartException {
  /// Creates a new [SoLoudSoundHashNotFoundDartException].
  const SoLoudSoundHashNotFoundDartException(this.soundHash, [String? message])
      : super(message);

  /// The sound hash that was not found.
  final SoundHash soundHash;

  @override
  String get description => 'The sound with specified hash is not found '
      '(on the Dart side).';
}

/// An exception that is thrown when SoLoud (Dart) tries to create a voice
/// group but something gone wrong.
class SoLoudCreateVoiceGroupDartException extends SoLoudDartException {
  /// Creates a new [SoLoudCreateVoiceGroupDartException].
  const SoLoudCreateVoiceGroupDartException([super.message]);

  @override
  String get description => 'SoLoud.createVoiceGroup() was not able to create '
      ' a new voice group.';
}

/// An exception that is thrown when trying to set a filter for a single
/// [AudioSource] on the Web platform.
class SoLoudFilterForSingleSoundOnWebDartException extends SoLoudDartException {
  /// Creates a new [SoLoudFilterForSingleSoundOnWebDartException].
  const SoLoudFilterForSingleSoundOnWebDartException([super.message]);

  @override
  String get description => 'Filters for single sounds are not supported on '
      'the Web platform.';
}

/// An exception that is thrown when setting the wrong Opus parameters.
class SoLoudWrongOpusParamsException extends SoLoudDartException {
  /// Creates a new [SoLoudWrongOpusParamsException].
  const SoLoudWrongOpusParamsException([super.message]);

  @override
  String get description => 'Wrong Opus parameter(s). '
      'When using Opus, the sample rate must be 8, 12, 16, 24, or 48 kHz and '
      'the channel count must be 1 or 2.';
}

/// An exception that is thrown when at buit-time the Opus and Ogg libraries
/// are not available and trying to use the Opus codec.
class SoLoudOpusOggLibsNotAvailableException extends SoLoudDartException {
  /// Creates a new [SoLoudOpusOggLibsNotAvailableException].
  const SoLoudOpusOggLibsNotAvailableException([super.message]);

  @override
  String get description => 'The Opus and Ogg libraries are not available. '
      'If your target platform is Android, could happens that the NDK built '
      'files are not updated. If this is the case, or you just want to read '
      'more about enablig/disabling the libs, please read the documentation: '
      'https://github.com/alnitak/flutter_soloud/blob/main/NO_OPUS_OGG_LIBS.md';
}

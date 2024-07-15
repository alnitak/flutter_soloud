part of 'exceptions.dart';

// //////////////////////////////////////
// / C++-side exceptions for the player /
// //////////////////////////////////////

/// An exception that is thrown when an invalid parameter was passed
/// to SoLoud (C++).
class SoLoudInvalidParameterException extends SoLoudCppException {
  /// Creates a new [SoLoudInvalidParameterException].
  const SoLoudInvalidParameterException([super.message]);

  @override
  String get description => 'An invalid parameter was passed to SoLoud '
      '(on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) can't find the file.
class SoLoudFileNotFoundException extends SoLoudCppException {
  /// Creates a new [SoLoudFileNotFoundException].
  const SoLoudFileNotFoundException([super.message]);

  @override
  String get description => 'The file was not found (on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) could find but
/// can't load the file.
class SoLoudFileLoadFailedException extends SoLoudCppException {
  /// Creates a new [SoLoudFileLoadFailedException].
  const SoLoudFileLoadFailedException([super.message]);

  @override
  String get description => 'The file was found, but could not be loaded '
      '(on the C++ side).';
}

// Note: PlayerErrors.fileAlreadyLoaded is not thrown as an exception.

/// An exception that is thrown when the SoLoud (C++) dynamic library
/// (.dll, .so, .dylib) was not found.
class SoLoudDllNotFoundException extends SoLoudCppException {
  /// Creates a new [SoLoudDllNotFoundException].
  const SoLoudDllNotFoundException([super.message]);

  @override
  String get description => 'The DLL was not found.';
}

/// An exception that is thrown when SoLoud (C++) runs out of memory.
class SoLoudOutOfMemoryException extends SoLoudCppException {
  /// Creates a new [SoLoudOutOfMemoryException].
  const SoLoudOutOfMemoryException([super.message]);

  @override
  String get description => 'Out of memory on the C++ side.';
}

/// An exception that is thrown when a SoLoud (C++) feature is being used
/// that is not implemented in this package.
class SoLoudNotImplementedException extends SoLoudCppException {
  /// Creates a new [SoLoudNotImplementedException].
  const SoLoudNotImplementedException([super.message]);

  @override
  String get description => 'The feature is not implemented on the C++ side.';
}

/// An exception that is thrown when an unknown error occurred in SoLoud (C++).
class SoLoudUnknownErrorException extends SoLoudCppException {
  /// Creates a new [SoLoudUnknownErrorException].
  const SoLoudUnknownErrorException([super.message]);

  @override
  String get description => 'An unknown error occurred on the C++ side.';
}

/// An exception that is thrown when SoLoud (C++) experiences a null pointer
/// exception.
class SoLoudNullPointerException extends SoLoudCppException {
  /// Creates a new [SoLoudNullPointerException].
  const SoLoudNullPointerException([super.message]);

  @override
  String get description => 'A null pointer was captured on the C++ side. '
      'This could happen when passing a non '
      'initialized pointer (with calloc()) to retrieve FFT or wave data. '
      'Or, setVisualization has not been enabled.';
}

/// An exception that is thrown when SoLoud (C++) receives a sound hash
/// that is not found.
class SoLoudSoundHashNotFoundCppException extends SoLoudCppException {
  /// Creates a new [SoLoudSoundHashNotFoundCppException].
  const SoLoudSoundHashNotFoundCppException([super.message]);

  @override
  String get description => 'The sound with specified hash is not found '
      '(on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) backend is not initialized.
class SoLoudBackendNotInitedException extends SoLoudCppException {
  /// Creates a new [SoLoudBackendNotInitedException].
  const SoLoudBackendNotInitedException([super.message]);

  @override
  String get description => 'The player is not initialized (on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) is asked to use a filter
/// that is not found.
class SoLoudFilterNotFoundException extends SoLoudCppException {
  /// Creates a new [SoLoudFilterNotFoundException].
  const SoLoudFilterNotFoundException([super.message]);

  @override
  String get description => 'The filter was not found (on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) cannot process a command
/// (asking for audio data) because the visualization is not enabled.
class SoLoudVisualizationNotEnabledException extends SoLoudCppException {
  /// Creates a new [SoLoudVisualizationNotEnabledException].
  const SoLoudVisualizationNotEnabledException([super.message]);

  @override
  String get description => 'Asking for audio data is not enabled '
      '(on the C++ side). '
      'Please use `setVisualizationEnabled(true);` to enable.';
}

/// An exception that is thrown when SoLoud (C++) cannot add another filter.
/// The max number of concurrent filter is set to 8.
class SoLoudMaxFilterNumberReachedException extends SoLoudCppException {
  /// Creates a new [SoLoudMaxFilterNumberReachedException].
  const SoLoudMaxFilterNumberReachedException([super.message]);

  @override
  String get description => 'Askind to add another filter, but no more then 8 '
      'is allowed (on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) cannot add a filter
/// that has already been added.
class SoLoudFilterAlreadyAddedException extends SoLoudCppException {
  /// Creates a new [SoLoudFilterAlreadyAddedException].
  const SoLoudFilterAlreadyAddedException([super.message]);

  @override
  String get description => 'Asking to add a filter that has '
      'already been added. Only one of each type is allowed (on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) cannot add a filter
/// that has already been added.
class SoLoudPlayerAlreadyInitializedException extends SoLoudCppException {
  /// Creates a new [SoLoudPlayerAlreadyInitializedException].
  const SoLoudPlayerAlreadyInitializedException([super.message]);

  @override
  String get description => 'The player has already been initialized '
      '(on the C++ side).';
}

/// An exception that is thrown when SoLoud (C++) receives a handle
/// that is not found. This could happen when trying to use the given handle
/// to get/set some of its attributes (like setting handle volume) after the
/// handle has been stopped/ended and hence it becomes invalid.
class SoLoudSoundHandleNotFoundCppException extends SoLoudCppException {
  /// Creates a new [SoLoudSoundHandleNotFoundCppException].
  const SoLoudSoundHandleNotFoundCppException([super.message]);

  @override
  String get description => 'The sound handle is not found '
      '(on the C++ side).';
}

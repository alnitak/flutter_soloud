/// CaptureDevice exposed to Dart
final class CaptureDevice {
  /// Constructs a new [CaptureDevice].
  // ignore: avoid_positional_boolean_parameters
  const CaptureDevice(this.name, this.isDefault);

  /// The name of the device.
  final String name;

  /// Whether this is the default capture device.
  final bool isDefault;
}

/// Possible capture errors
enum CaptureErrors {
  /// No error
  captureNoError,

  /// Capture failed to initialize
  captureInitFailed,

  /// Capture not yet initialized
  captureNotInited,

  /// null pointer. Could happens when passing a non initialized
  /// pointer (with calloc()) to retrieve FFT or wave data
  nullPointer;

  /// Returns a human-friendly sentence describing the error.
  String get _asSentence {
    switch (this) {
      case CaptureErrors.captureNoError:
        return 'No error';
      case CaptureErrors.captureInitFailed:
        return 'Capture failed to initialize';
      case CaptureErrors.captureNotInited:
        return 'Capture not yet initialized';
      case CaptureErrors.nullPointer:
        return 'Capture null pointer error. Could happens when passing a non '
            'initialized pointer (with calloc()) to retrieve FFT or wave data. '
            'Or, setVisualization has not been enabled.';
    }
  }

  @override
  String toString() => 'CaptureErrors.$name ($_asSentence)';
}

/// Possible player errors
enum PlayerErrors {
  /// No error
  noError,

  /// Some parameter is invalid
  invalidParameter,

  /// File not found
  fileNotFound,

  /// File found, but could not be loaded
  fileLoadFailed,

  /// The sound file has already been loaded
  fileAlreadyLoaded,

  /// DLL not found, or wrong DLL
  dllNotFound,

  /// Out of memory
  outOfMemory,

  /// Feature not implemented
  notImplemented,

  /// Other error
  unknownError,

  /// null pointer. Could happens when passing a non initialized
  /// pointer (with calloc()) to retrieve FFT or wave data
  nullPointer,

  /// The sound with specified hash is not found
  soundHashNotFound,

  /// Player not initialized
  backendNotInited,

  /// Audio isolate already started
  @Deprecated('Use multipleInitialization instead')
  isolateAlreadyStarted,

  /// Engine has already been initialized successfully
  /// but a new call to `initialize()` was made.
  multipleInitialization,

  /// Engine is currently being initialized
  /// and another call to `initialize()` was made.
  concurrentInitialization,

  /// Audio isolate not yet started
  isolateNotStarted,

  /// Engine not yet started
  engineNotInited,

  /// The engine took too long to initialize.
  engineInitializationTimedOut,

  /// Filter not found
  filterNotFound;

  /// Returns a human-friendly sentence describing the error.
  String get _asSentence {
    switch (this) {
      case PlayerErrors.noError:
        return 'No error';
      case PlayerErrors.invalidParameter:
        return 'Some parameter is invalid';
      case PlayerErrors.fileNotFound:
        return 'File not found';
      case PlayerErrors.fileLoadFailed:
        return 'File found, but could not be loaded';
      case PlayerErrors.fileAlreadyLoaded:
        return 'The sound file has already been loaded';
      case PlayerErrors.dllNotFound:
        return 'DLL not found, or wrong DLL';
      case PlayerErrors.outOfMemory:
        return 'Out of memory';
      case PlayerErrors.notImplemented:
        return 'Feature not implemented';
      case PlayerErrors.unknownError:
        return 'Unknown error';
      case PlayerErrors.nullPointer:
        return 'Capture null pointer error. Could happens when passing a non '
            'initialized pointer (with calloc()) to retrieve FFT or wave data. '
            'Or, setVisualization has not been enabled.';
      case PlayerErrors.soundHashNotFound:
        return 'The sound with specified hash is not found';
      case PlayerErrors.backendNotInited:
        return 'Player not initialized';
      // ignore: deprecated_member_use_from_same_package
      case PlayerErrors.isolateAlreadyStarted:
        return 'Audio isolate already started';
      case PlayerErrors.multipleInitialization:
        return 'Engine has already been initialized successfully '
            'but a new call to initialize() was made';
      case PlayerErrors.concurrentInitialization:
        return 'Engine is currently being initialized '
            'and another call to initialize() was made';
      case PlayerErrors.isolateNotStarted:
        return 'Audio isolate not yet started';
      case PlayerErrors.engineNotInited:
        return 'Engine not yet started. '
            'Either asynchronously await `SoLoud.ready` '
            'or synchronously check `SoLoud.isReady` '
            'before calling the function.';
      case PlayerErrors.engineInitializationTimedOut:
        return 'Engine initialization timed out';
      case PlayerErrors.filterNotFound:
        return 'Filter not found';
    }
  }

  @override
  String toString() => 'PlayerErrors.$name ($_asSentence)';
}

/// Wave forms
enum WaveForm {
  /// Raw, harsh square wave
  square,

  /// Raw, harsh saw wave
  saw,

  /// Sine wave
  sin,

  /// Triangle wave
  triangle,

  /// Bounce, i.e, abs(sin())
  bounce,

  /// Quater sine wave, rest of period quiet
  jaws,

  /// Half sine wave, rest of period quiet
  humps,

  /// "Fourier" square wave; less noisy
  fSquare,

  /// "Fourier" saw wave; less noisy
  fSaw,
}

/// The way an audio file is loaded.
enum LoadMode {
  /// Load and decompress the audio file into RAM.
  /// Less CPU, more memory allocated, low latency.
  memory,

  /// Keep the file on disk and only load chunks as needed.
  /// More CPU, less memory allocated, seeking lags with MP3s.
  disk,
}

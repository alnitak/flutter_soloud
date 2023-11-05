/// CaptureDevice exposed to Dart
final class CaptureDevice {
  CaptureDevice(this.name, this.isDefault);
  final String name;
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
  nullPointer,
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
  isolateAlreadyStarted,

  /// Audio isolate not yet started
  isolateNotStarted,

  /// Engine not yet started
  engineNotInited,

  /// Filter not found
  filterNotFound,
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


import 'package:meta/meta.dart';

/// Possible player errors.
/// New values must be enumerated at the bottom
///
/// WARNING: Keep these in sync with `src/enums.h`.
@internal
enum PlayerErrors {
  /// No error
  noError(0),

  /// Some parameter is invalid
  invalidParameter(1),

  /// File not found
  fileNotFound(2),

  /// File found, but could not be loaded
  fileLoadFailed(3),

  /// The sound file has already been loaded
  fileAlreadyLoaded(4),

  /// DLL not found, or wrong DLL
  dllNotFound(5),

  /// Out of memory
  outOfMemory(6),

  /// Feature not implemented
  notImplemented(7),

  /// Other error
  unknownError(8),

  /// null pointer. Could happens when passing a non initialized
  /// pointer (with calloc()) to retrieve FFT or wave data
  nullPointer(9),

  /// The sound with specified hash is not found
  soundHashNotFound(10),

  /// Player not initialized
  backendNotInited(11),

  /// Filter not found
  filterNotFound(12),

  /// asking for wave and FFT is not enabled
  visualizationNotEnabled(13),

  /// The maximum number of filters has been reached (default is 8).
  maxNumberOfFiltersReached(14),

  /// The filter has already been added.
  filterAlreadyAdded(15),

  /// Player already inited.
  playerAlreadyInited(16),

  /// Audio handle is not found
  soundHandleNotFound(17);

  const PlayerErrors(this.value);

  /// The integer value of the error. This is the same number that is returned
  /// from the C++ API.
  final int value;

  /// Returns a human-friendly sentence describing the error.
  String get _asSentence {
    switch (this) {
      case PlayerErrors.noError:
        return 'No error';
      case PlayerErrors.invalidParameter:
        return 'Some parameters are invalid!';
      case PlayerErrors.fileNotFound:
        return 'File not found!';
      case PlayerErrors.fileLoadFailed:
        return 'File found, but could not be loaded!';
      case PlayerErrors.fileAlreadyLoaded:
        return 'The sound file has already been loaded!';
      case PlayerErrors.dllNotFound:
        return 'DLL not found, or wrong DLL!';
      case PlayerErrors.outOfMemory:
        return 'Out of memory!';
      case PlayerErrors.notImplemented:
        return 'Feature not implemented!';
      case PlayerErrors.unknownError:
        return 'Unknown error!';
      case PlayerErrors.nullPointer:
        return 'Capture null pointer error. Could happens when passing a non '
            'initialized pointer (with calloc()) to retrieve FFT or wave data. '
            'Or, setVisualization has not been enabled.';
      case PlayerErrors.soundHashNotFound:
        return 'The sound with specified hash is not found!';
      case PlayerErrors.backendNotInited:
        return 'Player not initialized!';
      case PlayerErrors.filterNotFound:
        return 'Filter not found!';
      case PlayerErrors.visualizationNotEnabled:
        return 'Asking for audio data is not enabled! Please use '
            '`setVisualizationEnabled(true);` to enable!';
      case PlayerErrors.maxNumberOfFiltersReached:
        return 'The maximum number of filters has been reached (default is 8)!';
      case PlayerErrors.filterAlreadyAdded:
        return 'Filter not found!';
      case PlayerErrors.playerAlreadyInited:
        return 'The player has already been inited!';
      case PlayerErrors.soundHandleNotFound:
        return 'The handle is not found! The playing handle could have been '
            'stopped or ended and it is no more valid!';
    }
  }

  @override
  String toString() => 'PlayerErrors.$name ($_asSentence)';
}

/// The types of waveforms.
enum WaveForm {
  /// Raw, harsh square wave.
  square,

  /// Raw, harsh saw wave.
  saw,

  /// Sine wave.
  sin,

  /// Triangle wave.
  triangle,

  /// Bounce, i.e, abs(sin()).
  bounce,

  /// Quarter sine wave, rest of period quiet.
  jaws,

  /// Half sine wave, rest of period quiet.
  humps,

  /// "Fourier" square wave; less noisy.
  fSquare,

  /// "Fourier" saw wave; less noisy.
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

/// Audio state changes. Not doing much now. Notifications should work
/// on iOS but none of the Android backends will report this notification.
/// However the started and stopped events should be reliable for all backends.
enum PlayerStateNotification {
  ///
  started,

  ///
  stopped,

  ///
  rerouted,

  ///
  interruptionBegan,

  ///
  interruptionEnded,

  ///
  unlocked,
}

/// The channels to be used while initializing the player.
enum Channels {
  /// One channel.
  mono(1),

  /// Two channels.
  stereo(2),

  /// Four channels.
  quad(4),

  /// Six channels.
  surround51(6),

  /// Eight channels.
  dolby71(8);

  const Channels(this.count);

  /// The channels count.
  final int count;
}

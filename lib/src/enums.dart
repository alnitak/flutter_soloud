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
  soundHandleNotFound(17),

  /// Error getting filter parameter.
  filterParameterGetError(18),

  /// Trying to add PCM data but the buffer is full or stream buffer has been
  /// set to be ended.
  pcmBufferFullOrStreamEnded(19),

  /// No playback devices were found.
  noPlaybackDevicesFound(20);

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
        return 'File found, but could not be loaded! Could be a permission '
            'error or the file is corrupted.';
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
      case PlayerErrors.filterParameterGetError:
        return 'An error (nan or inf value) occurred while getting a '
            'filter parameter!';
      case PlayerErrors.pcmBufferFullOrStreamEnded:
        return 'Trying to add PCM data but the buffer is full or not large '
            'enough for the neded PCM data. Try increasing the buffer size. '
            'Or, stream buffer has been set to be ended. ';
      case PlayerErrors.noPlaybackDevicesFound:
        return 'No playback devices were found while initializing engine or '
            'when changing the output device.';
    }
  }

  @override
  String toString() => 'PlayerErrors.$name ($_asSentence)';
}

/// Possible read samples errors.
enum ReadSamplesErrors {
  /// No error
  readSamplesNoError(0),

  /// Initialization failed. Probably an unsupported format.
  noBackend(1),

  /// Failed to retrieve decoder data format.
  failedToGetDataFormat(2),

  /// Failed to seek audio data.
  failedToSeekPcm(3),

  /// Failed to read PCM frames.
  failedToReadPcmFrames(4);

  /// The integer value of the error. This is the same number that is returned
  /// from the C++ API.
  final int value;

  /// Constructs a valid error with [value].
  // ignore: sort_constructors_first
  const ReadSamplesErrors(this.value);

  /// Returns a [ReadSamplesErrors] from a [value].
  static ReadSamplesErrors fromValue(int value) => switch (value) {
        0 => readSamplesNoError,
        1 => noBackend,
        2 => failedToGetDataFormat,
        3 => failedToSeekPcm,
        4 => failedToReadPcmFrames,
        _ => throw ArgumentError('Unknown value for ReadSamplesErrors: $value'),
      };

  /// Returns a human-friendly sentence describing the error.
  String get _asSentence {
    switch (this) {
      case ReadSamplesErrors.readSamplesNoError:
        return 'No error';
      case ReadSamplesErrors.noBackend:
        return 'Initialization failed. Probably an unsupported format.';
      case ReadSamplesErrors.failedToGetDataFormat:
        return 'Failed to retrieve decoder data format.';
      case ReadSamplesErrors.failedToSeekPcm:
        return 'Failed to seek audio data.';
      case ReadSamplesErrors.failedToReadPcmFrames:
        return 'Failed to read PCM frames.';
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

  /// Returns a human-friendly channel name.
  @override
  String toString() {
    switch (this) {
      case Channels.mono:
        return 'Mono';
      case Channels.stereo:
        return 'Stereo';
      case Channels.quad:
        return 'Quad';
      case Channels.surround51:
        return 'Surround 5.1';
      case Channels.dolby71:
        return 'Dolby 7.1';
    }
  }
}

enum BufferPcmType {
  f32le(0),
  s8(1),
  s16le(2),
  s32le(3);

  final int value;

  const BufferPcmType(this.value);
}

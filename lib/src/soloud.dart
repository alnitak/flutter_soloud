// ignore_for_file: require_trailing_commas, avoid_positional_boolean_parameters

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/audio_isolate.dart';
import 'package:flutter_soloud/src/soloud_controller.dart';

/// sound event types
enum SoundEvent {
  /// handle reached the end of playback
  handleIsNoMoreValid,

  /// the sound has been disposed
  soundDisposed,
}

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({SoundEvent event, SoundProps sound, int handle});

/// the sound class
class SoundProps {
  ///
  SoundProps(this.soundHash);

  /// the [hash] returned by [loadFile()]
  final int soundHash;

  /// handles of this sound. Multiple instances of this sound can be
  /// played, each with their unique handle
  Set<int> handle = {};

  ///
  // TODO(me): make marker keys time able to trigger an event
  List<double> keys = [];

  /// the user can listen ie when a sound ends or key events (TODO)
  StreamController<StreamSoundEvent> soundEvents = StreamController.broadcast();

  @override
  String toString() {
    return 'soundHash: $soundHash has ${handle.length} active handles';
  }
}

/// The events exposed by the plugin
enum AudioEvent {
  /// emitted when audio isolate is started
  isolateStarted,

  /// emitted when audio isolate is stopped
  isolateStopped,

  /// emitted when audio capture is started
  captureStarted,

  /// emitted when audio capture is stopped
  captureStopped,
}

/// The main class to call all the audio methods
///
class SoLoud {
  ///
  factory SoLoud() => _instance ??= SoLoud._();

  SoLoud._();

  static SoLoud? _instance;

  /// the way to talk to the audio isolate
  SendPort? _mainToIsolateStream;

  /// internally used to listen from isolate
  StreamController<dynamic>? _returnedEvent;

  /// the isolate used to spawn the audio management
  Isolate? _isolate;

  /// the way to receive events from audio isolate
  ReceivePort? _isolateToMainStream;

  /// Stream audio events
  StreamController<AudioEvent> audioEvent = StreamController.broadcast();

  /// status of player
  bool isPlayerInited = false;

  /// status of capture
  bool isCaptureInited = false;

  /// Used both in main and audio isolates
  /// should be synchronized with each other
  final List<SoundProps> activeSounds = [];

  /// Wait for the isolate to return after the event has been completed.
  /// The event must be recognized by [event] and [args] sent to
  /// the audio isolate.
  /// ie:
  /// - call [loadFile()] with completeFileName arg
  /// - wait the audio isolate to call the FFI loadFile function
  /// - the audio isolate will then send back the args used in the call and
  ///   eventually the return value of the FFI function
  ///
  Future<dynamic> _waitForEvent(MessageEvents event, Record args) async {
    final completer = Completer<dynamic>();

    await _returnedEvent?.stream.firstWhere((element) {
      final e = element as Map<String, Object?>;

      // if the event with its args are what we are waiting for...
      if ((e['event']! as MessageEvents) != event) return false;
      if ((e['args']! as Record) != args) return false;

      // return the result
      completer.complete(e['return']);
      return true;
    });

    return completer.future;
  }

  /// Initializes the audio engine.
  ///
  /// Use [initialize] instead. This method is simply an alias for [initialize]
  /// for backwards compatibility. It will be removed in a future version.
  @Deprecated('use initialize() instead')
  Future<PlayerErrors> startIsolate() => initialize();

  /// Initializes the audio engine.
  ///
  /// Run this before anything else, and `await` its result. Only when
  /// this method returns [PlayerErrors.noError] will the engine
  /// be ready.
  ///
  /// If you call any other methods (such as [play]) before initialization
  /// completes, those calls will be ignored and you will get
  /// a [PlayerErrors.engineNotInited] back.
  Future<PlayerErrors> initialize() async {
    // Start the audio isolate and listen for messages coming from it.
    // Messages are streamed with [_returnedEvent] and processed
    // by [_waitForEvent] when they come.

    if (_isolate != null) return PlayerErrors.isolateAlreadyStarted;
    activeSounds.clear();
    final completer = Completer<PlayerErrors>();
    _isolateToMainStream = ReceivePort();
    _returnedEvent = StreamController.broadcast();

    _isolateToMainStream?.listen((data) {
      if (data is SendPort) {
        _mainToIsolateStream = data;

        /// finally start the audio engine
        _initEngine().then((value) {
          if (value == PlayerErrors.noError) {
            audioEvent.add(AudioEvent.isolateStarted);
          }
          completer.complete(value);
        });
      } else {
        debugIsolates('******** MAIN EVENT data: $data');
        if (data is StreamSoundEvent) {
          /// TODO: replace with pkg:logger
          debugPrint('@@@@@@@@@@@ SOUND EVENT: ${data.event}  '
              'handle: ${data.handle}  '
              'sound: ${data.sound}');
          /// find the sound which received the [SoundEvent] and...
          final sound = activeSounds.firstWhere(
            (sound) => sound.soundHash == data.sound.soundHash,
            orElse: () {
              /// TODO: replace with pkg:logger
              debugPrint('Receive an event for sound with handle: '
                  '${data.handle} but there is not that sound! '
                  'Call the Police!');
              return SoundProps(0);
            },
          );

          /// send the disposed event to listeners and remove the sound
          if (data.event == SoundEvent.soundDisposed) {
            sound.soundEvents.add(data);
            activeSounds.removeWhere(
                (element) => element.soundHash == data.sound.soundHash);
          }

          /// send the handle event to the listeners and remove it
          if (data.event == SoundEvent.handleIsNoMoreValid) {
            /// ...put in its own stream the event, then remove the handle
            if (sound.soundHash != 0) {
              sound.soundEvents.add(data);
              sound.handle.removeWhere(
                (handle) {
                  return handle == data.handle;
                },
              );
            }
          }
        } else {
          // if not a StreamSoundEvent, queue this into [_returnedEvent]
          _returnedEvent?.add(data);
        }
      }
    });

    _isolate =
        await Isolate.spawn(audioIsolate, _isolateToMainStream!.sendPort);
    if (_isolate == null) return PlayerErrors.isolateNotStarted;

    return completer.future;
  }

  /// An alias for [dispose], for backwards compatibility.
  ///
  /// Use [dispose] instead. The [stopIsolate] alias will be removed
  /// in a future version.
  @Deprecated('use dispose() instead')
  Future<bool> stopIsolate() => dispose();

  /// Stops the engine and disposes of all resources, including sounds
  /// and the audio isolate.
  ///
  /// Returns `true` when everything has been disposed. Returns `false`
  /// if there was nothing to dispose (e.g. the engine hasn't ever been
  /// initialized).
  Future<bool> dispose() async {
    if (_isolate == null || !isPlayerInited) return false;
    await disposeAllSound();
    // engine will be disposed in the audio isolate, so just set this variable
    isPlayerInited = false;
    await _stopLoop();
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.exitIsolate,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.exitIsolate, ());
    await _returnedEvent?.close();
    _returnedEvent = null;
    _isolateToMainStream?.close();
    _isolateToMainStream = null;
    _isolate?.kill();
    _isolate = null;
    audioEvent.add(AudioEvent.isolateStopped);
    return true;
  }

  /// return true if the audio isolate is running
  ///
  bool isIsolateRunning() {
    return _isolate != null;
  }

  //////////////////////////////////////////////////
  /// isolate loop events management
  //////////////////////////////////////////////////

  /// Start the isolate loop to catch the end
  /// of sounds (handles) playback or keys
  ///
  /// The loop recursively call itself to check the state of
  /// all active sound handles. Therefore it can cause some lag for
  /// other event calls.
  /// Not starting this will implies not receive [SoundEvent]s,
  /// it will therefore be up to the developer to check
  /// the sound handle validity
  ///
  Future<bool> _startLoop() async {
    if (_isolate == null || !isPlayerInited) return false;

    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.startLoop,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.startLoop, ());
    return true;
  }

  /// stop the [SoundEvent]s loop
  ///
  Future<bool> _stopLoop() async {
    if (_isolate == null || !isPlayerInited) return false;

    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.stopLoop,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.stopLoop, ());
    return true;
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the player
  // ////////////////////////////////////////////////

  /// A deprecated method that manually starts the engine.
  ///
  /// Do not use. The engine is fully started with [initialize].
  /// This method will be removed in a future version.
  @Deprecated('Use initialize() instead')
  Future<PlayerErrors> initEngine() => _initEngine();

  /// Initialize the audio engine.
  ///
  /// Defaults are:
  /// Miniaudio audio backend
  /// sample rate 44100
  /// buffer 2048
  ///
  Future<PlayerErrors> _initEngine() async {
    if (_isolate == null) return PlayerErrors.isolateNotStarted;
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.initEngine,
        'args': (),
      },
    );
    final ret =
        await _waitForEvent(MessageEvents.initEngine, ()) as PlayerErrors;
    isPlayerInited = ret == PlayerErrors.noError;
    printPlayerError('initEngine()', ret);

    /// start also the loop in the audio isolate
    if (isPlayerInited) {
      await _startLoop();
    }
    return ret;
  }

  /// A deprecated method that manually disposes the engine.
  ///
  /// Do not use. The engine is fully disposed with [dispose].
  /// This method will be removed in a future version.
  @Deprecated('Use dispose() instead')
  Future<bool> disposeEngine() => _disposeEngine();

  /// Stop the engine
  /// The audio isolate doesn't get killed
  ///
  /// Returns true if success
  ///
  Future<bool> _disposeEngine() async {
    if (_isolate == null || !isPlayerInited) return false;

    await disposeAllSound();

    /// first stop the loop
    await _stopLoop();

    /// then ask to audio isolate to dispose the engine
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeEngine,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.disposeEngine, ());
    return true;
  }

  /// Load a new sound to be played once or multiple times later
  ///
  /// [completeFileName] the complete file path.
  /// [LoadMode] if `LoadMode.memory`, the whole uncompressed RAW PCM 
  /// audio is loaded into memory. Used to prevent gaps or lags
  /// when seeking/starting a sound (less CPU, more memory allocated).
  /// If `LoadMode.disk` is used, the audio data is loaded 
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [LoadMode] = `LoadMode.disk`.
  /// Default is `LoadMode.memory`.
  /// Returns PlayerErrors.noError if success and a new sound.
  ///
  Future<({PlayerErrors error, SoundProps? sound})> loadFile(
    String completeFileName, {
    LoadMode mode = LoadMode.memory,
  }) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, sound: null);
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.loadFile,
        'args': (completeFileName: completeFileName, mode: mode),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.loadFile,
      (completeFileName: completeFileName, mode: mode),
    )) as ({PlayerErrors error, SoundProps? sound});
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound!);
    }
    printPlayerError('loadFile()', ret.error);
    return (error: ret.error, sound: ret.sound);
  }

  /// Load a new waveform to be played once or multiple times later
  ///
  /// [waveform]
  /// [superWave]
  /// [scale]
  /// [detune]
  /// [hash] return hash of the sound
  /// Returns [PlayerErrors.noError] if success
  Future<({PlayerErrors error, SoundProps? sound})> loadWaveform(
    WaveForm waveform,
    bool superWave,
    double scale,
    double detune,
  ) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, sound: null);
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.loadWaveform,
        'args': (
          waveForm: waveform.index,
          superWave: superWave,
          scale: scale,
          detune: detune,
        ),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.loadWaveform,
      (
        waveForm: waveform.index,
        superWave: superWave,
        scale: scale,
        detune: detune,
      ),
    )) as ({PlayerErrors error, SoundProps? sound});
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound!);
    }
    printPlayerError('loadWaveform()', ret.error);
    return (error: ret.error, sound: ret.sound);
  }

  /// Set the scale of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newWaveform]
  PlayerErrors setWaveform(SoundProps sound, WaveForm newWaveform) {
    if (!isPlayerInited) {
      printPlayerError('setWaveform()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveform(sound.soundHash, newWaveform);
    return PlayerErrors.noError;
  }

  /// Set the scale of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newScale]
  PlayerErrors setWaveformScale(SoundProps sound, double newScale) {
    if (!isPlayerInited) {
      printPlayerError('setWaveformScale()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformScale(sound.soundHash, newScale);
    return PlayerErrors.noError;
  }

  /// Set the scale of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newDetune]
  PlayerErrors setWaveformDetune(SoundProps sound, double newDetune) {
    if (!isPlayerInited) {
      printPlayerError('setWaveformDetune()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformDetune(sound.soundHash, newDetune);
    return PlayerErrors.noError;
  }

  /// Set a new frequency of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newFreq]
  PlayerErrors setWaveformFreq(SoundProps sound, double newFreq) {
    if (!isPlayerInited) {
      printPlayerError('setWaveformFreq()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformFreq(sound.soundHash, newFreq);
    return PlayerErrors.noError;
  }

  /// Set a new frequency of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [superwave]
  PlayerErrors setWaveformSuperWave(SoundProps sound, bool superwave) {
    if (!isPlayerInited) {
      printPlayerError('setWaveformSuperWave()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformSuperWave(
          sound.soundHash,
          superwave ? 1 : 0,
        );
    return PlayerErrors.noError;
  }

  /// Speech the given text
  ///
  /// [textToSpeech] the text to be spoken
  /// Returns PlayerErrors.noError if success and a new sound
  ///
  Future<({PlayerErrors error, SoundProps sound})> speechText(
    String textToSpeech,
  ) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, sound: SoundProps(-1));
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.speechText,
        'args': (textToSpeech: textToSpeech),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.speechText,
      (textToSpeech: textToSpeech),
    )) as ({PlayerErrors error, SoundProps sound});
    printPlayerError('speechText()', ret.error);
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound);
    }
    return (error: ret.error, sound: activeSounds.last);
  }

  /// Play already loaded sound identified by [sound]
  ///
  /// [sound] the sound to play
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [paused] 0 not pause
  /// Returns PlayerErrors.noError if success, the new sound and
  /// the new handle newHandle
  ///
  Future<({PlayerErrors error, SoundProps sound, int newHandle})> play(
    SoundProps sound, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
  }) async {
    if (!isPlayerInited) {
      printPlayerError('play()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, sound: sound, newHandle: 0);
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.play,
        'args': (
          soundHash: sound.soundHash,
          volume: volume,
          pan: pan,
          paused: paused
        ),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.play,
      (soundHash: sound.soundHash, volume: volume, pan: pan, paused: paused),
    )) as ({PlayerErrors error, int newHandle});
    try {
      /// add the new handle to the sound
      activeSounds
          .firstWhere((s) => s.soundHash == sound.soundHash)
          .handle
          .add(ret.newHandle);
      sound.handle.add(ret.newHandle);
    } catch (e) {
      printPlayerError(
        'play(): shoundHash ${sound.soundHash} not found!',
        PlayerErrors.soundHashNotFound,
      );
      return (
        error: PlayerErrors.soundHashNotFound,
        sound: sound,
        newHandle: 0
      );
    }
    return (
      error: PlayerErrors.noError,
      sound: sound,
      newHandle: ret.newHandle
    );
  }

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  PlayerErrors pauseSwitch(int handle) {
    if (!isPlayerInited) {
      printPlayerError('pauseSwitch()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.pauseSwitch(handle);
    return PlayerErrors.noError;
  }

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  PlayerErrors setPause(int handle, bool pause) {
    if (!isPlayerInited) {
      printPlayerError('setPause()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setPause(handle, pause ? 1 : 0);
    return PlayerErrors.noError;
  }

  /// Gets the pause state
  ///
  /// [handle] the sound handle
  /// Return [PlayerErrors.noError] on success and true if paused
  ///
  ({PlayerErrors error, bool pause}) getPause(int handle) {
    if (!isPlayerInited) {
      printPlayerError('getPause()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, pause: false);
    }
    final ret = SoLoudController().soLoudFFI.getPause(handle);
    return (error: PlayerErrors.noError, pause: ret);
  }

  /// Set a sound's relative play speed.
  /// Setting the value to 0 will cause undefined behavior, likely a crash.
  /// Change the relative play speed of a sample. This changes the effective
  /// sample rate while leaving the base sample rate alone.
  ///
  /// Note that playing a sound at a higher sample rate will require SoLoud
  /// to request more samples from the sound source, which will require more
  /// memory and more processing power. Playing at a slower sample
  /// rate is cheaper.
  ///
  /// [handle] the sound handle
  /// [speed] the new speed
  PlayerErrors setRelativePlaySpeed(int handle, double speed) {
    if (!isPlayerInited) {
      printPlayerError('setRelativePlaySpeed()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setRelativePlaySpeed(handle, speed);
    return PlayerErrors.noError;
  }

  /// Return the current play speed.
  ///
  /// [handle] the sound handle
  ({PlayerErrors error, double speed}) getRelativePlaySpeed(int handle) {
    if (!isPlayerInited) {
      printPlayerError('getRelativePlaySpeed()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, speed: 1);
    }
    final ret = SoLoudController().soLoudFFI.getRelativePlaySpeed(handle);
    return (error: PlayerErrors.noError, speed: ret);
  }

  /// Stop already loaded sound identified by [handle] and clear it from the
  /// sound handle list
  ///
  /// [handle] the sound handle to stop
  /// Return [PlayerErrors.noError] on success
  ///
  Future<PlayerErrors> stop(int handle) async {
    if (!isPlayerInited) {
      printPlayerError('stop()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.stop,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(MessageEvents.stop, (handle: handle));

    /// find a sound with this handle and remove that handle from the list
    for (final sound in activeSounds) {
      sound.handle.removeWhere((element) => element == handle);
    }
    return PlayerErrors.noError;
  }

  /// Stop all handles of the already loaded sound identified
  /// by soundHash of [sound] and dispose it
  ///
  /// [sound] the sound to clear
  /// Return [PlayerErrors.noError] on success
  ///
  Future<PlayerErrors> disposeSound(SoundProps sound) async {
    if (!isPlayerInited) {
      printPlayerError('disposeSound()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeSound,
        'args': (soundHash: sound.soundHash),
      },
    );
    await _waitForEvent(
        MessageEvents.disposeSound, (soundHash: sound.soundHash));

    /// remove the sound with [soundHash]
    activeSounds.removeWhere(
      (element) {
        return element.soundHash == sound.soundHash;
      },
    );
    return PlayerErrors.noError;
  }

  /// Dispose all sounds already loaded. Complete silence
  ///
  /// Return [PlayerErrors.noError] on success
  ///
  Future<PlayerErrors> disposeAllSound() async {
    if (!isPlayerInited) {
      printPlayerError('disposeAllSound()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeAllSound,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.disposeAllSound, ());

    /// remove the sound with [soundHash]
    activeSounds.clear();
    return PlayerErrors.noError;
  }

  /// This function can be used to set a sample to play on repeat,
  /// instead of just playing once
  ///
  /// [handle] the handle for which enable or disable the loop
  /// [enable]
  /// Return [PlayerErrors.noError] on success
  ///
  PlayerErrors setLooping(int handle, bool enable) {
    if (!isPlayerInited) {
      printPlayerError('setLooping()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setLooping(handle, enable);
    return PlayerErrors.noError;
  }

  /// Enable or disable visualization.
  /// When enabled it will be possible to get FFT and wave data.
  ///
  /// [enabled]
  /// Return [PlayerErrors.noError] on success
  ///
  PlayerErrors setVisualizationEnabled(bool enabled) {
    if (!isPlayerInited) {
      printPlayerError(
          'setVisualizationEnabled()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setVisualizationEnabled(enabled);
    return PlayerErrors.noError;
  }

  /// Get the sound length in seconds
  ///
  /// [sound] the sound hash to get the length
  /// returns sound length in seconds
  ///
  ({PlayerErrors error, double length}) getLength(SoundProps sound) {
    if (!isPlayerInited) {
      printPlayerError('getLength()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, length: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getLength(sound.soundHash);
    return (error: PlayerErrors.noError, length: ret);
  }

  /// Seek playing in [time] seconds
  /// [time]
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  /// 
  /// NOTE: when seeking an MP3 file loaded using `mode`=`LoadMode.disk` the
  /// seek operation is performed but there will be delays. This occurs because
  /// the MP3 codec must compute each frame length to gain a new position.
  /// The problem is explained in souloud_wavstream.cpp 
  /// in `WavStreamInstance::seek` function.
  ///
  /// This mode is useful ie for background music, not for a music player
  /// where a seek slider for MP3s is a must.
  /// If you need to seek MP3s without lags, please, use
  /// `mode`=`LoadMode.memory` instead or other supported audio formats!
  ///
  PlayerErrors seek(int handle, double time) {
    if (!isPlayerInited) {
      printPlayerError('seek()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.seek(handle, time);
    return PlayerErrors.values[ret];
  }

  /// Get current sound position in seconds
  ///
  /// [handle] the sound handle
  /// Return PlayerErrors.noError if success and position in seconds
  ///
  ({PlayerErrors error, double position}) getPosition(int handle) {
    if (!isPlayerInited) {
      printPlayerError('getPosition()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, position: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getPosition(handle);
    return (error: PlayerErrors.noError, position: ret);
  }

  /// Get current Global volume
  ///
  /// Return PlayerErrors.noError if success and volume
  ///
  ({PlayerErrors error, double volume}) getGlobalVolume() {
    if (!isPlayerInited) {
      printPlayerError('getGlobalVolume()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, volume: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getGlobalVolume();
    return (error: PlayerErrors.noError, volume: ret);
  }

  /// Get current Global volume
  ///
  /// Return PlayerErrors.noError if success
  ///
  PlayerErrors setGlobalVolume(double volume) {
    if (!isPlayerInited) {
      printPlayerError('setGlobalVolume()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.setGlobalVolume(volume);
    return PlayerErrors.values[ret];
  }

  /// Get current [handle] volume
  ///
  /// Return PlayerErrors.noError if success and volume
  ///
  ({PlayerErrors error, double volume}) getVolume(int handle) {
    if (!isPlayerInited) {
      printPlayerError('getVolume()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, volume: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getVolume(handle);
    return (error: PlayerErrors.noError, volume: ret);
  }

  /// Set [handle] volume
  ///
  /// Return PlayerErrors.noError if success
  ///
  PlayerErrors setVolume(int handle, double volume) {
    if (!isPlayerInited) {
      printPlayerError('setVolume()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.setVolume(handle, volume);
    return PlayerErrors.values[ret];
  }

  /// Check if a handle is still valid
  ///
  /// [handle] handle to check
  /// Return PlayerErrors.noError if success and isvalid==true if valid
  ///
  ({PlayerErrors error, bool isValid}) getIsValidVoiceHandle(int handle) {
    if (!isPlayerInited) {
      printPlayerError('getIsValidVoiceHandle()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, isValid: false);
    }
    final ret = SoLoudController().soLoudFFI.getIsValidVoiceHandle(handle);
    return (error: PlayerErrors.noError, isValid: ret);
  }

  /// Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data.
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up. The last one will be lost.
  ///
  /// [audioData]
  /// Return [PlayerErrors.noError] if success
  ///
  PlayerErrors getAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) {
    if (!isPlayerInited || audioData == ffi.nullptr) {
      printPlayerError('getAudioTexture2D()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.getAudioTexture2D(audioData);
    if (ret != PlayerErrors.noError || audioData.value == ffi.nullptr) {
      printPlayerError('getAudioTexture2D()', PlayerErrors.nullPointer);
      return PlayerErrors.nullPointer;
    }
    return PlayerErrors.noError;
  }

  /// Smooth FFT data.
  /// When new data is read and the values are decreasing, the new value
  /// will be decreased with an amplitude between the old and the new value.
  /// This will resul on a less shaky visualization.
  ///
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  /// Return [PlayerErrors.noError] if success
  ///
  PlayerErrors setFftSmoothing(double smooth) {
    if (!isPlayerInited) {
      printPlayerError('setFftSmoothing()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setFftSmoothing(smooth);
    return PlayerErrors.noError;
  }

  /////////////////////////////////////////
  /// faders
  /////////////////////////////////////////

  /// Smoothly change the global volume over specified time.
  ///
  PlayerErrors fadeGlobalVolume(double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('fadeGlobalVolume()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.fadeGlobalVolume(to, time);
    return PlayerErrors.values[ret];
  }

  /// Smoothly change a channel's volume over specified time.
  ///
  PlayerErrors fadeVolume(int handle, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('fadeVolume()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.fadeVolume(handle, to, time);
    return PlayerErrors.values[ret];
  }

  /// Smoothly change a channel's pan setting over specified time.
  ///
  PlayerErrors fadePan(int handle, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('fadePan()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.fadePan(handle, to, time);
    return PlayerErrors.values[ret];
  }

  /// Smoothly change a channel's relative play speed over specified time.
  ///
  PlayerErrors fadeRelativePlaySpeed(int handle, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('fadeRelativePlaySpeed()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.fadeRelativePlaySpeed(handle, to, time);
    return PlayerErrors.values[ret];
  }

  /// After specified time, pause the channel.
  ///
  PlayerErrors schedulePause(int handle, double time) {
    if (!isPlayerInited) {
      printPlayerError('schedulePause()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.schedulePause(handle, time);
    return PlayerErrors.values[ret];
  }

  /// After specified time, stop the channel.
  ///
  PlayerErrors scheduleStop(int handle, double time) {
    if (!isPlayerInited) {
      printPlayerError('scheduleStop()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.scheduleStop(handle, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the volume at specified frequency.
  ///
  PlayerErrors oscillateVolume(
      int handle, double from, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('oscillateVolume()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.oscillateVolume(handle, from, to, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the panning at specified frequency.
  ///
  PlayerErrors oscillatePan(int handle, double from, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('oscillatePan()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.oscillatePan(handle, from, to, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the relative play speed at specified frequency.
  ///
  PlayerErrors oscillateRelativePlaySpeed(
      int handle, double from, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError(
          'oscillateRelativePlaySpeed()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController()
        .soLoudFFI
        .oscillateRelativePlaySpeed(handle, from, to, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the global volume at specified frequency.
  ///
  PlayerErrors oscillateGlobalVolume(double from, double to, double time) {
    if (!isPlayerInited) {
      printPlayerError('oscillateGlobalVolume()', PlayerErrors.engineNotInited);
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.oscillateGlobalVolume(from, to, time);
    return PlayerErrors.values[ret];
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the capture
  // ////////////////////////////////////////////////

  /// List available input devices. Useful on desktop to choose
  /// which input device to use.
  ///
  List<CaptureDevice> listCaptureDevices() {
    return SoLoudController().captureFFI.listCaptureDevices();
  }

  /// Initialize input device with [deviceID].
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  CaptureErrors initCapture({int deviceID = -1}) {
    final ret = SoLoudController().captureFFI.initCapture(deviceID);
    printCaptureError('initCapture()', ret);
    if (ret == CaptureErrors.captureNoError) {
      isCaptureInited = true;
      audioEvent.add(AudioEvent.captureStarted);
    }

    return ret;
  }

  /// Get the status of the device.
  ///
  bool isCaptureInitialized() {
    return SoLoudController().captureFFI.isCaptureInited();
  }

  /// Returns true if the device is capturing audio.
  ///
  bool isCaptureStarted() {
    return SoLoudController().captureFFI.isCaptureStarted();
  }

  /// Stop and deinit capture device.
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  CaptureErrors stopCapture() {
    final ret = SoLoudController().captureFFI.stopCapture();
    printCaptureError('stopCapture()', ret);
    if (ret == CaptureErrors.captureNoError) {
      isCaptureInited = false;
      audioEvent.add(AudioEvent.captureStopped);
    }
    return ret;
  }

  /// Start capturing audio data.
  ///
  /// Return [CaptureErrors.captureNoError] if no error
  ///
  CaptureErrors startCapture() {
    final ret = SoLoudController().captureFFI.startCapture();
    printCaptureError('startCapture()', ret);
    if (ret == CaptureErrors.captureNoError) {
      audioEvent.add(AudioEvent.captureStarted);
    }
    return ret;
  }

  /// Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up (the last one will be lost).
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  CaptureErrors getCaptureAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) {
    if (!isCaptureInited || audioData == ffi.nullptr) {
      printCaptureError(
          'getCaptureAudioTexture2D()', CaptureErrors.captureNotInited);
      return CaptureErrors.captureNotInited;
    }

    final ret =
        SoLoudController().captureFFI.getCaptureAudioTexture2D(audioData);
    if (ret != CaptureErrors.captureNoError || audioData.value == ffi.nullptr) {
      printCaptureError(
          'getCaptureAudioTexture2D()', CaptureErrors.nullPointer);
      return CaptureErrors.nullPointer;
    }
    return CaptureErrors.captureNoError;
  }

  /// Smooth FFT data.
  ///
  /// When new data is read and the values are decreasing, the new value will be
  /// decreased with an amplitude between the old and the new value.
  /// This will resul on a less shaky visualization.
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  CaptureErrors setCaptureFftSmoothing(double smooth) {
    final ret = SoLoudController().captureFFI.setCaptureFftSmoothing(smooth);
    printCaptureError('setCaptureFftSmoothing()', ret);
    return ret;
  }

  /////////////////////////////////////////
  /// Filters
  /////////////////////////////////////////

  /// Check if the given filter is active or not.
  ///
  /// [filterType] filter to check
  /// Returns [PlayerErrors.noError] if no errors and the index of
  /// the given filter (-1 if the filter is not active)
  ///
  ({PlayerErrors error, int index}) isFilterActive(FilterType filterType) {
    final ret = SoLoudController().soLoudFFI.isFilterActive(filterType.index);
    return ret;
  }

  /// Get parameters names of the given filter.
  ///
  /// [filterType] filter to get param names
  /// Returns [PlayerErrors.noError] if no errors and the list of param names
  ///
  ({PlayerErrors error, List<String> names}) getFilterParamNames(
      FilterType filterType) {
    final ret =
        SoLoudController().soLoudFFI.getFilterParamNames(filterType.index);
    return ret;
  }

  /// Add the filter [filterType].
  ///
  /// [filterType] filter to add
  /// Returns [PlayerErrors.noError] if no errors
  ///
  PlayerErrors addGlobalFilter(FilterType filterType) {
    final ret = SoLoudController().soLoudFFI.addGlobalFilter(filterType.index);
    return PlayerErrors.values[ret];
  }

  /// Remove the filter [filterType].
  ///
  /// [filterType] filter to remove
  /// Returns [PlayerErrors.noError] if no errors
  ///
  PlayerErrors removeGlobalFilter(FilterType filterType) {
    final ret =
        SoLoudController().soLoudFFI.removeGlobalFilter(filterType.index);
    return PlayerErrors.values[ret];
  }

  /// Set the effect parameter with id [attributeId]
  /// of [filterType] with [value] value.
  ///
  /// [filterType] filter to modify a param
  /// Returns [PlayerErrors.noError] if no errors
  ///
  PlayerErrors setFxParams(
      FilterType filterType, int attributeId, double value) {
    final ret = SoLoudController()
        .soLoudFFI
        .setFxParams(filterType.index, attributeId, value);
    return PlayerErrors.values[ret];
  }

  /// Get the effect parameter with id [attributeId] of [filterType].
  ///
  /// [filterType] filter to modify a param
  /// Returns the value of param
  ///
  double getFxParams(FilterType filterType, int attributeId) {
    final ret =
        SoLoudController().soLoudFFI.getFxParams(filterType.index, attributeId);
    return ret;
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the 3D audio
  // more info: https://solhsa.com/soloud/core3d.html
  //
  // coordinate system is right handed
  //           Y
  //           ^
  //           |
  //           |
  //           |
  //           --------> X
  //          /
  //         /
  //        Z
  // ////////////////////////////////////////////////

  /// play3d() is the 3d version of the play() call.
  ///
  /// Returns the handle of the sound, 0 if error.
  ///
  Future<({PlayerErrors error, SoundProps sound, int newHandle})> play3d(
    SoundProps sound,
    double posX,
    double posY,
    double posZ, {
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    bool paused = false,
  }) async {
    if (!isPlayerInited) {
      printPlayerError('play3d()', PlayerErrors.engineNotInited);
      return (error: PlayerErrors.engineNotInited, sound: sound, newHandle: 0);
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.play3d,
        'args': (
          soundHash: sound.soundHash,
          posX: posX,
          posY: posY,
          posZ: posZ,
          velX: velX,
          velY: velY,
          velZ: velZ,
          volume: volume,
          paused: paused
        ),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.play3d,
      (
        soundHash: sound.soundHash,
        posX: posX,
        posY: posY,
        posZ: posZ,
        velX: velX,
        velY: velY,
        velZ: velZ,
        volume: volume,
        paused: paused
      ),
    )) as ({PlayerErrors error, int newHandle});
    try {
      /// add the new handle to the sound
      activeSounds
          .firstWhere((s) => s.soundHash == sound.soundHash)
          .handle
          .add(ret.newHandle);
      sound.handle.add(ret.newHandle);
    } catch (e) {
      printPlayerError(
        'play3d(): shoundHash ${sound.soundHash} not found!',
        PlayerErrors.soundHashNotFound,
      );
      return (
        error: PlayerErrors.soundHashNotFound,
        sound: sound,
        newHandle: 0
      );
    }
    return (
      error: PlayerErrors.noError,
      sound: sound,
      newHandle: ret.newHandle
    );
  }

  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  ///
  void set3dSoundSpeed(double speed) {
    SoLoudController().soLoudFFI.set3dSoundSpeed(speed);
  }

  /// Get the sound speed
  ///
  double get3dSoundSpeed() {
    return SoLoudController().soLoudFFI.get3dSoundSpeed();
  }

  /// You can set the position, at-vector, up-vector and velocity
  /// parameters of the 3d audio listener with one call.
  ///
  void set3dListenerParameters(
      double posX,
      double posY,
      double posZ,
      double atX,
      double atY,
      double atZ,
      double upX,
      double upY,
      double upZ,
      double velocityX,
      double velocityY,
      double velocityZ) {
    SoLoudController().soLoudFFI.set3dListenerParameters(posX, posY, posZ, atX,
        atY, atZ, upX, upY, upZ, velocityX, velocityY, velocityZ);
  }

  /// You can set the position parameter of the 3d audio listener.
  ///
  void set3dListenerPosition(double posX, double posY, double posZ) {
    SoLoudController().soLoudFFI.set3dListenerPosition(posX, posY, posZ);
  }

  /// You can set the "at" vector parameter of the 3d audio listener.
  ///
  void set3dListenerAt(double atX, double atY, double atZ) {
    SoLoudController().soLoudFFI.set3dListenerAt(atX, atY, atZ);
  }

  /// You can set the "up" vector parameter of the 3d audio listener.
  ///
  void set3dListenerUp(double upX, double upY, double upZ) {
    SoLoudController().soLoudFFI.set3dListenerUp(upX, upY, upZ);
  }

  /// You can set the listener's velocity vector parameter.
  ///
  void set3dListenerVelocity(
      double velocityX, double velocityY, double velocityZ) {
    SoLoudController()
        .soLoudFFI
        .set3dListenerVelocity(velocityX, velocityY, velocityZ);
  }

  /// You can set the position and velocity parameters of a live
  /// 3d audio source with one call.
  ///
  void set3dSourceParameters(int handle, double posX, double posY, double posZ,
      double velocityX, double velocityY, double velocityZ) {
    SoLoudController().soLoudFFI.set3dSourceParameters(
        handle, posX, posY, posZ, velocityX, velocityY, velocityZ);
  }

  /// You can set the position parameters of a live 3d audio source.
  ///
  void set3dSourcePosition(int handle, double posX, double posY, double posZ) {
    SoLoudController().soLoudFFI.set3dSourcePosition(handle, posX, posY, posZ);
  }

  /// You can set the velocity parameters of a live 3d audio source.
  ///
  void set3dSourceVelocity(
      int handle, double velocityX, double velocityY, double velocityZ) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
  }

  /// You can set the minimum and maximum distance parameters
  /// of a live 3d audio source.
  ///
  void set3dSourceMinMaxDistance(
      int handle, double minDistance, double maxDistance) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
  }

  /// You can change the attenuation model and rolloff factor parameters of
  /// a live 3d audio source.
  /// ```
  /// 0 NO_ATTENUATION        No attenuation
  /// 1 INVERSE_DISTANCE      Inverse distance attenuation model
  /// 2 LINEAR_DISTANCE       Linear distance attenuation model
  /// 3 EXPONENTIAL_DISTANCE  Exponential distance attenuation model
  /// ```
  /// see https://solhsa.com/soloud/concepts3d.html
  ///
  void set3dSourceAttenuation(
    int handle,
    int attenuationModel,
    double attenuationRolloffFactor,
  ) {
    SoLoudController().soLoudFFI.set3dSourceAttenuation(
          handle,
          attenuationModel,
          attenuationRolloffFactor,
        );
  }

  /// You can change the doppler factor of a live 3d audio source.
  ///
  void set3dSourceDopplerFactor(int handle, double dopplerFactor) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceDopplerFactor(handle, dopplerFactor);
  }
}

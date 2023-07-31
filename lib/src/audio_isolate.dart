// ignore_for_file: public_member_api_docs, unnecessary_breaks, require_trailing_commas

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/src/bindings_capture_ffi.dart';
import 'package:flutter_soloud/src/flutter_soloud_bindings_ffi.dart';
import 'package:flutter_soloud/src/soloud_controller.dart';

/// Author note: I am a bit scared on how the use of
/// these 2 isolates implementation is gone. But hey,
/// Records saved my life! \O/

/// print some infos when isolate receive events
/// from main isolate and vice versa
void debugIsolates(String text) {
  // print(text);
}

enum _MessageEvents {
  exitIsolate,
  initEngine,
  disposeEngine,
  startLoop,
  stopLoop,
  loop,
  loadFile,
  speechText,
  pauseSwitch,
  getPause,
  play,
  stop,
  stopSound,
  setVisualizationEnabled,
  getFft, // TODO
  getWave, // TODO
  getAudioTexture, // TODO
  getAudioTexture2D,
  getLength,
  seek,
  getPosition,
  getIsValidVoiceHandle,
  setFftSmoothing,
  play3d,
}

/// definitions to be checked in main isolate
typedef ArgsInitEngine = ();
typedef ArgsDisposeEngine = ();
typedef ArgsLoadFile = ({String completeFileName});
typedef ArgsSpeechText = ({String textToSpeech});
typedef ArgsPlay = ({int soundHash, double volume, double pan, bool paused});
typedef ArgsPauseSwitch = ({int handle});
typedef ArgsGetPause = ({int handle});
typedef ArgsStop = ({int handle});
typedef ArgsStopSound = ({int soundHash});
typedef ArgsSetVisualizationEnabled = ({bool enabled});
typedef ArgsGetLength = ({int soundHash});
typedef ArgsSeek = ({int handle, double time});
typedef ArgsGetPosition = ({int handle});
typedef ArgsGetIsValidVoiceHandle = ({int handle});
typedef ArgsGetAudioTexture2D = ({int audioDataAddress});
typedef ArgsSetFftSmoothing = ({double smooth});
typedef ArgsPlay3d = ({
  int soundHash,
  double posX,
  double posY,
  double posZ,
  double velX,
  double velY,
  double velZ,
  double volume,
  bool paused
});

/// sound event types
enum SoundEvent {
  /// handle reache the end of playback
  handleIsNoMoreValid,
}

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({SoundEvent event, SoundProps sound, int handle});

/// the sound class
class SoundProps {
  SoundProps(this.soundHash);

  /// the [hash] returned by [loadFile]
  final int soundHash;

  /// handles of this sound. Multiple instances of this sound can be
  /// played, each with their unique handle
  List<int> handle = [];

  /// TODO: make keys time able to trigger an event
  List<double> keys = [];

  /// the user can listed ie when a sound ends or key events (TODO)
  StreamController<StreamSoundEvent> soundEvents = StreamController.broadcast();
}

/// The events exposed by the plugin
enum AudioEvent {
  isolateStarted,
  isolateStopped,
  captureStarted,
  captureStopped,
}

/// Top Level audio isolate function
///
/// The purpose of this isolate is:
/// - send back to main isolate the communication port
/// - listen to messages from main isolate
/// - when a new message come, execute it and send back the result
/// Since from C is difficult to call dart function from another thread for now,
/// I did this isolate with the main purpose to make use of some callbacks
/// like [playEndedCallback]. Ref: https://github.com/dart-lang/sdk/issues/37022
void audioIsolate(SendPort isolateToMainStream) {
  final mainToIsolateStream = ReceivePort();
  final soLoudController = SoLoudController();

  /// the active sounds
  final activeSounds = <SoundProps>[];
  var loopRunning = false;

  /// Tell the main isolate how to communicate with this isolate
  isolateToMainStream.send(mainToIsolateStream.sendPort);

  /// Listen to all requests from the main isolate
  mainToIsolateStream.listen((data) {
    final event = data as Map<String, Object>;
    if ((event['event']! as _MessageEvents) != _MessageEvents.loop) {
      /// don't print the loop message
      debugIsolates('******** ISOLATE EVENT data: $data');
    }

    switch (event['event']! as _MessageEvents) {
      case _MessageEvents.exitIsolate:
        mainToIsolateStream.close();
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': (), 'return': ()});
        break;

      case _MessageEvents.initEngine:
        final args = event['args']! as ArgsInitEngine;
        final ret = soLoudController.soLoudFFI.initEngine();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.disposeEngine:
        final args = event['args']! as ArgsDisposeEngine;
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.loadFile:
        final args = event['args']! as ArgsLoadFile;
        final ret = soLoudController.soLoudFFI.loadFile(args.completeFileName);
        // add the new sound handler to the list
        SoundProps? newSound;
        if (ret.error == PlayerErrors.noError) {
          newSound = SoundProps(ret.soundHash);
          activeSounds.add(newSound);
        }
        isolateToMainStream.send({
          'event': event['event'],
          'args': args,
          'return': (error: ret.error, sound: newSound),
        });
        break;

      case _MessageEvents.speechText:
        final args = event['args']! as ArgsSpeechText;
        final ret = soLoudController.soLoudFFI.speechText(args.textToSpeech);
        // add the new sound handler to the list
        final newSound = SoundProps(ret.handle);
        if (ret.error == PlayerErrors.noError) {
          activeSounds.add(newSound);
        }
        isolateToMainStream.send({
          'event': event['event'],
          'args': args,
          'return': (error: ret.error, sound: newSound),
        });
        break;

      case _MessageEvents.play:
        final args = event['args']! as ArgsPlay;
        final ret = soLoudController.soLoudFFI.play(
          args.soundHash,
          volume: args.volume,
          pan: args.pan,
          paused: args.paused,
        );
        // add the new handle to the [activeSound] hash list
        try {
          activeSounds
              .firstWhere((s) => s.soundHash == args.soundHash)
              .handle
              .add(ret);
        } catch (e) {
          debugPrint('No sound with shoundHash ${args.soundHash} found!');
          isolateToMainStream.send({
            'event': event['event'],
            'args': args,
            'return': (error: PlayerErrors.soundHashNotFound, newHandle: -1),
          });
          break;
        }
        isolateToMainStream.send({
          'event': event['event'],
          'args': args,
          'return': (error: PlayerErrors.noError, newHandle: ret),
        });
        break;

      case _MessageEvents.pauseSwitch:
        final args = event['args']! as ArgsPauseSwitch;
        soLoudController.soLoudFFI.pauseSwitch(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.getPause:
        final args = event['args']! as ArgsGetPause;
        final ret = soLoudController.soLoudFFI.getPause(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.stop:
        final args = event['args']! as ArgsStop;
        soLoudController.soLoudFFI.stop(args.handle);

        /// find a sound with this handle and remove that handle from the list
        for (final sound in activeSounds) {
          sound.handle.removeWhere((element) => element == args.handle);
        }

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.stopSound:
        final args = event['args']! as ArgsStopSound;
        soLoudController.soLoudFFI.stopSound(args.soundHash);

        /// find a sound with this handle and remove that handle from the list
        activeSounds
            .removeWhere((element) => element.soundHash == args.soundHash);

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.setVisualizationEnabled:
        final args = event['args']! as ArgsSetVisualizationEnabled;
        soLoudController.soLoudFFI.setVisualizationEnabled(args.enabled);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.getLength:
        final args = event['args']! as ArgsGetLength;
        final ret = soLoudController.soLoudFFI.getLength(args.soundHash);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.seek:
        final args = event['args']! as ArgsSeek;
        final ret = soLoudController.soLoudFFI.seek(args.handle, args.time);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.getPosition:
        final args = event['args']! as ArgsGetPosition;
        final ret = soLoudController.soLoudFFI.getPosition(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.getIsValidVoiceHandle:
        final args = event['args']! as ArgsGetIsValidVoiceHandle;
        final ret =
            soLoudController.soLoudFFI.getIsValidVoiceHandle(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.getAudioTexture2D:
        final args = event['args']! as ArgsGetAudioTexture2D;
        final audioDataFromAddress =
            ffi.Pointer<ffi.Pointer<ffi.Float>>.fromAddress(
                args.audioDataAddress);
        final ret =
            soLoudController.soLoudFFI.getAudioTexture2D(audioDataFromAddress);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.setFftSmoothing:
        final args = event['args']! as ArgsSetFftSmoothing;
        soLoudController.soLoudFFI.setFftSmoothing(args.smooth);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      //////////////////////////////////
      /// 3D audio

      case _MessageEvents.play3d:
        final args = event['args']! as ArgsPlay3d;
        final ret = soLoudController.soLoudFFI.play3d(
          args.soundHash,
          args.posX,
          args.posY,
          args.posZ,
          velX: args.velX,
          velY: args.velY,
          velZ: args.velZ,
          volume: args.volume,
          paused: args.paused,
        );
        // add the new handle to the [activeSound] hash list
        try {
          activeSounds
              .firstWhere((s) => s.soundHash == args.soundHash)
              .handle
              .add(ret);
        } catch (e) {
          debugPrint('No sound with shoundHash ${args.soundHash} found!');
          isolateToMainStream.send({
            'event': event['event'],
            'args': args,
            'return': (error: PlayerErrors.soundHashNotFound, newHandle: -1),
          });
          break;
        }
        isolateToMainStream.send({
          'event': event['event'],
          'args': args,
          'return': (error: PlayerErrors.noError, newHandle: ret),
        });
        break;

      //////////////////////////////////
      /// LOOP
      case _MessageEvents.startLoop:
        loopRunning = true;
        isolateToMainStream.send(
            {'event': _MessageEvents.startLoop, 'args': (), 'return': ()});
        mainToIsolateStream.sendPort.send(
          {
            'event': _MessageEvents.loop,
            'args': (),
          },
        );
        break;

      case _MessageEvents.stopLoop:
        loopRunning = false;
        isolateToMainStream
            .send({'event': _MessageEvents.stopLoop, 'args': (), 'return': ()});
        break;

      case _MessageEvents.loop:
        if (loopRunning) {
          for (final sound in activeSounds) {
            final removeInvalid = <void Function()>[];
            // check valids handles in [sound] list
            for (final handle in sound.handle) {
              final isValid =
                  soLoudController.soLoudFFI.getIsValidVoiceHandle(handle);
              if (!isValid) {
                isolateToMainStream.send(
                  (
                    event: SoundEvent.handleIsNoMoreValid,
                    sound: sound,
                    handle: handle
                  ),
                );
                removeInvalid.add(() {
                  sound.handle.remove(handle);
                });
              }
            }
            for (final f in removeInvalid) {
              f();
            }
          }

          /// Call again this isolate after N ms to let other messages
          /// to be managed
          Future.delayed(const Duration(milliseconds: 10), () {
            // TODO: is 10 ms ok to loop again?
            mainToIsolateStream.sendPort.send(
              {'event': _MessageEvents.loop, 'args': ()},
            );
          });
        }
        break;

      default:
        print('Isolate: No event with that name!');
    }
  });
}

/// The main class to call all the methods
///
class AudioIsolate {
  factory AudioIsolate() => _instance ??= AudioIsolate._();

  AudioIsolate._();

  static AudioIsolate? _instance;

  /// the way to talk to the audio isolate
  SendPort? _mainToIsolateStream;

  /// internally used to listen from isolate
  StreamController<dynamic>? _returnedEvent;

  /// the isolate used to spawn the audio management
  Isolate? _isolate;

  /// the way to receive events from audio isolate
  ReceivePort? _isolateToMainStream;

  StreamController<AudioEvent> audioEvent = StreamController.broadcast();

  bool isPlayerInited = false;
  bool isCaptureInited = false;

  /// Used both in main and audio isolates
  /// should be synchronized with each other
  final List<SoundProps> activeSounds = [];

  /// Wait for the isolate to return after the event has been completed.
  /// The event must be recognized by [event] and [args] sent to
  /// the audio isolate.
  /// ie:
  /// - call [loadFile] with [completeFileName] arg
  /// - wait the audio isolate to call the FFI loadFile function
  /// - the audio isolate will then send back the args used in the call and
  ///   eventually the return value of the FFI function
  Future<dynamic> _waitForEvent(_MessageEvents event, Record args) async {
    final completer = Completer<dynamic>();

    await _returnedEvent?.stream.firstWhere((element) {
      final e = element as Map<String, Object?>;

      // if the event with its args are what we are waiting for...
      if ((e['event']! as _MessageEvents) != event) return false;
      if ((e['args']! as Record) != args) return false;

      // return the result
      completer.complete(e['return']);
      return true;
    });

    return completer.future;
  }

  /// Start the audio isolate and listen for messages coming from it.
  /// Messages are streamed with [_returnedEvent] and processed
  /// by [_waitForEvent] when they come.
  Future<PlayerErrors> startIsolate() async {
    if (_isolate != null) return PlayerErrors.isolateAlreadyStarted;
    activeSounds.clear();
    final completer = Completer<PlayerErrors>();
    _isolateToMainStream = ReceivePort();
    _returnedEvent = StreamController.broadcast();

    _isolateToMainStream?.listen((data) {
      if (data is SendPort) {
        _mainToIsolateStream = data;

        /// finally start the audio engine
        initEngine().then((value) {
          if (value == PlayerErrors.noError) {
            audioEvent.add(AudioEvent.isolateStarted);
          }
          completer.complete(value);
        });
      } else {
        debugIsolates('******** MAIN EVENT data: $data');
        if (data is StreamSoundEvent) {
          debugPrint('@@@@@@@@@@@ STREAM EVENT: ${data.event}  '
              'handle: ${data.sound.handle}');

          /// find the sound which received the [SoundEvent] and...
          final sound = activeSounds.firstWhere(
            (sound) => sound.soundHash == data.sound.soundHash,
            orElse: () {
              debugPrint('Receive an event for sound with handle: '
                  '${data.handle} but there is not that sound! '
                  'Call the Police!');
              return SoundProps(0);
            },
          );

          /// ...put in its own stream the event
          if (sound.soundHash != 0) {
            sound.soundEvents.add(data);
            sound.handle.removeWhere(
              (handle) {
                return handle == data.handle;
              },
            );
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

  /// Stop the loop, stop the engine and kill the isolate
  ///
  Future<bool> stopIsolate() async {
    if (_isolate == null) return false;
    // engine will be disposed in the audio isolate, so just set this variable
    isPlayerInited = false;
    await _stopLoop();
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.exitIsolate,
        'args': (),
      },
    );
    await _waitForEvent(_MessageEvents.exitIsolate, ());
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

  /// start the isolate loop to catch the end
  /// of sounds (handles) playback or keys
  ///
  /// The loop recursively call itself to check the state of
  /// all active sound handles. Therefore it can cause some lag for
  /// other event calls.
  /// Not starting this will implies not receive [SoundEvent]s,
  /// it will therefore be up to the developer to check
  /// the sound handle validity
  Future<bool> _startLoop() async {
    if (_isolate == null || !isPlayerInited) return false;

    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.startLoop,
        'args': (),
      },
    );
    await _waitForEvent(_MessageEvents.startLoop, ());
    return true;
  }

  /// stop the [SoundEvent]s loop
  Future<bool> _stopLoop() async {
    if (_isolate == null || !isPlayerInited) return false;

    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.stopLoop,
        'args': (),
      },
    );
    await _waitForEvent(_MessageEvents.stopLoop, ());
    return true;
  }

  //////////////////////////////////////////////////
  /// Below all the methods implemented with FFI for the player
  //////////////////////////////////////////////////

  /// Initialize the audio engine.
  ///
  /// Defaults are:
  /// Miniaudio audio backend
  /// sample rate 44100
  /// buffer 2048
  Future<PlayerErrors> initEngine() async {
    if (_isolate == null) return PlayerErrors.isolateNotStarted;
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.initEngine,
        'args': (),
      },
    );
    final ret =
        await _waitForEvent(_MessageEvents.initEngine, ()) as PlayerErrors;
    isPlayerInited = ret == PlayerErrors.noError;

    /// start also the loop in the audio isolate
    if (isPlayerInited) {
      await _startLoop();
    }
    return ret;
  }

  /// Stop the engine
  /// The audio isolate doesn't get killed
  Future<bool> disposeEngine() async {
    if (_isolate == null || !isPlayerInited) return false;

    /// first stop the loop
    await _stopLoop();

    /// then ask to audio isolate to dispose the engine
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.disposeEngine,
        'args': (),
      },
    );
    await _waitForEvent(_MessageEvents.disposeEngine, ());
    return true;
  }

  /// Load a new sound to be played once or multiple times later
  ///
  /// [completeFileName] the complete file path
  /// Returns [PlayerErrors.noError] if success and a new [sound]
  Future<({PlayerErrors error, SoundProps? sound})> loadFile(
    String completeFileName,
  ) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, sound: null);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.loadFile,
        'args': (completeFileName: completeFileName),
      },
    );
    final ret = (await _waitForEvent(
      _MessageEvents.loadFile,
      (completeFileName: completeFileName),
    )) as ({PlayerErrors error, SoundProps? sound});
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound!);
    }
    return (error: ret.error, sound: ret.sound);
  }

  /// Speech the given text
  ///
  /// [textToSpeech] the text to be spoken
  /// Returns [PlayerErrors.noError] if success and a new [sound]
  Future<({PlayerErrors error, SoundProps sound})> speechText(
    String textToSpeech,
  ) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, sound: SoundProps(-1));
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.speechText,
        'args': (textToSpeech: textToSpeech),
      },
    );
    final ret = (await _waitForEvent(
      _MessageEvents.speechText,
      (textToSpeech: textToSpeech),
    )) as ({PlayerErrors error, SoundProps sound});
    activeSounds.add(ret.sound);
    return (error: ret.error, sound: activeSounds.last);
  }

  /// Play already loaded sound identified by [sound]
  ///
  /// [sound] the sound to play
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [paused] 0 not pause
  /// Returns [PlayerErrors.noError] if success, the new [sound] and
  ///   the new sound [handle]
  Future<({PlayerErrors error, SoundProps sound, int newHandle})> play(
    SoundProps sound, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
  }) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, sound: sound, newHandle: 0);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.play,
        'args': (
          soundHash: sound.soundHash,
          volume: volume,
          pan: pan,
          paused: paused
        ),
      },
    );
    final ret = (await _waitForEvent(
      _MessageEvents.play,
      (soundHash: sound.soundHash, volume: volume, pan: pan, paused: paused),
    )) as ({PlayerErrors error, int newHandle});
    try {
      /// add the new handle to the sound
      activeSounds
          .firstWhere((s) => s.soundHash == sound.soundHash)
          .handle
          .add(ret.newHandle);
    } catch (e) {
      debugPrint('No sound with shoundHash ${sound.soundHash} found!');
      return (
        error: PlayerErrors.soundHashNotFound,
        sound: sound,
        newHandle: 0
      );
    }
    return (
      error: PlayerErrors.engineNotInited,
      sound: sound,
      newHandle: ret.newHandle
    );
  }

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  Future<PlayerErrors> pauseSwitch(int handle) async {
    if (!isPlayerInited) return PlayerErrors.engineNotInited;
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.pauseSwitch,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(_MessageEvents.pauseSwitch, (handle: handle));
    return PlayerErrors.noError;
  }

  /// Gets the pause state
  ///
  /// [handle] the sound handle
  /// return true if paused
  Future<({PlayerErrors error, bool pause})> getPause(int handle) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, pause: false);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getPause,
        'args': (handle: handle),
      },
    );

    final ret = await _waitForEvent(_MessageEvents.getPause, (handle: handle));
    return (error: PlayerErrors.noError, pause: ret as bool);
  }

  /// Stop already loaded sound identified by [handle] and clear it from the
  /// sound handle list
  ///
  /// [handle] the sound handle to stop
  Future<PlayerErrors> stop(int handle) async {
    if (!isPlayerInited) {
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.stop,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(_MessageEvents.stop, (handle: handle));

    /// find a sound with this handle and remove that handle from the list
    for (final sound in activeSounds) {
      sound.handle.removeWhere((element) => element == handle);
    }
    return PlayerErrors.noError;
  }

  /// Stop all handles of the already loaded sound identified
  /// by soundHash of [sound] and clear it
  ///
  /// [sound] the sound to clear
  Future<PlayerErrors> stopSound(SoundProps sound) async {
    if (!isPlayerInited) {
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.stopSound,
        'args': (soundHash: sound.soundHash),
      },
    );
    await _waitForEvent(_MessageEvents.stopSound, (soundHash: sound.soundHash));

    /// remove the sound with [soundHash]
    activeSounds.removeWhere(
      (element) {
        return element.soundHash == sound.soundHash;
      },
    );
    return PlayerErrors.noError;
  }

  /// This function can be used to set a sample to play on repeat,
  /// instead of just playing once
  ///
  /// [sound] the sound for which enable or disable the loop
  /// [enable]
  void setLooping(int handle, bool enable) {
    SoLoudController().soLoudFFI.setLooping(handle, enable);
  }

  /// Enable or disable visualization.
  /// When enabled it will be possible to get FFT and wave data.
  ///
  /// [enabled]
  /// Return [PlayerErrors.noError] on success
  // ignore: avoid_positional_boolean_parameters
  Future<PlayerErrors> setVisualizationEnabled(bool enabled) async {
    if (!isPlayerInited) {
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.setVisualizationEnabled,
        'args': (enabled: enabled),
      },
    );
    await _waitForEvent(
        _MessageEvents.setVisualizationEnabled, (enabled: enabled));
    return PlayerErrors.noError;
  }

  /// Get the sound length in seconds
  ///
  /// [soundHash] the sound hash to get the length
  /// returns sound length in seconds
  Future<({PlayerErrors error, double length})> getLength(int soundHash) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, length: 0.0);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getLength,
        'args': (soundHash: soundHash),
      },
    );
    final ret =
        (await _waitForEvent(_MessageEvents.getLength, (soundHash: soundHash)))
            as double;
    return (error: PlayerErrors.noError, length: ret);
  }

  /// Seek playing in seconds
  ///
  /// [time] the time to seek
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  Future<PlayerErrors> seek(int handle, double time) async {
    if (!isPlayerInited) {
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.seek,
        'args': (handle: handle, time: time),
      },
    );
    final ret =
        (await _waitForEvent(_MessageEvents.seek, (handle: handle, time: time)))
            as int;
    return PlayerErrors.values[ret];
  }

  /// Get current sound position in seconds
  ///
  /// [handle] the sound handle
  /// Return time in seconds
  Future<({PlayerErrors error, double position})> getPosition(
      int handle) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, position: 0.0);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getPosition,
        'args': (handle: handle),
      },
    );
    final ret =
        (await _waitForEvent(_MessageEvents.getPosition, (handle: handle)))
            as double;
    return (error: PlayerErrors.noError, position: ret);
  }

  /// Check if a handle is still valid
  ///
  /// [handle] handle to check
  /// Return [PlayerErrors.noError] if success and [isvalid]==true if valid
  Future<({PlayerErrors error, bool isValid})> getIsValidVoiceHandle(
      int handle) async {
    if (!isPlayerInited) {
      return (error: PlayerErrors.engineNotInited, isValid: false);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getIsValidVoiceHandle,
        'args': (handle: handle),
      },
    );
    final ret = (await _waitForEvent(
        _MessageEvents.getIsValidVoiceHandle, (handle: handle))) as bool;
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
  Future<PlayerErrors> getAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) async {
    // if (!isPlayerInited) return PlayerErrors.engineNotStarted;

    // final r = (audioDataAddress: audioData.address);
    // _mainToIsolateStream?.send(
    //   {
    //     'event': _MessageEvents.getAudioTexture2D,
    //     'args': r,
    //   },
    // );
    // final ret = await _waitForEvent(_MessageEvents.getAudioTexture2D, r);
    // if (audioData.address == ffi.nullptr.address) {
    //   return PlayerErrors.nullPointer;
    // }
    // return PlayerErrors.noError;

    /// Prefer using direct FFI call since there is less gap then calling
    /// the audio isolate.
    if (!isPlayerInited || audioData == ffi.nullptr) {
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.getAudioTexture2D(audioData);
    if (ret != PlayerErrors.noError || audioData.value == ffi.nullptr) {
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
  Future<PlayerErrors> setFftSmoothing(double smooth) async {
    if (!isPlayerInited) return PlayerErrors.engineNotInited;
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.setFftSmoothing,
        'args': (smooth: smooth),
      },
    );
    await _waitForEvent(_MessageEvents.setFftSmoothing, (smooth: smooth));
    return PlayerErrors.noError;
  }

  //////////////////////////////////////////////////
  /// Below all the methods implemented with FFI for the capture
  //////////////////////////////////////////////////

  /// List available input devices. Useful on desktop to choose
  /// which input device to use
  ///
  List<CaptureDevice> listCaptureDevices() {
    return SoLoudController().captureFFI.listCaptureDevices();
  }

  /// Initialize input device with [deviceID]
  ///
  /// Return [CaptureErrors.captureNoError] if no error
  CaptureErrors initCapture({int deviceID = -1}) {
    final ret = SoLoudController().captureFFI.initCapture(deviceID);
    if (ret == CaptureErrors.captureNoError) {
      isCaptureInited = true;
      audioEvent.add(AudioEvent.captureStarted);
    }

    return ret;
  }

  /// Get the status of the device
  ///
  bool isCaptureInitialized() {
    return SoLoudController().captureFFI.isCaptureInited();
  }

  /// Returns true if the device is capturing audio
  ///
  bool isCaptureStarted() {
    return SoLoudController().captureFFI.isCaptureStarted();
  }

  /// Stop and deinit capture device
  ///
  /// Return [CaptureErrors.captureNoError] if no error
  CaptureErrors stopCapture() {
    final ret = SoLoudController().captureFFI.stopCapture();
    if (ret == CaptureErrors.captureNoError) {
      isCaptureInited = false;
      audioEvent.add(AudioEvent.captureStopped);
    }
    return ret;
  }

  /// Start capturing audio data
  ///
  /// Return [CaptureErrors.captureNoError] if no error
  CaptureErrors startCapture() {
    final ret = SoLoudController().captureFFI.startCapture();
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
  /// Return [CaptureErrors.captureNoError] if no error
  CaptureErrors getCaptureAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) {
    if (!isCaptureInited || audioData == ffi.nullptr) {
      return CaptureErrors.captureNotInited;
    }

    final ret =
        SoLoudController().captureFFI.getCaptureAudioTexture2D(audioData);
    if (ret != CaptureErrors.captureNoError || audioData.value == ffi.nullptr) {
      return CaptureErrors.nullPointer;
    }
    return CaptureErrors.captureNoError;
  }

  /// Smooth FFT data.
  /// When new data is read and the values are decreasing, the new value will be
  /// decreased with an amplitude between the old and the new value.
  /// This will resul on a less shaky visualization.
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  ///
  /// Return [CaptureErrors.captureNoError] if no error
  CaptureErrors setCaptureFftSmoothing(double smooth) {
    final ret = SoLoudController().captureFFI.setCaptureFftSmoothing(smooth);
    return ret;
  }

  //////////////////////////////////////////////////
  /// Below all the methods implemented with FFI for the 3D audio
  /// more info: https://solhsa.com/soloud/core3d.html
  ///
  /// coordinate system is right handed
  ///           Y
  ///           ^
  ///           |
  ///           |
  ///           |
  ///           --------> X
  ///         /
  ///       /
  ///     Z
  //////////////////////////////////////////////////

  /// play3d() is the 3d version of the play() call
  ///
  /// Returns the handle of the sound, 0 if error
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
      return (error: PlayerErrors.engineNotInited, sound: sound, newHandle: 0);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.play3d,
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
      _MessageEvents.play3d,
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
    } catch (e) {
      debugPrint('No sound with shoundHash ${sound.soundHash} found!');
      return (
        error: PlayerErrors.soundHashNotFound,
        sound: sound,
        newHandle: 0
      );
    }
    return (
      error: PlayerErrors.engineNotInited,
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
  /// parameters of the 3d audio listener with one call
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

  /// You can set the position parameter of the 3d audio listener
  void set3dListenerPosition(double posX, double posY, double posZ) {
    SoLoudController().soLoudFFI.set3dListenerPosition(posX, posY, posZ);
  }

  /// You can set the "at" vector parameter of the 3d audio listener
  void set3dListenerAt(double atX, double atY, double atZ) {
    SoLoudController().soLoudFFI.set3dListenerAt(atX, atY, atZ);
  }

  /// You can set the "up" vector parameter of the 3d audio listener
  void set3dListenerUp(double upX, double upY, double upZ) {
    SoLoudController().soLoudFFI.set3dListenerUp(upX, upY, upZ);
  }

  /// You can set the listener's velocity vector parameter
  void set3dListenerVelocity(
      double velocityX, double velocityY, double velocityZ) {
    SoLoudController()
        .soLoudFFI
        .set3dListenerVelocity(velocityX, velocityY, velocityZ);
  }

  /// You can set the position and velocity parameters of a live
  /// 3d audio source with one call
  void set3dSourceParameters(int handle, double posX, double posY, double posZ,
      double velocityX, double velocityY, double velocityZ) {
    SoLoudController().soLoudFFI.set3dSourceParameters(
        handle, posX, posY, posZ, velocityX, velocityY, velocityZ);
  }

  /// You can set the position parameters of a live 3d audio source
  void set3dSourcePosition(int handle, double posX, double posY, double posZ) {
    SoLoudController().soLoudFFI.set3dSourcePosition(handle, posX, posY, posZ);
  }

  /// You can set the velocity parameters of a live 3d audio source
  void set3dSourceVelocity(
      int handle, double velocityX, double velocityY, double velocityZ) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
  }

  /// You can set the minimum and maximum distance parameters
  /// of a live 3d audio source
  void set3dSourceMinMaxDistance(
      int handle, double minDistance, double maxDistance) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
  }

  /// You can change the attenuation model and rolloff factor parameters of
  /// a live 3d audio source.
  ///
  /// NO_ATTENUATION 	      No attenuation
  /// INVERSE_DISTANCE 	    Inverse distance attenuation model
  /// LINEAR_DISTANCE 	    Linear distance attenuation model
  /// EXPONENTIAL_DISTANCE 	Exponential distance attenuation model
  ///
  /// see https://solhsa.com/soloud/concepts3d.html
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

  /// You can change the doppler factor of a live 3d audio source
  void set3dSourceDopplerFactor(int handle, double dopplerFactor) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceDopplerFactor(handle, dopplerFactor);
  }
}

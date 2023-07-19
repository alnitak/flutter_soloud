// ignore_for_file: public_member_api_docs, unused_local_variable, strict_raw_type, avoid_print, cascade_invocations, lines_longer_than_80_chars, omit_local_variable_types, cast_nullable_to_non_nullable, unnecessary_breaks

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:flutter/material.dart';

import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';
import 'package:flutter_soloud/soloud_controller.dart';

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
  getFft,
  getWave,
  getAudioTexture,
  getAudioTexture2D,
  getLength,
  seek,
  getPosition,
  getIsValidVoiceHandle,
  setFftSmoothing,
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

/// sound event types
enum SoundEvent {
  handleIsNoMoreValid,
}

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({SoundEvent event, SoundProps sound, int handle});

/// the sound class
class SoundProps {
  SoundProps(this.soundHash);

  // the [hash] returned by [loadFile]
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
  // the active sounds
  final List<SoundProps> activeSounds = [];
  bool loopRunning = false;

  isolateToMainStream.send(mainToIsolateStream.sendPort);

  soLoudController.initialize();

  /// Listen to all requests from the main isolate
  mainToIsolateStream.listen((data) {
    final event = data as Map<String, Object>;
    if ((event['event'] as _MessageEvents) !=
        _MessageEvents.loop) {
      debugIsolates('******** ISOLATE EVENT data: $data');
    }

    switch (event['event'] as _MessageEvents) {
      case _MessageEvents.exitIsolate:
        mainToIsolateStream.close();
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': (), 'return': ()});
        break;

      case _MessageEvents.initEngine:
        final args = event['args'] as ArgsInitEngine;
        final ret = soLoudController.soLoudFFI.initEngine();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.disposeEngine:
        final args = event['args'] as ArgsDisposeEngine;
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.loadFile:
        final args = event['args'] as ArgsLoadFile;
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
        final args = event['args'] as ArgsSpeechText;
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
        final args = event['args'] as ArgsPlay;
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
        final args = event['args'] as ArgsPauseSwitch;
        soLoudController.soLoudFFI.pauseSwitch(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.getPause:
        final args = event['args'] as ArgsGetPause;
        final ret = soLoudController.soLoudFFI.getPause(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.stop:
        final args = event['args'] as ArgsStop;
        soLoudController.soLoudFFI.stop(args.handle);

        /// find a sound with this handle and remove that handle from the list
        for (final sound in activeSounds) {
          sound.handle.removeWhere((element) => element == args.handle);
        }

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.stopSound:
        final args = event['args'] as ArgsStopSound;
        soLoudController.soLoudFFI.stopSound(args.soundHash);

        /// find a sound with this handle and remove that handle from the list
        activeSounds
            .removeWhere((element) => element.soundHash == args.soundHash);

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.setVisualizationEnabled:
        final args = event['args'] as ArgsSetVisualizationEnabled;
        soLoudController.soLoudFFI.setVisualizationEnabled(args.enabled);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.getLength:
        final args = event['args'] as ArgsGetLength;
        final ret = soLoudController.soLoudFFI.getLength(args.soundHash);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.seek:
        final args = event['args'] as ArgsSeek;
        final ret = soLoudController.soLoudFFI.seek(args.handle, args.time);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.getPosition:
        final args = event['args'] as ArgsGetPosition;
        final ret = soLoudController.soLoudFFI.getPosition(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.getIsValidVoiceHandle:
        final args = event['args'] as ArgsGetIsValidVoiceHandle;
        final ret =
            soLoudController.soLoudFFI.getIsValidVoiceHandle(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case _MessageEvents.getAudioTexture2D:
        final args = event['args'] as ArgsGetAudioTexture2D;
        final ffi.Pointer<ffi.Pointer<ffi.Float>> audioDataFromAddress =
            ffi.Pointer.fromAddress(args.audioDataAddress);
        soLoudController.soLoudFFI.getAudioTexture2D(audioDataFromAddress);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case _MessageEvents.setFftSmoothing:
        final args = event['args'] as ArgsSetFftSmoothing;
        soLoudController.soLoudFFI.setFftSmoothing(args.smooth);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
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
            final List<void Function()> removeInvalid = [];
            // check valids handles in [sound] list
            for (final handle in sound.handle) {
              final bool isValid =
                  soLoudController.soLoudFFI.getIsValidVoiceHandle(handle);
              if (!isValid) {
                isolateToMainStream.send((
                  event: SoundEvent.handleIsNoMoreValid,
                  sound: sound,
                  handle: handle
                ));
                removeInvalid.add(() {
                  sound.handle.remove(handle);
                });
              }
            }
            for (final f in removeInvalid) {
              f();
            }
          }

          // TODO: is 10 ms ok to loop again?
          Future.delayed(const Duration(milliseconds: 10), () {
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

///
class AudioIsolate {
  factory AudioIsolate() => _instance ??= AudioIsolate._();

  AudioIsolate._();

  static AudioIsolate? _instance;

  SendPort? _mainToIsolateStream;

  /// internally used to listen from isolate
  StreamController<dynamic>? _returnedEvent;
  Isolate? _isolate;
  ReceivePort? _isolateToMainStream;

  StreamController<AudioEvent> audioEvent = StreamController.broadcast();

  bool engineInited = false;

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
    final Completer<dynamic> completer = Completer();

    final ret = await _returnedEvent?.stream.firstWhere((element) {
      final e = element as Map<String, Object?>;

      // if the event with its args are what we are waiting for...
      if ((e['event'] as _MessageEvents) != event) return false;
      if ((e['args'] as Record) != args) return false;

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
        final ret = initEngine().then((value) {
          if (value == PlayerErrors.noError) {
            audioEvent.add(AudioEvent.isolateStarted);
          }
          completer.complete(value);
        });
      } else {
        debugIsolates('******** MAIN EVENT data: $data');
        if (data is StreamSoundEvent) {
          print('@@@@@@@@@@@ STREAM EVENT: ${data.event}  '
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
  Future<bool> stopIsolate() async {
    if (_isolate == null) return false;
    engineInited = false; // engine will be disposed in the audio isolate
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
    if (_isolate == null || !engineInited) return false;

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
    if (_isolate == null || !engineInited) return false;

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
  /// Below all the methods implemented with FFI
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
    engineInited = ret == PlayerErrors.noError;

    /// start also the loop in the audio isolate
    if (engineInited) {
      await _startLoop();
    }
    return ret;
  }

  /// Stop the engine
  Future<bool> disposeEngine() async {
    if (_isolate == null || !engineInited) return false;

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

  /// @brief Load a new sound to be played once or multiple times later
  /// @param completeFileName the complete file path
  /// @return Returns [PlayerErrors.noError] if success and a new [sound]
  Future<({PlayerErrors error, SoundProps? sound})> loadFile(
      String completeFileName) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, sound: null);
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

  /// @brief Speech given text
  /// @param textToSpeech the text to be spoken
  /// @return Returns [PlayerErrors.noError] if success and a new [sound]
  Future<({PlayerErrors error, SoundProps sound})> speechText(
    String textToSpeech,
  ) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, sound: SoundProps(-1));
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

  /// @brief Play already loaded sound identified by [sound]
  /// @param sound the sound to play
  /// @param volume 1.0f full volume
  /// @param pan 0.0f centered
  /// @param paused 0 not pause
  /// @return Returns [PlayerErrors.noError] if success, the new [sound] and
  ///   the new [handle]
  Future<({PlayerErrors error, SoundProps sound, int newHandle})> play(
    SoundProps sound, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
  }) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, sound: sound, newHandle: 0);
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
      error: PlayerErrors.engineNotStarted,
      sound: sound,
      newHandle: ret.newHandle
    );
  }

  /// @brief Pause or unpause already loaded sound identified by [handle]
  /// @param handle the sound handle
  Future<PlayerErrors> pauseSwitch(int handle) async {
    if (!engineInited) return PlayerErrors.engineNotStarted;
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.pauseSwitch,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(_MessageEvents.pauseSwitch, (handle: handle));
    return PlayerErrors.noError;
  }

  /// @brief Gets the pause state
  /// @param handle the sound handle
  /// @return true if paused
  Future<({PlayerErrors error, bool pause})> getPause(int handle) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, pause: false);
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

  /// @brief Stop already loaded sound identified by [handle] and clear it
  /// @param handle
  Future<PlayerErrors> stop(int handle) async {
    if (!engineInited) {
      return PlayerErrors.engineNotStarted;
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

  /// @brief Stop all handles of the already loaded sound identified
  ///     by [soundHash] and clear it
  /// @param sound
  Future<PlayerErrors> stopSound(SoundProps sound) async {
    if (!engineInited) {
      return PlayerErrors.engineNotStarted;
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

  /// @brief Enable or disable visualization.
  ///   When enabled it will be possible to get FFT and wave data.
  /// @param enabled
  /// @return [PlayerErrors.noError] on success
  // ignore: avoid_positional_boolean_parameters
  Future<PlayerErrors> setVisualizationEnabled(bool enabled) async {
    if (!engineInited) {
      return PlayerErrors.engineNotStarted;
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

  /// @brief get the sound length in seconds
  /// @param soundHash the sound hash
  /// @return returns sound length in seconds
  Future<({PlayerErrors error, double length})> getLength(int soundHash) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, length: 0.0);
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

  /// @brief seek playing in seconds
  /// @param time the time to seek
  /// @param handle the sound handle
  /// @return Returns [PlayerErrors.noError] if success
  Future<PlayerErrors> seek(int handle, double time) async {
    if (!engineInited) {
      return PlayerErrors.engineNotStarted;
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

  /// @brief get current sound position in seconds
  /// @param handle the sound handle
  /// @return time in seconds
  Future<({PlayerErrors error, double position})> getPosition(
      int handle) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, position: 0.0);
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

  /// @brief check if a handle is still valid.
  /// @param handle handle to check
  /// @return [PlayerErrors.noError] if success and [isvalid]==true if valid
  Future<({PlayerErrors error, bool isValid})> getIsValidVoiceHandle(
      int handle) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, isValid: false);
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

  /// @brief Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up (the last one will be lost).
  /// @param samples
  /// @return [PlayerErrors.noError] if success
  Future<PlayerErrors> getAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) async {
    if (!engineInited) return PlayerErrors.engineNotStarted;

    final r = (audioDataAddress: audioData.address);
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getAudioTexture2D,
        'args': r,
      },
    );
    await _waitForEvent(_MessageEvents.getAudioTexture2D, r);
    if (audioData.address == ffi.nullptr.address) {
      return PlayerErrors.nullPointer;
    }
    return PlayerErrors.noError;
  }

  /// @brief smooth FFT data.
  /// When new data is read and the values are decreasing, the new value will be
  /// decreased with an amplitude between the old and the new value.
  /// This will resul on a less shaky visualization
  /// @param [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  /// @return [PlayerErrors.noError] if success
  Future<PlayerErrors> setFftSmoothing(double smooth) async {
    if (!engineInited) return PlayerErrors.engineNotStarted;
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.setFftSmoothing,
        'args': (smooth: smooth),
      },
    );
    await _waitForEvent(_MessageEvents.setFftSmoothing, (smooth: smooth));
    return PlayerErrors.noError;
  }
}

// ignore_for_file: public_member_api_docs, unused_local_variable, strict_raw_type, avoid_print, cascade_invocations, lines_longer_than_80_chars, omit_local_variable_types, cast_nullable_to_non_nullable, unnecessary_breaks

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:flutter/material.dart';

import 'flutter_soloud_bindings_ffi.dart';
import 'soloud_controller.dart';

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
  playFile,
  speechText,
  pauseSwitch,
  getPause,
  play,
  stop,
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
typedef ArgsPlayFile = ({String completeFileName});
typedef ArgsSpeechText = ({String textToSpeech});
typedef ArgsPlay = ({int handle});
typedef ArgsPauseSwitch = ({int handle});
typedef ArgsGetPause = ({int handle});
typedef ArgsStop = ({int handle});
typedef ArgsSetVisualizationEnabled = ({bool enabled});
typedef ArgsGetLength = ({int handle});
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
typedef StreamSoundEvent = ({SoundEvent event, SoundProps sound});

/// the sound which receive the event
class SoundProps {
  SoundProps(this.handle);

  final int handle;
  double length = 0;
  List<double> keys = [];

  /// the user can listed ie when a sound ends
  StreamController<StreamSoundEvent> soundEvents = StreamController.broadcast();
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
        (event['event'] as _MessageEvents)) {
      debugIsolates('*** MAIN TO ISOLATE data: $data');
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

      case _MessageEvents.playFile:
        final args = event['args'] as ArgsPlayFile;
        final ret = soLoudController.soLoudFFI.playFile(args.completeFileName);
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

      /// TODO: this should return a handle. And this handle must be another sound
      case _MessageEvents.play:
        final args = event['args'] as ArgsPlay;
        final ret = soLoudController.soLoudFFI.play(args.handle);
        // add the new sound handler to the list
        final newSound = SoundProps(ret);
        activeSounds.add(newSound);
        isolateToMainStream.send({
          'event': event['event'],
          'args': args,
          'return': (error: PlayerErrors.noError, sound: newSound),
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
        activeSounds.removeWhere((item) => item.handle == args.handle);
        soLoudController.soLoudFFI.stop(args.handle);
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
        final ret = soLoudController.soLoudFFI.getLength(args.handle);
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
          final List<void Function()> pendingRemoves = [];
          for (final sound in activeSounds) {
            // check valids
            final bool isValid =
                soLoudController.soLoudFFI.getIsValidVoiceHandle(sound.handle);
            if (!isValid) {
              isolateToMainStream
                  .send((event: SoundEvent.handleIsNoMoreValid, sound: sound));
              // remove the ended sound from the list
              pendingRemoves.add(() {
                activeSounds.remove(sound);
              });
            }
          }
          // TODO: this is executed every loop. Design in another
          //way even if it's fast
          for (final pendingRemove in pendingRemoves.reversed) {
            pendingRemove();
          }

          // TODO: is 10 ms ok?
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

class AudioIsolate {
  factory AudioIsolate() => _instance ??= AudioIsolate._();

  AudioIsolate._();

  static AudioIsolate? _instance;

  SendPort? _mainToIsolateStream;

  /// internally used to listen from isolate
  StreamController<dynamic>? _returnedEvent;
  Isolate? _isolate;
  ReceivePort? _isolateToMainStream;

  bool engineInited = false;

  /// should be synchronized with the other in the isolate
  final List<SoundProps> activeSounds = [];

  /// Wait for the isolate to return after the event has been completed.
  /// The event must be recognized by [event] and [args] sent to
  /// the audio isolate.
  /// ie:
  /// - call [playFile] with [completeFileName] arg
  /// - wait the audio isolate to call the FFI playFile function
  /// - the audio isolate will then send back what has been called and
  ///   eventually the return value
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
    final completer = Completer<PlayerErrors>();
    _isolateToMainStream = ReceivePort();
    _returnedEvent = StreamController.broadcast();

    _isolateToMainStream?.listen((data) {
      if (data is SendPort) {
        _mainToIsolateStream = data;

        /// finally start the audio engine
        initEngine().then(completer.complete);
      } else {
        debugIsolates('******** ISOLATE TO MAIN: $data');
        if (data is StreamSoundEvent) {
          print(
              '@@@@@@@@@@@ STREAM EVENT: ${data.event}  handle: ${data.sound.handle}');

          /// find the sound receive the [SoundEvent] and...
          final sound = activeSounds.firstWhere(
            (element) => element.handle == data.sound.handle,
            orElse: () {
              debugPrint('Receive an event for sound with handle: '
                  '${data.sound.handle} but there is not that sound! '
                  'Call the Police!');
              return SoundProps(-1);
            },
          );

          /// ...put in its own stream the event
          if (sound.handle != -1) {
            sound.soundEvents.add(data);
            activeSounds.removeWhere(
              (element) {
                return element.handle == sound.handle;
              },
            );
          }
        } else {
          // if not a StreamSoundEvent queue this into [_returnedEvent]
          _returnedEvent?.add(data);
        }
      }
    });

    _isolate =
        await Isolate.spawn(audioIsolate, _isolateToMainStream!.sendPort);
    if (_isolate == null) return PlayerErrors.isolateNotStarted;

    return completer.future;
  }

  /// kill the isolate
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
    _isolate!.kill();
    _isolate = null;
    return true;
  }

  bool isIsolateRunning() {
    return _isolate != null;
  }

  //////////////////////////////////////////////////
  /// isolate loop management
  //////////////////////////////////////////////////

  /// start the isolate loop to catch end of sound playback or keys
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

  Future<({PlayerErrors error, SoundProps sound})> playFile(
    String completeFileName,
  ) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, sound: SoundProps(-1));
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.playFile,
        'args': (completeFileName: completeFileName),
      },
    );
    final ret = (await _waitForEvent(
      _MessageEvents.playFile,
      (completeFileName: completeFileName),
    )) as ({PlayerErrors error, SoundProps sound});
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound);
    }
    return (error: ret.error, sound: ret.sound);
  }

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

  Future<({PlayerErrors error, SoundProps sound})> play(int handle) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, sound: SoundProps(-1));
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.play,
        'args': (handle: handle),
      },
    );
    final ret = (await _waitForEvent(
      _MessageEvents.play,
      (handle: handle),
    )) as ({PlayerErrors error, SoundProps sound});
    activeSounds.add(ret.sound);
    return (error: PlayerErrors.engineNotStarted, sound: ret.sound);
  }

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
    activeSounds.removeWhere(
      (element) {
        return element.handle == handle;
      },
    );
    return PlayerErrors.noError;
  }

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

  Future<({PlayerErrors error, double length})> getLength(int handle) async {
    if (!engineInited) {
      return (error: PlayerErrors.engineNotStarted, length: 0.0);
    }
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getLength,
        'args': (handle: handle),
      },
    );
    final ret =
        (await _waitForEvent(_MessageEvents.getLength, (handle: handle)))
            as double;
    return (error: PlayerErrors.noError, length: ret);
  }

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

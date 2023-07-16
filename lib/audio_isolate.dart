// ignore_for_file: public_member_api_docs, unused_local_variable, strict_raw_type, avoid_print, cascade_invocations, lines_longer_than_80_chars, omit_local_variable_types, cast_nullable_to_non_nullable, unnecessary_breaks

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

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
  loop,
  exitIsolate,
  initEngine,
  disposeEngine,
  playFile,
  setPlayEndedCallback,
  startLoop,
  stopLoop,
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
    debugIsolates('*** MAIN TO ISOLATE data: $data');
    final event = data as Map<String, Object>;

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

    /// TODO: this should return a handle. And this handle must be another sound
      case _MessageEvents.play:
        final args = event['args'] as ArgsPlay;
        soLoudController.soLoudFFI.play(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
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

  /// should be synchronized with the other in the isolate
  final List<SoundProps> activeSounds = [];

  /// Start the audio isolate and listen for messages coming from it.
  /// Messages are streamed with [_returnedEvent] and processed
  /// by [_waitForEvent] when they come.
  Future<void> startIsolate() async {
    if (_isolate != null) return;
    final Completer<void> completer = Completer();
    _isolateToMainStream = ReceivePort();
    _returnedEvent = StreamController.broadcast();

    _isolateToMainStream?.listen((data) {
      if (data is SendPort) {
        _mainToIsolateStream = data;
        completer.complete();
      } else {
        debugIsolates('******** ISOLATE TO MAIN: $data');
        if (data is StreamSoundEvent) {
          print(
              '@@@@@@@@@@@ send STREAM with sound hash: ${data.sound.hashCode}');
          final sound = activeSounds
              .firstWhere((element) => element.handle == data.sound.handle);
          sound.soundEvents.add(data);
          activeSounds.removeWhere(
            (element) {
              return element.handle == sound.handle;
            },
          );
        } else {
          _returnedEvent?.add(data);
        }
      }
    });

    _isolate =
        await Isolate.spawn(audioIsolate, _isolateToMainStream!.sendPort);
    return completer.future;
  }

  /// kill the isolate
  Future<void> stopIsolate() async {
    if (_isolate == null) return;
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
  }

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

  //////////////////////////////////////////////////
  /// isolate loop management
  //////////////////////////////////////////////////

  /// start the isolate loop to catch end of sound playback or keys
  Future<void> startLoop() async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.startLoop,
        'args': (),
      },
    );
    return _waitForEvent(_MessageEvents.startLoop, ());
  }

  Future<void> stopLoop() async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.stopLoop,
        'args': (),
      },
    );
    return _waitForEvent(_MessageEvents.stopLoop, ());
  }

  //////////////////////////////////////////////////
  /// Below all the methods implemented with FFI
  //////////////////////////////////////////////////
  Future<PlayerErrors> initEngine() async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.initEngine,
        'args': (),
      },
    );
    return (await _waitForEvent(_MessageEvents.initEngine, ())) as PlayerErrors;
  }

  Future<void> disposeEngine() async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.disposeEngine,
        'args': (),
      },
    );
    return _waitForEvent(_MessageEvents.disposeEngine, ());
  }

  Future<({PlayerErrors error, SoundProps sound})> playFile(
    String completeFileName,
  ) async {
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
    activeSounds.add(ret.sound);
    return (error: ret.error, sound: activeSounds.last);
  }

  /// TODO: this should return a handle. And this handle must be another sound
  Future<void> play(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.play,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(_MessageEvents.play, (handle: handle));
  }

  Future<void> pauseSwitch(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.pauseSwitch,
        'args': (handle: handle),
      },
    );
    return _waitForEvent(_MessageEvents.pauseSwitch, (handle: handle));
  }

  Future<bool> getPause(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getPause,
        'args': (handle: handle),
      },
    );

    final ret = await _waitForEvent(_MessageEvents.getPause, (handle: handle));
    return ret as bool;
  }

  Future<void> stop(int handle) async {
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
  }

  Future<void> setVisualizationEnabled(bool enabled) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.setVisualizationEnabled,
        'args': (enabled: enabled),
      },
    );
    await _waitForEvent(_MessageEvents.stop, (enabled: enabled));
  }

  Future<double> getLength(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getLength,
        'args': (handle: handle),
      },
    );
    final ret =
        (await _waitForEvent(_MessageEvents.getLength, (handle: handle)))
            as double;
    return ret;
  }

  Future<PlayerErrors> seek(int handle, double time) async {
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

  Future<double> getPosition(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getPosition,
        'args': (handle: handle),
      },
    );
    final ret =
        (await _waitForEvent(_MessageEvents.getPosition, (handle: handle)))
            as double;
    return ret;
  }

  Future<bool> getIsValidVoiceHandle(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getIsValidVoiceHandle,
        'args': (handle: handle),
      },
    );
    final ret = (await _waitForEvent(
        _MessageEvents.getIsValidVoiceHandle, (handle: handle))) as bool;
    return ret;
  }

  Future<void> getAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) async {
    final r = (audioDataAddress: audioData.address);
    _mainToIsolateStream?.send(
      {
        'event': _MessageEvents.getAudioTexture2D,
        'args': r,
      },
    );
    await _waitForEvent(_MessageEvents.getAudioTexture2D, r);
    // for (int i = 0; i < 10; i++) print('${audioData.value[i]}');
  }
}

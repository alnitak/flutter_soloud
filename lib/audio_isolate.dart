// ignore_for_file: public_member_api_docs, unused_local_variable, strict_raw_type, avoid_print, cascade_invocations, lines_longer_than_80_chars, omit_local_variable_types, cast_nullable_to_non_nullable, unnecessary_breaks

import 'dart:async';
import 'dart:isolate';

import 'flutter_soloud_bindings_ffi.dart';
import 'soloud_controller.dart';

enum MessageEvents {
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
  setFftSmoothing,
}

typedef ArgsInitEngine = ();
typedef ArgsDisposeEngine = ();
typedef ArgsPlayFile = ({String completeFileName});
typedef ArgsPlay = ({int handle});
typedef ArgsPauseSwitch = ({int handle});
typedef ArgsGetPause = ({int handle});
typedef ArgsStop = ({int handle});
typedef ArgsGetLength = ({int handle});
typedef ArgsSeek = ({int handle, double time});
typedef ArgsGetPosition = ({int handle});

class SoundProps {
  SoundProps(this.handler);

  final int handler;
  double length = 0;
  double pos = 0;
  List<double> keys = [];
  bool checkEndPlayback = true;
}

/// Top Level audio isolate Function
///
/// The purpose of this isolate is:
/// - send back to main isolate the comunication port
/// - listen to messages from main isolate
/// - when a new message come,  execute it and send back the result
/// Since from C is difficult to call dart function from another thread for now,
/// I did this isolate with the main purpose to make use of some callbacks
/// like [playEndedCallback]. Ref: https://github.com/dart-lang/sdk/issues/37022
void audioIsolate(SendPort isolateToMainStream) {
  final mainToIsolateStream = ReceivePort();
  isolateToMainStream.send(mainToIsolateStream.sendPort);
  final soLoudController = SoLoudController();
  final List<SoundProps> activeSounds = [];
  bool loopRunning = false;

  soLoudController.initialize();

  mainToIsolateStream.listen((data) {
    print('*** MAIN TO ISOLATE data: $data');
    final event = data as Map<String, Object>;

    switch (event['event'] as MessageEvents) {
      case MessageEvents.exitIsolate:
        mainToIsolateStream.close();
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': (), 'return': ()});
        break;

      case MessageEvents.initEngine:
        final args = event['args'] as ArgsInitEngine;
        final ret = soLoudController.soLoudFFI.initEngine();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.disposeEngine:
        final args = event['args'] as ArgsDisposeEngine;
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.playFile:
        final args = event['args'] as ArgsPlayFile;
        final ret = soLoudController.soLoudFFI.playFile(args.completeFileName);
        // add the new sound handler to the list
        if (ret.error == PlayerErrors.noError) {
          activeSounds.add(SoundProps(ret.handle));
        }
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.play:
        final args = event['args'] as ArgsPlay;
        soLoudController.soLoudFFI.play(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.pauseSwitch:
        final args = event['args'] as ArgsPauseSwitch;
        soLoudController.soLoudFFI.pauseSwitch(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.getPause:
        final args = event['args'] as ArgsGetPause;
        final ret = soLoudController.soLoudFFI.getPause(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.stop:
        final args = event['args'] as ArgsStop;
        activeSounds.removeWhere((item) => item.handler == args.handle);
        soLoudController.soLoudFFI.stop(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.getLength:
        final args = event['args'] as ArgsGetLength;
        final ret = soLoudController.soLoudFFI.getLength(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.seek:
        final args = event['args'] as ArgsSeek;
        final ret = soLoudController.soLoudFFI.seek(args.handle, args.time);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.getPosition:
        final args = event['args'] as ArgsGetPosition;
        final ret = soLoudController.soLoudFFI.getPosition(args.handle);
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.startLoop:
        loopRunning = true;
        isolateToMainStream
            .send({'event': MessageEvents.startLoop, 'args': (), 'return': ()});
        mainToIsolateStream.sendPort.send(
          {
            'event': MessageEvents.loop,
            'args': (),
          },
        );
        break;

      case MessageEvents.stopLoop:
        loopRunning = false;
        isolateToMainStream
            .send({'event': MessageEvents.stopLoop, 'args': (), 'return': ()});
        break;

      case MessageEvents.loop:
        if (loopRunning) {
          for (final sound in activeSounds) {
            final ret = soLoudController.soLoudFFI.getPosition(sound.handler);
            print('************* handler: ${sound.handler}  pos: $ret');
            if (ret > 4.0 && ret < 5.15)
              print('AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA');
          }
          Future.delayed(const Duration(milliseconds: 10), () {
            mainToIsolateStream.sendPort.send(
              {'event': MessageEvents.loop, 'args': ()},
            );
          });
        }
        break;

      default:
        print('No event with that name!');
    }
  });
}

class AudioIsolate {
  factory AudioIsolate() => _instance ??= AudioIsolate._();

  AudioIsolate._();

  static AudioIsolate? _instance;

  SendPort? _mainToIsolateStream;

  StreamController<dynamic>? _returnedEvent;
  Isolate? _isolate;
  ReceivePort? _isolateToMainStream;

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
        print('******** ISOLATE TO MAIN: $data');
        _returnedEvent?.add(data);
      }
    });

    _isolate = await Isolate.spawn(audioIsolate, _isolateToMainStream!.sendPort);
    return completer.future;
  }

  /// Gracefully kill the isolate
  Future<void> stopIsolate() async {
    if (_isolate == null) return;
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
  Future<dynamic> _waitForEvent(MessageEvents event, Record args) async {
    final Completer<dynamic> completer = Completer();

    final ret = await _returnedEvent?.stream.firstWhere((element) {
      final e = element as Map<String, Object?>;

      // if the event with its args are what we are waiting for...
      if ((e['event'] as MessageEvents) != event) return false;
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
        'event': MessageEvents.startLoop,
        'args': (),
      },
    );
    return _waitForEvent(MessageEvents.startLoop, ());
  }

  Future<void> stoptLoop() async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.stopLoop,
        'args': (),
      },
    );
    return _waitForEvent(MessageEvents.stopLoop, ());
  }

  //////////////////////////////////////////////////
  /// Below all the methods implemented with FFI
  //////////////////////////////////////////////////
  Future<PlayerErrors> initEngine() async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.initEngine,
        'args': (),
      },
    );
    return (await _waitForEvent(MessageEvents.initEngine, ())) as PlayerErrors;
  }

  Future<void> disposeEngine() async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeEngine,
        'args': (),
      },
    );
    return _waitForEvent(MessageEvents.disposeEngine, ());
  }

  Future<({PlayerErrors error, int handle})> playFile(
      String completeFileName) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.playFile,
        'args': (completeFileName: completeFileName),
      },
    );

    final ret = (await _waitForEvent(
      MessageEvents.playFile,
      (completeFileName: completeFileName),
    )) as ({PlayerErrors error, int handle});
    return ret;
  }

  Future<void> play(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.play,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(MessageEvents.play, (handle: handle));
  }

  Future<void> pauseSwitch(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.pauseSwitch,
        'args': (handle: handle),
      },
    );
    return _waitForEvent(MessageEvents.pauseSwitch, (handle: handle));
  }

  Future<bool> getPause(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.getPause,
        'args': (handle: handle),
      },
    );

    final ret = await _waitForEvent(MessageEvents.getPause, (handle: handle));
    return ret as bool;
  }

  Future<void> stop(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.stop,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(MessageEvents.stop, (handle: handle));
  }

  Future<double> getLength(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.getLength,
        'args': (handle: handle),
      },
    );
    final ret = (await _waitForEvent(MessageEvents.getLength, (handle: handle)))
        as double;
    return ret;
  }

  Future<PlayerErrors> seek(int handle, double time) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.seek,
        'args': (handle: handle, time: time),
      },
    );
    final ret =
        (await _waitForEvent(MessageEvents.seek, (handle: handle, time: time)))
            as int;
    return PlayerErrors.values[ret];
  }

  Future<double> getPosition(int handle) async {
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.getPosition,
        'args': (handle: handle),
      },
    );
    final ret =
        (await _waitForEvent(MessageEvents.getPosition, (handle: handle)))
            as double;
    return ret;
  }
}

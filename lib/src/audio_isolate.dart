// ignore_for_file: require_trailing_commas, public_member_api_docs, 
// ignore_for_file: unnecessary_breaks

import 'dart:async';
import 'dart:isolate';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/src/flutter_soloud_bindings_ffi.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/soloud_controller.dart';

/// Author note: I am a bit scared on how the use of
/// these 2 isolates implementation is gone. But hey,
/// Records saved my life! \O/

/// print some infos when isolate receive events
/// from main isolate and vice versa
void debugIsolates(String text) {
  // print(text);
}

enum MessageEvents {
  exitIsolate,
  initEngine,
  disposeEngine,
  startLoop,
  stopLoop,
  loop,
  loadFile,
  speechText,
  play,
  play3d,
}

/// definitions to be checked in main isolate
typedef ArgsInitEngine = ();
typedef ArgsDisposeEngine = ();
typedef ArgsLoadFile = ({String completeFileName});
typedef ArgsSpeechText = ({String textToSpeech});
typedef ArgsPlay = ({int soundHash, double volume, double pan, bool paused});
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

/// Top Level audio isolate function
///
/// The purpose of this isolate is:
/// - send back to main isolate the communication port
/// - listen to messages from main isolate
/// - when a new message come, execute it and send back the result
/// Since from C is difficult to call dart function from another thread for now,
/// I did this isolate with the main purpose to make use of some callbacks
/// like playEndedCallback. Ref: https://github.com/dart-lang/sdk/issues/37022
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
    if ((event['event']! as MessageEvents) != MessageEvents.loop) {
      /// don't print the loop message
      debugIsolates('******** ISOLATE EVENT data: $data');
    }

    switch (event['event']! as MessageEvents) {
      case MessageEvents.exitIsolate:
        mainToIsolateStream.close();
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': (), 'return': ()});
        break;

      case MessageEvents.initEngine:
        final args = event['args']! as ArgsInitEngine;
        final ret = soLoudController.soLoudFFI.initEngine();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ret});
        break;

      case MessageEvents.disposeEngine:
        final args = event['args']! as ArgsDisposeEngine;
        soLoudController.soLoudFFI.dispose();
        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.loadFile:
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

      case MessageEvents.speechText:
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

      case MessageEvents.play:
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

      //////////////////////////////////
      /// 3D audio

      case MessageEvents.play3d:
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
      case MessageEvents.startLoop:
        loopRunning = true;
        isolateToMainStream.send(
            {'event': MessageEvents.startLoop, 'args': (), 'return': ()});
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
                /// later, outside the loop, remove the handle
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
            // TODO(me): is 10 ms ok to loop again?
            mainToIsolateStream.sendPort.send(
              {'event': MessageEvents.loop, 'args': ()},
            );
          });
        }
        break;
    }
  });
}

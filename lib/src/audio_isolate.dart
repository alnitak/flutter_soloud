// ignore_for_file: require_trailing_commas, public_member_api_docs,
// ignore_for_file: unnecessary_breaks

import 'dart:async';
import 'dart:isolate';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/soloud_controller.dart';

/// print some infos when isolate receive events
/// from main isolate and vice versa
void debugIsolates(String text) {
  // TODO(filiph): Decide how to forward logging from isolate to main thread?
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
  loadWaveform,
  speechText,
  play,
  play3d,
  stop,
  disposeSound,
  disposeAllSound,
}

/// definitions to be checked in main isolate
typedef ArgsInitEngine = ();
typedef ArgsDisposeEngine = ();
typedef ArgsLoadFile = ({String completeFileName});
typedef ArgsLoadWaveform = ({
  int waveForm,
  bool superWave,
  double scale,
  double detune,
});
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
typedef ArgsStop = ({int handle});
typedef ArgsDisposeSound = ({int soundHash});
typedef ArgsDisposeAllSound = ();

/// Top Level audio isolate function
///
/// The purpose of this isolate is:
/// - send back to main isolate the communication port
/// - listen to messages from main isolate
/// - when a new message come, execute it and send back the result
/// Since from C is difficult to call dart function from another thread for now,
/// I did this isolate with the main purpose to make use of some callbacks
/// like playEndedCallback. Ref: https://github.com/dart-lang/sdk/issues/37022
/// MD https://github.com/mraleph/language/blob/shared-memory/working/333%20-%20shared%20memory%20multithreading/proposal.md
/// PR doc Shared Memory Multithreading https://github.com/dart-lang/language/pull/3531
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
        } else if (ret.error == PlayerErrors.fileAlreadyLoaded) {
          /// the file is already loaded.
          /// Check if it is already in [activeSound] else add it
          var isAlreadyThere = true;
          newSound = activeSounds.firstWhere(
            (s) => s.soundHash == ret.soundHash,
            orElse: () {
              isAlreadyThere = false;
              return SoundProps(ret.soundHash);
            },
          );
          if (!isAlreadyThere) activeSounds.add(newSound);
        }
        isolateToMainStream.send({
          'event': event['event'],
          'args': args,
          'return': (error: ret.error, sound: newSound),
        });
        break;

      case MessageEvents.loadWaveform:
        final args = event['args']! as ArgsLoadWaveform;
        final ret = soLoudController.soLoudFFI.loadWaveform(
          WaveForm.values[args.waveForm],
          args.superWave,
          args.scale,
          args.detune,
        );
        // add the new sound handler to the list
        SoundProps? newSound;
        if (ret.error == PlayerErrors.noError) {
          newSound = SoundProps(ret.soundHash);
          activeSounds.add(newSound);
        } else if (ret.error == PlayerErrors.fileAlreadyLoaded) {
          /// the file is already loaded.
          /// Check if it is already in [activeSound] else add it
          var isAlreadyThere = true;
          newSound = activeSounds.firstWhere(
            (s) => s.soundHash == ret.soundHash,
            orElse: () {
              isAlreadyThere = false;
              return SoundProps(ret.soundHash);
            },
          );
          if (!isAlreadyThere) activeSounds.add(newSound);
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

      case MessageEvents.stop:
        final args = event['args']! as ArgsStop;
        soLoudController.soLoudFFI.stop(args.handle);

        /// find a sound with this handle and remove that handle from the list
        for (final sound in activeSounds) {
          sound.handle.removeWhere((element) => element == args.handle);
        }

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.disposeSound:
        final args = event['args']! as ArgsDisposeSound;
        soLoudController.soLoudFFI.disposeSound(args.soundHash);

        /// find a sound with this handle and remove that handle from the list
        activeSounds
            .removeWhere((element) => element.soundHash == args.soundHash);

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
        break;

      case MessageEvents.disposeAllSound:
        final args = event['args']! as ArgsDisposeAllSound;
        soLoudController.soLoudFFI.disposeAllSound();

        /// send the [SoundEvent.soundDisposed] event to main isolate
        for (final sound in activeSounds) {
          isolateToMainStream.send(
            (
              event: SoundEvent.soundDisposed,
              sound: sound,
              handle: 0,
            ),
          );
        }

        /// Clear the sound list
        activeSounds.clear();

        isolateToMainStream
            .send({'event': event['event'], 'args': args, 'return': ()});
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
            final removeInvalid = <void Function()>[];
            // check valids handles in [sound] list
            for (final handle in sound.handle) {
              final isValid =
                  soLoudController.soLoudFFI.getIsValidVoiceHandle(handle);
              if (!isValid) {
                /// later, outside the loop, remove the handle
                removeInvalid.add(() {
                  sound.handle.remove(handle);

                  isolateToMainStream.send(
                    (
                      event: SoundEvent.handleIsNoMoreValid,
                      sound: sound,
                      handle: handle,
                    ),
                  );
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

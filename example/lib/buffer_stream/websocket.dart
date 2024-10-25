// ignore_for_file: avoid_print, unnecessary_lambdas

import 'dart:async';
import 'dart:developer' as dev;
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/buffer_stream/ui/buffer_widget.dart';
import 'package:logging/logging.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

///

/// feat: send PCM audio data to the player
///
/// Would be nice to be able to send PCM data to the player
/// and have the player be able to render it.
/// This could be done extending the SoLoud C++
/// [AudioSource](https://solhsa.com/soloud/newsoundsources.html) class to
/// have a `BufferStream` class that can be used as a normal AudioSource sound.
///
/// The advantages of this is that the player can be used as a normal
/// flutter_soloud [AudioSource] and can be used to play audio data that could
/// be retrieved from the web or created directly from Flutter. It can then be
/// able to use all the features of the flutter_soloud like filters, faders,
/// oscillators and so on.
/// Think for example of some AI web services that let you compose voices like
/// [this](https://elevenlabs.io/docs/api-reference/websockets).
///
/// Would also be nice to have a way to send PCM data directly to the output
/// device without having to create a new [AudioSource]. I short compose
/// some sound in Flutter and then send it to the player.
/// But this is maybe for another feature.
///
///
///
///
/// Experimenting with the `BufferStream` I come with some good results, but
/// opinion and suggestions are welcome.
///
/// What I did so far is to simulate a websocket connection that stream PCM data
/// using `ffmpeg`.
/// [Here](https://github.com/alnitak/websocketd) the source code. The only
/// needs are to have `ffmpeg` and `websocketd` installed, and in the
/// `main.dart` modify `final audioPath` to point to an audio file on your PC.
///
/// [IMMAGINE WEBSOCKETD]
/// *Here you choose in which format you want to send the PCM audio data.*
///
/// In the `websocket` branch there is an example that demostrate this feature
/// in `lib/buffer_stream/websocket.dart' file.
/// Another example `lib/buffer_stream/generate.dart` is an example of how to
/// generate PCM audio data within an Isolate and send them to the AudioBuffer.
///
/// [ANIMATED GIF EXAMPLE]
///
/// I am working on Linux, should work also on Mac and Windows.
///
/// cc: maybe @maks could be interested in this?
///
///
///
/// TODO: check win, mac, web, ios
/// TODO: provare a mandare dati da un isolate
/// TODO: aggiungere README a websocketd

void main() async {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
  // You can listen to the logs as shown below.
  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
  });

  WidgetsFlutterBinding.ensureInitialized();

  /// Initialize the player.
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: WebsocketExample(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class WebsocketExample extends StatefulWidget {
  const WebsocketExample({super.key});

  @override
  State<WebsocketExample> createState() => _WebsocketExampleState();
}

class _WebsocketExampleState extends State<WebsocketExample> {
  final websocketUri = 'ws://HAL:8080/';
  final sampleRate = [11025, 22050, 44100, 48000];
  final format = ['f32le', 's8', 's16le', 's32le'];
  int srId = 2;
  int chId = 0;
  int fmtId = 0;
  WebSocket? webSocket;
  WebSocketChannel? channel;
  AudioSource? currentSound;
  SoundHandle? handle;
  int numberOfChunks = 0;
  int byteSize = 0;
  final isBuffering = ValueNotifier(false);

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              /// SAMPLERATE
              Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  for (var i = 0; i < sampleRate.length; i++)
                    SizedBox(
                      width: 160,
                      child: RadioListTile<int>(
                        title: Text(sampleRate[i].toString()),
                        value: i,
                        groupValue: srId,
                        onChanged: (int? value) {
                          setState(() {
                            srId = value!;
                          });
                        },
                      ),
                    ),
                ],
              ),

              /// CHANNELS
              Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  for (var i = 0; i < Channels.values.length; i++)
                    SizedBox(
                      width: 210,
                      child: RadioListTile<int>(
                        title: Text(Channels.values[i].toString()),
                        value: i,
                        groupValue: chId,
                        onChanged: (int? value) {
                          setState(() {
                            chId = value!;
                          });
                        },
                      ),
                    ),
                ],
              ),

              /// FORMAT
              Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  for (var i = 0; i < format.length; i++)
                    SizedBox(
                      width: 160,
                      child: RadioListTile<int>(
                        title: Text(format[i]),
                        value: i,
                        groupValue: fmtId,
                        onChanged: (int? value) {
                          setState(() {
                            fmtId = value!;
                          });
                        },
                      ),
                    ),
                ],
              ),
            ],
          ),
          OutlinedButton(
            onPressed: () async {
              await channel?.sink.close();
              await SoLoud.instance.disposeAllSources();

              currentSound = SoLoud.instance.setBufferStream(
                maxBufferSize: 1024 * 1024 * 600, // 150 MB
                sampleRate: sampleRate[srId],
                channels: Channels.values[chId],
                pcmFormat: BufferPcmType.values[fmtId],
                onBuffering: () async {
                  if (context.mounted) {
                    setState(() {
                      isBuffering.value = !isBuffering.value;
                    });
                  }
                  SoLoud.instance.pauseSwitch(handle!);
                  await Future.delayed(const Duration(seconds: 5), () {});
                  SoLoud.instance.pauseSwitch(handle!);
                  if (context.mounted) {
                    setState(() {
                      isBuffering.value = !isBuffering.value;
                    });
                  }
                },
              );
              setState(() {});
            },
            child: const Text('set chosen stream type'),
          ),
          const SizedBox(height: 16),
          OutlinedButton(
            onPressed: () async {
              if (currentSound == null) {
                debugPrint('A stream has not been set yet!');
                return;
              }
              /// Connect to the websocket
              final wsUrl = Uri.parse(websocketUri);
              channel = WebSocketChannel.connect(wsUrl);

              /// Wait for the websocket to be ready
              try {
                await channel?.ready;
              } on SocketException catch (e) {
                debugPrint(e.toString());
              } on WebSocketChannelException catch (e) {
                debugPrint(e.toString());
              }

              /// Listen to the websocket
              channel?.stream.listen(
                (message) async {
                  numberOfChunks++;
                  byteSize += (message as List<int>).length;

                  try {
                    SoLoud.instance.addAudioDataStream(
                      currentSound!,
                      Uint8List.fromList(message),
                    );
                  } on SoLoudPcmBufferFullOrStreamEndedCppException {
                    debugPrint('pcm buffer full or stream already set '
                        'to be ended');
                    await channel?.sink.close();
                  }
                },
                onDone: () {
                  if (currentSound != null) {
                    SoLoud.instance.setDataIsEnded(currentSound!);
                  }
                  debugPrint('ws channel closed. '
                      'numberOfChunks: $numberOfChunks  byteSize: $byteSize');
                  numberOfChunks = 0;
                  byteSize = 0;
                },
                onError: (error) {},
              );
            },
            child: const Text('connect to ws and receive audio data'),
          ),
          const SizedBox(height: 16),
          OutlinedButton(
            onPressed: () async {
              if (currentSound == null) return;
              handle = await SoLoud.instance.play(
                currentSound!,
                // looping: true,
              );
              print('handle: $handle');
              Timer.periodic(const Duration(milliseconds: 1000), (timer) {
                if (currentSound == null ||
                    SoLoud.instance.getIsValidVoiceHandle(handle!) == false) {
                  timer.cancel();
                  setState(() {});
                }
              });
            },
            child: const Text('paly'),
          ),
          const SizedBox(height: 16),
          OutlinedButton(
            onPressed: () async {
              currentSound = null;
              await SoLoud.instance.disposeAllSources();
              await channel?.sink.close();
              setState(() {});
            },
            child: const Text('stop all sounds and close ws'),
          ),
          const SizedBox(height: 16),
          BufferBar(sound: currentSound),
          const SizedBox(height: 16),
          ValueListenableBuilder(
            valueListenable: isBuffering,
            builder: (_, value, __) {
              if (value) {
                return const Text('BUFFERING!');
              }
              return const SizedBox.shrink();
            },
          ),
        ],
      ),
    );
  }
}

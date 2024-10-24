// ignore_for_file: avoid_print, unnecessary_lambdas

import 'dart:async';
import 'dart:developer' as dev;
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/buffer_stream/buffer_widget.dart';
import 'package:logging/logging.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

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
      home: HelloFlutterSoLoud(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
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
                maxBufferSize: 1024 * 1024 * 300, // 100 MB
                sampleRate: sampleRate[srId],
                channels: Channels.values[chId],
                pcmFormat: BufferPcmType.values[fmtId],
                onBuffering: () async {
                  print('DART onBuffering!!!!!!!!!!!!!!!!!!!!!!!!!');
                  SoLoud.instance.pauseSwitch(handle!);
                  await Future.delayed(const Duration(seconds: 5), () {});
                  SoLoud.instance.pauseSwitch(handle!);
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
              final wsUrl = Uri.parse(websocketUri);
              channel = WebSocketChannel.connect(wsUrl);
      
              try {
                await channel?.ready;
              } on SocketException catch (e) {
                debugPrint(e.toString());
              } on WebSocketChannelException catch (e) {
                debugPrint(e.toString());
              }
      
              channel?.stream.listen(
                (message) async {
                  numberOfChunks++;
                  byteSize += (message as List<int>).length;
      
                  try {
                    SoLoud.instance.addAudioDataStream(
                      currentSound!.soundHash.hash,
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
                looping: true,
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
            },
            child: const Text('stop all sounds and close ws'),
          ),
          const SizedBox(height: 16),
          BufferBar(sound: currentSound),
        ],
      ),
    );
  }
}

// ignore_for_file: avoid_print, unnecessary_lambdas

import 'dart:async';
import 'dart:developer' as dev;
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
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
  final file = File('/home/deimos/receivedPCM.pcm');
  final websocketUri = 'ws://HAL:8080/';
  final sampleRate = [11025, 22050, 44100, 48000];
  final channels = [1, 2];
  final format = ['f32le', 's8', 's16le', 's32le'];
  int srId = 2;
  int chId = 0;
  int fmtId = 0;
  bool writeToFile = false;
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

    if (writeToFile && file.existsSync()) {
      file
        ..deleteSync()
        ..createSync();
    }

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
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
                    for (var i = 0; i < channels.length; i++)
                      SizedBox(
                        width: 100,
                        child: RadioListTile<int>(
                          title: Text(channels[i].toString()),
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

                currentSound = await SoLoud.instance.setBufferStream(
                  'uniqueName',
                  1024 * 1024 * 200, // 100 MB
                  sampleRate[srId],
                  channels[chId],
                  BufferPcmType.values[fmtId],
                );
              },
              child: const Text('set chosen stream type'),
            ),
            const SizedBox(height: 16),
            OutlinedButton(
              onPressed: () async {
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

                    SoLoud.instance.addAudioDataStream(
                      currentSound!.soundHash.hash,
                      Uint8List.fromList(message),
                    );

                    if (writeToFile) {
                      file.writeAsBytesSync(
                        message,
                        mode: FileMode.append,
                      );
                    }
                    // debugPrint('numberOfChunks: $numberOfChunks, '
                    //     'byteSize: $byteSize, last chunk: ${message.length}');
                  },
                  onDone: () {
                    SoLoud.instance.setDataIsEnded(currentSound!);
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
                handle = await SoLoud.instance.play(currentSound!);
                print('handle: $handle');
                Timer.periodic(const Duration(milliseconds: 1000), (timer) {
                  if (SoLoud.instance.getIsValidVoiceHandle(handle!) == false) {
                    timer.cancel();
                  }
                  final d = SoLoud.instance.getLength(currentSound!);
                  final pos = SoLoud.instance.getPosition(handle!);
                  print('d: $d, pos: $pos');
                });
              },
              child: const Text('paly'),
            ),
            const SizedBox(height: 16),
            OutlinedButton(
              onPressed: () async {
                await SoLoud.instance.disposeAllSources();
                await channel?.sink.close();
              },
              child: const Text('stop all sounds and close ws'),
            ),
          ],
        ),
      ),
    );
  }
}

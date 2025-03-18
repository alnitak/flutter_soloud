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

/// This example shows how to use BufferStream with a websocket.
///
/// You must have a server which streams audio data. Here a repo that uses
/// websocketd as a server and ffmpeg to provide audio data:
/// https://github.com/alnitak/websocketd
///
/// Run it and choose which audio format you want to stream and the speed for
/// testing BufferStream buffering.
/// Then run this example and choose the same audio format.

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
  await SoLoud.instance.init(sampleRate: 24000);

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
  final websocketUri = 'ws://192.168.1.2:8080/';
  // Supported Opus sample rates:
  // 48000
  // 24000
  // 16000
  // 12000
  // 8000
  final sampleRate = [8000, 12000, 16000, 24000, 44100, 48000];
  final format = ['f32le', 's8', 's16le', 's32le', 'opus'];
  int srId = 5;
  int chId = 0;
  int fmtId = 4;
  WebSocketChannel? channel;
  AudioSource? currentSound;
  SoundHandle? handle;
  int numberOfChunks = 0;
  int byteSize = 0;
  final streamBuffering = ValueNotifier(false);

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      appBar: AppBar(
        title: const Text('PCM audio data from a websocket Example'),
      ),
      body: Column(
        mainAxisSize: MainAxisSize.min,
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              /// SAMPLERATE
              Expanded(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    for (var i = 0; i < sampleRate.length; i++)
                      RadioListTile<int>(
                        title: Text(
                          sampleRate[i].toString(),
                          style: const TextStyle(fontSize: 12),
                        ),
                        value: i,
                        visualDensity: const VisualDensity(
                          horizontal: VisualDensity.minimumDensity,
                          vertical: VisualDensity.minimumDensity,
                        ),
                        materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        groupValue: srId,
                        onChanged: (int? value) {
                          setState(() {
                            srId = value!;
                          });
                        },
                      ),
                  ],
                ),
              ),

              /// CHANNELS
              Expanded(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    for (var i = 0; i < Channels.values.length; i++)
                      RadioListTile<int>(
                        title: Text(
                          Channels.values[i].toString(),
                          style: const TextStyle(fontSize: 12),
                        ),
                        value: i,
                        visualDensity: const VisualDensity(
                          horizontal: VisualDensity.minimumDensity,
                          vertical: VisualDensity.minimumDensity,
                        ),
                        materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        groupValue: chId,
                        onChanged: (int? value) {
                          setState(() {
                            chId = value!;
                          });
                        },
                      ),
                  ],
                ),
              ),

              /// FORMAT
              Expanded(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    for (var i = 0; i < format.length; i++)
                      RadioListTile<int>(
                        title: Text(
                          format[i],
                          style: const TextStyle(fontSize: 12),
                        ),
                        value: i,
                        visualDensity: const VisualDensity(
                          horizontal: VisualDensity.minimumDensity,
                          vertical: VisualDensity.minimumDensity,
                        ),
                        materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        groupValue: fmtId,
                        onChanged: (int? value) {
                          setState(() {
                            fmtId = value!;
                          });
                        },
                      ),
                  ],
                ),
              ),
            ],
          ),
          OutlinedButton(
            onPressed: () async {
              await channel?.sink.close();
              await SoLoud.instance.disposeAllSources();
              streamBuffering.value = false;

              currentSound = SoLoud.instance.setBufferStream(
                // maxBufferSizeBytes: 1024 * 1024 * 200, // 200 MB
                maxBufferSizeDuration: const Duration(minutes: 5),
                bufferingTimeNeeds: 0.5,
                sampleRate: sampleRate[srId],
                channels: Channels.values[chId],
                format: BufferType.values[fmtId],
                // ignore: avoid_redundant_argument_values
                bufferingType: BufferingType.preserved,
                onBuffering: (isBuffering, handle, time) async {
                  debugPrint('started buffering? $isBuffering  with '
                      'handle: $handle at time $time');
                  if (context.mounted) {
                    setState(() {
                      streamBuffering.value = !streamBuffering.value;
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
              /// This could be a way to receive PCM data from some sources and
              /// use it to create a new [AudioSource].
              /// Could be done inside an Isolate for example to manage received
              /// data if it is in JSON format or the audio data is base64.
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
                  } on Exception catch (e) {
                    debugPrint('error adding audio data: $e');
                    await channel?.sink.close();
                  }

                  // start playing at first audio chunk received
                  if (numberOfChunks == 1) {
                    handle = await SoLoud.instance.play(currentSound!);
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
                onError: (Object error) {
                  debugPrint('ws error: $error');
                },
              );
            },
            child: const Text('connect to WS and receive audio data'),
          ),
          const SizedBox(height: 16),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              OutlinedButton(
                onPressed: () async {
                  if (currentSound == null) return;
                  handle = await SoLoud.instance.play(
                    currentSound!,
                    volume: 0.6,
                    // looping: true,
                  );
                  Timer.periodic(const Duration(milliseconds: 1000), (timer) {
                    if (currentSound == null ||
                        SoLoud.instance.getIsValidVoiceHandle(handle!) ==
                            false) {
                      timer.cancel();
                      setState(() {});
                    }
                  });
                },
                child: const Text('play'),
              ),
              const SizedBox(width: 8),
              OutlinedButton(
                onPressed: () async {
                  if (currentSound == null) return;
                  SoLoud.instance.resetBufferStream(currentSound!);
                },
                child: const Text('reset buffer'),
              ),
              const SizedBox(width: 8),
              OutlinedButton(
                onPressed: () async {
                  currentSound = null;
                  await SoLoud.instance.disposeAllSources();
                  await channel?.sink.close();
                  setState(() {});
                },
                child: const Text('stop all sounds and close ws'),
              ),
            ],
          ),
          const SizedBox(height: 16),
          BufferBar(sound: currentSound),
          const SizedBox(height: 16),
          ValueListenableBuilder(
            valueListenable: streamBuffering,
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

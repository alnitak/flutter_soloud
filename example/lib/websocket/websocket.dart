// ignore_for_file: avoid_print, unnecessary_lambdas

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
  WebSocket? webSocket;
  AudioSource? currentSound;
  bool isFirstChunk = true;

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ElevatedButton(
              onPressed: () async {
                final wsUrl = Uri.parse('ws://HAL:8080/');
                final channel = WebSocketChannel.connect(wsUrl);

                await channel.ready;

                channel.stream.listen((message) async {
                  // channel.sink.add('received!');
                  // channel.sink.close(status.goingAway);
                  print(message);
                  if (isFirstChunk) {
                    currentSound = await SoLoud.instance.loadAudioStream(
                      'uniqueName',
                      message as Uint8List,
                      1024 * 1024 * 50,
                      44100,
                      2,
                      4,
                      0,
                    );
                    isFirstChunk = false;
                  } else {
                    SoLoudController().soLoudFFI.addAudioDataStream(
                          currentSound!.soundHash.hash,
                          message as Uint8List,
                        );
                  }
                });
              },
              child: const Text('connect'),
            ),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () async {
                await SoLoud.instance.play(currentSound!);
              },
              child: const Text('paly'),
            ),
          ],
        ),
      ),
    );
  }
}


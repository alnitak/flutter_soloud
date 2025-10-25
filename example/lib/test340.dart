import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

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
  final soloud = SoLoud.instance;
  List<AudioSource> sounds = [];
  final List<String> assets = [
    'assets/audio/12Bands/audiocheck.net_sin_16Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_31.5Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_63Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_125Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_250Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_500Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_1000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_2000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_4000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_8000Hz_-3dBFS_2s.wav',
  ];

  @override
  void dispose() {
    soloud.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 12,
          children: [
            ElevatedButton(
              onPressed: () async {
                /// Initialize the player.
                await SoLoud.instance.init();
                sounds.clear();

                await Future.wait([
                  for (final soundEffect in assets)
                    () async {
                      final source = await soloud.loadAsset(soundEffect);
                      sounds.add(source);
                    }(),
                ]);

                soloud.deinit();

                debugPrint('${sounds.length} sounds loaded');
              },
              child: const Text('load async'),
            ),
            ElevatedButton(
              onPressed: () async {
                /// Initialize the player.
                await SoLoud.instance.init();
                sounds.clear();

                for (final soundEffect in assets) {
                  final source = await soloud.loadAsset(soundEffect);
                  sounds.add(source);
                }

                soloud.deinit();

                debugPrint('${sounds.length} sounds loaded');
              },
              child: const Text('load sync'),
            ),
          ],
        ),
      ),
    );
  }
}

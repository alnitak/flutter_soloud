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
  final soloud = SoLoud.instance;
  AudioSource? currentSound;

  @override
  void dispose() {
    soloud.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!soloud.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 30,
          children: [
            ElevatedButton(
              onPressed: () async {
                /// This will eventually dispose (and stop) any previously
                /// loaded sources.
                await soloud.disposeAllSources();

                /// Load the audio source. Note that the audio is loaded
                /// into memory and should manually disposed to free it.
                if (kIsWeb) {
                  /// Load the audio file using [LoadMode.disk] (mandatory for
                  /// the Web platform).
                  currentSound = await soloud.loadAsset(
                    'assets/audio/8_bit_mentality.mp3',
                    mode: LoadMode.disk,
                  );
                } else {
                  /// Load the audio file
                  currentSound = await soloud
                      .loadAsset('assets/audio/8_bit_mentality.mp3');
                }

                /// Play it. The sound will be instantly available when it
                /// needs to be played until it's disposed.
                await soloud.play(currentSound!);
              },
              child: const Text(
                'play asset',
                textAlign: TextAlign.center,
              ),
            ),
            ElevatedButton(
              onPressed: () async {
                /// Just play a sound from a source. It will start playing
                /// as soon as it will take to load it.
                /// The sound will be disposed automatically when it's finished.
                await soloud.playSource(asset: 'assets/audio/explosion.mp3');
              },
              child: const Text(
                'play source',
                textAlign: TextAlign.center,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

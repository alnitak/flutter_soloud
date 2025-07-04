import 'dart:developer' as dev;
import 'dart:io';

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
  AudioSource? currentSound;

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
        child: ElevatedButton(
          onPressed: () async {
            // read the file bytes from "home/deimos/8/sample-1.ogg"
            // final f = File('/home/deimos/8/sample-1.ogg');
            // final bytes = f.readAsBytesSync();

            // final waveformData = await SoLoud.instance.readSamplesFromMem(
            //   bytes,
            //   2000,
            //   // average: true, // Average samples to smooth out the waveform
            // );

            final waveformData = await SoLoud.instance.readSamplesFromFile(
              '/home/deimos/8/sample-1.ogg',
              2000,
              // average: true, // Average samples to smooth out the waveform
            );
            bool b;
            b = true;
            print(b);
          },
          child: const Text(
            'play asset',
            textAlign: TextAlign.center,
          ),
        ),
      ),
    );
  }
}

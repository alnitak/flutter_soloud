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
  String audioFocusState = '';
  HeadsetInfo? headsetInfo;
  AudioSource? currentSound;

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 16,
          children: [
            ElevatedButton(
              onPressed: () async {
                await SoLoud.instance.interruptions.initAndroidInterruptions();
                SoLoud.instance.interruptions.headsetStateAndroidEvents.listen(
                  (event) => setState(() {
                    headsetInfo = event;
                  }),
                );

                SoLoud.instance.interruptions.audioAndroidFocusEvents.listen(
                  (event) => setState(() {
                    audioFocusState = event.name;
                  }),
                );
              },
              child: const Text('listen interruptions'),
            ),
            ElevatedButton(
              onPressed: () async {
                await SoLoud.instance.init();
                currentSound = null;
              },
              child: const Text('init'),
            ),
            ElevatedButton(
              onPressed: () async {
                await SoLoud.instance.disposeAllSources();

                if (kIsWeb) {
                  /// load the audio file using [LoadMode.disk] (better for the
                  /// Web platform).
                  currentSound = await SoLoud.instance.loadAsset(
                    'assets/audio/8_bit_mentality.mp3',
                    mode: LoadMode.disk,
                  );
                } else {
                  /// load the audio file
                  currentSound = await SoLoud.instance
                      .loadAsset('assets/audio/8_bit_mentality.mp3');
                }

                /// play it
                await SoLoud.instance.play(currentSound!, looping: true);
              },
              child: const Text(
                'play asset',
                textAlign: TextAlign.center,
              ),
            ),
            ElevatedButton(
              onPressed: () {
                SoLoud.instance.deinit();
                currentSound = null;
              },
              child: const Text('deinit'),
            ),
            Text(
                'Audio focus state:\n$audioFocusState\n\nHeadset info:\n$headsetInfo'),
          ],
        ),
      ),
    );
  }
}

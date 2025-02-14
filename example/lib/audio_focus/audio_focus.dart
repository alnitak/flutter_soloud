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
  String androidFocusState = '';
  String playerState = '';
  HeadsetInfo? androidHeadsetInfo;
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
                    androidHeadsetInfo = event;
                  }),
                );

                SoLoud.instance.interruptions.audioAndroidFocusEvents.listen(
                  (event) => setState(() {
                    androidFocusState = event.name;
                  }),
                );

                SoLoud.instance.interruptions.stateChangedEvents.listen(
                  (event) => setState(() {
                    playerState = event.name;
                  }),
                );
              },
              child: const Text('listen interruptions'),
            ),
            ElevatedButton(
              onPressed: () async {
                await SoLoud.instance.init();

                /// load the audio file
                currentSound = await SoLoud.instance
                    .loadAsset('assets/audio/8_bit_mentality.mp3');

                /// play it
                await SoLoud.instance.play(
                  currentSound!,
                  looping: true,
                  volume: 0.3,
                );
              },
              child: const Text('init & play asset'),
            ),
            ElevatedButton(
              onPressed: () {
                SoLoud.instance.deinit();
                currentSound = null;
              },
              child: const Text('deinit'),
            ),
            Text('Player state:\n$playerState\n\n'
                'Android audio focus:\n$androidFocusState\n\n'
                'Android Headset info:\n$androidHeadsetInfo'),
          ],
        ),
      ),
    );
  }
}

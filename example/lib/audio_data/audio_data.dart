import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/audio_data/data_widget.dart';
import 'package:logging/logging.dart';

/// Example on how [AudioData] can be used.
///
/// After [SoLoud] player is initialized, we need to activate the
/// visualization with [SoLoud.setVisualizationEnabled]. Without this is not
/// possible to read audio and FFT data.
///
/// Optionally [SoLoud.setFftSmoothing] is used to smooth FFT data.
///
/// [AudioDataWidget] visualizes FFT and wave data using a CustomPainter.
/// It uses a [Ticker] to update the audio data to be read later in
/// the CustomPainter.
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
  await SoLoud.instance.init(bufferSize: 1024, channels: Channels.mono);

  /// Activate the visualization. Mandatory to acquire audio data.
  SoLoud.instance.setVisualizationEnabled(true);

  /// Smooth FFT data.
  SoLoud.instance.setFftSmoothing(0.93);

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
  void initState() {
    super.initState();
    SoLoud.instance
        .loadAsset('assets/audio/8_bit_mentality.mp3', mode: LoadMode.disk)
        .then((value) {
      currentSound = value;
      SoLoud.instance.play(currentSound!, looping: true, volume: 0.5);
      if (context.mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (currentSound == null) return const SizedBox.shrink();

    return const Scaffold(
      body: AudioDataWidget(),
    );
  }
}

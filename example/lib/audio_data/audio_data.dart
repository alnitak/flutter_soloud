// ignore_for_file: avoid_redundant_argument_values

import 'dart:developer' as dev;
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/audio_data/data_widget.dart';
import 'package:logging/logging.dart';

/// Example on how [AudioVisualizationData] can be used.
///
/// After [SoLoud] player is initialized, we need to activate the
/// visualization with [SoLoud.setVisualizationEnabled].
///
/// Audio data packets are received reactively via
/// [SoLoud.audioVisualizationEvents].
///
/// Optionally [SoLoud.setFftSmoothing] is used to smooth FFT data.
///
/// [AudioDataWidget] visualizes FFT and wave data using a CustomPainter.
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
  await SoLoud.instance.init(bufferSize: 1024, channels: Channels.stereo);

  /// Activate the visualization. Mandatory to acquire audio data.
  SoLoud.instance.setVisualizationEnabled(
    true,
    windowSize: 512,
    channel: VisualizationChannel.all,
  );

  /// Smooth FFT data.
  SoLoud.instance.setFftSmoothing(0.8);

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
  SoundHandle? currentHandle;
  bool isPlaying = false;
  late final AppLifecycleListener _lifecycleListener;

  @override
  void initState() {
    super.initState();
    _lifecycleListener = AppLifecycleListener(
      onExitRequested: () async {
        await SoLoud.instance.deinitAsync();
        return AppExitResponse.exit;
      },
    );
    _startPlaying();
  }

  Future<void> _startPlaying() async {
    final sound = await SoLoud.instance.loadAsset(
      'assets/audio/explosion_panned.mp3',
      mode: LoadMode.disk,
    );
    currentSound = sound;
    final handle = SoLoud.instance.play(
      sound,
      looping: true,
      volume: 0.5,
    );
    currentHandle = handle;
    isPlaying = true;
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _lifecycleListener.dispose();
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Audio Data Visualization'),
        actions: [
          IconButton(
            icon: Icon(isPlaying ? Icons.pause : Icons.play_arrow),
            onPressed: () async {
              if (currentSound == null) {
                await _startPlaying();
                return;
              }
              if (isPlaying) {
                if (currentHandle != null) {
                  SoLoud.instance.pauseSwitch(currentHandle!);
                }
                setState(() {
                  isPlaying = false;
                });
              } else {
                if (currentHandle != null &&
                    SoLoud.instance.getIsValidVoiceHandle(currentHandle!)) {
                  SoLoud.instance.pauseSwitch(currentHandle!);
                } else {
                  currentHandle = SoLoud.instance.play(
                    currentSound!,
                    looping: true,
                    volume: 0.5,
                  );
                }
                setState(() {
                  isPlaying = true;
                });
              }
            },
          ),
        ],
      ),
      body: const AudioDataWidget(),
    );
  }
}

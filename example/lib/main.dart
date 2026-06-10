import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
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
  ValueNotifier<AudioSource?> currentSound = ValueNotifier<AudioSource?>(null);
  ValueNotifier<Duration> position = ValueNotifier<Duration>(Duration.zero);
  Timer? _positionTimer;

  @override
  void dispose() {
    soloud.deinit();
    _positionTimer?.cancel();
    super.dispose();
  }

  void _startPositionTimer() {
    _positionTimer?.cancel();
    _positionTimer = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (currentSound.value != null &&
          currentSound.value!.handles.isNotEmpty) {
        position.value = soloud.getPosition(currentSound.value!.handles.first);
      }
    });
  }

  void _stopPositionTimer() {
    _positionTimer?.cancel();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            spacing: 30,
            children: [
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  ElevatedButton(
                    onPressed: () async {
                      position.value = Duration.zero;
                      currentSound.value = await soloud.playSource(
                          asset: 'assets/audio/explosion_vorbis.ogg');
                    },
                    child: const Text('play vorbis'),
                  ),
                  ElevatedButton(
                    onPressed: () async {
                      position.value = Duration.zero;
                      currentSound.value = await soloud.loadAsset(
                        'assets/audio/explosion_vorbis.ogg',
                        autoDispose: true,
                      );
                      soloud.play(currentSound.value!);
                    },
                    child: const Text('load and play vorbis'),
                  ),
                ],
              ),


              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  ElevatedButton(
                    onPressed: () async {
                      position.value = Duration.zero;
                      currentSound.value = await soloud.playSource(
                          asset: 'assets/audio/explosion_opus.ogg');
                    },
                    child: const Text('play opus'),
                  ),
                  ElevatedButton(
                    onPressed: () async {
                      position.value = Duration.zero;
                      currentSound.value = await soloud.loadAsset(
                        'assets/audio/explosion_opus.ogg',
                        autoDispose: true,
                      );
                      soloud.play(currentSound.value!);
                    },
                    child: const Text('load and play opus'),
                  ),
                ],
              ),


              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  ElevatedButton(
                    onPressed: () async {
                      position.value = Duration.zero;
                      currentSound.value = await soloud.playSource(
                          asset: 'assets/audio/explosion_flac.ogg');
                    },
                    child: const Text('play FLAC'),
                  ),
                  ElevatedButton(
                    onPressed: () async {
                      position.value = Duration.zero;
                      currentSound.value = await soloud.loadAsset(
                        'assets/audio/explosion_flac.ogg',
                        autoDispose: true,
                      );
                      soloud.play(currentSound.value!);
                    },
                    child: const Text('load and play FLAC'),
                  ),
                ],
              ),


              ElevatedButton(
                onPressed: () async {
                  position.value = Duration.zero;
                  final bytes =
                      await rootBundle.load('assets/audio/explosion_flac.ogg');
                  currentSound.value = soloud.setBufferStream();
                  soloud
                    ..addAudioDataStream(
                      currentSound.value!,
                      bytes.buffer.asUint8List(),
                    )
                    ..play(currentSound.value!);
                },
                child: const Text('load and play FLAC Buffer'),
              ),
              ValueListenableBuilder<AudioSource?>(
                valueListenable: currentSound,
                builder: (context, sound, child) {
                  final l =
                      sound == null ? Duration.zero : soloud.getLength(sound);
                  debugPrint('**** Sound length: $l');
                  _startPositionTimer();
                  return ValueListenableBuilder(
                    valueListenable: _positionTimer == null
                        ? ValueNotifier(Duration.zero)
                        : position,
                    builder: (context, value, child) {
                      return Slider(
                        value: _positionTimer == null
                            ? 0
                            : value.inMilliseconds.toDouble(),
                        max: l.inMilliseconds.toDouble() + 0.0001,
                        onChanged: (value) {
                          soloud.seek(
                            currentSound.value!.handles.first,
                            Duration(milliseconds: value.toInt()),
                          );
                        },
                      );
                    },
                  );
                },
              ),
            ],
          ),
        ),
      ),
    );
  }
}

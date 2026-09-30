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

  AudioSource? soundMp3;
  AudioSource? soundM4a;
  AudioSource? soundMp4;
  AudioSource? soundAac;
  AudioSource? soundAc3;
  AudioSource? soundEac3;

  @override
  void initState() {
    super.initState();
    _initSounds();
  }

  Future<void> _initSounds() async {
    try {
      soundMp3 = await soloud.loadAsset('assets/audio/sample-MP3.mp3');
    } catch (e) {
      dev.log('Failed to load sample-MP3.mp3: $e');
    }

    try {
      soundM4a = await soloud.loadAsset('assets/audio/sample-AAC.m4a');
    } catch (e) {
      dev.log('Failed to load sample-AAC.m4a: $e');
    }

    try {
      soundMp4 = await soloud.loadAsset('assets/audio/sample-AAC.mp4');
    } catch (e) {
      dev.log('Failed to load sample-AAC.mp4: $e');
    }

    try {
      soundAac = await soloud.loadAsset('assets/audio/sample-AAC.aac');
    } catch (e) {
      dev.log('Failed to load sample-AAC.aac: $e');
    }

    if (!kIsWeb) {
      try {
        soundAc3 = await soloud.loadAsset('assets/audio/sample-AC3.ac3');
      } catch (e) {
        dev.log('Failed to load sample-AC3.ac3: $e');
      }

      try {
        soundEac3 = await soloud.loadAsset('assets/audio/sample-EAC3.eac3');
      } catch (e) {
        dev.log('Failed to load sample-EAC3.eac3: $e');
      }
    }

    if (mounted) {
      setState(() {});
    }
  }

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
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            spacing: 30,
            children: [
              ElevatedButton(
                onPressed: () async {
                  soundMp3 ??= await soloud.loadAsset(
                    'assets/audio/sample-MP3.mp3',
                  );
                  if (soundMp3 != null) {
                    soloud.play(soundMp3!);
                  }
                },
                child: const Text('play MP3 source'),
              ),
              ElevatedButton(
                onPressed: () async {
                  soundM4a ??= await soloud.loadAsset(
                    'assets/audio/sample-AAC.m4a',
                  );
                  if (soundM4a != null) {
                    soloud.play(soundM4a!);
                  }
                },
                child: const Text('play M4A source'),
              ),
              ElevatedButton(
                onPressed: () async {
                  soundMp4 ??= await soloud.loadAsset(
                    'assets/audio/sample-AAC.mp4',
                  );
                  if (soundMp4 != null) {
                    soloud.play(soundMp4!);
                  }
                },
                child: const Text('play MP4 source'),
              ),
              ElevatedButton(
                onPressed: () async {
                  soundAac ??= await soloud.loadAsset(
                    'assets/audio/sample-AAC.aac',
                  );
                  if (soundAac != null) {
                    soloud.play(soundAac!);
                  }
                },
                child: const Text('play AAC source'),
              ),
              ElevatedButton(
                onPressed: () async {
                  if (kIsWeb) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(
                        content: Text(
                          'AC-3 is not supported on the Web platform. '
                          'Web browsers do not support AC-3 in '
                          'WebCodecs or Web Audio.',
                        ),
                      ),
                    );
                    return;
                  }

                  try {
                    soundAc3 ??= await soloud.loadAsset(
                      'assets/audio/sample-AC3.ac3',
                    );
                    if (soundAc3 != null) {
                      soloud.play(soundAc3!);
                    }
                  } catch (e) {
                    dev.log('Failed to play AC-3: $e');
                  }
                },
                child: const Text('play AC3 source'),
              ),
              ElevatedButton(
                onPressed: () async {
                  if (kIsWeb) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(
                        content: Text(
                          'AC-3 is not supported on the Web platform. '
                          'Web browsers do not support AC-3 in '
                          'WebCodecs or Web Audio.',
                        ),
                      ),
                    );
                    return;
                  }

                  try {
                    soundEac3 ??= await soloud.loadAsset(
                      'assets/audio/sample-EAC3.eac3',
                    );
                    if (soundEac3 != null) {
                      soloud.play(soundEac3!);
                    }
                  } catch (e) {
                    dev.log('Failed to play EAC-3: $e');
                  }
                },
                child: const Text('play EAC3 source'),
              ),
              ElevatedButton(
                onPressed: () async {
                  /// This will eventually dispose (and stop) any previously
                  /// loaded sources.
                  await soloud.disposeAllSources();
                  setState(() {
                    soundMp3 = null;
                    soundM4a = null;
                    soundMp4 = null;
                    soundAac = null;
                    soundAc3 = null;
                    soundEac3 = null;
                  });
                },
                child: const Text('dispose all sources'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

import 'dart:developer' as dev;

import 'package:audio_session/audio_session.dart';
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
      home: AudioContext(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class AudioContext extends StatefulWidget {
  const AudioContext({super.key});

  @override
  State<AudioContext> createState() => _AudioContextState();
}

class _AudioContextState extends State<AudioContext> {
  final soloud = SoLoud.instance;
  late final AudioSession session;
  AudioSource? sound;
  SoundHandle? soundHandle;

  @override
  void initState() {
    super.initState();
    AudioSession.instance.then((audioSession) async {
      session = audioSession;
      await session.configure(const AudioSessionConfiguration.music());

      // Listen to audio interruptions and pause or duck as appropriate.
      _handleInterruptions(session);
    });
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: FutureBuilder<AudioSession>(
        future: AudioSession.instance,
        builder: (context, asyncSnapshot) {
          final session = asyncSnapshot.data;
          if (session == null) return const CircularProgressIndicator();
          return Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              spacing: 12,
              children: [
                ElevatedButton(
                  onPressed: () async {
                    await SoLoud.instance.disposeAllSources();

                    sound = await soloud
                        .loadAsset('assets/audio/8_bit_mentality.mp3');
                    soundHandle = await soloud.play(sound!, looping: true);

                    await session.setActive(true);
                  },
                  child: const Text('play asset'),
                ),
              ],
            ),
          );
        },
      ),
    );
  }

  void _handleInterruptions(AudioSession audioSession) {
    audioSession.becomingNoisyEventStream.listen((_) {
      // The user unplugged the headphones, so we should pause
      // or lower the volume.
      debugPrint('becomingNoisy, pausing...');
      soloud.setPause(soundHandle!, true);
    });
    audioSession.interruptionEventStream.listen((event) {
      debugPrint('interruption begin: ${event.begin}');
      debugPrint('interruption type: ${event.type}');
      if (event.begin) {
        switch (event.type) {
          case AudioInterruptionType.duck:
            // Another app started playing audio and we should duck.
            if (audioSession.androidAudioAttributes!.usage ==
                AndroidAudioUsage.game) {
              soloud.setGlobalVolume(soloud.getGlobalVolume() / 2);
            }
          case AudioInterruptionType.pause:
          case AudioInterruptionType.unknown:
            // Another app started playing audio and we should pause.
            soloud.setPause(soundHandle!, true);
        }
      } else {
        switch (event.type) {
          case AudioInterruptionType.duck:
            // The interruption ended and we should unduck.
            soloud.setGlobalVolume(soloud.getGlobalVolume() * 2);
          case AudioInterruptionType.pause:
            // The interruption ended and we should resume.
            soloud.setPause(soundHandle!, false);
          case AudioInterruptionType.unknown:
          // The interruption ended but we should not resume.
        }
      }
    });
    audioSession.devicesChangedEventStream.listen((event) {
      debugPrint('Devices added: ${event.devicesAdded}');
      debugPrint('Devices removed: ${event.devicesRemoved}');
    });
  }
}

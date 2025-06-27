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

enum ContextState {
  playing,
  paused,
  stopped,
  ducking,
  unknown,
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
  ValueNotifier<ContextState> isPlaying = ValueNotifier(ContextState.stopped);

  @override
  void initState() {
    super.initState();
    // Initialize the audio session.
    AudioSession.instance.then((audioSession) async {
      session = audioSession;
      await session.configure(
        const AudioSessionConfiguration(
          androidWillPauseWhenDucked: true,
          androidAudioAttributes: AndroidAudioAttributes(
            usage: AndroidAudioUsage.media,
            contentType: AndroidAudioContentType.music,
          ),
          androidAudioFocusGainType:
              AndroidAudioFocusGainType.gainTransientMayDuck,
          avAudioSessionCategory: AVAudioSessionCategory.playback,
          avAudioSessionCategoryOptions: AVAudioSessionCategoryOptions.none,
        ),
      );

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
                    isPlaying.value = ContextState.playing;
                  },
                  child: const Text('play asset'),
                ),

                ElevatedButton(
                  onPressed: () async {
                    await session.setActive(true);
                    soloud
                      ..setPause(soundHandle!, false)
                      ..fadeGlobalVolume(1, const Duration(milliseconds: 300));
                    isPlaying.value = ContextState.playing;
                  },
                  child: const Text('unpause'),
                ),

                // Display the current state.
                ValueListenableBuilder<ContextState>(
                  valueListenable: isPlaying,
                  builder: (context, state, child) {
                    return Text(
                      'Current state: ${state.name}',
                      style: const TextStyle(fontSize: 20),
                    );
                  },
                ),
              ],
            ),
          );
        },
      ),
    );
  }

  void fadeoutThenPause() {
    if (soundHandle == null) return;
    soloud.fadeGlobalVolume(0, const Duration(milliseconds: 300));
    Future.delayed(const Duration(milliseconds: 300), () {
      // After fading out, we can pause.
      soloud.setPause(soundHandle!, true);
      isPlaying.value = ContextState.paused;
    });
  }

  void fadeinThenResume() {
    if (soundHandle == null) return;
    isPlaying.value = ContextState.playing;
    soloud
      ..setPause(soundHandle!, false)
      ..fadeGlobalVolume(1, const Duration(milliseconds: 300));
  }

  void _handleInterruptions(AudioSession audioSession) {
    audioSession.becomingNoisyEventStream.listen((_) {
      // The user unplugged the headphones, so we should pause
      // or lower the volume.
      debugPrint('audio_context: becomingNoisy, pausing...');
      if (soundHandle == null) return;
      soloud.setPause(soundHandle!, true);
      isPlaying.value = ContextState.paused;
    });
    audioSession.interruptionEventStream.listen((event) {
      debugPrint('audio_context: interruption begin: ${event.begin}');
      debugPrint('audio_context: interruption type: ${event.type}');
      if (soundHandle == null) return;
      if (event.begin) {
        switch (event.type) {
          case AudioInterruptionType.duck:
            // Another app started playing audio and we should duck.
            soloud.fadeGlobalVolume(0.1, const Duration(milliseconds: 300));
            isPlaying.value = ContextState.ducking;
          case AudioInterruptionType.pause:
            // Another app started playing audio and we should pause.
            fadeoutThenPause();
            isPlaying.value = ContextState.paused;
          case AudioInterruptionType.unknown:
            // Another app started playing audio and we should pause.
            soloud.setPause(soundHandle!, true);
            isPlaying.value = ContextState.unknown;
        }
      } else {
        switch (event.type) {
          case AudioInterruptionType.duck:
            // The interruption ended and we should unduck.
            soloud.fadeGlobalVolume(1, const Duration(milliseconds: 300));
            isPlaying.value = ContextState.playing;
          case AudioInterruptionType.pause:
            // The interruption ended and we should resume.
            fadeinThenResume();
            isPlaying.value = ContextState.playing;
          case AudioInterruptionType.unknown:
            // The interruption ended but we should not resume.
            isPlaying.value = ContextState.unknown;
        }
      }
    });
    audioSession.devicesChangedEventStream.listen((event) {
      debugPrint('audio_context: Devices added: ${event.devicesAdded}');
      debugPrint('audio_context: Devices removed: ${event.devicesRemoved}');
    });
  }
}

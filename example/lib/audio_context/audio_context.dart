// ignore_for_file: avoid_redundant_argument_values

import 'dart:async';
import 'dart:developer' as dev;

import 'package:audio_service/audio_service.dart';
import 'package:audio_session/audio_session.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

late AudioHandler _audioHandler;

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

  /// Initialize the audio session and audio handler for background playback.
  _audioHandler = await AudioService.init(
    builder: SoLoudAudioHandler.new,
    config: const AudioServiceConfig(
      androidNotificationChannelId: 'com.example.flutter_soloud.channel.audio',
      androidNotificationChannelName: 'Audio playback',
      androidNotificationOngoing: true,
    ),
  );

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

/// Simple usecase of flutter_soloud plugin with audio_service for
/// background audio playback.
class AudioContext extends StatefulWidget {
  const AudioContext({super.key});

  @override
  State<AudioContext> createState() => _AudioContextState();
}

class _AudioContextState extends State<AudioContext> {
  ValueNotifier<ContextState> isPlaying = ValueNotifier(ContextState.stopped);

  @override
  void initState() {
    super.initState();

    // Listen to playback state changes from the audio handler.
    _audioHandler.playbackState.listen((state) {
      if (state.playing) {
        isPlaying.value = ContextState.playing;
      } else if (state.processingState == AudioProcessingState.idle) {
        isPlaying.value = ContextState.stopped;
      } else {
        isPlaying.value = ContextState.paused;
      }
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
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 12,
          children: [
            ElevatedButton(
              onPressed: () async {
                await _audioHandler.play();
              },
              child: const Text('play asset'),
            ),

            ElevatedButton(
              onPressed: () async => _audioHandler.pause(),
              child: const Text('pause'),
            ),

            ElevatedButton(
              onPressed: () async => _audioHandler.play(),
              child: const Text('unpause'),
            ),

            ElevatedButton(
              onPressed: () async => _audioHandler.stop(),
              child: const Text('stop'),
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
      ),
    );
  }
}

/// An [AudioHandler] for playing audio in the background using flutter_soloud.
///
/// This handler wraps the SoLoud audio engine to allow audio playback
/// to continue even when the app is in the background. It also integrates
/// with audio_session to handle audio interruptions (e.g., phone calls,
/// other apps playing audio) and supports media controls from the
/// notification panel and lock screen.
class SoLoudAudioHandler extends BaseAudioHandler with SeekHandler {
  /// Initialize the audio handler.
  SoLoudAudioHandler() {
    _init();
  }

  final soloud = SoLoud.instance;
  late final AudioSession session;
  AudioSource? sound;
  SoundHandle? soundHandle;

  bool _isInitialized = false;

  Future<void> _init() async {
    // Initialize SoLoud.
    await soloud.init();
    _isInitialized = true;

    // Initialize the audio session for handling interruptions.
    session = await AudioSession.instance;
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

    // Handle audio interruptions.
    _handleInterruptions(session);

    // Set initial playback state.
    playbackState.add(
      PlaybackState(
        controls: const [MediaControl.play],
        processingState: AudioProcessingState.idle,
        playing: false,
      ),
    );

    // Set media item metadata for notification.
    mediaItem.add(
      const MediaItem(
        id: 'background_music',
        album: 'Flutter SoLoud Example',
        title: '8 Bit Mentality',
        artist: 'Example Artist',
        duration: Duration(minutes: 3),
        artUri: null,
      ),
    );
  }

  @override
  Future<void> play() async {
    if (!_isInitialized) return;

    await session.setActive(true);

    // If we have a paused sound, resume it.
    if (soundHandle != null && soloud.getIsValidVoiceHandle(soundHandle!)) {
      soloud
        ..setPause(soundHandle!, false)
        ..fadeGlobalVolume(1, const Duration(milliseconds: 300));
    } else {
      // Otherwise, load and play a new sound.
      await soloud.disposeAllSources();

      sound = await soloud.loadAsset('assets/audio/8_bit_mentality.mp3');

      // soloud.resumeAudioDevice();

      soundHandle = await soloud.play(sound!, looping: true);

      // Ensure the volume is not muted.
      soloud.setGlobalVolume(1);
    }

    // Update playback state.
    playbackState.add(
      PlaybackState(
        controls: const [
          MediaControl.pause,
          MediaControl.stop,
        ],
        processingState: AudioProcessingState.ready,
        playing: true,
      ),
    );
  }

  @override
  Future<void> pause() async {
    if (!_isInitialized || soundHandle == null) return;

    // Fade out and pause.
    soloud.fadeGlobalVolume(0, const Duration(milliseconds: 300));
    await Future.delayed(const Duration(milliseconds: 300), () => null);
    soloud.setPause(soundHandle!, true);

    // soloud.pauseAudioDevice();

    // Update playback state.
    playbackState.add(
      PlaybackState(
        controls: const [
          MediaControl.play,
          MediaControl.stop,
        ],
        processingState: AudioProcessingState.ready,
        playing: false,
      ),
    );
  }

  @override
  Future<void> stop() async {
    if (!_isInitialized) return;

    // Stop playback and dispose sound.
    if (soundHandle != null) {
      await soloud.stop(soundHandle!);
      soundHandle = null;
    }
    await soloud.disposeAllSources();
    sound = null;

    // soloud.pauseAudioDevice();
    
    await session.setActive(false);

    // Update playback state.
    playbackState.add(
      PlaybackState(
        controls: const [MediaControl.play],
        processingState: AudioProcessingState.idle,
        playing: false,
      ),
    );
  }

  void _handleInterruptions(AudioSession audioSession) {
    audioSession.becomingNoisyEventStream.listen((_) {
      // The user unplugged the headphones, so we should pause.
      debugPrint('audio_context: becomingNoisy, pausing...');
      pause();
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
            playbackState.add(
              playbackState.value.copyWith(
                controls: const [
                  MediaControl.play,
                  MediaControl.stop,
                ],
              ),
            );
          case AudioInterruptionType.pause:
          case AudioInterruptionType.unknown:
            // Another app started playing audio and we should pause.
            pause();
        }
      } else {
        switch (event.type) {
          case AudioInterruptionType.duck:
            // The interruption ended and we should unduck.
            soloud.fadeGlobalVolume(1, const Duration(milliseconds: 300));
            playbackState.add(
              playbackState.value.copyWith(
                controls: const [
                  MediaControl.pause,
                  MediaControl.stop,
                ],
              ),
            );
          case AudioInterruptionType.pause:
          case AudioInterruptionType.unknown:
            // The interruption ended and we should resume.
            play();
        }
      }
    });

    audioSession.devicesChangedEventStream.listen((event) {
      debugPrint('audio_context: Devices added: ${event.devicesAdded}');
      debugPrint('audio_context: Devices removed: ${event.devicesRemoved}');
    });
  }
}

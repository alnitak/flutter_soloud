// ignore_for_file: avoid_redundant_argument_values

import 'dart:async';
import 'dart:developer' as dev;

import 'package:audio_service/audio_service.dart';
import 'package:audio_session/audio_session.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// # Background Audio Playback Example
///
/// This example demonstrates how to use `flutter_soloud` with background audio
/// playback support on iOS and Android. The audio continues playing when the
/// app is in the background or when the device screen is locked.
///
/// ## Plugins Used
///
/// ### 1. audio_service (https://pub.dev/packages/audio_service)
/// Handles the background audio service and media notification. This allows
/// the audio to play in the background and provides media controls in the
/// notification panel and lock screen.
///
/// **Key concepts:**
/// - [AudioHandler]: The main class that receives playback commands from the UI
///   notification, lock screen, and other media controllers.
/// - [MediaItem]: Metadata about the currently playing audio
/// (title, artist, etc.)
/// - [PlaybackState]: Current playback state (playing/paused, position, etc.)
///
/// ### 2. audio_session (https://pub.dev/packages/audio_session)
/// Manages the audio session configuration and audio interruptions (e.g.,
/// phone calls, other apps playing audio, headphones unplugged).
///
/// **Key concepts:**
/// - [AudioSessionConfiguration]: Configures how audio should behave
///   (e.g., playback category, ducking behavior).
/// - Audio interruptions: Handle when the OS needs to temporarily take over
///   audio (incoming call, Siri, etc.).
///
/// ## Platform Configuration Required
///
/// ### iOS (Info.plist)
/// Add the following to enable background audio:
/// ```xml
/// <key>UIBackgroundModes</key>
/// <array>
///     <string>audio</string>
/// </array>
/// ```
///
/// ### Android (AndroidManifest.xml)
/// Add permissions and service declaration:
/// ```xml
/// <uses-permission android:name="android.permission.WAKE_LOCK" />
/// <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
///
/// <application ...>
///     <!-- Audio Service for background playback -->
///     <service android:name="com.ryanheise.audioservice.AudioService"
///         android:foregroundServiceType="mediaPlayback"
///         android:exported="true">
///         <intent-filter>
///             <action android:name="android.media.browse.MediaBrowserService" />
///         </intent-filter>
///     </service>
///
///     <!-- Media button receiver -->
///     <receiver android:name="com.ryanheise.audioservice.MediaButtonReceiver"
///         android:exported="true">
///         <intent-filter>
///             <action android:name="android.intent.action.MEDIA_BUTTON" />
///         </intent-filter>
///     </receiver>
/// </application>
/// ```
///
/// ### Android (MainActivity.kt)
/// Change `MainActivity` to extend `AudioServiceActivity`:
/// ```kotlin
/// import com.ryanheise.audioservice.AudioServiceActivity
/// class MainActivity: AudioServiceActivity()
/// ```

/// Global reference to the audio handler, initialized in main().
/// This is used to communicate between the UI and background service.
late AudioHandler _audioHandler;

void main() async {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
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

  /// Initialize the audio service with our custom [SoLoudAudioHandler].
  /// This creates the background service that will receive playback commands
  /// from the notification, lock screen, and UI.
  _audioHandler = await AudioService.init(
    builder: SoLoudAudioHandler.new,
    config: const AudioServiceConfig(
      androidNotificationChannelId: 'com.example.flutter_soloud.channel.audio',
      androidNotificationChannelName: 'Audio playback',
      androidNotificationOngoing: true,
      androidNotificationIcon: 'mipmap/ic_launcher',
      androidNotificationClickStartsActivity: true,
      androidShowNotificationBadge: true,
    ),
  );

  runApp(
    const MaterialApp(
      home: AudioContext(),
    ),
  );
}

/// UI states for displaying the current playback state.
enum ContextState {
  playing,
  paused,
  stopped,
  ducking,
  unknown,
}

/// The main UI widget for the example.
///
/// This widget displays simple buttons to control playback and shows the
/// current state. The actual audio logic is handled by [SoLoudAudioHandler].
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

    /// Listen to playback state changes from the audio handler.
    /// This keeps the UI in sync with the background service.
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
                /// Delegate playback to the audio handler.
                /// The handler manages both the UI request AND background
                /// service.
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

/// An [AudioHandler] that integrates flutter_soloud with audio_service.
///
/// This is the core class that enables background audio. It:
/// 1. Wraps the SoLoud audio engine for playback
/// 2. Handles media controls from notification/lock screen
/// 3. Manages audio session configuration and interruptions
/// 4. Updates playback state for the notification UI (position, duration, etc.)
///
/// The handler extends [BaseAudioHandler] (from audio_service) which provides
/// the basic infrastructure, and mixes in [SeekHandler] for seek support.
class SoLoudAudioHandler extends BaseAudioHandler with SeekHandler {
  SoLoudAudioHandler() {
    _init();
  }

  final soloud = SoLoud.instance;
  late final AudioSession session;
  AudioSource? sound;
  SoundHandle? soundHandle;
  Timer? _positionTimer;

  bool _isInitialized = false;

  /// Initialize the audio handler.
  ///
  /// This sets up:
  /// - SoLoud audio engine
  /// - Audio session for interruption handling
  /// - Initial playback state and media metadata
  Future<void> _init() async {
    // Initialize SoLoud audio engine.
    await soloud.init();
    _isInitialized = true;

    /// Configure the audio session.
    ///
    /// [avAudioSessionCategory: AVAudioSessionCategory.playback] tells iOS
    /// this app plays audio and should continue in background.
    ///
    /// [androidAudioFocusGainType: gainTransientMayDuck] tells Android
    /// this app can be ducked (volume lowered) when other audio plays.
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

    // Set up listeners for audio interruptions (calls, other apps, etc.)
    _handleInterruptions(session);

    // Set initial playback state (stopped, no position)
    playbackState.add(
      PlaybackState(
        controls: const [MediaControl.play],
        processingState: AudioProcessingState.idle,
        playing: false,
      ),
    );

    /// Set media item metadata.
    ///
    /// This metadata is displayed in:
    /// - Android notification media controls
    /// - iOS Control Center and Lock Screen
    /// - Connected devices (car displays, smart watches, etc.)
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

  /// Get the current playback position from SoLoud.
  Duration _getPosition() {
    if (soundHandle == null) return Duration.zero;
    return soloud.getPosition(soundHandle!);
  }

  /// Update the playback state with current position.
  ///
  /// This broadcasts the state to all listeners:
  /// - UI widgets via _audioHandler.playbackState
  /// - Android notification (seek bar position)
  /// - iOS Control Center (progress bar)
  void _updatePlaybackState({
    required List<MediaControl> controls,
    required AudioProcessingState processingState,
    required bool playing,
  }) {
    final position = _getPosition();
    playbackState.add(
      PlaybackState(
        controls: controls,
        processingState: processingState,
        playing: playing,

        /// [updatePosition] sets the current position for the seek bar.
        /// Without this, the notification seek bar won't show progress.
        updatePosition: position,
        bufferedPosition: position,

        /// [systemActions] enables seek gestures in the notification.
        systemActions: const {
          MediaAction.seek,
          MediaAction.seekForward,
          MediaAction.seekBackward,
        },
      ),
    );
  }

  /// Handle seek requests from the notification/lock screen.
  ///
  /// Called when user drags the seek bar in the Android notification
  /// or iOS Control Center.
  @override
  Future<void> seek(Duration position) async {
    if (!_isInitialized || soundHandle == null) return;
    soloud.seek(soundHandle!, position);
    // Update playback state with new position so UI reflects the seek.
    _updatePlaybackState(
      controls: const [MediaControl.pause, MediaControl.stop],
      processingState: AudioProcessingState.ready,
      playing: soloud.getPause(soundHandle!) == false,
    );
  }

  /// Start or resume playback.
  ///
  /// This is called from:
  /// - UI "play" button
  /// - Android notification play button
  /// - iOS Control Center play button
  /// - Headphone button press
  @override
  Future<void> play() async {
    if (!_isInitialized) return;

    /// Activate the audio session.
    ///
    /// On iOS, this ensures the app takes audio focus and announces
    /// to the system that this app is now playing audio.
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

      // Get the actual sound duration for accurate metadata.
      final duration = soloud.getLength(sound!);

      soundHandle = await soloud.play(sound!, looping: true);

      soloud.setGlobalVolume(1);

      // Update media item with actual duration for the notification.
      mediaItem.add(
        MediaItem(
          id: 'background_music',
          album: 'Flutter SoLoud Example',
          title: '8 Bit Mentality',
          artist: 'Example Artist',
          duration: duration,
          artUri: null,
        ),
      );
    }

    // Start the position update timer for the notification seek bar.
    _startPositionTimer();

    // Update playback state to "playing".
    _updatePlaybackState(
      controls: const [MediaControl.pause, MediaControl.stop],
      processingState: AudioProcessingState.ready,
      playing: true,
    );
  }

  /// Start a timer to periodically update the playback position.
  ///
  /// The notification seek bar needs periodic updates to show progress.
  /// This timer fires every second while audio is playing.
  void _startPositionTimer() {
    _positionTimer?.cancel();
    _positionTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (soundHandle != null && soloud.getPause(soundHandle!) == false) {
        _updatePlaybackState(
          controls: const [MediaControl.pause, MediaControl.stop],
          processingState: AudioProcessingState.ready,
          playing: true,
        );
      }
    });
  }

  /// Stop the position update timer.
  void _stopPositionTimer() {
    _positionTimer?.cancel();
    _positionTimer = null;
  }

  /// Pause playback.
  ///
  /// Called from UI, notification, lock screen, etc.
  @override
  Future<void> pause() async {
    if (!_isInitialized || soundHandle == null) return;

    // Fade out and pause.
    soloud.fadeGlobalVolume(0, const Duration(milliseconds: 300));
    await Future.delayed(const Duration(milliseconds: 300), () => null);
    soloud.setPause(soundHandle!, true);

    // Stop updating the position since we're paused.
    _stopPositionTimer();

    // Update state to "paused" but preserve position for seek bar.
    _updatePlaybackState(
      controls: const [MediaControl.play, MediaControl.stop],
      processingState: AudioProcessingState.ready,
      playing: false,
    );
  }

  /// Stop playback completely.
  ///
  /// Unlike pause, this resets everything and releases audio resources.
  @override
  Future<void> stop() async {
    if (!_isInitialized) return;

    _stopPositionTimer();

    if (soundHandle != null) {
      await soloud.stop(soundHandle!);
      soundHandle = null;
    }
    await soloud.disposeAllSources();
    sound = null;

    /// Deactivate the audio session.
    ///
    /// This tells the OS we're done with audio, allowing other apps
    /// to take full audio focus without ducking.
    await session.setActive(false);

    // Update state to "stopped" with zero position.
    playbackState.add(
      PlaybackState(
        controls: const [MediaControl.play],
        processingState: AudioProcessingState.idle,
        playing: false,
        updatePosition: Duration.zero,
        bufferedPosition: Duration.zero,
      ),
    );
  }

  /// Handle audio interruptions (phone calls, other apps, etc.)
  ///
  /// audio_session provides streams for various audio events:
  /// - "becomingNoisyEventStream": Headphones unplugged
  /// - "interruptionEventStream": Calls, Siri, other apps
  /// - "devicesChangedEventStream": Audio devices added/removed
  void _handleInterruptions(AudioSession audioSession) {
    // Handle headphones being unplugged.
    audioSession.becomingNoisyEventStream.listen((_) {
      debugPrint('audio_context: becomingNoisy, pausing...');
      pause();
    });

    // Handle audio interruptions (calls, other apps playing, etc.)
    audioSession.interruptionEventStream.listen((event) {
      debugPrint('audio_context: interruption begin: ${event.begin}');
      debugPrint('audio_context: interruption type: ${event.type}');

      if (soundHandle == null) return;

      if (event.begin) {
        // Interruption started - pause or duck based on type.
        switch (event.type) {
          case AudioInterruptionType.duck:
            // Lower volume temporarily (e.g., navigation announcement).
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
            // Full interruption (phone call) - pause playback.
            pause();
        }
      } else {
        // Interruption ended - resume or unduck.
        switch (event.type) {
          case AudioInterruptionType.duck:
            // Restore volume after ducking.
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
            // Resume after interruption ended.
            play();
        }
      }
    });

    // Handle audio device changes (BT headphones, etc.)
    audioSession.devicesChangedEventStream.listen((event) {
      debugPrint('audio_context: Devices added: ${event.devicesAdded}');
      debugPrint('audio_context: Devices removed: ${event.devicesRemoved}');
    });
  }

  /// Clean up when the audio service is destroyed.
  ///
  /// This is called when the app is terminated or the service
  /// is explicitly stopped by the system.
  @override
  Future<void> onTaskRemoved() async {
    _stopPositionTimer();
    await stop();
    await super.onTaskRemoved();
  }
}

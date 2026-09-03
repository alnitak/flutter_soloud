# Background audio: audio_service + audio_session with flutter_soloud

Distilled from `example/lib/audio_context/audio_context.dart`. flutter_soloud only plays audio; it does **not** manage the OS audio session, background execution, or media notifications. For that, combine it with [`audio_service`](https://pub.dev/packages/audio_service) (background service + notification/lock-screen controls) and [`audio_session`](https://pub.dev/packages/audio_session) (audio focus, interruptions, ducking).

## Dependencies

```yaml
dependencies:
  flutter_soloud: ^5.0.0-pre.2
  audio_service: ^0.18.0
  audio_session: ^0.2.2
```

## Required platform configuration

### iOS — `ios/Runner/Info.plist`

```xml
<key>UIBackgroundModes</key>
<array>
    <string>audio</string>
</array>
```

### Android — `android/app/src/main/AndroidManifest.xml`

```xml
<uses-permission android:name="android.permission.WAKE_LOCK" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />

<application ...>
    <service android:name="com.ryanheise.audioservice.AudioService"
        android:foregroundServiceType="mediaPlayback"
        android:exported="true">
        <intent-filter>
            <action android:name="android.media.browse.MediaBrowserService" />
        </intent-filter>
    </service>

    <receiver android:name="com.ryanheise.audioservice.MediaButtonReceiver"
        android:exported="true">
        <intent-filter>
            <action android:name="android.intent.action.MEDIA_BUTTON" />
        </intent-filter>
    </receiver>
</application>
```

### Android — `MainActivity.kt`

```kotlin
import com.ryanheise.audioservice.AudioServiceActivity
class MainActivity: AudioServiceActivity()
```

## Wiring: AudioService.init in main()

```dart
import 'package:audio_service/audio_service.dart';

late AudioHandler _audioHandler;

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  _audioHandler = await AudioService.init(
    builder: SoLoudAudioHandler.new,
    config: const AudioServiceConfig(
      androidNotificationChannelId: 'com.example.app.channel.audio',
      androidNotificationChannelName: 'Audio playback',
      androidNotificationOngoing: true,
      androidNotificationIcon: 'mipmap/ic_launcher',
    ),
  );
  runApp(const MyApp());
}
```

UI talks only to `_audioHandler` (`play()`, `pause()`, `stop()`, plus `playbackState` stream); the handler owns SoLoud.

## The handler

```dart
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

  Future<void> _init() async {
    await soloud.init();
    _isInitialized = true;

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
    _handleInterruptions(session);

    playbackState.add(
      PlaybackState(
        controls: const [MediaControl.play],
        processingState: AudioProcessingState.idle,
        playing: false,
      ),
    );
    mediaItem.add(
      const MediaItem(
        id: 'track1', album: 'Album', title: 'Title',
        artist: 'Artist', duration: Duration(minutes: 3),
      ),
    );
  }

  @override
  Future<void> play() async {
    if (!_isInitialized) return;
    await session.setActive(true); // take audio focus BEFORE playing

    if (soundHandle != null && soloud.getIsValidVoiceHandle(soundHandle!)) {
      soloud
        ..setPause(soundHandle!, false)
        ..fadeGlobalVolume(1, const Duration(milliseconds: 300));
    } else {
      await soloud.disposeAllSources();
      sound = await soloud.loadAsset('assets/audio/track.mp3');
      mediaItem.add(MediaItem(
        id: 'track1', album: 'Album', title: 'Title', artist: 'Artist',
        duration: soloud.getLength(sound!), // real duration for the seek bar
      ));
      soundHandle = soloud.play(sound!, looping: true);
      soloud.setGlobalVolume(1);
    }

    _startPositionTimer();
    _updatePlaybackState(
      controls: const [MediaControl.pause, MediaControl.stop],
      processingState: AudioProcessingState.ready,
      playing: true,
    );
  }

  @override
  Future<void> pause() async {
    if (!_isInitialized || soundHandle == null) return;
    soloud.fadeGlobalVolume(0, const Duration(milliseconds: 300));
    await Future.delayed(const Duration(milliseconds: 300));
    soloud.setPause(soundHandle!, true);
    _stopPositionTimer();
    _updatePlaybackState(
      controls: const [MediaControl.play, MediaControl.stop],
      processingState: AudioProcessingState.ready,
      playing: false,
    );
  }

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
    await session.setActive(false); // release audio focus
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

  @override
  Future<void> seek(Duration position) async {
    if (!_isInitialized || soundHandle == null) return;
    soloud.seek(soundHandle!, position);
    // Re-emit state so notification/lock-screen position updates immediately.
  }

  @override
  Future<void> onTaskRemoved() async {
    _stopPositionTimer();
    await stop();
    await super.onTaskRemoved();
  }
  // ... helpers below
}
```

## Position timer and playback state

The notification/lock-screen seek bar only moves if `playbackState` gets fresh `updatePosition` values. The example uses a 1-second timer while playing:

```dart
Duration _getPosition() =>
    soundHandle == null ? Duration.zero : soloud.getPosition(soundHandle!);

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
      updatePosition: position,
      bufferedPosition: position,
      systemActions: const {
        MediaAction.seek,
        MediaAction.seekForward,
        MediaAction.seekBackward,
      },
    ),
  );
}

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

void _stopPositionTimer() {
  _positionTimer?.cancel();
  _positionTimer = null;
}
```

## Interruptions (audio_session streams)

```dart
void _handleInterruptions(AudioSession audioSession) {
  // Headphones unplugged -> pause.
  audioSession.becomingNoisyEventStream.listen((_) => pause());

  audioSession.interruptionEventStream.listen((event) {
    if (soundHandle == null) return;
    if (event.begin) {
      switch (event.type) {
        case AudioInterruptionType.duck:
          // e.g. navigation announcement: lower volume temporarily.
          soloud.fadeGlobalVolume(0.1, const Duration(milliseconds: 300));
        case AudioInterruptionType.pause:
        case AudioInterruptionType.unknown:
          pause();
      }
    } else {
      switch (event.type) {
        case AudioInterruptionType.duck:
          soloud.fadeGlobalVolume(1, const Duration(milliseconds: 300));
        case AudioInterruptionType.pause:
        case AudioInterruptionType.unknown:
          play();
      }
    }
  });

  // Optional: BT devices added/removed.
  audioSession.devicesChangedEventStream.listen((event) {});
}
```

## Traps

- Call `session.setActive(true)` **before** `play()` and `setActive(false)` on `stop()`, or iOS won't route/focus audio correctly.
- Without `updatePosition` + `systemActions: {MediaAction.seek, ...}` on `PlaybackState`, the notification seek bar shows no progress and seek gestures do nothing — even though `seek()` is implemented.
- Without the periodic position timer, the seek bar freezes at the position captured when `play()` was pressed.
- `soloud.getLength(sound)` only works on a loaded source; the initial `MediaItem.duration` is a placeholder until the real file is loaded.
- If you use `audio_session` to manage Android audio attributes yourself, pass `androidAAudioAttributes: AndroidAAudioAttributes.unmanaged` (and note it only applies with `lowLatency: false`) to `SoLoud.instance.init()` so SoLoud doesn't overwrite the stream attributes you configured.
- iOS + `google_mobile_ads`: set `MobileAds.shared.audioVideoManager.isAudioSessionApplicationManaged = true` in `AppDelegate.swift` to avoid audio playback conflicts.
- SoLoud's `stopAudioDevice()`/`startAudioDevice()` deal with the output device, not focus; they are orthogonal to the audio session. Idle timeouts still apply in background apps — see `setAudioDeviceIdleTimeout` in SKILL.md.

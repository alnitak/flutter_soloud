import 'dart:async' show StreamController;

import 'package:flutter/services.dart';
import 'package:meta/meta.dart';

/// Audio state changes. Not doing much now. Notifications should work
/// on iOS but none of the Android backends will report this notification.
/// However the started and stopped events should be reliable for all backends.
enum PlayerStateNotification {
  ///
  started,

  ///
  stopped,

  ///
  rerouted,

  ///
  interruptionBegan,

  ///
  interruptionEnded,

  ///
  unlocked,
}

/// Audio focus states from Android AudioManager
/// https://developer.android.com/reference/android/media/AudioManager#AUDIOFOCUS_GAIN
enum AndroidInterruptions {
  /// Gain audio focus
  audioFocusGain,

  /// Gain temporary audio focus
  audioFocusGainTransient,

  /// Gain temporary audio focus, guaranteed that no other app will also
  /// play audio
  audioFocusGainTransientExclusive,

  /// Gain temporary audio focus where other apps can continue playing at
  /// lower volume
  audioFocusGainTransientMayDuck,

  /// Loss of audio focus, stop playback
  audioFocusLoss,

  /// Temporary loss of audio focus, can expect it back shortly
  audioFocusLossTransient,

  /// Temporary loss of audio focus where can continue playing at lower volume
  audioFocusLossTransientCanDuck,

  /// No audio focus change
  audioFocusNone,
}

/// Usage categories for audio playback
/// https://developer.android.com/reference/android/media/AudioAttributes#USAGE_ALARM
enum AndroidUsage {
  /// For alarms and clock operations
  usageAlarm,

  /// For accessibility voice prompts
  usageAssistanceAccessibility,

  /// For GPS and navigation prompts
  usageAssistanceNavigationGuidance,

  /// For user interface sounds and feedback
  usageAssistanceSonification,

  /// For general assistance
  usageAssistance,

  /// For game audio
  usageGame,

  /// For music, movies, and other media playback
  usageMedia,

  /// For notification sounds
  usageNotification,

  /// For delayed communication notifications (e.g., email)
  usageNotificationCommunicationDelayed,

  /// For instant communications (e.g., chat)
  usageNotificationCommunicationInstant,

  /// For communication requests (e.g., incoming call)
  usageNotificationCommunicationRequest,

  /// For event notifications
  usageNotificationEvent,

  /// For phone ringtones
  usageNotificationRingtone,

  /// For notifications with unknown type
  usageNotificationUnknown,

  /// For voice/video calls
  usageVoiceCommunication,

  /// For in-call signalling
  usageVoiceCommunicationSignalling,
}

/// Content type categories for audio playback
/// https://developer.android.com/reference/android/media/AudioAttributes#CONTENT_TYPE_MOVIE
enum AndroidContentType {
  /// For movie/video content
  contentTypeMovie,

  /// For music content
  contentTypeMusic,

  /// For sound effects and other short sounds
  contentTypeSonification,

  /// For spoken word content
  contentTypeSpeech,

  /// For content with unknown type
  contentTypeUnknown,
}

/// Configuration class for Android audio attributes that control how audio playback
/// behaves in relation to other audio streams.
///
/// Audio attributes help the system understand the purpose and characteristics of
/// your audio playback, allowing it to manage audio focus and routing appropriately.
///
/// Example:
/// ```dart
/// final attributes = AndroidAttributes(
///   usage: AndroidUsage.usageGame,
///   contentType: AndroidContentType.contentTypeMusic,
/// );
/// ```
///
/// See also:
///   * [AndroidUsage] for available usage categories
///   * [AndroidContentType] for available content type categories
///   * https://developer.android.com/reference/android/media/AudioAttributes
class AndroidAttributes {
  // ignore: public_member_api_docs
  const AndroidAttributes({
    this.usage = AndroidUsage.usageGame,
    this.contentType = AndroidContentType.contentTypeMusic,
    this.acceptsDelayedFocusGain = true,
    this.willPauseWhenDucked = true,
  });
  final AndroidUsage usage;
  final AndroidContentType contentType;

  /// Temporarily reducing the audio level of one app so that another can be heard clearly
  /// Ref: https://developer.android.com/media/optimize/audio-focus#automatic_ducking
  final bool willPauseWhenDucked;

  /// Marks this focus request as compatible with delayed focus.
  /// Ref: https://developer.android.com/reference/android/media/AudioFocusRequest.Builder#setAcceptsDelayedFocusGain(boolean)
  final bool acceptsDelayedFocusGain;

  /// Converts the object to a map suitable for passing to the platform channel.
  Map<String, dynamic> toMap() {
    return {
      'usage': usage.toString().split('.').last,
      'contentType': contentType.toString().split('.').last,
      'willPauseWhenDucked': willPauseWhenDucked,
      'acceptsDelayedFocusGain': acceptsDelayedFocusGain,
    };
  }
}

/// Represents the current state and capabilities of a connected audio device.
///
/// This class provides information about audio devices such as headphones,
/// headsets, or bluetooth audio devices that are connected to the system.
///
/// Example:
/// ```dart
/// void onHeadsetChanged(HeadsetInfo info) {
///   if (info.isConnected) {
///     print('Device ${info.name} connected');
///     if (info.hasMicrophone) {
///       print('Device has microphone capability');
///     }
///   }
/// }
/// ```
///
/// The [state] field indicates connection status:
///   * 1: Device is connected
///   * 0: Device is disconnected
///
/// For Android API level 23 and above, detailed device information including
/// name and microphone capability is available. For older versions, only basic
/// connection state information may be available.
class HeadsetInfo {
  // ignore: public_member_api_docs
  const HeadsetInfo({
    required this.state,
    required this.name,
    required this.hasMicrophone,
  });

  /// Creates a HeadsetInfo from a map
  factory HeadsetInfo.fromMap(Map<String, dynamic> map) {
    return HeadsetInfo(
      state: map['state'] as int,
      name: map['name'] as String,
      hasMicrophone: map['hasMicrophone'] as bool,
    );
  }

  /// Connected state: 1 = connected, 0 = disconnected
  final int state;

  /// Device name if available
  final String name;

  /// Whether the device has a microphone
  final bool hasMicrophone;

  /// Returns true if the headset is connected
  bool get isConnected => state == 1;

  @override
  String toString() => 'HeadsetInfo(state: $state, name: $name, '
      'hasMicrophone: $hasMicrophone)';
}

/// https://developer.android.com/media/optimize/audio-focus
@experimental
class Interruptions {
  /// The method channel used in FlutterSoloudPlugin.java
  static const channel = MethodChannel('flutter_soloud');

  /// Controller to listen to player state changes.
  @experimental
  final StreamController<PlayerStateNotification> stateChangedController =
      StreamController.broadcast();

  /// listener for player state changes.
  @experimental
  Stream<PlayerStateNotification> get stateChangedEvents =>
      stateChangedController.stream;

  /// Controller to listen to player state changes.
  @experimental
  @internal
  final StreamController<AndroidInterruptions> audioAndroidFocusController =
      StreamController.broadcast();

  /// listener for player state changes.
  @experimental
  Stream<AndroidInterruptions> get audioAndroidFocusEvents =>
      audioAndroidFocusController.stream;

  /// Controller to listen to headset state changes.
  @experimental
  @internal
  final StreamController<HeadsetInfo> headsetStateAndroidController =
      StreamController.broadcast();

  /// listener for headset state changes.
  @experimental
  Stream<HeadsetInfo> get headsetStateAndroidEvents =>
      headsetStateAndroidController.stream;

  /// Initializes audio focus manager on Android.
  ///
  /// This method is only available on Android platforms.
  ///
  /// The [onFocusChanged] callback is called when the audio focus changes.
  /// The [onHeadsetChanged] callback is called when the headset (e.g. headphones)
  /// is connected or disconnected.
  ///
  /// When [androidAttributes] is not specified, the default attributes are used.
  /// The default attributes have the following settings:
  ///   * [AndroidAttributes.usage]: [AndroidUsage.usageGame]
  ///   * [AndroidAttributes.contentType]: [AndroidContentType.contentTypeMusic]
  ///   * [AndroidAttributes.willPauseWhenDucked]: true
  ///   * [AndroidAttributes.acceptsDelayedFocusGain]: true
  @experimental
  Future<void> initAndroidInterruptions({
    AndroidAttributes androidAttributes = const AndroidAttributes(),
    // void Function(AndroidInterruptions focusState)? onFocusChanged,
    // void Function(HeadsetInfo headsetInfo)? onHeadsetChanged,
  }) async {
    // if (onFocusChanged != null || onHeadsetChanged != null) {
    channel.setMethodCallHandler((call) async {
      switch (call.method) {
        case 'onAudioFocusChanged':
          final focusState = AndroidInterruptions.values.firstWhere(
            (e) =>
                e.toString() ==
                'AndroidInterruptions.${call.arguments as String}',
            orElse: () => AndroidInterruptions.audioFocusNone,
          );
          // onFocusChanged?.call(focusState);
          audioAndroidFocusController.add(focusState);
        case 'onHeadsetChanged':
          // Cast the dynamic Map to Map<String, dynamic> because when
          // the data is sent through platform channels, the type
          // information gets loosened.
          final rawMap = call.arguments as Map<dynamic, dynamic>;
          final headsetInfo = HeadsetInfo.fromMap(
            rawMap.map((key, value) => MapEntry(key.toString(), value)),
          );
          // onHeadsetChanged?.call(headsetInfo);
          headsetStateAndroidController.add(headsetInfo);
      }
    });
    // }

    await channel.invokeMethod<bool>('initialize', androidAttributes.toMap());
    return;
  }
}

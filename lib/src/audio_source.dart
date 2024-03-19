import 'dart:async';
import 'dart:collection';

import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:meta/meta.dart';

/// Deprecated alias to [SoundEventType].
@Deprecated('Use SoundEventType instead')
typedef SoundEvent = SoundEventType;

/// A deprecated alias for [AudioSource].
@Deprecated("Use 'AudioSource' instead")
typedef SoundProps = AudioSource;

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({
  SoundEventType event,
  AudioSource sound,
  SoundHandle handle,
});

/// A representation of an audio source: something that can be played.
///
/// Audio sources cannot be instantiated directly. The need to be loaded
/// (via something like `SoLoud.loadFile()`, for example).
/// Then they can be played once or several times.
/// Finally, the need to be disposed with something like
/// `SoLoud.disposeSound()` or `SoLoud.disposeAllSounds()`.
///
/// Audio sources are uniquely identified by their [soundHash], which is
/// what you pass to methods such as `SoLoud.play()`.
///
/// Playing a sound in this way creates a new sound _instance_. There can
/// be several instances of one audio source playing simultaneously.
/// You can access the currently playing instances' handles via [handles].
///
/// You can listen to the broadcast stream of [soundEvents].
class AudioSource {
  /// Constructs an instance of [AudioSource].
  @internal
  AudioSource(this.soundHash);

  /// The hash uniquely identifying this loaded sound.
  final SoundHash soundHash;

  /// The handles of currently playing instances of this sound.
  ///
  /// A sound (expressed as [AudioSource]) can be loaded once, but then
  /// played multiple times. It's even possible to play several instances
  /// of the sound simultaneously.
  ///
  /// Each time you `SoLoud.play()` a sound, you get back a [SoundHandle],
  /// and that same handle will be added to this [handles] set.
  /// When the sound finishes playing, its handle will be removed from this set.
  ///
  /// This set is unmodifiable.
  late final UnmodifiableSetView<SoundHandle> handles =
      UnmodifiableSetView(handlesInternal);

  /// The [internal] backing of [handles]/ Use [handles] from outside
  /// the package.
  ///
  /// If you are removing a handle from this set, remember to check whether
  /// this was the last one (`handles.isEmpty`). If so, you should
  /// add an event to [allInstancesFinishedController].
  @internal
  final Set<SoundHandle> handlesInternal = {};

  ///
  // TODO(marco): make marker keys time able to trigger an event
  final List<double> keys = [];

  /// Backing controller for [soundEvents].
  @internal
  final StreamController<StreamSoundEvent> soundEventsController =
      StreamController.broadcast();

  /// This getter is [deprecated] and will be removed. Use [handles] instead.
  @Deprecated("Use 'handles' instead")
  UnmodifiableSetView<SoundHandle> get handle => handles;

  /// the user can listen ie when a sound ends or key events (TODO)
  Stream<StreamSoundEvent> get soundEvents => soundEventsController.stream;

  /// Backing controller for [allInstancesFinished].
  @internal
  final StreamController<void> allInstancesFinishedController =
      StreamController.broadcast();

  /// A stream that adds an event every time the number of concurrently
  /// playing instances of this [AudioSource] reaches zero.
  ///
  /// This can be used to await times when an audio source can be safely
  /// disposed. For example:
  ///
  /// ```dart
  /// final source = soloud.loadAsset('...');
  /// // Wait for the first time all the instances of the sound are finished
  /// // (finished playing or were stopped with soloud.stop()).
  /// source.allInstancesFinished.first.then(
  ///   // Dispose of the sound.
  ///   (_) => soloud.disposeSound(source))
  /// );
  /// soloud.play(source);
  /// ```
  @experimental
  Stream<void> get allInstancesFinished =>
      allInstancesFinishedController.stream;

  @override
  String toString() {
    return 'soundHash: $soundHash has ${handles.length} active handles';
  }
}

/// sound event types
enum SoundEventType {
  /// handle reached the end of playback
  handleIsNoMoreValid,

  /// the sound has been disposed
  soundDisposed,
}

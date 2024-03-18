import 'dart:async';
import 'dart:collection';

import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:meta/meta.dart';

/// Deprecated alias to [SoundEventType].
@Deprecated('Use SoundEventType instead')
typedef SoundEvent = SoundEventType;

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({
  SoundEventType event,
  SoundProps sound,
  SoundHandle handle,
});

/// sound event types
enum SoundEventType {
  /// handle reached the end of playback
  handleIsNoMoreValid,

  /// the sound has been disposed
  soundDisposed,
}

class SoundProps {
  ///
  SoundProps(this.soundHash);

  /// The hash uniquely identifying this loaded sound.
  final SoundHash soundHash;

  /// The handles of currently playing instances of this sound.
  ///
  /// A sound (expressed as [SoundProps]) can be loaded once, but then
  /// played multiple times. It's even possible to play several instances
  /// of the sound simultaneously.
  ///
  /// Each time you [SoLoud.play] a sound, you get back a [SoundHandle],
  /// and that same handle will be added to this [handles] set.
  /// When the sound finishes playing, its handle will be removed from this set.
  ///
  /// This set is unmodifiable.
  late final UnmodifiableSetView<SoundHandle> handles =
      UnmodifiableSetView(handlesInternal);

  /// The [internal] backing of [handles].
  ///
  /// Use [handles].
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

  @override
  String toString() {
    return 'soundHash: $soundHash has ${handles.length} active handles';
  }
}

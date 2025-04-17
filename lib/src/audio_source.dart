import 'dart:async';
import 'dart:collection';

import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:meta/meta.dart';

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({
  SoundEventType event,
  AudioSource sound,
  SoundHandle handle,
});

/// sound event types
enum SoundEventType {
  /// handle reached the end of playback
  handleIsNoMoreValid,

  /// the sound has been disposed
  soundDisposed,
}

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
///
/// Every [AudioSource] can have their own filters. You can add, remove and
/// query wether a filter is active with `addFilter()`,
/// `removeGlobalFilter()` and `isFilterActive()`, respectively. The effect must
/// be set before playing the sound and parameters can be changed after having
/// the a voice handle:
/// ```dart
///   final sound = await SoLoud.instance.loadAsset('...');
///   sound.addFilter(FilterType.echoFilter);
///   final handle = SoLoud.instance.play(sound, paused: true);
///   sound.setFilterParameter();
/// ```
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

  /// Backing controller for [soundEvents].
  @internal
  late final StreamController<StreamSoundEvent> soundEventsController =
      StreamController.broadcast();

  /// the user can listen when a sound ends
  Stream<StreamSoundEvent> get soundEvents => soundEventsController.stream;

  /// Backing controller for [allInstancesFinished].
  @internal
  late final StreamController<void> allInstancesFinishedController =
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

  /// This can be used to access all the available filter functionalities
  /// for this [AudioSource].
  ///
  /// **IMPORTANT**: filters for individual sounds are not supported on the
  /// web platform.
  ///
  /// **IMPORTANT**: the filter must be added before playing. Only voice handles
  /// played after adding a filter will play with the filter chosen:
  ///
  /// ```dart
  /// AudioSource sound = await SoLoud.instance.loadAsset(...);
  /// /// activate the filter.
  /// sound.filters.pitchShiftFilter.activate();
  /// /// start playing it.
  /// soundHandle = await SoLoud.instance.play(sound, looping: true);
  /// /// deactivate it.
  /// sound.filters.pitchShiftFilter.deactivate();
  /// ```
  ///
  /// It's possible to get and set filter parameters:
  /// ```dart
  /// /// Set
  /// sound.filters.pitchShiftFilter.wet(soundHandle: soundHandle).value = 0.6;
  /// /// Get
  /// final shiftValue = sound.filters.pitchShiftFilter.wet(
  ///   soundHandle: soundHandle,
  /// ).value;
  /// ```
  /// or fade/oscillate a parameter:
  /// ```dart
  /// /// Fade
  /// sound.filters.pitchShiftFilter.shift(soundHandle: soundHandle)
  ///     .fadeFilterParameter(
  ///       to: 3,
  ///       time: const Duration(milliseconds: 2500),
  ///     );
  /// /// Oscillate
  /// sound.filters.pitchShiftFilter.shift(soundHandle: soundHandle)
  ///     .oscillateFilterParameter(
  ///       from: 0.4,
  ///       to: 1.8,
  ///       time: const Duration(milliseconds: 2500),
  ///     );
  /// ```
  ///
  /// It's possible to query filter parameters:
  /// ```dart
  /// final shiftParams = sound.filters.pitchShiftFilter.queryShift;
  /// ```
  ///
  /// Now with "shiftParams" you have access to:
  /// - `toString()` gives the "human readable" parameter name.
  /// - `min` which represent the "shift" minimum accepted value.
  /// - `max` which represent the "shift" maximum accepted value.
  /// - `def` which represent the "shift" default value.
  ///
  late final filters = FiltersSingle(soundHash: soundHash);

  @override
  String toString() {
    return 'soundHash: $soundHash has ${handles.length} active handles';
  }
}

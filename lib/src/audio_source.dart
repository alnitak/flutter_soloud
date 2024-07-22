import 'dart:async';
import 'dart:collection';

import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filter_params.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:logging/logging.dart';
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
/// ```
///   final sound = await SoLoud.instance.loadAsset('...');
///   sound.addFilter(FilterType.echoFilter);
///   final handle = SoLoud.instance.play(sound, paused: true);
///   sound.setFilterParameter();
/// ```
class AudioSource {
  /// Constructs an instance of [AudioSource].
  @internal
  AudioSource(this.soundHash);

  static final Logger _log = Logger('flutter_soloud.AudioSource');

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

  // ///////////////////////////////////////
  // / Filters for this [soundHash]
  // ///////////////////////////////////////

  /// Checks whether the given [filterType] is active.
  ///
  /// Returns `-1` if the filter is not active. Otherwise, returns
  /// the index of the given filter.
  int isFilterActive(FilterType filterType) {
    final ret = SoLoudController().soLoudFFI.isFilterActive(
          filterType,
          soundHash: soundHash,
        );
    if (ret.error != PlayerErrors.noError) {
      _log.severe(() => 'isFilterActive(): ${ret.error}');
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.index;
  }

  /// Adds a filter [filterType] to this sound.
  /// IMPORTANT: the filter must be added before playing. Only voice handles
  /// played after adding a filter will play with the filter chosen.
  ///
  /// Throws [SoLoudMaxFilterNumberReachedException] when the max number of
  ///     concurrent filter is reached (default max filter is 8).
  /// Throws [SoLoudFilterAlreadyAddedException] when trying to add a filter
  ///     that has already been added.
  void addFilter(FilterType filterType) {
    final error = SoLoudController().soLoudFFI.addFilter(
          filterType,
          soundHash: soundHash,
        );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'addGlobalFilter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Removes [filterType] from all sounds.
  void removeFilter(FilterType filterType) {
    final error = SoLoudController().soLoudFFI.removeFilter(
          filterType,
          soundHash: soundHash,
        );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'removeGlobalFilter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Set the effect parameter with id [attributeId] of [filterType]
  /// with [value] value.
  ///
  /// Specify the [attributeId] of the parameter (which you can learn from
  /// [SoLoud.getFilterParamNames]), and its new [value].
  ///
  /// [handle] the handle to set the filter to.
  /// [filterType] filter to modify a param.
  /// Returns [PlayerErrors.noError] if no errors.
  void setFilterParameter(
    SoundHandle handle,
    FilterType filterType,
    int attributeId,
    double value,
  ) {
    final error = SoLoudController().soLoudFFI.setFilterParams(
          filterType,
          attributeId,
          value,
          handle: handle,
        );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'setFxParams(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Get the effect parameter value with id [attributeId] of [filterType].
  ///
  /// Specify the [attributeId] of the parameter (which you can learn from
  /// [SoLoud.getFilterParamNames]).
  ///
  /// [handle] the handle to get the attribute value from. If equal to 0,
  /// it gets the global filter value.
  /// [filterType] the filter to modify a parameter.
  /// Returns the value of the parameter.
  double getFilterParameter(
    SoundHandle handle,
    FilterType filterType,
    int attributeId,
  ) {
    final ret = SoLoudController().soLoudFFI.getFilterParams(
          filterType,
          attributeId,
          handle: handle,
        );
    if (ret.error == PlayerErrors.filterNotFound) {
      throw const SoLoudFilterNotFoundException();
    }
    if (ret.error == PlayerErrors.soundHandleNotFound) {
      throw const SoLoudSoundHandleNotFoundCppException();
    }
    if (ret.error != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.value;
  }

  /// Fade a parameter of a filter.
  ///
  /// [handle] the handle of the voice to apply the fade. If equal to 0,
  /// it fades the global filter.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [to] value the attribute should go in [time] duration.
  /// [time] the fade slope duration.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void fadeFilterParameter(
    SoundHandle handle,
    FilterType filterType,
    int attributeId,
    double to,
    Duration time,
  ) {
    if (!SoLoud.instance.isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = SoLoudController().soLoudFFI.fadeFilterParameter(
          filterType,
          attributeId,
          to,
          time.toDouble(),
          handle: handle,
        );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'fadeFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Oscillate a parameter of a filter.
  ///
  /// [handle] the handle of the voice to apply the fade. If equal to 0,
  /// it fades the global filter.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [from] the starting value the attribute sould start to oscillate.
  /// [to] the ending value the attribute sould end to oscillate.
  /// [time] the fade slope duration.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void oscillateFilterParameter(
    SoundHandle handle,
    FilterType filterType,
    int attributeId,
    double from,
    double to,
    Duration time,
  ) {
    if (!SoLoud.instance.isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = SoLoudController().soLoudFFI.oscillateFilterParameter(
          filterType,
          attributeId,
          from,
          to,
          time.toDouble(),
          handle: handle,
        );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'oscillateFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  @override
  String toString() {
    return 'soundHash: $soundHash has ${handles.length} active handles';
  }
}

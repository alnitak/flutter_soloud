import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:logging/logging.dart';

/// Helper class to manage all buses.
class Buses {
  /// The singleton instance of this class.
  factory Buses() => _instance;

  Buses._internal();

  static final Buses _instance = Buses._internal();

  /// The list of all buses.
  final List<Bus> buses = [];

  /// Get a bus by name.
  ///
  /// [name] the name of the bus.
  /// [orElse] the function to call if the bus is not found.
  Bus? byName(String name, {Bus Function()? orElse}) {
    return buses.firstWhere((bus) => bus.name == name, orElse: orElse);
  }

  /// Get a bus by ID.
  ///
  /// [id] the ID of the bus.
  /// [orElse] the function to call if the bus is not found.
  Bus? byId(int id, {Bus Function()? orElse}) {
    return buses.firstWhere((bus) => bus.busId == id, orElse: orElse);
  }
}

/// A Set of mixing buses to have a global control of all active buses.
///

/// A mixing bus is a special audio source that plays other audio sources
/// through it. Useful for grouped volume control, per-bus filtering, and
/// routing.
///
/// See also:
/// - [SoLoud.createMixingBus] to create a new bus.
class Bus {
  /// Creates a new bus.
  Bus({this.name = ''}) {
    busId = SoLoudController().soLoudFFI.createBus();
    if (busId > 0) {
      _isValid = true;
      Buses().buses.add(this);
    }
    filters = FiltersSingle(busId: busId);
  }

  static final Logger _log = Logger('flutter_soloud.Bus');

  /// The filters for this bus.
  ///
  /// Please see [SoLoud.filters]
  late FiltersSingle filters;

  /// The sound handle of the bus itself once it's playing on the engine.
  SoundHandle? soundHandle;

  /// The name of this bus.
  final String name;

  /// The ID of this bus.
  /// Internally managed on native side. The first bus has ID 1.
  late final int busId;

  /// Whether this bus is valid.
  bool _isValid = false;

  /// The number of channels of this bus.
  var _channels = Channels.stereo;

  /// Destroys this bus.
  ///
  /// All the sounds playing through this bus will be stopped.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  void dispose() {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    Buses().buses.remove(this);
    SoLoudController().soLoudFFI.destroyBus(busId);
    _isValid = false;
    soundHandle = const SoundHandle.error();
  }

  /// Play the bus itself on the main SoLoud engine so it becomes audible.
  /// You must call this before sounds routed through the bus can be heard.
  ///
  /// [volume] playback volume (1.0 = full).
  /// [paused] whether to start paused.
  /// Returns the voice handle for the bus, or 0 on error.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  SoundHandle playOnEngine({double volume = 1.0, bool paused = false}) {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    final handle = SoLoudController().soLoudFFI.busPlayOnEngine(
          busId,
          volume,
          paused,
        );
    soundHandle = SoundHandle(handle);
    return soundHandle!;
  }

  /// Play an audio source through a mixing bus.
  ///
  /// [source] the audio source to play.
  /// [volume] playback volume.
  /// [pan] panning (-1 left, 0 center, 1 right).
  /// [paused] whether to start paused.
  /// Returns the voice handle, or 0 on error.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  SoundHandle play(
    AudioSource source, {
    double volume = 1.0,
    double pan = 0.0,
    bool paused = false,
  }) {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    final handle = SoLoudController().soLoudFFI.busPlay(
          busId,
          source.soundHash.hash,
          volume,
          pan,
          paused,
        );
    return SoundHandle(handle);
  }

  /// Set the number of output channels for the bus (default is 2 = stereo).
  ///
  /// [channels] number of channels.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  void setChannels({Channels channels = Channels.stereo}) {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    _channels = channels;
    SoLoudController().soLoudFFI.busSetChannels(busId, channels.count);
  }

  /// Get the approximate output volume for a specific channel of this bus.
  /// Useful for VU meters or level indicators.
  /// Visualization must be enabled first.
  ///
  /// [channel] the output channel index (0 = left, 1 = right, etc.).
  /// Returns the approximate volume, or 0 if the bus is not found.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  double getChannelVolume(int channel) {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    if (channel < 0 || channel >= _channels.count) {
      return 0;
    }
    return SoLoudController().soLoudFFI.busGetApproximateVolume(busId, channel);
  }

  /// Move a live voice (identified by its handle) into this bus.
  /// The voice will be reparented so it plays through the bus.
  /// Useful for dynamically routing sounds in/out of filtered busses.
  ///
  /// [handle] handle of the voice to annex.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  void annexSound(SoundHandle handle) {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    SoLoudController().soLoudFFI.busAnnexSound(busId, handle.id);
  }

  /// Get the number of voices currently playing through this bus.
  ///
  /// Returns the active voice count, or 0 if the bus is not found.
  ///
  /// Throws [SoLoudBusDisposedDartException] if the bus has already
  /// been disposed.
  int getActiveVoiceCount() {
    if (!_isValid) {
      _log.warning('bus $busId is already disposed');
      throw const SoLoudBusDisposedDartException();
    }
    return SoLoudController().soLoudFFI.busGetActiveVoiceCount(busId);
  }
}

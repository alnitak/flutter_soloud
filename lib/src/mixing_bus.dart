import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

/// A Set of mixing buses to have a global control of all active buses.
///

/// A mixing bus is a special audio source that plays other audio sources
/// through it. Useful for grouped volume control, per-bus filtering, and
/// routing.
///
/// See also:
/// - [SoLoud.createBus] to create a new bus.
class Bus {
  /// Creates a new bus.
  Bus() {
    _busId = SoLoudController().soLoudFFI.createBus();
  }

  /// The ID of this bus.
  late final int _busId;

  /// The channels of this bus.
  var _channels = Channels.stereo;

  /// Destroys this bus.
  void dispose() {
    SoLoudController().soLoudFFI.destroyBus(_busId);
  }

  /// Play the bus itself on the main SoLoud engine so it becomes audible.
  /// You must call this before sounds routed through the bus can be heard.
  ///
  /// [volume] playback volume (1.0 = full).
  /// [paused] whether to start paused.
  /// Returns the voice handle for the bus, or 0 on error.
  SoundHandle playOnEngine({double volume = 1.0, bool paused = false}) {
    final handle = SoLoudController().soLoudFFI.busPlayOnEngine(
          _busId,
          volume,
          paused,
        );
    return SoundHandle(handle);
  }

  /// Play a loaded sound (identified by [soundHash]) through a mixing bus.
  /// The sound must have been previously loaded via loadFile/loadMem.
  ///
  /// [soundHash] hash of the loaded audio source.
  /// [volume] playback volume.
  /// [pan] panning (-1 left, 0 center, 1 right).
  /// [paused] whether to start paused.
  /// Returns the voice handle, or 0 on error.
  SoundHandle play(
    int soundHash, {
    double volume = 1.0,
    double pan = 0.0,
    bool paused = false,
  }) {
    final handle = SoLoudController().soLoudFFI.busPlay(
          _busId,
          soundHash,
          volume,
          pan,
          paused,
        );
    return SoundHandle(handle);
  }

  /// Set the number of output channels for the bus (default is 2 = stereo).
  ///
  /// [channels] number of channels.
  void setChannels({Channels channels = Channels.stereo}) {
    _channels = channels;
    SoLoudController().soLoudFFI.busSetChannels(_busId, channels.count);
  }

  /// Get the approximate output volume for a specific channel of this bus.
  /// Useful for VU meters or level indicators.
  /// Visualization must be enabled first.
  ///
  /// [channel] the output channel index (0 = left, 1 = right, etc.).
  /// Returns the approximate volume, or 0 if the bus is not found.
  double getChannelVolume(int channel) {
    if (channel < 0 || channel >= _channels.count) {
      // TODO(alnitak): maybe return 0 instead of throwing an error?
      throw RangeError('Channel index out of range');
    }
    return SoLoudController()
        .soLoudFFI
        .busGetApproximateVolume(_busId, channel);
  }

  /// Move a live voice (identified by its handle) into this bus.
  /// The voice will be reparented so it plays through the bus.
  /// Useful for dynamically routing sounds in/out of filtered busses.
  ///
  /// [handle] handle of the voice to annex.
  void annexSound({required SoundHandle handle}) {
    SoLoudController().soLoudFFI.busAnnexSound(_busId, handle.id);
  }

  /// Get the number of voices currently playing through this bus.
  ///
  /// Returns the active voice count, or 0 if the bus is not found.
  int getActiveVoiceCount() {
    return SoLoudController().soLoudFFI.busGetActiveVoiceCount(_busId);
  }
}

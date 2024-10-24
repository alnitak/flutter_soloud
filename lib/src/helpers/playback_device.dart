import 'package:flutter_soloud/src/soloud.dart';
import 'package:meta/meta.dart';

/// CaptureDevice exposed to Dart.
///
/// Used to get a list available playback devices by calling
/// [SoLoud.listPlaybackDevices].
/// Once you have a list of playback devices you can use
/// [SoLoud.changeDevice] to change the output device while the engine
/// is running or use [SoLoud.init] to set the output device while
/// initializing the engine.
final class PlaybackDevice {
  /// Constructs a new [PlaybackDevice].
  @internal
  // ignore: avoid_positional_boolean_parameters
  const PlaybackDevice(this.id, this.isDefault, this.name);

  /// The ID of the device.
  final int id;

  /// Whether this is the default playback device.
  final bool isDefault;

  /// The name of the device.
  final String name;

  @override
  String toString() =>
      '\nPlaybackDevice(id: $id, isDefault: $isDefault, name: $name)';
}

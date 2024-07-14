import 'package:meta/meta.dart';

/// A handle for a sound that is currently playing.
///
/// SoLoud's methods, such as `play()`, return the [SoundHandle] of the
/// newly instanced sound. That handle can then be used in other methods,
/// such as `stop()` or `seek()`, to affect that particular instance.
///
/// On the C++ side, sound handles are just raw integers.
/// On the Dart side, they are implemented as
/// an [extension type](https://dart.dev/language/extension-types) around [int]
/// that makes them statically type safe.
/// It is not possible, for example, to mistakenly pass a random
/// integer number as if it was a handle, or a sound hash as if it was
/// a handle.
///
/// Constructors are marked [internal] because it should not be possible
/// for users to create a handle from Dart.
extension type const SoundHandle._(int id) {
  /// Constructs a valid handle with [id].
  @internal
  const SoundHandle(this.id)
      : assert(
            id >= 0,
            'Handle with id<0 is being constructed. '
            'These are reserved for invalid handles.');

  /// Constructs an invalid handle (for APIs that need to return _some_ handle
  /// even during errors).
  @internal
  const SoundHandle.error() : this._(-1);

  /// Checks if the handle represents an error (it was constructed
  /// with [SoundHandle.error]).
  ///
  /// Does _not_ check whether the handle is "valid" (i.e. attached
  /// to an active sound) or not.
  bool get isError => id < 0;
}

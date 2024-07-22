import 'dart:math';

import 'package:meta/meta.dart';

/// A hash of an `AudioSource` instance.
///
/// Each newly loaded sound gets a unique hash. This hash is then used
/// to uniquely identify the loaded sound to SoLoud.
///
/// On the C++ side, sound hashes are just raw integers.
/// On the Dart side, they are implemented as
/// an [extension type](https://dart.dev/language/extension-types) around [int]
/// that makes them statically type safe.
/// It is not possible, for example, to mistakenly pass a random
/// integer number as if it was a sound hash, or a sound _handle_ as if it was
/// a sound hash.
///
/// Constructors are marked [internal] because it should not be possible
/// for users to create a sound hash from Dart.
extension type const SoundHash._(int hash) {
  /// Constructs a valid sound hash with [hash].
  @internal
  const SoundHash(this.hash)
      : assert(
          hash > 0,
          'Trying to create a valid sound hash with the value 0',
        );

  /// Constructs an invalid sound hash
  /// (for APIs that need to return _some_ hash even during errors).
  @internal
  const SoundHash.invalid() : this._(0);

  /// Generate a "fake" [SoundHash] for generated (i.e. non-loaded) sounds.
  ///
  /// Sound hashes are normally computed from a file name in the C code.
  @internal
  factory SoundHash.random() {
    // Dart must support 32 bit systems.
    // Shifting by 31 because the leftmost bit is used for the sign.
    const largest32BitInt = 1 << 31;
    // Generate a random integer, but not 0 (which is reserved for invalid
    // sound hashes).
    final soundHash = _random.nextInt(largest32BitInt - 1) + 1;
    return SoundHash(soundHash);
  }

  /// Checks if the hash was constructed normally (`true`)
  /// or if it should be considered an error (`false`).
  bool get isValid => hash != 0;

  /// A random generator used for creating (fake) sound hashes for
  /// generated (i.e. non-loaded) sounds.
  static final Random _random = Random();
}

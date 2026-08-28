// Prebuilt Xiph library wiring for the flutter_soloud build hook.
//
// The Xiph libraries (Opus, Ogg, Vorbis, FLAC) are NOT compiled by the hook;
// the prebuilt artifacts checked into this repo (produced by the scripts in
// `xiph/`) are linked instead:
//
// - iOS/macOS: static archives are linked into the plugin library.
// - Android/Windows: shared libraries/DLLs are linked and additionally
//   emitted as code assets so they are bundled with the app.
// - Linux: the bundled shared libraries are linked (as before); they are not
//   bundled — set up system libraries or rpath as before this migration.

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';

/// Xiph library basenames, in link order (vorbisfile/vorbisenc before vorbis,
/// everything before ogg).
const _xiphLibs = ['FLAC', 'opus', 'vorbisfile', 'vorbisenc', 'vorbis', 'ogg'];

final _androidAbi = {
  Architecture.arm: 'armeabi-v7a',
  Architecture.arm64: 'arm64-v8a',
  Architecture.ia32: 'x86',
  Architecture.x64: 'x86_64',
};

/// How the prebuilt Xiph libraries are linked for one target.
final class XiphLink {
  XiphLink._({
    required this.libraries,
    required this.libraryDirectories,
    required this.includeDirs,
    required this.bundledAssets,
    required this.dependencies,
  });

  factory XiphLink.empty() => XiphLink._(
    libraries: const [],
    libraryDirectories: const [],
    includeDirs: const [],
    bundledAssets: const [],
    dependencies: const [],
  );

  factory XiphLink.forTarget(BuildInput input) {
    final code = input.config.code;
    final os = code.targetOS;
    final packageRoot = input.packageRoot;
    final packageName = input.packageName;

    switch (os) {
      case OS.macOS:
        const dir = 'macos/flutter_soloud/libs';
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [packageRoot.resolve(dir).toFilePath()],
          includeDirs: const ['macos/flutter_soloud/include'],
          bundledAssets: const [],
          dependencies: [
            for (final lib in _xiphLibs) packageRoot.resolve('$dir/lib$lib.a'),
          ],
        );

      case OS.iOS:
        // Device and simulator builds use separate static archives.
        final suffix = switch (code.iOS.targetSdk) {
          IOSSdk.iPhoneSimulator => 'simulator',
          _ => 'device',
        };
        const dir = 'ios/flutter_soloud/libs';
        final names = [for (final lib in _xiphLibs) '${lib}_iOS-$suffix'];
        return XiphLink._(
          libraries: names,
          libraryDirectories: [packageRoot.resolve(dir).toFilePath()],
          includeDirs: const ['ios/flutter_soloud/include'],
          bundledAssets: const [],
          dependencies: [
            for (final name in names) packageRoot.resolve('$dir/lib$name.a'),
          ],
        );

      case OS.android:
        final abi = _androidAbi[code.targetArchitecture];
        if (abi == null) {
          throw UnsupportedError(
            'Unsupported Android architecture: ${code.targetArchitecture}',
          );
        }
        final dir = 'android/libs/$abi';
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [packageRoot.resolve(dir).toFilePath()],
          includeDirs: const ['android/include'],
          bundledAssets: [
            for (final lib in _xiphLibs)
              CodeAsset(
                package: packageName,
                name: 'xiph/$abi/lib$lib.so',
                linkMode: DynamicLoadingBundled(),
                file: packageRoot.resolve('$dir/lib$lib.so'),
              ),
          ],
          dependencies: [
            for (final lib in _xiphLibs) packageRoot.resolve('$dir/lib$lib.so'),
          ],
        );

      case OS.windows:
        const dir = 'windows/libs';
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [packageRoot.resolve(dir).toFilePath()],
          includeDirs: const ['windows/include'],
          bundledAssets: [
            for (final lib in _xiphLibs)
              CodeAsset(
                package: packageName,
                name: 'xiph/$lib.dll',
                linkMode: DynamicLoadingBundled(),
                file: packageRoot.resolve('$dir/$lib.dll'),
              ),
          ],
          dependencies: [
            for (final lib in _xiphLibs) ...[
              packageRoot.resolve('$dir/$lib.lib'),
              packageRoot.resolve('$dir/$lib.dll'),
            ],
          ],
        );

      case OS.linux:
        const dir = 'linux/libs';
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [packageRoot.resolve(dir).toFilePath()],
          includeDirs: const ['linux/include'],
          bundledAssets: const [],
          dependencies: [
            for (final lib in _xiphLibs)
              packageRoot.resolve('$dir/lib$lib.so'),
          ],
        );

      default:
        throw UnsupportedError('Unsupported target OS: $os');
    }
  }

  /// Library names passed to the linker (`-l<name>`).
  final List<String> libraries;

  /// Directories searched for [libraries]. Absolute paths.
  final List<String> libraryDirectories;

  /// Xiph header directories, relative to the package root.
  final List<String> includeDirs;

  /// Extra code assets to bundle (prebuilt shared libraries).
  final List<CodeAsset> bundledAssets;

  /// Prebuilt files consumed by the build, for cache invalidation.
  final List<Uri> dependencies;
}

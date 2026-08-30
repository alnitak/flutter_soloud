// Build hook for flutter_soloud.
//
// Compiles the SoLoud engine and the plugin sources in `src/` into
// `libflutter_soloud_plugin` for the target OS/architecture, linking the
// prebuilt Xiph libraries checked into this repo (see `xiph/`).
//
// This replaces the previous per-platform build plumbing (Android CMake,
// iOS/macOS CocoaPods script phases, SwiftPM unity build). Web is not covered
// by hooks; the emscripten build in `web/` is unchanged.
//
// Configuration (in the *root app* pubspec.yaml):
//
// hooks:
//   user_defines:
//     flutter_soloud:
//       no_xiph_libs: true   # build without Opus/Ogg/Vorbis/FLAC support

import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

/// The asset id is `package:flutter_soloud/src/bindings.cpp`, matching the
/// `asset-id` in `ffigen.yaml` used by the generated `@Native` bindings.
const _assetName = 'src/bindings.cpp';
const _libName = 'flutter_soloud_plugin';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final code = input.config.code;
    final os = code.targetOS;
    final arch = code.targetArchitecture;
    final isApple = os == OS.macOS || os == OS.iOS;

    // Xiph libs are disabled via
    // `hooks.user_defines.flutter_soloud.no_xiph_libs: true` in the root
    // app's pubspec.
    final noXiph = input.userDefines['no_xiph_libs'] == true;
    final xiph = noXiph ? XiphLink.empty() : XiphLink.forTarget(input);

    final defines = <String, String?>{
      'FLUTTER_PLUGIN_IMPL': null,
      'SIGNALSMITH_USE_PFFFT': null,
      'WITH_MINIAUDIO': null,
      'WITH_NULL': null,
      // Force NDEBUG across all compilers (comment out for native debugging).
      'NDEBUG': null,
      // Remove PulseAudio since it can cause stutters and glitches.
      'MA_NO_PULSEAUDIO': null,
      if (isApple) 'WITH_COREAUDIO': null,
      if (os == OS.linux) 'WITH_ALSA': null,
      if (os == OS.windows) ...{
        'NOMINMAX': null,
        '_CRT_SECURE_NO_WARNINGS': null,
      },
      if (noXiph) 'NO_XIPH_LIBS': null,
    };

    final flags = <String>[
      if (os != OS.windows) '-fvisibility=hidden',
      // Force maximum optimization regardless of Flutter build mode.
      // For native debugging: comment out 'NDEBUG' above and replace the line
      // below with: if (os == OS.windows) ...['/Od', '/Zi', '/EHsc'] else ...['-O0', '-g'],
      if (os == OS.windows) ...['/Ox', '/EHsc'] else '-O3',
      if (os == OS.android) ...[
        '-ffast-math',
        '-funroll-loops',
        '-fomit-frame-pointer',
        '-ffunction-sections',
        '-fdata-sections',
        '-Wl,--gc-sections',
        // Support Android 15 16k page size.
        '-Wl,-z,max-page-size=16384',
        // arm64: NEON is mandatory — no extra flags needed. On armeabi-v7a
        // enable NEON explicitly (hard-float is the default on modern NDKs;
        // -mfloat=softfp was removed from NDK r25+ clang).
        if (arch == Architecture.arm) '-mfpu=neon',
        if (arch == Architecture.ia32 || arch == Architecture.x64) ...[
          '-msse2',
          '-msse3',
        ],
      ],
      if (isApple || os == OS.linux) '-Wno-vla',
      if (os == OS.linux && arch == Architecture.x64) ...['-msse2', '-msse3'],
    ];

    final includes = [
      'src',
      'src/soloud/include',
      'src/soloud/src',
      'src/pffft',
      ...xiph.includeDirs,
    ];

    // iOS 26.4+/Xcode 16+ duplicate-symbol workaround for miniaudio's
    // ma_ios_notification_handler (renames the ObjC class per plugin).
    final forcedIncludes = [
      if (os == OS.iOS)
        'ios/flutter_soloud/Sources/flutter_soloud/miniaudio_objc_prefix.h',
    ];

    // On Apple, miniaudio.h pulls in AVFoundation Objective-C headers, so the
    // SoLoud miniaudio backend must be compiled as Objective-C++. CBuilder
    // with `language: .cpp` force-feeds `-x c++` for every source, which
    // breaks that — so the backend is built separately as a static library
    // (no `-x` override: the .mm extension selects Objective-C++) and linked
    // into the plugin library below.
    if (isApple) {
      final objcBuilder = CBuilder.library(
        name: 'flutter_soloud_miniaudio_objc',
        language: Language.objectiveC,
        std: 'c++17',
        linkModePreference: LinkModePreference.static,
        sources: const ['src/soloud_miniaudio_objc.mm'],
        includes: includes,
        forcedIncludes: forcedIncludes,
        defines: defines,
        flags: flags,
      );
      await objcBuilder.run(input: input, output: output);
    }

    final builder = CBuilder.library(
      name: _libName,
      assetName: _assetName,
      language: Language.cpp,
      std: 'c++17',
      sources: collectSources(input.packageRoot, os),
      includes: includes,
      forcedIncludes: forcedIncludes,
      defines: defines,
      flags: flags,
      // Link the C++ runtime statically on Android: the prebuilt Xiph
      // libraries don't need libc++_shared.so, and nothing else bundles it
      // into the APK now that the CMake/AGP wiring is gone.
      cppLinkStdLib: os == OS.android ? 'c++_static' : null,
      frameworks: isApple
          ? const ['Foundation', 'AudioToolbox', 'AVFAudio']
          : const [],
      libraries: [
        ...xiph.libraries,
        if (isApple) 'flutter_soloud_miniaudio_objc',
        if (os == OS.android) ...['log', 'android'],
        if (os == OS.linux) 'asound',
      ],
      // '.' is the hook output directory, where the miniaudio ObjC++ static
      // library was just built.
      libraryDirectories: [if (isApple) '.', ...xiph.libraryDirectories],
    );
    await builder.run(input: input, output: output);

    // Bundle prebuilt shared Xiph libraries on platforms where they are not
    // statically linked into the plugin library.
    for (final asset in xiph.bundledAssets) {
      output.assets.code.add(asset);
    }
    for (final dependency in xiph.dependencies) {
      output.dependencies.add(dependency);
    }
  });
}

// ---------------------------------------------------------------------------
// Source collection
// ---------------------------------------------------------------------------

/// Collects the plugin sources (paths relative to [packageRoot]) for
/// [targetOS].
List<String> collectSources(Uri packageRoot, OS targetOS) {
  final rootPath = packageRoot.toFilePath().replaceAll(r'\', '/');
  final isApple = targetOS == OS.macOS || targetOS == OS.iOS;
  final sources = <String>[];

  void addDir(
    String rel, {
    bool recursive = false,
    List<String> extensions = const ['.cpp'],
    bool Function(String path)? exclude,
  }) {
    final dir = Directory.fromUri(packageRoot.resolve('src/$rel'));
    if (!dir.existsSync()) return;
    for (final entity in dir.listSync(recursive: recursive)) {
      if (entity is! File) continue;
      final path = entity.path.replaceAll(r'\', '/');
      if (!extensions.any(path.endsWith)) continue;
      if (exclude?.call(path) ?? false) continue;
      final relPath = path.startsWith(rootPath)
          ? path.substring(rootPath.length)
          : path;
      sources.add(relPath.startsWith('/') ? relPath.substring(1) : relPath);
    }
  }

  // Plugin sources (src/CMakeLists.txt PLUGIN_SOURCES). `flutter_soloud.cpp`
  // was the SwiftPM unity translation unit and is excluded on purpose.
  addDir('', exclude: (p) => p.endsWith('/flutter_soloud.cpp'));
  addDir('audiobuffer/');
  addDir('filters/');
  addDir('mixeroutput/');
  addDir('synth/');
  addDir('waveform/');
  // pffft.c is C99; the toolchain compiles it as C based on its extension.
  addDir('pffft/', extensions: const ['.c']);

  // SoLoud engine (src/src.cmake TARGET_SOURCES).
  addDir('soloud/src/core/');
  addDir('soloud/src/filter/');
  addDir(
    'soloud/src/audiosource/',
    recursive: true,
    // openmpt is Windows-only with explicit opt-in; never built here.
    exclude: (p) => p.contains('/openmpt/'),
  );

  // Backends: null and miniaudio everywhere, ALSA on Linux, CoreAudio on
  // Apple. On Apple, soloud_miniaudio.cpp must be compiled as Objective-C++
  // (miniaudio.h uses AVFoundation), so it is built separately from
  // src/soloud_miniaudio_objc.mm by hook/build.dart instead.
  addDir('soloud/src/backend/null/');
  if (!isApple) {
    addDir('soloud/src/backend/miniaudio/');
  }
  if (targetOS == OS.linux) {
    addDir('soloud/src/backend/alsa/');
  }
  if (isApple) {
    addDir('soloud/src/backend/coreaudio/');
  }

  return sources..sort();
}

// ---------------------------------------------------------------------------
// Prebuilt Xiph library wiring
// ---------------------------------------------------------------------------

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
            for (final lib in _xiphLibs) packageRoot.resolve('$dir/lib$lib.so'),
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

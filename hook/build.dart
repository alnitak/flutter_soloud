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
//       no_xiph_libs: true         # build without Opus/Ogg/Vorbis/FLAC
//       use_system_xiph_libs: true # link system Xiph libs (apt, brew, pacman)

// ignore_for_file: avoid_print

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

    // Xiph configuration:
    // - `no_xiph_libs: true` -> build without Xiph support
    // Platform-specific user defines:
    // - `<platform>_use_system_libs: true` -> link against system packages
    // - `<platform>_force_build_libs: true` -> compile from source via CMake
    // Default (both false): uses prebuilt libraries from GitHub release
    // or local cache (.dart_tool/flutter_soloud/xiph/prebuild/).
    bool? parseBool(Object? val) {
      if (val == null) return null;
      if (val is bool) return val;
      if (val is String) {
        if (val.toLowerCase() == 'true' || val == '1') return true;
        if (val.toLowerCase() == 'false' || val == '0') return false;
      }
      return null;
    }

    final noXiph = parseBool(input.userDefines['no_xiph_libs']) ?? false;

    final bool useSystemLibs;
    final bool forceBuildLibs;

    switch (os) {
      case OS.linux:
        useSystemLibs =
            parseBool(input.userDefines['linux_use_system_libs']) ??
            parseBool(input.userDefines['use_system_xiph_libs']) ??
            false;
        forceBuildLibs =
            parseBool(input.userDefines['linux_force_build_libs']) ?? false;
      case OS.macOS:
        useSystemLibs =
            parseBool(input.userDefines['macos_use_system_libs']) ?? false;
        forceBuildLibs =
            parseBool(input.userDefines['macos_force_build_libs']) ?? false;
      case OS.windows:
        useSystemLibs =
            parseBool(input.userDefines['windows_use_system_libs']) ?? false;
        forceBuildLibs =
            parseBool(input.userDefines['windows_force_build_libs']) ?? false;
      case OS.android:
        useSystemLibs = false;
        forceBuildLibs =
            parseBool(input.userDefines['android_force_build_libs']) ?? false;
      case OS.iOS:
        useSystemLibs = false;
        forceBuildLibs =
            parseBool(input.userDefines['ios_force_build_libs']) ?? false;
      default:
        useSystemLibs = false;
        forceBuildLibs = false;
    }

    if (useSystemLibs && forceBuildLibs) {
      throw ArgumentError(
        '[flutter_soloud] Conflicting options in pubspec.yaml: cannot set both '
        '`${os.name}_use_system_libs: true` and '
        '`${os.name}_force_build_libs: true`.',
      );
    }

    final XiphLink xiph;
    final String xiphMode;
    if (noXiph) {
      xiphMode = 'disabled';
      xiph = XiphLink.empty();
    } else if (useSystemLibs) {
      xiphMode = 'from system';
      xiph = await XiphLink.forSystem(input);
    } else if (forceBuildLibs) {
      xiphMode = 'built from git';
      xiph = await XiphLink.fromSource(input);
    } else {
      xiphMode = 'prebuilt';
      xiph = await XiphLink.forPrebuild(input);
    }

    final platformDisplayName = switch (os) {
      OS.macOS => 'macOS',
      OS.iOS => 'iOS',
      OS.linux => 'Linux',
      OS.windows => 'Windows',
      OS.android => 'Android',
      OS.fuchsia => 'Fuchsia',
      _ => os.name,
    };

    print(
      '[flutter_soloud] Building on $platformDisplayName ($arch) with Xiph '
      'libs [$xiphMode]',
    );

    final defines = <String, String?>{
      'FLUTTER_PLUGIN_IMPL': null,
      'SIGNALSMITH_USE_PFFFT': null,
      'WITH_MINIAUDIO': null,
      'WITH_NULL': null,
      // Force NDEBUG across all compilers (comment out for native debugging).
      'NDEBUG': null,
      if (os == OS.windows) ...{
        'NOMINMAX': null,
        '_CRT_SECURE_NO_WARNINGS': null,
      },
      if (noXiph) 'NO_XIPH_LIBS': null,
    };

    final flags = <String>[
      if (os != OS.windows) ...[
        '-fvisibility=hidden',
        '-Wno-unused-command-line-argument',
      ],
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
      'src/native_decoder',
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
        sources: const [
          'src/soloud_miniaudio_objc.mm',
          'src/native_decoder/os_decoder_apple.mm',
        ],
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
          ? const ['Foundation', 'AudioToolbox', 'AVFAudio', 'CoreAudio']
          : const [],
      libraries: [
        ...xiph.libraries,
        if (isApple) 'flutter_soloud_miniaudio_objc',
        if (os == OS.android) ...['log', 'android', 'mediandk'],
        if (os == OS.windows) ...['mfplat', 'mfreadwrite', 'mfuuid', 'shlwapi'],
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
  addDir(
    'native_decoder/',
    exclude: (p) {
      if (p.endsWith('.mm')) return true; // Built by objcBuilder on Apple
      // All Apple decoder logic is in os_decoder_apple.mm.
      if (isApple) return true;
      if (targetOS == OS.android) return !p.endsWith('os_decoder_android.cpp');
      if (targetOS == OS.windows) return !p.endsWith('os_decoder_windows.cpp');
      if (targetOS == OS.linux) return !p.endsWith('os_decoder_linux.cpp');
      return false;
    },
  );
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

  // Backends: null and miniaudio everywhere. On Apple, soloud_miniaudio.cpp
  // must be compiled as Objective-C++ (miniaudio.h uses AVFoundation), so it
  // is built separately from src/soloud_miniaudio_objc.mm by hook/build.dart
  // instead.
  addDir('soloud/src/backend/null/');
  if (!isApple) {
    addDir('soloud/src/backend/miniaudio/');
  }

  return sources..sort();
}

// ---------------------------------------------------------------------------
// Prebuilt Xiph library wiring
// ---------------------------------------------------------------------------

/// Xiph library basenames, in link order (vorbisfile/vorbisenc before vorbis,
/// everything before ogg).
const _xiphLibs = ['FLAC', 'opus', 'vorbisfile', 'vorbisenc', 'vorbis', 'ogg'];

/// Android ABI names matching the directories in xiph/prebuild/android/.
String? _androidAbi(Architecture arch) => switch (arch) {
  Architecture.arm64 => 'arm64-v8a',
  Architecture.arm => 'armeabi-v7a',
  Architecture.x64 => 'x86_64',
  Architecture.ia32 => 'x86',
  _ => null,
};

/// How the Xiph libraries are linked for one target.
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

  /// Links against prebuilt Xiph libraries.
  ///
  /// Searches for prebuilt files in the local cache directory
  /// `.dart_tool/flutter_soloud/xiph/prebuild/`. If missing, automatically
  /// downloads the corresponding archive from GitHub releases and extracts it.
  static Future<XiphLink> forPrebuild(BuildInput input) async {
    final code = input.config.code;
    final os = code.targetOS;
    final arch = code.targetArchitecture;
    final packageRoot = input.packageRoot;
    final packageName = input.packageName;

    final tag =
        (input.userDefines['prebuild_tag'] as String?)?.trim() ??
        _defaultPrebuildTag;

    final includeDir = await _ensurePrebuildInclude(packageRoot, tag);

    switch (os) {
      case OS.macOS:
        final dir = await _ensurePrebuildPlatform(
          packageRoot: packageRoot,
          os: os,
          arch: arch,
          subDir: 'macos',
          archiveName: 'xiph-macos.tar.gz',
          isZip: false,
          tag: tag,
          validator: (Directory d) => _xiphLibs.every(
            (String lib) => File('${d.path}/lib$lib.a').existsSync(),
          ),
        );
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [dir.path],
          includeDirs: [includeDir.path],
          bundledAssets: const [],
          dependencies: [
            for (final lib in _xiphLibs) File('${dir.path}/lib$lib.a').uri,
          ],
        );

      case OS.iOS:
        final suffix = switch (code.iOS.targetSdk) {
          IOSSdk.iPhoneSimulator => 'simulator',
          _ => 'device',
        };
        final names = [for (final lib in _xiphLibs) '${lib}_iOS-$suffix'];
        final dir = await _ensurePrebuildPlatform(
          packageRoot: packageRoot,
          os: os,
          arch: arch,
          subDir: 'ios',
          archiveName: 'xiph-ios.tar.gz',
          isZip: false,
          tag: tag,
          validator: (Directory d) => names.every(
            (String name) => File('${d.path}/lib$name.a').existsSync(),
          ),
        );
        return XiphLink._(
          libraries: names,
          libraryDirectories: [dir.path],
          includeDirs: [includeDir.path],
          bundledAssets: const [],
          dependencies: [
            for (final name in names) File('${dir.path}/lib$name.a').uri,
          ],
        );

      case OS.android:
        final abi = _androidAbi(code.targetArchitecture);
        if (abi == null) {
          throw UnsupportedError(
            'Unsupported Android architecture: ${code.targetArchitecture}',
          );
        }
        final dir = await _ensurePrebuildPlatform(
          packageRoot: packageRoot,
          os: os,
          arch: arch,
          subDir: 'android/$abi',
          archiveName: 'xiph-android.tar.gz',
          isZip: false,
          tag: tag,
          validator: (Directory d) => _xiphLibs.every(
            (String lib) => File('${d.path}/lib$lib.so').existsSync(),
          ),
        );
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [dir.path],
          includeDirs: [includeDir.path],
          bundledAssets: [
            for (final lib in _xiphLibs)
              CodeAsset(
                package: packageName,
                name: 'xiph/$abi/lib$lib.so',
                linkMode: DynamicLoadingBundled(),
                file: File('${dir.path}/lib$lib.so').uri,
              ),
          ],
          dependencies: [
            for (final lib in _xiphLibs) File('${dir.path}/lib$lib.so').uri,
          ],
        );

      case OS.windows:
        if (code.targetArchitecture != Architecture.x64 &&
            code.targetArchitecture != Architecture.arm64) {
          throw UnsupportedError(
            '[flutter_soloud] Bundled prebuilt Windows libraries only support '
            'x64 and arm64. For ${code.targetArchitecture.name}, either '
            'install system libraries and set `windows_use_system_libs: true` '
            'in pubspec.yaml, or build from source with '
            '`windows_force_build_libs: true`.',
          );
        }
        final archStr = arch == Architecture.arm64 ? 'arm64' : 'x64';
        final dir = await _ensurePrebuildPlatform(
          packageRoot: packageRoot,
          os: os,
          arch: arch,
          subDir: 'windows/$archStr',
          archiveName: 'xiph-windows-$archStr.zip',
          isZip: true,
          tag: tag,
          validator: (Directory d) => _xiphLibs.every(
            (String lib) =>
                File('${d.path}/$lib.dll').existsSync() &&
                File('${d.path}/$lib.lib').existsSync(),
          ),
        );
        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [dir.path],
          includeDirs: [includeDir.path],
          bundledAssets: [
            for (final lib in _xiphLibs)
              CodeAsset(
                package: packageName,
                name: 'xiph/$lib.dll',
                linkMode: DynamicLoadingBundled(),
                file: File('${dir.path}/$lib.dll').uri,
              ),
          ],
          dependencies: [
            for (final lib in _xiphLibs) ...[
              File('${dir.path}/$lib.lib').uri,
              File('${dir.path}/$lib.dll').uri,
            ],
          ],
        );

      case OS.linux:
        if (code.targetArchitecture != Architecture.x64 &&
            code.targetArchitecture != Architecture.arm64) {
          throw UnsupportedError(
            '[flutter_soloud] Bundled prebuilt Linux libraries only support '
            'x64 and arm64. For ${code.targetArchitecture.name}, either '
            'install system libraries and set `linux_use_system_libs: true` '
            'in pubspec.yaml, or build from source with '
            '`linux_force_build_libs: true`.',
          );
        }
        final archStr = arch == Architecture.arm64 ? 'arm64' : 'x64';
        final dir = await _ensurePrebuildPlatform(
          packageRoot: packageRoot,
          os: os,
          arch: arch,
          subDir: 'linux/$archStr',
          archiveName: 'xiph-linux-$archStr.tar.gz',
          isZip: false,
          tag: tag,
          validator: (Directory d) => _xiphLibs.every(
            (String lib) => File('${d.path}/lib$lib.so').existsSync(),
          ),
        );

        // Bundle each library by its runtime SONAME (e.g., libFLAC.so.14)
        // so references resolve properly without duplicating symlinks (fixes
        // issue #559).
        final soFiles = <File>[
          for (final lib in _xiphLibs) await _resolveLinuxSoFile(dir, lib),
        ];

        return XiphLink._(
          libraries: _xiphLibs,
          libraryDirectories: [dir.path],
          includeDirs: [includeDir.path],
          bundledAssets: [
            for (final file in soFiles)
              CodeAsset(
                package: packageName,
                name: 'xiph/${file.uri.pathSegments.last}',
                linkMode: DynamicLoadingBundled(),
                file: file.uri,
              ),
          ],
          dependencies: [for (final file in soFiles) file.uri],
        );

      default:
        throw UnsupportedError('Unsupported OS for bundled Xiph: $os');
    }
  }

  static const String _prebuildRepo = 'alnitak/flutter_soloud_prebuilds';
  static const String _defaultPrebuildTag = 'latest';
  static const String _prebuildVersionFileName =
      'flutter_soloud_prebuild_version.txt';

  static String _baseUrlForTag(String tag) => tag == 'latest'
      ? 'https://github.com/$_prebuildRepo/releases/latest/download'
      : 'https://github.com/$_prebuildRepo/releases/download/$tag';

  static File _getVersionFile(Directory dir) {
    final file = File('${dir.path}/$_prebuildVersionFileName');
    if (file.existsSync()) return file;
    return File('${dir.path}/version.txt');
  }

  static bool _hasHeaders(Directory dir, [String targetTag = 'latest']) {
    if (!dir.existsSync()) return false;
    if (targetTag != 'latest') {
      final versionFile = _getVersionFile(dir);
      if (versionFile.existsSync()) {
        final v = versionFile.readAsStringSync().trim();
        if (v != targetTag && 'v$v' != targetTag) return false;
      }
    }
    return File('${dir.path}/ogg/ogg.h').existsSync() &&
        File('${dir.path}/ogg/config_types.h').existsSync() &&
        File('${dir.path}/vorbis/codec.h').existsSync() &&
        File('${dir.path}/opus/opus.h').existsSync() &&
        File('${dir.path}/FLAC/all.h').existsSync();
  }

  static Future<Directory> _ensurePrebuildInclude(
    Uri packageRoot, [
    String tag = _defaultPrebuildTag,
  ]) async {
    final cacheDir = Directory.fromUri(
      packageRoot.resolve('.dart_tool/flutter_soloud/xiph/prebuild/include'),
    );
    if (_hasHeaders(cacheDir, tag)) return cacheDir;

    print(
      '[flutter_soloud] Downloading Xiph headers ($tag) from '
      '$_prebuildRepo...',
    );
    if (cacheDir.existsSync()) {
      cacheDir.deleteSync(recursive: true);
    }
    final resolvedTag = await _downloadAndExtract(
      '${_baseUrlForTag(tag)}/xiph-include.tar.gz',
      cacheDir,
      isZip: false,
    );
    final tagToWrite = resolvedTag ?? tag;
    File(
      '${cacheDir.path}/$_prebuildVersionFileName',
    ).writeAsStringSync('$tagToWrite\n');

    File.fromUri(
        packageRoot.resolve(
          '.dart_tool/flutter_soloud/xiph/prebuild/$_prebuildVersionFileName',
        ),
      )
      ..parent.createSync(recursive: true)
      ..writeAsStringSync('$tagToWrite\n');
    return cacheDir;
  }

  static Future<Directory> _ensurePrebuildPlatform({
    required Uri packageRoot,
    required OS os,
    required Architecture arch,
    required String subDir,
    required String archiveName,
    required bool isZip,
    required bool Function(Directory dir) validator,
    String tag = _defaultPrebuildTag,
  }) async {
    final cacheDir = Directory.fromUri(
      packageRoot.resolve('.dart_tool/flutter_soloud/xiph/prebuild/$subDir'),
    );
    final versionFile = _getVersionFile(cacheDir);
    if (tag != 'latest' && versionFile.existsSync()) {
      final v = versionFile.readAsStringSync().trim();
      if (v != tag && 'v$v' != tag) {
        if (cacheDir.existsSync()) {
          cacheDir.deleteSync(recursive: true);
        }
      }
    }
    if (validator(cacheDir)) return cacheDir;

    print(
      '[flutter_soloud] Downloading prebuilt Xiph libraries ($tag) '
      'for $os ($arch)...',
    );
    final extractTarget = (os == OS.android)
        ? Directory.fromUri(
            packageRoot.resolve(
              '.dart_tool/flutter_soloud/xiph/prebuild/android',
            ),
          )
        : cacheDir;
    final resolvedTag = await _downloadAndExtract(
      '${_baseUrlForTag(tag)}/$archiveName',
      extractTarget,
      isZip: isZip,
    );
    final tagToWrite = resolvedTag ?? tag;
    File(
      '${cacheDir.path}/$_prebuildVersionFileName',
    ).writeAsStringSync('$tagToWrite\n');

    if (!validator(cacheDir)) {
      throw StateError(
        'Downloaded prebuilt Xiph libraries for $os ($arch) from '
        '${_baseUrlForTag(tag)}/$archiveName, but validation failed in '
        '${cacheDir.path}.',
      );
    }
    return cacheDir;
  }

  static Future<String?> _downloadAndExtract(
    String url,
    Directory destination, {
    required bool isZip,
  }) async {
    destination.createSync(recursive: true);
    final tempFile = File(
      '${destination.path}/dl_${DateTime.now().millisecondsSinceEpoch}.${isZip ? 'zip' : 'tar.gz'}',
    );

    String? resolvedTag;
    final client = HttpClient();
    try {
      final uri = Uri.parse(url);
      final request = await client.getUrl(uri);
      final response = await request.close();
      if (response.statusCode != HttpStatus.ok) {
        throw HttpException(
          'Failed to download prebuilt libraries from $url '
          '(HTTP ${response.statusCode}). Ensure you have an internet '
          'connection or build from source using '
          '`<platform>_force_build_libs: true`.',
          uri: uri,
        );
      }
      for (final r in response.redirects) {
        final match = RegExp(
          '/releases/download/([^/]+)/',
        ).firstMatch(r.location.toString());
        if (match != null) {
          resolvedTag = match.group(1);
          break;
        }
      }
      await response.pipe(tempFile.openWrite());
    } finally {
      client.close();
    }

    try {
      if (isZip && Platform.isWindows) {
        final tarRes = await Process.run('tar', [
          '-xf',
          tempFile.path,
          '-C',
          destination.path,
        ]);
        if (tarRes.exitCode != 0) {
          final cmd =
              'Expand-Archive -Path "${tempFile.path}" '
              '-DestinationPath "${destination.path}" -Force';
          final psRes = await Process.run('powershell', [
            '-NoProfile',
            '-Command',
            cmd,
          ]);
          if (psRes.exitCode != 0) {
            throw ProcessException(
              'powershell',
              ['Expand-Archive'],
              psRes.stderr.toString(),
              psRes.exitCode,
            );
          }
        }
      } else {
        final res = await Process.run('tar', [
          '-xzf',
          tempFile.path,
          '-C',
          destination.path,
        ]);
        if (res.exitCode != 0) {
          throw ProcessException(
            'tar',
            ['-xzf', tempFile.path],
            res.stderr.toString(),
            res.exitCode,
          );
        }
      }
    } finally {
      if (tempFile.existsSync()) {
        tempFile.deleteSync();
      }
    }
    return resolvedTag;
  }

  /// Links against system-installed Xiph libraries (apt, brew, pacman, etc.).
  static Future<XiphLink> forSystem(BuildInput input) async {
    final code = input.config.code;
    final os = code.targetOS;
    final includeDirs = <String>[];
    final libDirs = <String>[];

    // Query pkg-config if available
    try {
      final res = await Process.run('pkg-config', [
        '--cflags-only-I',
        '--libs-only-L',
        'ogg',
        'vorbis',
        'vorbisfile',
        'vorbisenc',
        'opus',
        'flac',
      ]);
      if (res.exitCode == 0) {
        final out = (res.stdout as String).trim();
        for (final token in out.split(RegExp(r'\s+'))) {
          if (token.startsWith('-I')) {
            final dir = token.substring(2);
            if (Directory(dir).existsSync() && !includeDirs.contains(dir)) {
              includeDirs.add(dir);
            }
          } else if (token.startsWith('-L')) {
            final dir = token.substring(2);
            if (Directory(dir).existsSync() && !libDirs.contains(dir)) {
              libDirs.add(dir);
            }
          }
        }
      }
    } catch (_) {}

    // Add standard OS fallback paths
    if (os == OS.linux) {
      const fallbackIncludes = [
        '/usr/include',
        '/usr/include/opus',
        '/usr/local/include',
        '/usr/local/include/opus',
      ];
      for (final inc in fallbackIncludes) {
        if (Directory(inc).existsSync() && !includeDirs.contains(inc)) {
          includeDirs.add(inc);
        }
      }
      const fallbackLibs = [
        '/usr/lib',
        '/usr/local/lib',
        '/usr/lib64',
        '/usr/lib/aarch64-linux-gnu',
        '/usr/lib/x86_64-linux-gnu',
        '/usr/lib/arm-linux-gnueabihf',
        '/usr/lib/riscv64-linux-gnu',
      ];
      for (final lib in fallbackLibs) {
        if (Directory(lib).existsSync() && !libDirs.contains(lib)) {
          libDirs.add(lib);
        }
      }
    } else if (os == OS.macOS) {
      const fallbackIncludes = [
        '/opt/homebrew/include',
        '/opt/homebrew/include/opus',
        '/usr/local/include',
        '/usr/local/include/opus',
      ];
      for (final inc in fallbackIncludes) {
        if (Directory(inc).existsSync() && !includeDirs.contains(inc)) {
          includeDirs.add(inc);
        }
      }
      const fallbackLibs = ['/opt/homebrew/lib', '/usr/local/lib'];
      for (final lib in fallbackLibs) {
        if (Directory(lib).existsSync() && !libDirs.contains(lib)) {
          libDirs.add(lib);
        }
      }
    } else if (os == OS.windows) {
      final vcpkgRoot = Platform.environment['VCPKG_ROOT'] ?? r'C:\vcpkg';
      final vcpkgTriplet = code.targetArchitecture == Architecture.arm64
          ? 'arm64-windows'
          : 'x64-windows';
      for (final root in [
        vcpkgRoot,
        r'C:\tools\vcpkg',
        r'C:\Program Files\Xiph',
        r'C:\Xiph',
      ]) {
        final inc = '$root\\installed\\$vcpkgTriplet\\include';
        final lib = '$root\\installed\\$vcpkgTriplet\\lib';
        if (Directory(inc).existsSync()) includeDirs.add(inc);
        if (Directory(lib).existsSync()) libDirs.add(lib);
        final directInc = '$root\\include';
        final directLib = '$root\\lib';
        if (Directory(directInc).existsSync()) includeDirs.add(directInc);
        if (Directory(directLib).existsSync()) libDirs.add(directLib);
      }
    }

    return XiphLink._(
      libraries: _xiphLibs,
      libraryDirectories: libDirs,
      includeDirs: includeDirs,
      bundledAssets: const [],
      dependencies: const [],
    );
  }

  /// Checks whether system-installed Xiph libraries and headers are available.
  static Future<bool> hasSystemLibs(OS os) async {
    if (os != OS.linux && os != OS.macOS && os != OS.windows) return false;

    // 1. Check via pkg-config if available
    try {
      final res = await Process.run('pkg-config', [
        '--exists',
        'ogg',
        'vorbis',
        'vorbisfile',
        'vorbisenc',
        'opus',
        'flac',
      ]);
      if (res.exitCode == 0) return true;
    } catch (_) {}

    // 2. Check filesystem search paths
    final includeDirs = <String>[];
    final libDirs = <String>[];
    if (os == OS.linux) {
      const fallbackIncludes = [
        '/usr/include',
        '/usr/include/opus',
        '/usr/local/include',
        '/usr/local/include/opus',
      ];
      for (final inc in fallbackIncludes) {
        if (Directory(inc).existsSync()) includeDirs.add(inc);
      }
      const fallbackLibs = [
        '/usr/lib',
        '/usr/local/lib',
        '/usr/lib64',
        '/usr/lib/aarch64-linux-gnu',
        '/usr/lib/x86_64-linux-gnu',
        '/usr/lib/arm-linux-gnueabihf',
        '/usr/lib/riscv64-linux-gnu',
      ];
      for (final lib in fallbackLibs) {
        if (Directory(lib).existsSync()) libDirs.add(lib);
      }
    } else if (os == OS.macOS) {
      const fallbackIncludes = [
        '/opt/homebrew/include',
        '/opt/homebrew/include/opus',
        '/usr/local/include',
        '/usr/local/include/opus',
      ];
      for (final inc in fallbackIncludes) {
        if (Directory(inc).existsSync()) includeDirs.add(inc);
      }
      const fallbackLibs = ['/opt/homebrew/lib', '/usr/local/lib'];
      for (final lib in fallbackLibs) {
        if (Directory(lib).existsSync()) libDirs.add(lib);
      }
    } else if (os == OS.windows) {
      final vcpkgRoot = Platform.environment['VCPKG_ROOT'] ?? r'C:\vcpkg';
      for (final root in [
        vcpkgRoot,
        r'C:\tools\vcpkg',
        r'C:\Program Files\Xiph',
        r'C:\Xiph',
      ]) {
        final inc = '$root\\installed\\x64-windows\\include';
        final lib = '$root\\installed\\x64-windows\\lib';
        if (Directory(inc).existsSync()) includeDirs.add(inc);
        if (Directory(lib).existsSync()) libDirs.add(lib);
        final directInc = '$root\\include';
        final directLib = '$root\\lib';
        if (Directory(directInc).existsSync()) includeDirs.add(directInc);
        if (Directory(directLib).existsSync()) libDirs.add(directLib);
      }
    }

    bool hasHeader(String relative) =>
        includeDirs.any((d) => File('$d/$relative').existsSync());
    bool hasAnyHeader(List<String> relatives) => relatives.any(hasHeader);

    final hasHeaders =
        hasHeader('ogg/ogg.h') &&
        hasHeader('vorbis/codec.h') &&
        hasAnyHeader(['opus.h', 'opus/opus.h']) &&
        hasHeader('FLAC/all.h');

    if (!hasHeaders) return false;

    bool hasLibrary(String name) => libDirs.any((d) {
      if (os == OS.windows) {
        return File('$d/$name.lib').existsSync();
      }
      return File('$d/lib$name.so').existsSync() ||
          File('$d/lib$name.a').existsSync() ||
          File('$d/lib$name.dylib').existsSync();
    });

    return [
      'ogg',
      'vorbis',
      'vorbisfile',
      'vorbisenc',
      'opus',
      'FLAC',
    ].every((lib) => hasLibrary(lib) || hasLibrary(lib.toLowerCase()));
  }

  /// Builds Xiph libraries from source by cloning git repositories into
  /// `.dart_tool/flutter_soloud/xiph/sources/` and building with CMake into
  /// `.dart_tool/flutter_soloud/xiph/install/<os>/<arch>/` (or `install/ios/<simulator|device>/<arch>/` on iOS).
  static Future<XiphLink> fromSource(BuildInput input) async {
    final code = input.config.code;
    final os = code.targetOS;
    final arch = code.targetArchitecture;
    final packageRoot = input.packageRoot;
    final isApple = os == OS.macOS || os == OS.iOS;
    final String? appleSysroot;
    if (os == OS.iOS) {
      appleSysroot = code.iOS.targetSdk == IOSSdk.iPhoneSimulator
          ? 'iphonesimulator'
          : 'iphoneos';
    } else {
      appleSysroot = null;
    }

    final cacheDir = Directory.fromUri(
      packageRoot.resolve('.dart_tool/flutter_soloud/xiph'),
    );
    final sourcesDir = Directory('${cacheDir.path}/sources');
    final String targetSubdir;
    final String targetDesc;
    if (os == OS.iOS) {
      final isSim = code.iOS.targetSdk == IOSSdk.iPhoneSimulator;
      final sdkName = isSim ? 'simulator' : 'device';
      targetSubdir = 'ios/$sdkName/${arch.name}';
      targetDesc = 'iOS ($arch $sdkName)';
    } else {
      targetSubdir = '${os.name}/${arch.name}';
      targetDesc = '$os ($arch)';
    }
    final buildDir = Directory('${cacheDir.path}/build/$targetSubdir');
    final installDir = Directory('${cacheDir.path}/install/$targetSubdir');
    final installIncDir = Directory('${installDir.path}/include');
    final installLibDir = Directory('${installDir.path}/lib');
    final installLib64Dir = Directory('${installDir.path}/lib64');

    // 1. Check if complete build already exists in cache
    if (_isCompleteInstall(installDir, os)) {
      print(
        '[flutter_soloud] Using cached from-source Xiph libs for $targetDesc',
      );
      return _linkFromInstall(input, installDir, os, arch);
    }

    // 2. Check build tool availability
    final hasGit = _checkTool('git');
    final hasCmake = _checkTool('cmake');
    if (!hasGit || !hasCmake) {
      throw UnsupportedError(
        'CMake and Git are required to build Xiph libraries from source for '
        '$targetDesc. Please ensure cmake and git are installed and available '
        'on your PATH. Alternatively, install system libraries and set '
        '`hooks.user_defines.flutter_soloud.use_system_xiph_libs: true` '
        'in pubspec.yaml, or disable Xiph with `no_xiph_libs: true`.',
      );
    }

    // 3. Clone / checkout repositories
    await _ensureRepo(
      'ogg',
      'https://github.com/xiph/ogg',
      'db5c7a4',
      sourcesDir,
    );
    await _ensureRepo(
      'vorbis',
      'https://github.com/xiph/vorbis',
      '84c0236',
      sourcesDir,
    );
    await _ensureRepo(
      'opus',
      'https://github.com/xiph/opus',
      'c79a9bd',
      sourcesDir,
    );
    await _ensureRepo(
      'flac',
      'https://github.com/xiph/flac',
      '9547dbc',
      sourcesDir,
    );

    // 4. Build with CMake
    installDir.createSync(recursive: true);
    final archStr = _cmakeArch(arch);
    final osxDeploymentTarget = os == OS.iOS ? '12.0' : '10.13';

    final String? androidNdk;
    final String? androidAbi;
    final int? androidNdkApi;
    if (os == OS.android) {
      androidAbi = _androidAbi(arch);
      if (androidAbi == null) {
        throw UnsupportedError('Unsupported Android architecture: $arch');
      }
      androidNdkApi = code.android.targetNdkApi;
      androidNdk = _findAndroidNdk(input);
      if (androidNdk == null) {
        throw UnsupportedError(
          'Could not find Android NDK for building Xiph libraries from source. '
          'Please ensure ANDROID_NDK_HOME or ANDROID_HOME is set.',
        );
      }
    } else {
      androidNdk = null;
      androidAbi = null;
      androidNdkApi = null;
    }

    const androidLinkerFlags =
        '-DCMAKE_SHARED_LINKER_FLAGS='
        '-Wl,-z,max-page-size=16384,--gc-sections -flto';
    final androidFlags = [
      if (os == OS.android) ...[
        '-DCMAKE_TOOLCHAIN_FILE=$androidNdk/build/cmake/android.toolchain.cmake',
        '-DANDROID_ABI=$androidAbi',
        '-DANDROID_PLATFORM=android-$androidNdkApi',
        '-DCMAKE_C_FLAGS=-Os -flto -ffunction-sections -fdata-sections',
        androidLinkerFlags,
      ],
    ];

    final commonCmakePlatformFlags = [
      if (isApple) ...[
        '-DBUILD_SHARED_LIBS=OFF',
        '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DCMAKE_OSX_ARCHITECTURES=$archStr',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=$osxDeploymentTarget',
        if (os == OS.iOS) ...[
          '-DCMAKE_SYSTEM_NAME=iOS',
          '-DCMAKE_OSX_SYSROOT=$appleSysroot',
        ],
      ] else if (os == OS.windows) ...[
        '-DBUILD_SHARED_LIBS=ON',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
        if (arch == Architecture.arm64) ...['-A', 'ARM64'],
      ] else if (os == OS.android) ...[
        '-DBUILD_SHARED_LIBS=ON',
        ...androidFlags,
      ] else ...[
        '-DBUILD_SHARED_LIBS=ON',
        '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DCMAKE_C_FLAGS=-O2 -flto -ffunction-sections -fdata-sections',
        '-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--gc-sections -flto',
        if (os == OS.linux &&
            arch == Architecture.arm64 &&
            Platform.version.contains('x64')) ...[
          '-DCMAKE_SYSTEM_NAME=Linux',
          '-DCMAKE_SYSTEM_PROCESSOR=aarch64',
          '-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc',
          '-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++',
        ],
      ],
    ];

    void prepareBuildDir(String lib) {
      final cacheFile = File('${buildDir.path}/$lib/CMakeCache.txt');
      if (cacheFile.existsSync()) {
        cacheFile.deleteSync();
      }
    }

    // Build ogg
    print('[flutter_soloud] Configuring ogg with CMake ($archStr)...');
    prepareBuildDir('ogg');
    await _runCmake([
      '-S',
      '${sourcesDir.path}/ogg',
      '-B',
      '${buildDir.path}/ogg',
      '-DCMAKE_INSTALL_PREFIX=${installDir.path}',
      '-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
      '-DINSTALL_DOCS=OFF',
      '-DBUILD_TESTING=OFF',
      ...commonCmakePlatformFlags,
    ]);
    print('[flutter_soloud] Compiling and installing ogg...');
    await _runCmake([
      '--build',
      '${buildDir.path}/ogg',
      '--config',
      'Release',
      '--target',
      'install',
    ]);

    // Build opus
    const opusAppleFlags =
        '-Os -fno-exceptions -fno-unwind-tables '
        '-fno-asynchronous-unwind-tables';
    print('[flutter_soloud] Configuring opus with CMake ($archStr)...');
    prepareBuildDir('opus');
    await _runCmake([
      '-S',
      '${sourcesDir.path}/opus',
      '-B',
      '${buildDir.path}/opus',
      '-DCMAKE_INSTALL_PREFIX=${installDir.path}',
      '-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
      '-DOPUS_BUILD_PROGRAMS=OFF',
      '-DOPUS_BUILD_TESTING=OFF',
      '-DOPUS_STACK_PROTECTOR=OFF',
      '-DOPUS_CUSTOM_MODES=ON',
      if (isApple) ...[
        '-DOPUS_BUILD_SHARED_LIBRARY=OFF',
        '-DCMAKE_C_FLAGS=$opusAppleFlags',
      ],
      ...commonCmakePlatformFlags,
    ]);
    print('[flutter_soloud] Compiling and installing opus...');
    await _runCmake([
      '--build',
      '${buildDir.path}/opus',
      '--config',
      'Release',
      '--target',
      'install',
    ]);

    // Locate built ogg library for vorbis and flac
    final oggLib = isApple
        ? '${installLibDir.path}/libogg.a'
        : (os == OS.windows
              ? '${installLibDir.path}/ogg.lib'
              : (File('${installLib64Dir.path}/libogg.so').existsSync()
                    ? '${installLib64Dir.path}/libogg.so'
                    : '${installLibDir.path}/libogg.so'));

    // Build vorbis
    print('[flutter_soloud] Configuring vorbis with CMake ($archStr)...');
    prepareBuildDir('vorbis');
    await _runCmake([
      '-S',
      '${sourcesDir.path}/vorbis',
      '-B',
      '${buildDir.path}/vorbis',
      '-DCMAKE_INSTALL_PREFIX=${installDir.path}',
      '-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
      '-DOGG_INCLUDE_DIR=${installIncDir.path}',
      '-DOGG_LIBRARY=$oggLib',
      '-DBUILD_TESTING=OFF',
      ...commonCmakePlatformFlags,
    ]);
    print('[flutter_soloud] Compiling and installing vorbis...');
    await _runCmake([
      '--build',
      '${buildDir.path}/vorbis',
      '--config',
      'Release',
      '--target',
      'install',
    ]);

    // Build flac
    print('[flutter_soloud] Configuring flac with CMake ($archStr)...');
    prepareBuildDir('flac');
    await _runCmake([
      '-S',
      '${sourcesDir.path}/flac',
      '-B',
      '${buildDir.path}/flac',
      '-DCMAKE_INSTALL_PREFIX=${installDir.path}',
      '-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
      '-DOGG_INCLUDE_DIR=${installIncDir.path}',
      '-DOGG_LIBRARY=$oggLib',
      '-DBUILD_CXXLIBS=OFF',
      '-DBUILD_PROGRAMS=OFF',
      '-DBUILD_EXAMPLES=OFF',
      '-DBUILD_TESTING=OFF',
      '-DBUILD_DOCS=OFF',
      '-DINSTALL_MANPAGES=OFF',
      if (isApple) '-DWITH_OGG=ON',
      if (os == OS.iOS) ...['-DIconv_FOUND=OFF', '-DIntl_FOUND=OFF'],
      ...commonCmakePlatformFlags,
    ]);
    print('[flutter_soloud] Compiling and installing flac...');
    await _runCmake([
      '--build',
      '${buildDir.path}/flac',
      '--config',
      'Release',
      '--target',
      'install',
    ]);

    // Copy FLAC share headers if present
    final shareSrc = Directory('${sourcesDir.path}/flac/include/share');
    if (shareSrc.existsSync()) {
      _copyDirSync(shareSrc, Directory('${installIncDir.path}/share'));
    }
    // Remove FLAC++ C++ wrapper headers and binaries
    final flacppDir = Directory('${installIncDir.path}/FLAC++');
    if (flacppDir.existsSync()) {
      flacppDir.deleteSync(recursive: true);
    }
    for (final dir in [installLibDir, installLib64Dir]) {
      if (!dir.existsSync()) continue;
      for (final entity in dir.listSync()) {
        if (entity.path.contains('libFLAC++')) {
          try {
            entity.deleteSync();
          } catch (_) {}
        }
      }
    }

    // Strip symbols from built libraries to match prebuilt releases
    if (os == OS.android && androidNdk != null) {
      final llvmStrip = _findNdkStrip(androidNdk);
      if (llvmStrip != null) {
        for (final dir in [installLibDir, installLib64Dir]) {
          if (!dir.existsSync()) continue;
          for (final file in dir.listSync()) {
            if (file is File && file.path.endsWith('.so')) {
              await Process.run(llvmStrip, [file.path]);
            }
          }
        }
      }
    } else if (os == OS.linux) {
      final isCrossArm64 =
          arch == Architecture.arm64 && Platform.version.contains('x64');
      final stripTool = isCrossArm64 ? 'aarch64-linux-gnu-strip' : 'strip';
      for (final dir in [installLibDir, installLib64Dir]) {
        if (!dir.existsSync()) continue;
        for (final file in dir.listSync()) {
          if (file is File && file.path.contains('.so')) {
            await Process.run(stripTool, ['--strip-unneeded', file.path]);
          }
        }
      }
    } else if (isApple) {
      for (final dir in [installLibDir, installLib64Dir]) {
        if (!dir.existsSync()) continue;
        for (final file in dir.listSync()) {
          if (file is File && file.path.endsWith('.a')) {
            await Process.run('strip', ['-x', file.path]);
          }
        }
      }
    }

    return _linkFromInstall(input, installDir, os, arch);
  }

  /// Library names passed to the linker (`-l<name>`).
  final List<String> libraries;

  /// Directories searched for [libraries]. Absolute paths.
  final List<String> libraryDirectories;

  /// Xiph header directories, relative to the package root or absolute paths.
  final List<String> includeDirs;

  /// Extra code assets to bundle (prebuilt shared libraries).
  final List<CodeAsset> bundledAssets;

  /// Prebuilt files consumed by the build, for cache invalidation.
  final List<Uri> dependencies;
}

// ---------------------------------------------------------------------------
// Build from source helpers
// ---------------------------------------------------------------------------

String _cmakeArch(Architecture arch) => switch (arch) {
  Architecture.arm64 => 'arm64',
  Architecture.x64 => 'x86_64',
  Architecture.arm => 'armv7-a',
  Architecture.ia32 => 'i686',
  Architecture.riscv64 => 'riscv64',
  _ => arch.name,
};

String? _findAndroidNdk(BuildInput input) {
  final envNdk =
      Platform.environment['ANDROID_NDK_HOME'] ??
      Platform.environment['ANDROID_NDK_ROOT'];
  if (envNdk != null &&
      Directory(envNdk).existsSync() &&
      File('$envNdk/build/cmake/android.toolchain.cmake').existsSync()) {
    return envNdk;
  }
  try {
    final cc = input.config.code.cCompiler?.compiler.toFilePath();
    if (cc != null && cc.contains('toolchains')) {
      final ndkPath = cc
          .substring(0, cc.indexOf('toolchains'))
          .replaceAll(RegExp(r'[/\\]$'), '');
      if (Directory(ndkPath).existsSync() &&
          File('$ndkPath/build/cmake/android.toolchain.cmake').existsSync()) {
        return ndkPath;
      }
    }
  } catch (_) {}

  final sdkRoot =
      Platform.environment['ANDROID_HOME'] ??
      Platform.environment['ANDROID_SDK_ROOT'];
  if (sdkRoot != null) {
    final ndkDir = Directory('$sdkRoot/ndk');
    if (ndkDir.existsSync()) {
      final versions = ndkDir.listSync().whereType<Directory>().toList();
      if (versions.isNotEmpty) {
        versions.sort((a, b) => a.path.compareTo(b.path));
        final candidate = versions.last.path;
        if (File(
          '$candidate/build/cmake/android.toolchain.cmake',
        ).existsSync()) {
          return candidate;
        }
      }
    }
  }
  return null;
}

String? _findNdkStrip(String ndkPath) {
  final prebuiltDir = Directory('$ndkPath/toolchains/llvm/prebuilt');
  if (!prebuiltDir.existsSync()) return null;
  final exe = Platform.isWindows ? 'llvm-strip.exe' : 'llvm-strip';
  for (final host in prebuiltDir.listSync()) {
    if (host is Directory) {
      final stripBin = File('${host.path}/bin/$exe');
      if (stripBin.existsSync()) {
        return stripBin.path;
      }
    }
  }
  return null;
}

bool _checkTool(String tool) {
  try {
    final res = Process.runSync(tool, ['--version']);
    return res.exitCode == 0;
  } catch (_) {
    return false;
  }
}

bool _isCompleteInstall(Directory installDir, OS os) {
  if (!installDir.existsSync()) return false;
  final inc = Directory('${installDir.path}/include');
  if (!File('${inc.path}/ogg/ogg.h').existsSync() ||
      !File('${inc.path}/vorbis/codec.h').existsSync() ||
      !File('${inc.path}/opus/opus.h').existsSync() ||
      !File('${inc.path}/FLAC/all.h').existsSync()) {
    return false;
  }
  final libDir = Directory('${installDir.path}/lib');
  final lib64Dir = Directory('${installDir.path}/lib64');
  bool hasLib(String name) =>
      File('${libDir.path}/$name').existsSync() ||
      File('${lib64Dir.path}/$name').existsSync();

  if (os == OS.macOS || os == OS.iOS) {
    return [
      'libogg.a',
      'libvorbis.a',
      'libvorbisfile.a',
      'libvorbisenc.a',
      'libopus.a',
      'libFLAC.a',
    ].every(hasLib);
  } else if (os == OS.windows) {
    return [
      'ogg.lib',
      'vorbis.lib',
      'vorbisfile.lib',
      'vorbisenc.lib',
      'opus.lib',
      'FLAC.lib',
    ].every(hasLib);
  } else {
    return [
      'libogg.so',
      'libvorbis.so',
      'libvorbisfile.so',
      'libvorbisenc.so',
      'libopus.so',
      'libFLAC.so',
    ].every(hasLib);
  }
}

Future<void> _ensureRepo(
  String name,
  String url,
  String commit,
  Directory parent,
) async {
  final repoDir = Directory('${parent.path}/$name');
  if (!repoDir.existsSync()) {
    parent.createSync(recursive: true);
    print('[flutter_soloud] Cloning $name ($commit)...');
    final cloneRes = await Process.run('git', ['clone', url, repoDir.path]);
    if (cloneRes.exitCode != 0) {
      throw ProcessException(
        'git',
        ['clone', url, repoDir.path],
        cloneRes.stderr.toString(),
        cloneRes.exitCode,
      );
    }
    await Process.run('git', [
      'checkout',
      commit,
    ], workingDirectory: repoDir.path);
  }
}

Future<void> _runCmake(List<String> args, {String? workingDir}) async {
  final res = await Process.run('cmake', args, workingDirectory: workingDir);
  if (res.exitCode != 0) {
    throw ProcessException(
      'cmake',
      args,
      '${res.stdout}\n${res.stderr}',
      res.exitCode,
    );
  }
}

void _copyDirSync(Directory src, Directory dst) {
  if (!src.existsSync()) return;
  dst.createSync(recursive: true);
  for (final entity in src.listSync(recursive: true)) {
    final relPath = entity.path.substring(src.path.length + 1);
    final targetPath = '${dst.path}/$relPath';
    if (entity is Directory) {
      Directory(targetPath).createSync(recursive: true);
    } else if (entity is File) {
      File(targetPath).parent.createSync(recursive: true);
      entity.copySync(targetPath);
    }
  }
}

/// Resolves the actual runtime SONAME file for [lib] in [dir] to avoid
/// bundling duplicate unversioned or full-versioned files (e.g., bundles only
/// `libFLAC.so.14` rather than `libFLAC.so`, `libFLAC.so.14`, and
/// `libFLAC.so.14.0.0`).
Future<File> _resolveLinuxSoFile(Directory dir, String lib) async {
  final unversioned = File('${dir.path}/lib$lib.so');
  if (!unversioned.existsSync()) return unversioned;
  try {
    final res = await Process.run('readelf', ['-d', unversioned.path]);
    if (res.exitCode == 0) {
      final out = res.stdout as String;
      final match = RegExp(
        r'\(SONAME\)\s+Library soname:\s+\[([^\]]+)\]',
      ).firstMatch(out);
      if (match != null) {
        final sonameFile = File('${dir.path}/${match.group(1)}');
        if (sonameFile.existsSync()) {
          return sonameFile;
        }
      }
    }
  } catch (_) {}
  return unversioned;
}

Future<XiphLink> _linkFromInstall(
  BuildInput input,
  Directory installDir,
  OS os,
  Architecture arch,
) async {
  final packageName = input.packageName;
  final libDir = Directory('${installDir.path}/lib');
  final lib64Dir = Directory('${installDir.path}/lib64');
  final binDir = Directory('${installDir.path}/bin');
  final libraryDirs = [
    if (libDir.existsSync()) libDir.path,
    if (lib64Dir.existsSync()) lib64Dir.path,
    if (binDir.existsSync()) binDir.path,
  ];
  final includeDirs = ['${installDir.path}/include'];
  final bundledAssets = <CodeAsset>[];
  final dependencies = <Uri>[];

  if (os == OS.linux) {
    for (final lib in _xiphLibs) {
      final dir = File('${libDir.path}/lib$lib.so').existsSync()
          ? libDir
          : lib64Dir;
      final file = await _resolveLinuxSoFile(dir, lib);
      if (file.existsSync()) {
        dependencies.add(file.uri);
        bundledAssets.add(
          CodeAsset(
            package: packageName,
            name: 'xiph/${file.uri.pathSegments.last}',
            linkMode: DynamicLoadingBundled(),
            file: file.uri,
          ),
        );
      }
    }
  } else if (os == OS.android) {
    final abi = _androidAbi(arch);
    if (abi == null) {
      throw UnsupportedError('Unsupported Android architecture: $arch');
    }
    for (final dir in [libDir, lib64Dir]) {
      if (!dir.existsSync()) continue;
      for (final file in dir.listSync()) {
        if (file is File && file.path.endsWith('.so')) {
          final fileName = file.path.split(Platform.pathSeparator).last;
          if (_xiphLibs.any((lib) => fileName == 'lib$lib.so')) {
            dependencies.add(file.uri);
            bundledAssets.add(
              CodeAsset(
                package: packageName,
                name: 'xiph/$abi/$fileName',
                linkMode: DynamicLoadingBundled(),
                file: file.uri,
              ),
            );
          }
        }
      }
    }
  } else if (os == OS.windows) {
    for (final dir in [binDir, libDir]) {
      if (!dir.existsSync()) continue;
      for (final file in dir.listSync()) {
        if (file is File) {
          if (file.path.endsWith('.lib')) {
            dependencies.add(file.uri);
          } else if (file.path.endsWith('.dll')) {
            dependencies.add(file.uri);
            bundledAssets.add(
              CodeAsset(
                package: packageName,
                name: 'xiph/${file.path.split(Platform.pathSeparator).last}',
                linkMode: DynamicLoadingBundled(),
                file: file.uri,
              ),
            );
          }
        }
      }
    }
  } else {
    // macOS / iOS: static libraries
    if (libDir.existsSync()) {
      for (final file in libDir.listSync()) {
        if (file is File && file.path.endsWith('.a')) {
          dependencies.add(file.uri);
        }
      }
    }
  }

  return XiphLink._(
    libraries: _xiphLibs,
    libraryDirectories: libraryDirs,
    includeDirs: includeDirs,
    bundledAssets: bundledAssets,
    dependencies: dependencies,
  );
}

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

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

import 'sources.dart';
import 'xiph.dart';

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

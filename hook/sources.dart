// Source collection for the flutter_soloud build hook.
//
// Mirrors the source list that used to live in `src/CMakeLists.txt` +
// `src/src.cmake`: the plugin sources, the vendored SoLoud engine (miniaudio
// and null backends everywhere, ALSA on Linux, CoreAudio on Apple), and pffft.

import 'dart:io';

import 'package:code_assets/code_assets.dart';

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
  addDir(
    '',
    exclude: (p) => p.endsWith('/flutter_soloud.cpp'),
  );
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

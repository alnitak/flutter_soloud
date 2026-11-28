// The native test builds take the plugin's sources from one list,
// test/support/plugin_sources.txt. Every source that the build hook
// (hook/build.dart) compiles for a platform must be in it, or a new source
// file or directory leaves the native tests failing to link, or testing less
// than the plugin ships.
//
// Not compared: pffft.c and the audio backends, which each test build picks
// itself, and on Apple the Objective-C++ files, which the hook builds apart.
@TestOn('vm')
library;

import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:test/test.dart';

import '../../hook/build.dart' as hook;

const _listPath = 'test/support/plugin_sources.txt';

/// The sources [_listPath] gives [platform], with the globs expanded.
Set<String> _listedSources(String platform) {
  final sources = <String>{};
  for (var line in File(_listPath).readAsLinesSync()) {
    line = line.split('#').first.trim();
    if (line.isEmpty) continue;
    final tag = RegExp(r'^(\w+):\s*(.+)$').firstMatch(line);
    if (tag != null) {
      if (tag.group(1) != platform) continue;
      line = tag.group(2)!;
    }
    final slash = line.lastIndexOf('/');
    final dir = line.substring(0, slash);
    final name = line.substring(slash + 1);
    if (!name.contains('*')) {
      expect(File(line).existsSync(), isTrue, reason: '$_listPath: $line');
      sources.add(line);
      continue;
    }
    final pattern = RegExp(
      '^${name.split('*').map(RegExp.escape).join('.*')}\$',
    );
    final matched = Directory(dir)
        .listSync()
        .whereType<File>()
        .map((f) => f.uri.pathSegments.last)
        .where(pattern.hasMatch);
    expect(matched, isNotEmpty, reason: '$_listPath: $line matches nothing');
    sources.addAll(matched.map((f) => '$dir/$f'));
  }
  return sources;
}

void main() {
  const platforms = {
    'apple': [OS.macOS, OS.iOS],
    'android': [OS.android],
    'linux': [OS.linux],
    'windows': [OS.windows],
  };

  for (final MapEntry(key: platform, value: oses) in platforms.entries) {
    for (final os in oses) {
      test('$_listPath has every source the hook builds for $os', () {
        final listed = _listedSources(platform);
        final missing = hook
            .collectSources(Directory.current.uri, os)
            .where((s) => !s.startsWith('src/pffft/'))
            .where((s) => !s.startsWith('src/soloud/src/backend/'))
            .where((s) => !listed.contains(s))
            .toList();
        expect(missing, isEmpty, reason: 'add them to $_listPath');
      });
    }
  }
}

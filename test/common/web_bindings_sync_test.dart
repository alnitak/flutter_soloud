// The web bindings (`lib/src/bindings/js_extension.dart`) call the WASM
// module through `@JS('Module_soloud._name')` externs. This checks that they
// are in sync with what `web/compile_wasm.sh` exports, as
// ffigen_bindings_sync_test.dart does for the FFI bindings:
//
// - the export list: the names in EXPORTED_FUNCTIONS plus every
//   FFI_PLUGIN_EXPORT function of the sources the script compiles, with the
//   preprocessor evaluated for the web (`__EMSCRIPTEN__` defined, and
//   MA_ENABLE_AUDIO_WORKLETS for the multi-threaded flavor);
// - every function bound in js_extension.dart (or probed with
//   `wasmHasExport`) is in the export list, and every export is bound, or is
//   listed below with the reason it is not called from Dart on the web;
// - the committed builds in web/ export exactly that list (a stale build
//   fails here).
@TestOn('vm')
library;

import 'dart:io';

import 'package:path/path.dart' as p;
import 'package:test/test.dart';

const _jsExtensionPath = 'lib/src/bindings/js_extension.dart';
const _bindingsDir = 'lib/src/bindings';
const _compileScriptPath = 'web/compile_wasm.sh';

/// The committed builds: flavor -> glue script.
const _builds = {
  'st': 'web/libflutter_soloud_plugin.js',
  'mt': 'web/libflutter_soloud_plugin_mt.js',
};

/// Macros defined when compile_wasm.sh builds each flavor (the COMPILER_DEFINES
/// of the script and the flags of `build_flavor`, plus the compiler's own).
const _definedMacros = {
  'st': {
    '__EMSCRIPTEN__',
    '__cplusplus',
    'WITH_MINIAUDIO',
    'SIGNALSMITH_USE_PFFFT',
    'SOLOUD_NO_ASSERTS',
  },
  'mt': {
    '__EMSCRIPTEN__',
    '__cplusplus',
    'WITH_MINIAUDIO',
    'SIGNALSMITH_USE_PFFFT',
    'SOLOUD_NO_ASSERTS',
    'MA_ENABLE_AUDIO_WORKLETS',
  },
};

/// Exports the web bindings deliberately do not call, with the reason. A new
/// export must be bound in js_extension.dart or added here.
const _notBoundOnWeb = {
  // Native-only lifecycle and callback plumbing (FFI NativeCallables, engine
  // ownership across Flutter engines, hot restart).
  '_clearDartCallbackRegistrations': 'FFI callback plumbing',
  '_clearDartCallbackRegistrationsForEngine': 'FFI callback plumbing',
  '_currentEngineShutdownEpoch': 'native engine ownership',
  '_prepareEngineInitForRequest': 'native engine ownership',
  '_requestEngineTeardownForEngine': 'native engine ownership',
  '_retireDartCallbacksFinalizer': 'FFI finalizer',
  '_setDartEventCallback': 'FFI callback plumbing',
  '_setMixerOutputCallbackForEngine': 'FFI callback plumbing',
  '_setVisualizationCallback': 'FFI callback plumbing',
  '_setVisualizationCallbackForEngine': 'FFI callback plumbing',
  '_visualizationPacketDone': 'FFI callback plumbing',
  '_voiceEndedCallback': 'FFI callback plumbing',
  '_voiceInactiveCallback': 'FFI callback plumbing',
  '_nativeFree': 'the web frees with _free',
  '_debugTriggerAudioInterruption': 'native test hook',
  // Platform-specific settings that are no-ops on the web.
  '_setAndroidAAudioAttributes': 'Android only',
  '_setLinuxAudioBackend': 'Linux only',
  '_setAudioDeviceIdleTimeout': 'the device always runs on the web',
  // Implemented in Dart on the web, or not available there.
  '_getOutputLatency': 'render-ahead is native only (web returns zero)',
  '_getPlayheadTime': 'render-ahead is native only (web uses engine time)',
  '_isRenderAheadEnabled': 'render-ahead is native only (web: false)',
  '_readSamplesFromFile': 'no file system on the web',
  '_play3d': 'the web calls play3dWithLoopPoints',
  '_getFilterParamNamesBounded': 'the web calls getFilterParamNames',
  '_listPlaybackDevicesBounded': 'the web calls listPlaybackDevices',
  // VST3 hosting is desktop only.
  '_soloud_vst3CloseEditor': 'VST3 is desktop only',
  '_soloud_vst3FreeScanResults': 'VST3 is desktop only',
  '_soloud_vst3GetFilterCallbackPtr': 'VST3 is desktop only',
  '_soloud_vst3GetLatencySamples': 'VST3 is desktop only',
  '_soloud_vst3GetParameterCount': 'VST3 is desktop only',
  '_soloud_vst3GetParameterInfo': 'VST3 is desktop only',
  '_soloud_vst3GetParameterNormalized': 'VST3 is desktop only',
  '_soloud_vst3GetPluginUserData': 'VST3 is desktop only',
  '_soloud_vst3GetSourceCallbackPtr': 'VST3 is desktop only',
  '_soloud_vst3GetState': 'VST3 is desktop only',
  '_soloud_vst3HasEditor': 'VST3 is desktop only',
  '_soloud_vst3HostUnloadAll': 'VST3 is desktop only',
  '_soloud_vst3IsEditorOpen': 'VST3 is desktop only',
  '_soloud_vst3IsSupported': 'VST3 is desktop only',
  '_soloud_vst3LoadPlugin': 'VST3 is desktop only',
  '_soloud_vst3MapSlotParameter': 'VST3 is desktop only',
  '_soloud_vst3OpenEditor': 'VST3 is desktop only',
  '_soloud_vst3ScanBundle': 'VST3 is desktop only',
  '_soloud_vst3SendMidiNote': 'VST3 is desktop only',
  '_soloud_vst3SetParameterEditCallback': 'VST3 is desktop only',
  '_soloud_vst3SetParameterNormalized': 'VST3 is desktop only',
  '_soloud_vst3SetRestartComponentCallback': 'VST3 is desktop only',
  '_soloud_vst3SetState': 'VST3 is desktop only',
  '_soloud_vst3SetTransport': 'VST3 is desktop only',
  '_soloud_vst3UnloadPlugin': 'VST3 is desktop only',
  // Runtime exports requested by the script for the glue code.
  '_memcpy': 'EXPORTED_FUNCTIONS, used by the glue',
  '_memset': 'EXPORTED_FUNCTIONS, used by the glue',
};

/// Exports that come from the vendored libraries or Emscripten itself, not
/// from the plugin's own sources.
bool _isRuntimeExport(String name) =>
    name.startsWith('_ma_') ||
    name.startsWith('_emscripten') ||
    name.startsWith('__') ||
    name.startsWith('_pthread') ||
    name.startsWith('_wasm_worker');

/// The text of [file] with `\n` line endings: git checks the sources out
/// with `\r\n` on Windows (core.autocrlf), and the line-based parsers below
/// would not recognize a directive followed by `\r`.
String _readText(File file) => file.readAsStringSync().replaceAll('\r\n', '\n');

// ---------------------------------------------------------------------------
// js_extension.dart

/// `Module_soloud._name` -> `_name`, for every @JS extern.
Set<String> _boundFunctions(String dart) => {
  for (final m in RegExp(
    r"@JS\(\s*'Module_soloud\.(_\w+)'\s*\)",
  ).allMatches(dart))
    m[1]!,
};

/// Names passed to `wasmHasExport` anywhere in the bindings: exports the
/// Dart side probes before calling them.
Set<String> _probedFunctions() => {
  for (final f in Directory(_bindingsDir).listSync().whereType<File>())
    if (f.path.endsWith('.dart'))
      for (final m in RegExp(
        r"wasmHasExport\(\s*'(_\w+)'\s*\)",
      ).allMatches(_readText(f)))
        m[1]!,
};

// ---------------------------------------------------------------------------
// compile_wasm.sh

/// The names in `-s "EXPORTED_FUNCTIONS=[...]"`.
Set<String> _scriptExportedFunctions(String script) {
  final m = RegExp(r'EXPORTED_FUNCTIONS=\[([^\]]*)\]').firstMatch(script);
  if (m == null) return {};
  return {for (final n in RegExp(r"'(_\w+)'").allMatches(m[1]!)) n[1]!};
}

/// The files of the `SOURCES=( ... )` array, globs expanded (relative to
/// web/, where the script runs), without the vendored SoLoud sources.
List<File> _scriptSources(String script) {
  final m = RegExp(r'SOURCES=\(([^)]*)\)').firstMatch(script);
  if (m == null) return [];
  final files = <File>[];
  for (final raw in m[1]!.split(RegExp(r'\s+'))) {
    if (raw.isEmpty || raw.startsWith('#')) continue;
    final path = p.normalize(p.join('web', raw));
    if (p.split(path).take(2).join('/') == 'src/soloud') continue;
    final dir = Directory(p.dirname(path));
    final pattern = RegExp(
      '^${RegExp.escape(p.basename(path)).replaceAll(r'\*', '.*')}\$',
    );
    if (!dir.existsSync()) continue;
    files.addAll(
      dir.listSync().whereType<File>().where(
        (f) => pattern.hasMatch(p.basename(f.path)),
      ),
    );
  }
  return files;
}

// ---------------------------------------------------------------------------
// A minimal C preprocessor: only conditionals, evaluated with [defined].

/// Evaluates a `#if` expression: `defined(X)`, `!`, `&&`, `||`, comparisons,
/// parentheses and integers. Macros not in [defined] are 0, defined ones 1.
bool _evalCondition(String expr, Set<String> defined) {
  var e = expr.replaceAllMapped(
    RegExp(r'defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)'),
    (m) => defined.contains(m[1] ?? m[2]) ? '1' : '0',
  );
  e = e.replaceAllMapped(
    RegExp(r'\b[A-Za-z_]\w*\b'),
    (m) => defined.contains(m[0]) ? '1' : '0',
  );
  final tokens = RegExp(
    r'\d+|&&|\|\||==|!=|<=|>=|[!()<>]',
  ).allMatches(e).map((m) => m[0]!).toList();
  var i = 0;
  late int Function() orExpr;
  int primary() {
    final t = tokens[i++];
    if (t == '!') return primary() == 0 ? 1 : 0;
    if (t == '(') {
      final v = orExpr();
      i++; // ')'
      return v;
    }
    return int.parse(t);
  }

  int compare() {
    var v = primary();
    while (i < tokens.length &&
        ['==', '!=', '<', '>', '<=', '>='].contains(tokens[i])) {
      final op = tokens[i++];
      final r = primary();
      v =
          switch (op) {
            '==' => v == r,
            '!=' => v != r,
            '<' => v < r,
            '>' => v > r,
            '<=' => v <= r,
            _ => v >= r,
          }
          ? 1
          : 0;
    }
    return v;
  }

  int andExpr() {
    var v = compare();
    while (i < tokens.length && tokens[i] == '&&') {
      i++;
      final r = compare();
      v = (v != 0 && r != 0) ? 1 : 0;
    }
    return v;
  }

  orExpr = () {
    var v = andExpr();
    while (i < tokens.length && tokens[i] == '||') {
      i++;
      final r = andExpr();
      v = (v != 0 || r != 0) ? 1 : 0;
    }
    return v;
  };
  return tokens.isNotEmpty && orExpr() != 0;
}

/// The lines of [code] that the preprocessor keeps with [defined].
String _preprocess(String code, Set<String> defined) {
  final out = StringBuffer();
  // Per nesting level: is this branch active, and was any branch taken.
  final stack = <({bool active, bool taken})>[];
  bool active() => stack.every((s) => s.active);
  final lines = code.replaceAll('\\\n', ' ').split('\n');
  for (final line in lines) {
    final m = RegExp(r'^\s*#\s*(\w+)\s*(.*)$').firstMatch(line);
    if (m == null) {
      if (active()) out.writeln(line);
      continue;
    }
    final rest = m[2]!.replaceAll(RegExp(r'//.*|/\*.*?\*/'), '').trim();
    switch (m[1]) {
      case 'ifdef':
        final v = defined.contains(rest.split(RegExp(r'\s')).first);
        stack.add((active: v, taken: v));
      case 'ifndef':
        final v = !defined.contains(rest.split(RegExp(r'\s')).first);
        stack.add((active: v, taken: v));
      case 'if':
        final v = _evalCondition(rest, defined);
        stack.add((active: v, taken: v));
      case 'elif':
        final top = stack.removeLast();
        final v = !top.taken && _evalCondition(rest, defined);
        stack.add((active: v, taken: top.taken || v));
      case 'else':
        final top = stack.removeLast();
        stack.add((active: !top.taken, taken: true));
      case 'endif':
        stack.removeLast();
      default:
        if (active()) out.writeln(line);
    }
  }
  return out.toString();
}

/// `FFI_PLUGIN_EXPORT <ret> name(...)` -> `_name`, in preprocessed [code].
Set<String> _ffiExports(String code) {
  final noComments = code
      .replaceAll(RegExp(r'/\*.*?\*/', dotAll: true), '')
      .replaceAll(RegExp('//.*'), '')
      .split('\n')
      .where((l) => !l.trimLeft().startsWith('#'))
      .join('\n');
  return {
    for (final m in RegExp(
      r'FFI_PLUGIN_EXPORT\b[^;{(]*?\b(\w+)\s*\(',
    ).allMatches(noComments))
      '_${m[1]}',
  };
}

// ---------------------------------------------------------------------------
// The committed builds.

/// `Module["_name"]` assignments of an Emscripten glue script.
Set<String> _gluedExports(String js) => {
  for (final m in RegExp(r'Module\["(_\w+)"\]').allMatches(js)) m[1]!,
};

void main() {
  final script = _readText(File(_compileScriptPath));
  final bound = _boundFunctions(_readText(File(_jsExtensionPath)));
  final probed = _probedFunctions();
  final scriptExported = _scriptExportedFunctions(script);
  final sources = _scriptSources(script);
  final headers = Directory(
    'src',
  ).listSync().whereType<File>().where((f) => f.path.endsWith('.h')).toList();

  /// flavor -> the plugin's exports in that flavor.
  final exportList = {
    for (final MapEntry(key: flavor, value: defined) in _definedMacros.entries)
      flavor: {
        ...scriptExported,
        for (final f in [...sources, ...headers])
          ..._ffiExports(_preprocess(_readText(f), defined)),
      },
  };
  final allExports = exportList.values.expand((s) => s).toSet();

  test('the files parse', () {
    expect(bound.length, greaterThan(100));
    expect(scriptExported, containsAll(['_malloc', '_free']));
    expect(
      sources.map((f) => p.basename(f.path)),
      containsAll(['bindings.cpp', 'player.cpp']),
    );
    for (final exports in exportList.values) {
      expect(exports.length, greaterThan(100));
    }
  });

  test('the preprocessor drops native-only and test-only exports', () {
    // Sanity check of _preprocess on exports guarded in bindings.cpp.
    final code = _readText(File('src/bindings.cpp'));
    final unguarded = _ffiExports(code);
    for (final exports in exportList.values) {
      expect(exports.length, lessThan(unguarded.length));
    }
  });

  test('every function bound in js_extension.dart is exported', () {
    final missing = bound.difference(allExports).toList()..sort();
    expect(
      missing,
      isEmpty,
      reason:
          'Bound in $_jsExtensionPath but not exported by $_compileScriptPath '
          '(not an FFI_PLUGIN_EXPORT of its SOURCES for __EMSCRIPTEN__, and '
          'not in EXPORTED_FUNCTIONS).',
    );
  });

  test('functions bound unconditionally are exported by both flavors', () {
    for (final MapEntry(key: flavor, value: exports) in exportList.entries) {
      final missing = bound.difference(exports).difference(probed).toList()
        ..sort();
      expect(
        missing,
        isEmpty,
        reason:
            'Not exported by the $flavor flavor: guard the call with '
            'wasmHasExport() or export it from both.',
      );
    }
  });

  test('every probed function is a known export', () {
    expect(probed.difference(allExports), isEmpty);
  });

  test('every export is bound in js_extension.dart', () {
    final unbound =
        allExports
            .difference(bound)
            .difference(probed)
            .difference(_notBoundOnWeb.keys.toSet())
            .toList()
          ..sort();
    expect(
      unbound,
      isEmpty,
      reason:
          'Exported for the web but not bound in $_jsExtensionPath. Bind it, '
          'or add it to _notBoundOnWeb with the reason.',
    );
  });

  test('_notBoundOnWeb lists only exports that exist and are unbound', () {
    final names = _notBoundOnWeb.keys.toSet();
    expect(names.difference(allExports), isEmpty, reason: 'no such export');
    expect(names.intersection(bound), isEmpty, reason: 'now bound');
  });

  group('the committed builds match the export list', () {
    for (final MapEntry(key: flavor, value: path) in _builds.entries) {
      test(flavor, () {
        final file = File(path);
        if (!file.existsSync()) {
          markTestSkipped('$path is not built');
          return;
        }
        final built = _gluedExports(
          _readText(file),
        ).where((n) => !_isRuntimeExport(n)).toSet();
        final expected = exportList[flavor]!;
        expect(
          (expected.difference(built).toList()..sort()),
          isEmpty,
          reason: 'In the export list but missing from $path: rebuild it.',
        );
        expect(
          (built.difference(expected).toList()..sort()),
          isEmpty,
          reason: 'Exported by $path but not by the sources: rebuild it.',
        );
      });
    }
  });
}

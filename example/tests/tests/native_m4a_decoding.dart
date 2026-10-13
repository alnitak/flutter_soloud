import 'dart:io';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// AAC-LC M4A fixtures: 2 s each (0.5 s silence, 1 s 1 kHz tone, 0.5 s
/// silence). Sample-level content is verified natively by
/// test/android_native_decoder_test.cpp.
const _fixtures = [
  'assets/audio/aac_lc_mono_44100.m4a',
  'assets/audio/aac_lc_stereo_48000.m4a',
];

void _expect(bool condition, String message) {
  if (!condition) throw StateError(message);
}

/// Open descriptors of this process (Android/Linux only, else -1).
int _openFdCount() {
  try {
    return Directory('/proc/self/fd').listSync().length;
  } on Object {
    return -1;
  }
}

/// Open descriptors grouped by target (e.g. `/dev/ashmem`, `socket`).
Map<String, int> _openFdTargets() {
  final counts = <String, int>{};
  try {
    for (final link in Directory('/proc/self/fd').listSync()) {
      try {
        var target = Link(link.path).targetSync();
        target = target.replaceAll(RegExp(r'[0-9a-f-]{36}|\[\d+\]|\d+$'), '');
        counts[target] = (counts[target] ?? 0) + 1;
      } on Object {
        // Closed while listing.
      }
    }
  } on Object {
    // Not available on this platform.
  }
  return counts;
}

/// Native M4A/AAC decoding through `loadFile`/`loadMem`: decoded duration,
/// playback to the end, malformed input, repeated cycles without leaking
/// native resources, concurrent loads, and engine teardown/reinit.
Future<OutputBuffer> testNativeM4aDecoding() async {
  final strBuf = OutputBuffer();
  if (kIsWeb) {
    strBuf.writeln('Skipped: loadFile is not available on Web');
    return strBuf;
  }
  await initialize();

  final dir = Directory.systemTemp.createTempSync('soloud_m4a_test');
  try {
    // Copy fixtures to real files so loadFile goes through the file path.
    final files = <String>[];
    final bytesByFile = <String, Uint8List>{};
    for (final asset in _fixtures) {
      final bytes = (await rootBundle.load(asset)).buffer.asUint8List();
      final file = File('${dir.path}/${asset.split('/').last}')
        ..writeAsBytesSync(bytes);
      files.add(file.path);
      bytesByFile[file.path] = bytes;
    }

    // Android trims priming and end padding exactly; other platforms' native
    // decoders may keep up to a couple of AAC frames.
    final toleranceMs = Platform.isAndroid ? 1 : 100;
    void expectTwoSeconds(AudioSource sound, String label) {
      final ms = SoLoud.instance.getLength(sound).inMicroseconds / 1000.0;
      strBuf.writeln('$label: ${ms.toStringAsFixed(2)} ms');
      _expect(
        (ms - 2000).abs() <= toleranceMs,
        '$label: decoded length $ms ms, expected 2000 ms',
      );
    }

    // 1. loadFile in both modes and loadMem decode the full 2 s.
    for (final path in files) {
      final name = path.split('/').last;
      for (final mode in LoadMode.values) {
        final sound = await SoLoud.instance.loadFile(path, mode: mode);
        expectTwoSeconds(sound, '$name loadFile ${mode.name}');
        await SoLoud.instance.disposeSource(sound);

        final memSound = await SoLoud.instance
            .loadMem('$name-mem-${mode.name}', bytesByFile[path]!, mode: mode);
        expectTwoSeconds(memSound, '$name loadMem ${mode.name}');
        await SoLoud.instance.disposeSource(memSound);
      }
    }

    // 2. Playback runs to the end of the decoded audio and then stops.
    final playSound = await SoLoud.instance.loadFile(files.last);
    final started = DateTime.now();
    final handle = SoLoud.instance.play(playSound);
    await delay(1000);
    _expect(
      SoLoud.instance.getIsValidVoiceHandle(handle),
      'voice ended before 1 s of a 2 s sound',
    );
    // Track the voice's own position: emulator wall clocks drift from the
    // audio clock.
    var maxPositionMs = 0;
    while (SoLoud.instance.getIsValidVoiceHandle(handle) &&
        DateTime.now().difference(started).inMilliseconds < 5000) {
      final positionMs = SoLoud.instance.getPosition(handle).inMilliseconds;
      maxPositionMs = max(maxPositionMs, positionMs);
      await delay(10);
    }
    final playedMs = DateTime.now().difference(started).inMilliseconds;
    strBuf.writeln(
      'Playback ended after $playedMs ms (last position $maxPositionMs ms)',
    );
    _expect(
      maxPositionMs >= 1900,
      'voice ended at position $maxPositionMs ms of a 2 s sound',
    );
    _expect(
      playedMs < 4000,
      'playback of a 2 s sound still running after $playedMs ms',
    );
    await SoLoud.instance.disposeSource(playSound);

    // 3. Malformed input fails cleanly with an exception, and promptly.
    final good = bytesByFile[files.last]!;
    final random = Random(42);
    final malformed = <String, List<int>>{
      'empty': <int>[],
      'truncated': good.sublist(0, (good.length * 0.6).round()),
      'ftyp+garbage': [
        ...[0, 0, 0, 0x18, 0x66, 0x74, 0x79, 0x70, 0x4D, 0x34, 0x41, 0x20],
        ...List.generate(4096, (_) => random.nextInt(256)),
      ],
      'random bytes': List.generate(8192, (_) => random.nextInt(256)),
    };
    for (final entry in malformed.entries) {
      final file = File('${dir.path}/bad_${entry.key.replaceAll('+', '_')}.m4a')
        ..writeAsBytesSync(entry.value);
      for (final mode in LoadMode.values) {
        final sw = Stopwatch()..start();
        Object? error;
        try {
          final s = await SoLoud.instance.loadFile(file.path, mode: mode);
          await SoLoud.instance.disposeSource(s);
        } on SoLoudException catch (e) {
          error = e;
        }
        strBuf.writeln(
          'loadFile ${entry.key} ${mode.name}: ${error.runtimeType} '
          '(${sw.elapsedMilliseconds} ms)',
        );
        _expect(error != null, '${entry.key} ${mode.name}: load succeeded');
        _expect(
          sw.elapsedMilliseconds < 15000,
          '${entry.key} ${mode.name}: took ${sw.elapsedMilliseconds} ms',
        );
      }
      if (entry.value.isNotEmpty) {
        Object? error;
        try {
          final s = await SoLoud.instance
              .loadMem('bad-${entry.key}', Uint8List.fromList(entry.value));
          await SoLoud.instance.disposeSource(s);
        } on SoLoudException catch (e) {
          error = e;
        }
        _expect(error != null, '${entry.key} loadMem: load succeeded');
      }
    }

    // 4. Repeated load/play/stop/dispose cycles don't leak descriptors
    //    (source files, temporary files, codec buffers).
    Future<void> cycle(int i) async {
      final path = files[i % files.length];
      final mode = LoadMode.values[i % LoadMode.values.length];
      final sound = i.isEven
          ? await SoLoud.instance.loadFile(path, mode: mode)
          : await SoLoud.instance.loadMem('cycle-$i', bytesByFile[path]!);
      final h = SoLoud.instance.play(sound);
      await delay(30);
      await SoLoud.instance.stop(h);
      await SoLoud.instance.disposeSource(sound);
    }

    // Codec2 keeps a per-process buffer-pool cache (ashmem) that fills over
    // the first codec instances and then stays flat (~40 cycles on the
    // emulator, ~80 on a Galaxy S26 Ultra); warm it up so only unbounded
    // growth is measured. A leak of one descriptor per cycle adds 100.
    for (var i = 0; i < 100; i++) {
      await cycle(i);
    }
    final fdsBefore = _openFdCount();
    final targetsBefore = _openFdTargets();
    for (var i = 0; i < 100; i++) {
      await cycle(i);
    }
    await delay(500);
    final fdsAfter = _openFdCount();
    strBuf.writeln('Open fds across 100 cycles: $fdsBefore -> $fdsAfter');
    if (fdsBefore >= 0) {
      final targetsAfter = _openFdTargets();
      final grown = {
        for (final t in targetsAfter.keys)
          if ((targetsAfter[t] ?? 0) != (targetsBefore[t] ?? 0))
            t: (targetsAfter[t] ?? 0) - (targetsBefore[t] ?? 0),
      };
      _expect(
        fdsAfter - fdsBefore <= 24,
        'file descriptors grew from $fdsBefore to $fdsAfter over 100 cycles: '
        '$grown',
      );
    }

    // 5. Concurrent loads all decode fully.
    final concurrent = await Future.wait([
      for (var i = 0; i < 8; i++)
        SoLoud.instance.loadFile(
          files[i % files.length],
          mode: LoadMode.values[(i ~/ 2) % LoadMode.values.length],
        ),
    ]);
    for (final (i, s) in concurrent.indexed) {
      expectTwoSeconds(s, 'concurrent load $i');
    }
    await SoLoud.instance.disposeAllSources();

    // 6. Engine teardown while sounds play and loads are in flight, then
    //    reinitialization.
    final s = await SoLoud.instance.loadFile(files.first);
    SoLoud.instance.play(s);
    final inFlight = [
      for (final path in files)
        SoLoud.instance
            .loadFile(path)
            .then<Object?>((v) => v)
            .catchError((Object e) => e),
    ];
    await delay(50);
    deinit();
    // Loads in flight during teardown must settle (with a source or an
    // error), not hang.
    await Future.wait(inFlight).timeout(const Duration(seconds: 20));
    await initialize();
    final afterReinit = await SoLoud.instance.loadFile(files.last);
    expectTwoSeconds(afterReinit, 'after reinit');
    final h = SoLoud.instance.play(afterReinit);
    await delay(200);
    _expect(
      SoLoud.instance.getIsValidVoiceHandle(h),
      'voice not playing after reinit',
    );
  } finally {
    deinit();
    dir.deleteSync(recursive: true);
  }
  return strBuf;
}

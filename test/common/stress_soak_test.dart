// Randomized soak of the public API with the real engine (TEST_PLAN X6):
// from a fixed seed, for a fixed time, load (generated WAVs in memory and
// stream mode, waveforms), play (also paused, looping, on a bus), stop, seek,
// pause, speed, volume fades, filters on sounds / buses / the global chain,
// bus create/dispose, dispose while playing, stopAll.
//
// At regular checkpoints everything is stopped and disposed, and then:
//   - no voice is left (getActiveVoiceCount, getVoiceCount);
//   - the engine clock has moved forward with wall time;
//   - process RSS has not grown past an allowance since the first
//     checkpoint (VM only; the browser has no RSS).
//
// Default 8 s. The ten minute soak is opt-in:
//   SOLOUD_STRESS_SECONDS=600 flutter test test/common/stress_soak_test.dart
//   flutter test --dart-define=SOLOUD_STRESS_SECONDS=600 ...   (also web)
// SOLOUD_STRESS_SEED replays another seed.
@Tags(['device', 'slow'])
library;

import 'dart:io' show Platform, ProcessInfo;
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

int _envInt(String name, int fallback) {
  const fromDefine = {
    'SOLOUD_STRESS_SECONDS': String.fromEnvironment('SOLOUD_STRESS_SECONDS'),
    'SOLOUD_STRESS_SEED': String.fromEnvironment('SOLOUD_STRESS_SEED'),
  };
  final defined = fromDefine[name] ?? '';
  if (defined.isNotEmpty) return int.tryParse(defined) ?? fallback;
  if (!kIsWeb) {
    final v = Platform.environment[name];
    if (v != null && v.isNotEmpty) return int.tryParse(v) ?? fallback;
  }
  return fallback;
}

int? _rss() => kIsWeb ? null : ProcessInfo.currentRss;

class _Soak {
  _Soak(int seed) : rng = Random(seed);

  final Random rng;
  final soloud = SoLoud.instance;
  final sounds = <AudioSource>[];
  final handles = <SoundHandle>[];
  final buses = <Bus>[];
  int ops = 0;
  int maxVoices = 0;

  static final _filters = FilterType.values
      .where((f) => !f.name.toLowerCase().contains('custom'))
      .toList();

  T? _any<T>(List<T> list) =>
      list.isEmpty ? null : list[rng.nextInt(list.length)];
  double _uni(double a, double b) => a + rng.nextDouble() * (b - a);

  Future<void> _load() async {
    if (sounds.length >= 12) return;
    if (rng.nextInt(4) == 0) {
      sounds.add(
        await soloud.loadWaveform(
          WaveForm.values[rng.nextInt(WaveForm.values.length)],
          rng.nextBool(),
          1,
          0.1,
        ),
      );
    } else {
      const rates = [22050, 44100, 48000];
      sounds.add(
        await loadTestSound(
          duration: Duration(milliseconds: 50 + rng.nextInt(1450)),
          sampleRate: rates[rng.nextInt(3)],
          channels: 1 + rng.nextInt(2),
          frequency: _uni(100, 2000),
          mode: rng.nextBool() ? LoadMode.memory : LoadMode.disk,
        ),
      );
    }
  }

  void _play() {
    final s = _any(sounds);
    if (s == null) return;
    final bus = rng.nextInt(3) == 0 ? _any(buses) : null;
    final h = soloud.play(
      s,
      busId: bus?.busId ?? 0,
      volume: _uni(0, 0.5),
      pan: _uni(-1, 1),
      paused: rng.nextInt(5) == 0,
      looping: rng.nextInt(3) == 0,
    );
    handles.add(h);
    if (handles.length > 64) handles.removeAt(0);
  }

  void _filter() {
    final f = _filters[rng.nextInt(_filters.length)];
    SoundHash? hash;
    int? busId;
    switch (rng.nextInt(3)) {
      case 0:
        // Filters on single sounds do not exist on the web.
        if (kIsWeb) return;
        hash = _any(sounds)?.soundHash;
        if (hash == null) return;
      case 1:
        busId = _any(buses)?.busId;
        if (busId == null) return;
    }
    try {
      if (rng.nextBool()) {
        f.activate(hash, busId);
      } else {
        f.deactivate(hash, busId);
      }
    } on SoLoudCppException {
      // Already active / not active: part of the random walk.
    }
  }

  Future<void> _dispose() async {
    if (sounds.isEmpty) return;
    final s = sounds.removeAt(rng.nextInt(sounds.length));
    await soloud.disposeSource(s);
  }

  void _bus() {
    if (buses.length < 3 && rng.nextBool()) {
      final b = soloud.createMixingBus()..playOnEngine(volume: 0.5);
      buses.add(b);
    } else if (buses.isNotEmpty) {
      buses.removeAt(rng.nextInt(buses.length)).dispose();
    }
  }

  Future<void> step() async {
    ops++;
    final h = _any(handles);
    final valid = h != null && soloud.getIsValidVoiceHandle(h);
    switch (rng.nextInt(16)) {
      case 0 || 1:
        await _load();
      case 2 || 3 || 4:
        _play();
      case 5:
        if (valid) await soloud.stop(h);
      case 6:
        if (valid) {
          try {
            soloud.seek(h, Duration(milliseconds: rng.nextInt(1000)));
          } on SoLoudInvalidParameterException {
            // A waveform (not seekable), or a voice that ended since the
            // validity check: documented.
          }
        }
      case 7:
        if (valid) soloud.setPause(h, rng.nextBool());
      case 8:
        if (valid) soloud.setRelativePlaySpeed(h, _uni(0.25, 3));
      case 9:
        if (valid) {
          soloud.fadeVolume(
            h,
            _uni(0, 0.5),
            Duration(milliseconds: rng.nextInt(300)),
          );
        }
      case 10:
        if (valid) soloud.setVolume(h, _uni(0, 0.5));
      case 11:
        _filter();
      case 12:
        await _dispose();
      case 13:
        _bus();
      case 14:
        if (rng.nextInt(20) == 0) {
          soloud.stopAll();
          // stopAll() also stops the bus voices; a stopped bus has to be
          // played again before sounds can play through it.
          for (final b in buses) {
            b.playOnEngine(volume: 0.5);
          }
        }
      case 15:
        if (valid) soloud.setLooping(h, rng.nextBool());
    }
  }

  Future<void> cleanUp() async {
    soloud.stopAll();
    for (final b in buses) {
      b.dispose();
    }
    buses.clear();
    await soloud.disposeAllSources();
    sounds.clear();
    handles.clear();
    for (final f in _filters) {
      try {
        f.deactivate(null, null);
      } on SoLoudCppException {
        // Not active.
      }
    }
  }
}

void main() {
  final seconds = max(3, _envInt('SOLOUD_STRESS_SECONDS', 8));
  final seed = _envInt('SOLOUD_STRESS_SEED', 20261120);

  setUpEngineTests(globalVolume: 0);

  test('random play/stop/seek/load/dispose with filters and buses for '
      '$seconds s (seed $seed) leaves no voices, no memory growth, and a '
      'running engine clock', () async {
    final soloud = SoLoud.instance;
    final soak = _Soak(seed);
    final wall = Stopwatch()..start();
    final checkpointEvery = Duration(
      milliseconds: (seconds * 1000 / 6).clamp(1000, 30000).round(),
    );
    var nextCheckpoint = checkpointEvery;
    final startEngine = soloud.getEngineTime();
    var lastEngine = startEngine;
    int? firstRss;
    int? lastRss;
    var checkpoints = 0;

    while (true) {
      final last = wall.elapsed.inSeconds >= seconds;
      if (wall.elapsed >= nextCheckpoint || last) {
        await soak.cleanUp();
        await Future<void>.delayed(const Duration(milliseconds: 150));
        checkpoints++;
        expect(
          soloud.getActiveVoiceCount(),
          0,
          reason: 'checkpoint $checkpoints: active voices left',
        );
        expect(
          soloud.getVoiceCount(),
          0,
          reason: 'checkpoint $checkpoints: voices left',
        );
        final engine = soloud.getEngineTime();
        expect(
          engine >= lastEngine,
          isTrue,
          reason: 'engine time went backwards: $lastEngine -> $engine',
        );
        lastEngine = engine;
        final rss = _rss();
        firstRss ??= rss;
        lastRss = rss;
        nextCheckpoint += checkpointEvery;
        if (last) break;
      }
      for (var i = 0; i < 10; i++) {
        await soak.step();
      }
      soak.maxVoices = max(soak.maxVoices, soloud.getActiveVoiceCount());
      await Future<void>.delayed(const Duration(milliseconds: 2));
    }

    final engineAdvance = (lastEngine - startEngine).inMilliseconds / 1000;
    final wallSeconds = wall.elapsedMilliseconds / 1000;
    final rssText = firstRss == null
        ? 'n/a'
        : '${firstRss ~/ 1024} -> ${lastRss! ~/ 1024} KiB';
    debugPrint(
      'soak: ${soak.ops} operations, $checkpoints checkpoints, up to '
      '${soak.maxVoices} voices; engine ${engineAdvance.toStringAsFixed(1)} s '
      'in ${wallSeconds.toStringAsFixed(1)} s; RSS $rssText',
    );
    expect(soak.maxVoices, greaterThan(0), reason: 'nothing ever played');
    // The device may start a little late and idles between bursts, so
    // only a broad agreement with wall time is required.
    expect(
      engineAdvance,
      greaterThan(wallSeconds * 0.5),
      reason: 'the engine clock fell behind wall time',
    );
    if (firstRss != null && lastRss != null) {
      // RSS is coarse (Dart GC, allocator caches): the allowance only
      // catches a real leak, which over thousands of loads is far larger.
      final growth = lastRss - firstRss;
      expect(
        growth,
        lessThan(48 * 1024 * 1024 + firstRss ~/ 5),
        reason: 'RSS grew by ${growth ~/ 1024} KiB',
      );
    }
  }, timeout: Timeout(Duration(seconds: seconds + 120)));
}

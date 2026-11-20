// The same audio checks on the single-threaded and the multi-threaded WASM
// builds: output level of a known sine, its frequency, the length of a loaded
// WAV, and when a voice-ended event arrives. Each check also has an absolute
// expectation, so the file is useful on its own; the results are printed as
// one `PARITY {json}` line, which test/web/web_mt_st_parity_test.sh compares
// across the builds (ST, MT, and MT with flutter_soloud_force_single_threaded
// through --dart-define=SOLOUD_FORCE_ST=true).
//
// Browser only: ./test/web/web_mt_st_parity_test.sh (or one build with
// ./test/support/web_tests.sh [--wasm] test/common/web_mt_st_parity_test.dart).
@TestOn('browser')
@Tags(['device'])
library;

import 'dart:convert';
import 'dart:js_interop';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

@JS('globalThis.crossOriginIsolated')
external bool? get _crossOriginIsolated;

@JS('self.flutter_soloud_force_single_threaded')
external bool? get _forceSt;

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  final results = <String, Object?>{};

  tearDownAll(() {
    results['crossOriginIsolated'] = _crossOriginIsolated ?? false;
    results['forceSingleThreaded'] = _forceSt ?? false;
    // ignore: avoid_print
    print('PARITY ${jsonEncode(results)}');
  });

  setUp(() => initTestEngine(globalVolume: 1));
  tearDown(deinitTestEngine);

  test('a 440 Hz sine at 0.5: level and frequency on the output', () async {
    final s = await loadSine(duration: const Duration(seconds: 3), channels: 2);
    soloud.play(s);
    final out = await captureOutput(frames: 16384);
    final rms = out.rms(0);
    final hz = out.frequency(0);
    results['sineRms'] = rms;
    results['sineHz'] = hz;
    // 0.5 / sqrt(2), with the default pan law of a centered voice.
    expect(rms, inInclusiveRange(0.2, 0.4));
    expect(hz, closeTo(440, 5));
  });

  test('length of a loaded 1.5 s WAV', () async {
    final s = await loadSine(duration: const Duration(milliseconds: 1500));
    final ms = soloud.getLength(s).inMicroseconds / 1000;
    results['lengthMs'] = ms;
    expect(ms, closeTo(1500, 1));
  });

  test('a 500 ms voice ends, and is reported, about 500 ms later', () async {
    final s = await loadSine(duration: const Duration(milliseconds: 500));
    final watch = Stopwatch()..start();
    soloud.play(s);
    await s.allInstancesFinished.first.timeout(const Duration(seconds: 5));
    final ms = watch.elapsedMilliseconds;
    results['voiceEndedMs'] = ms;
    expect(ms, inInclusiveRange(450, 1200));
  });
}

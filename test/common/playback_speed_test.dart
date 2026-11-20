// Relative play speed on a playing voice (set, fade, oscillate), with the
// real engine and a generated sound.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  late SoundHandle handle;
  setUp(() async {
    final sound = await loadSine(duration: const Duration(seconds: 2));
    handle = SoLoud.instance.play(sound, looping: true);
  });

  test('setRelativePlaySpeed and getRelativePlaySpeed', () {
    expect(SoLoud.instance.getRelativePlaySpeed(handle), closeTo(1, 0.01));
    for (final speed in [2.0, 0.5, 1.0]) {
      SoLoud.instance.setRelativePlaySpeed(handle, speed);
      expect(
        SoLoud.instance.getRelativePlaySpeed(handle),
        closeTo(speed, 1e-2),
      );
    }
  });

  test('a faster voice advances faster', () async {
    final sound = await loadSine(duration: const Duration(seconds: 5));
    final normal = SoLoud.instance.play(sound);
    final fast = SoLoud.instance.play(sound);
    SoLoud.instance.setRelativePlaySpeed(fast, 2);
    await Future<void>.delayed(const Duration(milliseconds: 500));
    final n = SoLoud.instance.getPosition(normal).inMilliseconds;
    final f = SoLoud.instance.getPosition(fast).inMilliseconds;
    expect(n, greaterThan(0));
    expect(f / n, closeTo(2, 0.3), reason: 'positions: normal $n, fast $f');
  });

  test('fadeRelativePlaySpeed reaches its target', () async {
    SoLoud.instance.fadeRelativePlaySpeed(
      handle,
      1.5,
      const Duration(milliseconds: 200),
    );
    expect(
      await waitUntil(
        () => (SoLoud.instance.getRelativePlaySpeed(handle) - 1.5).abs() < 0.05,
      ),
      isTrue,
      reason: 'speed ended at ${SoLoud.instance.getRelativePlaySpeed(handle)}',
    );
  });

  test('oscillateRelativePlaySpeed stays between its bounds', () async {
    SoLoud.instance.oscillateRelativePlaySpeed(
      handle,
      0.8,
      1.2,
      const Duration(milliseconds: 300),
    );
    // The oscillation starts on the engine's next mix.
    await waitUntil(() {
      final v = SoLoud.instance.getRelativePlaySpeed(handle);
      return v >= 0.8 - 1e-3 && v <= 1.2 + 1e-3;
    });
    final speeds = <double>[];
    for (var i = 0; i < 10; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
      speeds.add(SoLoud.instance.getRelativePlaySpeed(handle));
    }
    expect(speeds, everyElement(inInclusiveRange(0.8 - 1e-3, 1.2 + 1e-3)));
    expect(
      speeds.reduce((a, b) => a > b ? a : b) -
          speeds.reduce((a, b) => a < b ? a : b),
      greaterThan(0.1),
      reason: 'speed does not oscillate: $speeds',
    );
  });
}

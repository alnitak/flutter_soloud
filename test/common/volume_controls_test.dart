// Volume controls on a playing voice (set, fade, oscillate), with the real
// engine and a generated sound.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  late SoundHandle handle;
  setUp(() async {
    final sound = await loadSine();
    handle = SoLoud.instance.play(sound, looping: true);
  });

  test('setVolume and getVolume', () {
    for (final volume in [0.5, 0.0, 1.0]) {
      SoLoud.instance.setVolume(handle, volume);
      expect(SoLoud.instance.getVolume(handle), closeTo(volume, 0.01));
    }
  });

  test('fadeVolume reaches its target', () async {
    SoLoud.instance.setVolume(handle, 1);
    SoLoud.instance.fadeVolume(handle, 0.2, const Duration(milliseconds: 200));
    expect(
      await waitUntil(
        () => (SoLoud.instance.getVolume(handle) - 0.2).abs() < 0.05,
      ),
      isTrue,
      reason: 'volume ended at ${SoLoud.instance.getVolume(handle)}',
    );
  });

  test('oscillateVolume stays between its bounds and moves', () async {
    SoLoud.instance.oscillateVolume(
      handle,
      0.3,
      0.7,
      const Duration(milliseconds: 300),
    );
    // The oscillation starts on the engine's next mix.
    await waitUntil(() {
      final v = SoLoud.instance.getVolume(handle);
      return v >= 0.3 - 1e-3 && v <= 0.7 + 1e-3;
    });
    final volumes = <double>[];
    for (var i = 0; i < 10; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
      volumes.add(SoLoud.instance.getVolume(handle));
    }
    expect(volumes, everyElement(inInclusiveRange(0.3 - 1e-3, 0.7 + 1e-3)));
    expect(
      volumes.reduce((a, b) => a > b ? a : b) -
          volumes.reduce((a, b) => a < b ? a : b),
      greaterThan(0.1),
      reason: 'volume does not oscillate: $volumes',
    );

    // A fixed volume ends the oscillation.
    SoLoud.instance.setVolume(handle, 0.5);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    expect(SoLoud.instance.getVolume(handle), closeTo(0.5, 0.01));
  });
}

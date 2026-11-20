// Pan controls on a playing voice, with the real engine and a generated
// sound.
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
    handle = SoLoud.instance.play(sound, volume: 0.5, looping: true);
  });

  test('setPan and getPan', () {
    for (final pan in [-0.8, 0.8, 0.0, -1.0, 1.0]) {
      SoLoud.instance.setPan(handle, pan);
      expect(SoLoud.instance.getPan(handle), closeTo(pan, 1e-5));
    }
  });

  test('setPanAbsolute keeps the voice playing', () async {
    SoLoud.instance.setPanAbsolute(handle, 1, 0.3);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    SoLoud.instance.setPanAbsolute(handle, 0.3, 1);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
  });

  test('fadePan reaches its target', () async {
    SoLoud.instance.setPan(handle, 0);
    SoLoud.instance.fadePan(handle, -0.8, const Duration(milliseconds: 200));
    expect(
      await waitUntil(
        () => (SoLoud.instance.getPan(handle) + 0.8).abs() < 0.05,
      ),
      isTrue,
      reason: 'pan ended at ${SoLoud.instance.getPan(handle)}',
    );
  });

  test('oscillatePan stays between its bounds and moves', () async {
    SoLoud.instance.oscillatePan(
      handle,
      -0.5,
      0.5,
      const Duration(milliseconds: 300),
    );
    // The oscillation starts on the engine's next mix.
    await waitUntil(() {
      final v = SoLoud.instance.getPan(handle);
      return v >= -0.5 - 1e-3 && v <= 0.5 + 1e-3;
    });
    final pans = <double>[];
    for (var i = 0; i < 10; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
      pans.add(SoLoud.instance.getPan(handle));
    }
    expect(pans, everyElement(inInclusiveRange(-0.5 - 1e-3, 0.5 + 1e-3)));
    expect(
      pans.reduce((a, b) => a > b ? a : b) -
          pans.reduce((a, b) => a < b ? a : b),
      greaterThan(0.2),
      reason: 'pan does not oscillate: $pans',
    );
  });
}

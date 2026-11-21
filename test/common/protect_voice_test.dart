// Max active voices and protected voices, with the real engine and generated
// sounds.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  test('a protected voice survives voice stealing', () async {
    final defaultMax = SoLoud.instance.getMaxActiveVoiceCount();
    SoLoud.instance.setMaxActiveVoiceCount(6);
    expect(SoLoud.instance.getMaxActiveVoiceCount(), 6);

    final short = await loadSine(duration: const Duration(seconds: 3));
    final long = await loadSine(duration: const Duration(seconds: 10));

    for (var i = 0; i < 5; i++) {
      SoLoud.instance.play(short);
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    final protected = SoLoud.instance.play(long);
    SoLoud.instance.setProtectVoice(protected, true);
    expect(SoLoud.instance.getProtectVoice(protected), isTrue);
    for (var i = 0; i < 5; i++) {
      SoLoud.instance.play(short);
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    await Future<void>.delayed(const Duration(milliseconds: 100));

    expect(
      SoLoud.instance.getIsValidVoiceHandle(protected),
      isTrue,
      reason: 'the protected voice was stopped',
    );
    expect(SoLoud.instance.getActiveVoiceCount(), 6);
    expect(SoLoud.instance.getVoiceCount(), greaterThanOrEqualTo(6));

    SoLoud.instance.setInaudibleBehavior(protected, true, false);
    expect(SoLoud.instance.getIsValidVoiceHandle(protected), isTrue);

    // A new engine starts again from the default.
    deinitTestEngine();
    await initTestEngine();
    expect(SoLoud.instance.getMaxActiveVoiceCount(), defaultMax);
  });
}

// Play, pause, seek and position of a voice, with the real engine and a
// generated sound.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  test('length, pauseSwitch, seek and getPosition', () async {
    final sound = await loadSine(duration: const Duration(seconds: 3));
    expect(SoLoud.instance.getLength(sound).inMilliseconds, closeTo(3000, 1));
    final handle = SoLoud.instance.play(sound);
    await Future<void>.delayed(const Duration(milliseconds: 200));
    expect(SoLoud.instance.getPosition(handle), greaterThan(Duration.zero));

    SoLoud.instance.pauseSwitch(handle);
    expect(SoLoud.instance.getPause(handle), isTrue);
    SoLoud.instance.seek(handle, const Duration(seconds: 2));
    expect(
      SoLoud.instance.getPosition(handle).inMicroseconds,
      closeTo(2000000, 1000),
    );
    await Future<void>.delayed(const Duration(milliseconds: 100));
    expect(
      SoLoud.instance.getPosition(handle).inMicroseconds,
      closeTo(2000000, 1000),
      reason: 'a paused voice does not advance',
    );
    SoLoud.instance.pauseSwitch(handle);
    expect(SoLoud.instance.getPause(handle), isFalse);
  });

  test('schedulePause pauses after its delay', () async {
    final sound = await loadSine(duration: const Duration(seconds: 3));
    final handle = SoLoud.instance.play(sound);
    SoLoud.instance.schedulePause(handle, const Duration(milliseconds: 300));
    expect(SoLoud.instance.getPause(handle), isFalse);
    expect(
      await waitUntil(() => SoLoud.instance.getPause(handle)),
      isTrue,
      reason: 'the voice was not paused',
    );
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
  });

  test('a voice seeked near its end ends and its handle goes', () async {
    final sound = await loadSine(duration: const Duration(seconds: 3));
    final handle = SoLoud.instance.play(sound);
    SoLoud.instance.seek(
      handle,
      SoLoud.instance.getLength(sound) - const Duration(milliseconds: 100),
    );
    expect(
      await waitUntil(() => !SoLoud.instance.getIsValidVoiceHandle(handle)),
      isTrue,
    );
    expect(await waitUntil(() => sound.handles.isEmpty), isTrue);
  });
}

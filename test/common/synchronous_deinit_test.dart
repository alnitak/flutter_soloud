// Awaited init() followed by a synchronous deinit(), and the sounds deinit()
// releases, with the real engine and a generated sound.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  prepareEngineHost();
  tearDown(deinitTestEngine);

  test('init then deinit, repeatedly', () async {
    for (var i = 0; i < 11; i++) {
      Object? error;
      await SoLoud.instance.init().then(
        (_) {},
        onError: (Object e) {
          error = e;
        },
      );
      expect(
        error,
        anyOf(isNull, isA<SoLoudInitializationStoppedByDeinitException>()),
        reason: 'round $i',
      );
      SoLoud.instance.deinit();
      expect(SoLoud.instance.isInitialized, isFalse, reason: 'round $i');
      expect(
        SoLoudController().soLoudFFI.isInited(),
        isFalse,
        reason: 'round $i',
      );
    }
  });

  test('deinit releases playing sounds the app did not dispose', () async {
    await initTestEngine();
    final sound = await loadSine(duration: const Duration(seconds: 2));
    final handles = <SoundHandle>[];
    for (var i = 0; i < 3; i++) {
      handles.add(SoLoud.instance.play(sound));
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
    expect(SoLoud.instance.getActiveVoiceCount(), 3);
    SoLoud.instance.deinit();

    await initTestEngine();
    final ffi = SoLoudController().soLoudFFI;
    for (final handle in handles) {
      expect(ffi.getIsValidVoiceHandle(handle), isFalse);
    }
    expect(ffi.countAudioSource(sound.soundHash), 0);
    expect(ffi.getActiveVoiceCount(), 0);
    expect(SoLoud.instance.activeSounds, isEmpty);
  });
}

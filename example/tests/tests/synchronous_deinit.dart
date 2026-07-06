import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

import 'common.dart';

/// Test synchronous `init()`-`deinit()`.
Future<OutputBuffer> testSynchronousDeinit() async {
  /// test synchronous init-deinit looping with a short decreasing time
  /// waiting for `initialize()` to finish
  for (var t = 10; t >= 0; t--) {
    var error = '';

    /// Initialize the player
    await SoLoud.instance.init().then(
      (_) {},
      onError: (Object e) {
        if (e is SoLoudInitializationStoppedByDeinitException) {
          // This is to be expected.
          debugPrint('$e\n');
          return;
        }
        debugPrint('TEST FAILED delay: $t. Player starting error: $e');
        error = e.toString();
      },
    );
    assert(error.isEmpty, error);

    SoLoud.instance.deinit();

    assert(
      !SoLoud.instance.isInitialized ||
          !SoLoudController().soLoudFFI.isInited(),
      'ASSERT FAILED delay: $t. The player has not been '
      'inited or deinited correctly!',
    );

    debugPrint('------------- awaited init #$t passed\n');
  }

  /// Try init-play-deinit and again init-play without disposing the sound
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(0.2);

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  SoLoud.instance.play(currentSound);
  await delay(100);
  SoLoud.instance.play(currentSound);
  await delay(100);
  SoLoud.instance.play(currentSound);

  await delay(2000);

  SoLoud.instance.deinit();

  /// Initialize again and check if the sound has been
  /// disposed correctly by `deinit()`
  await SoLoud.instance.init();
  assert(
    SoLoudController()
            .soLoudFFI
            .getIsValidVoiceHandle(currentSound.handles.first) ==
        false,
    'getIsValidVoiceHandle(): sound not disposed by the engine',
  );
  assert(
    SoLoudController().soLoudFFI.countAudioSource(currentSound.soundHash) == 0,
    'getCountAudioSource(): sound not disposed by the engine',
  );
  assert(
    SoLoudController().soLoudFFI.getActiveVoiceCount() == 0,
    'getActiveVoiceCount(): sound not disposed by the engine',
  );
  SoLoud.instance.deinit();

  return OutputBuffer();
}

// The web engine when the browser keeps the AudioContext suspended (no
// autoplay permission, no user gesture yet), which is how a real page starts:
// init must still complete (not hang waiting for the context), sounds load,
// play returns a valid handle, and nothing advances until the context runs.
//
// Needs Chrome WITHOUT --autoplay-policy=no-user-gesture-required, which
// test/support/web_tests.sh always adds: run it with
// ./test/web/web_suspended_context_test.sh, which strips the flag and passes
// --dart-define=SOLOUD_EXPECT_SUSPENDED=true. Run by web_tests.sh (autoplay
// allowed) the suspended-only checks are skipped.
//
// Resuming needs a trusted user gesture, which a test page cannot produce, so
// "plays after resume" is checked only to the extent that a voice started
// while suspended is still valid and paused in time.
@TestOn('browser')
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

const _expectSuspended = bool.fromEnvironment('SOLOUD_EXPECT_SUSPENDED');

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  tearDown(deinitTestEngine);

  test('init completes while the AudioContext is suspended', () async {
    await soloud.init().timeout(const Duration(seconds: 10));
    expect(soloud.isInitialized, isTrue);
  });

  test(
    'load and play while suspended: valid handle, time does not advance',
    () async {
      await soloud.init().timeout(const Duration(seconds: 10));
      final s = await loadSine(duration: const Duration(seconds: 2));
      final h = soloud.play(s, volume: 0);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 800));
      expect(
        soloud.getIsValidVoiceHandle(h),
        isTrue,
        reason: 'a 2 s voice cannot have ended',
      );
      expect(
        soloud.getPosition(h),
        lessThan(const Duration(milliseconds: 50)),
        reason: 'the suspended context must not advance',
      );
    },
    skip: _expectSuspended ? false : 'the AudioContext runs (autoplay allowed)',
  );

  test('deinit and init again while suspended', () async {
    for (var i = 0; i < 3; i++) {
      await soloud.init().timeout(const Duration(seconds: 10));
      final tone = await loadTestWaveform();
      expect(
        soloud.getIsValidVoiceHandle(soloud.play(tone, volume: 0)),
        isTrue,
      );
      soloud.deinit();
    }
  });
}

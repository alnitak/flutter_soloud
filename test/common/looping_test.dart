// Looping and a bounded [loopingStartAt, loopingEndAt) region on a playing
// voice, with the real engine and a generated sound. The region validation is
// unit tested in looping_region_test.dart.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  test('a voice keeps looping inside its bounded region', () async {
    const start = Duration(milliseconds: 300);
    const end = Duration(milliseconds: 600);
    final sound = await loadSine();
    final handle = SoLoud.instance.play(
      sound,
      looping: true,
      loopingStartAt: start,
      loopingEndAt: end,
    );
    expect(SoLoud.instance.getLooping(handle), isTrue);
    expect(SoLoud.instance.getLoopPoint(handle), start);
    expect(SoLoud.instance.getLoopEndPoint(handle), end);

    // Past the first pass and several loops, it never leaves the region (a
    // non-looping voice of this 1 s sound would have ended).
    await Future<void>.delayed(const Duration(milliseconds: 1200));
    for (var i = 0; i < 5; i++) {
      expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
      final position = SoLoud.instance.getPosition(handle);
      expect(
        position,
        allOf(greaterThanOrEqualTo(start), lessThan(end)),
        reason: 'position $position outside the loop region',
      );
      await Future<void>.delayed(const Duration(milliseconds: 70));
    }

    SoLoud.instance.setLoopEndPoint(handle, null);
    expect(SoLoud.instance.getLoopEndPoint(handle), isNull);
    SoLoud.instance.setLooping(handle, false);
    expect(SoLoud.instance.getLooping(handle), isFalse);
    expect(
      await waitUntil(() => !SoLoud.instance.getIsValidVoiceHandle(handle)),
      isTrue,
      reason: 'without looping the voice ends',
    );
  });
}

// Voice handles of a sound and their end-of-playback events, with the real
// engine and a generated sound.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  test('a handle ends with its sound and emits handleIsNoMoreValid', () async {
    final sound = await loadSine(duration: const Duration(milliseconds: 300));
    final events = <SoundEventType>[];
    sound.soundEvents.listen((event) => events.add(event.event));

    final handle = SoLoud.instance.play(sound);
    expect(sound.soundHash.isValid, isTrue);
    expect(sound.handles, [handle]);

    expect(
      await waitUntil(
        () => events.contains(SoundEventType.handleIsNoMoreValid),
      ),
      isTrue,
      reason: 'the end of playback was not reported',
    );
    expect(sound.handles, isEmpty);
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isFalse);

    await SoLoud.instance.disposeSource(sound);
    expect(
      await waitUntil(() => events.contains(SoundEventType.soundDisposed)),
      isTrue,
    );
  });

  test('several handles of one sound are tracked and released', () async {
    final sound = await loadSine(duration: const Duration(milliseconds: 300));
    final handles = [for (var i = 0; i < 4; i++) SoLoud.instance.play(sound)];
    expect(handles.toSet(), hasLength(4), reason: 'handles are distinct');
    expect(sound.handles, hasLength(4));
    expect(
      await waitUntil(() => sound.handles.isEmpty),
      isTrue,
      reason: '${sound.handles.length} handles left after the sound ended',
    );
  });
}

// Text to speech with the real engine. speechText() synthesizes the text and
// starts speaking it right away; the AudioSource it returns has no Dart-side
// handles.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  test('speechText speaks the text and the voice ends by itself', () async {
    final hello = SoLoud.instance.speechText('Hello world!');
    expect(hello.soundHash.isValid, isTrue);
    expect(SoLoud.instance.isValidAudioSource(hello), isTrue);
    expect(hello.handles, isEmpty);
    expect(
      SoLoud.instance.getLength(hello),
      Duration.zero,
      reason: 'speech has no known length (getLength reports 0)',
    );
    expect(
      SoLoud.instance.getActiveVoiceCount(),
      greaterThanOrEqualTo(1),
      reason: 'speechText starts speaking at once',
    );
    expect(
      await waitUntil(
        () => SoLoud.instance.getActiveVoiceCount() == 0,
        timeout: const Duration(seconds: 5),
      ),
      isTrue,
      reason: 'the spoken text ends by itself',
    );

    final again = SoLoud.instance.speechText('Testing one two three');
    expect(again.soundHash, isNot(hello.soundHash));

    await SoLoud.instance.disposeSource(hello);
    await SoLoud.instance.disposeSource(again);
    expect(SoLoud.instance.isValidAudioSource(hello), isFalse);
    expect(SoLoud.instance.isValidAudioSource(again), isFalse);
  });

  test('play() of a speechText source plays it again', () async {
    final hello = SoLoud.instance.speechText('Hello');
    final handle = SoLoud.instance.play(hello);
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
    await SoLoud.instance.stop(handle);
    await SoLoud.instance.disposeSource(hello);
  });

  test('speechText throws before init', () {
    SoLoud.instance.deinit();
    expect(
      () => SoLoud.instance.speechText('nope'),
      throwsA(isA<SoLoudNotInitializedException>()),
    );
  });
}

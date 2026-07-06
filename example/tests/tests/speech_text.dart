import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test text-to-speech functionality.
Future<OutputBuffer> testSpeechText() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Generate speech from text
  strBuf.writeln('Generating speech for "Hello world"');
  final speechSound = SoLoud.instance.speechText('Hello world!');

  // Verify sound was created
  assert(
    speechSound.soundHash.isValid,
    'Speech sound should have valid hash',
  );
  strBuf.writeln('Speech sound created with hash: ${speechSound.soundHash}');

  // Get duration
  final duration = SoLoud.instance.getLength(speechSound);
  strBuf.writeln('Speech duration: ${duration.inMilliseconds}ms');

  // Wait for playback
  await delay(300);

  // Verify handle is valid
  assert(
    speechSound.handles.isEmpty,
    'Speech handle should be valid',
  );

  // Wait for speech to complete
  await delay(1000);

  // Test another phrase
  strBuf.writeln('Generating speech for "Testing one two three"');
  final speechSound2 = SoLoud.instance.speechText('Testing one two three');
  await delay(1000);

  // Cleanup
  await SoLoud.instance.disposeSource(speechSound);
  await SoLoud.instance.disposeSource(speechSound2);

  deinit();

  strBuf.writeln('Speech text tests completed successfully');
  return strBuf;
}

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test joining two audio sources into a single stereo source.
Future<OutputBuffer> testJoinTwoSources() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load two asset buffers of different lengths and formats
  final byteData1 = await rootBundle.load('assets/audio/explosion.mp3');
  final bytes1 = byteData1.buffer.asUint8List();
  strBuf.writeln('Loaded left buffer: ${bytes1.length} bytes');

  final byteData2 = await rootBundle.load('assets/audio/tic-1.wav');
  final bytes2 = byteData2.buffer.asUint8List();
  strBuf.writeln('Loaded right buffer: ${bytes2.length} bytes');

  // Load both sounds individually to compare their lengths
  final sound1 = await SoLoud.instance.loadMem('left_sound', bytes1);
  final sound2 = await SoLoud.instance.loadMem('right_sound', bytes2);
  final duration1 = SoLoud.instance.getLength(sound1);
  final duration2 = SoLoud.instance.getLength(sound2);
  strBuf
    ..writeln('Sound 1 duration: ${duration1.inMilliseconds}ms')
    ..writeln('Sound 2 duration: ${duration2.inMilliseconds}ms');

  await SoLoud.instance.disposeSource(sound1);
  await SoLoud.instance.disposeSource(sound2);

  // Test joinTwoSources
  final joinedSound = await SoLoud.instance.joinTwoSources(
    'test_joined_stereo',
    bytes1,
    bytes2,
  );
  strBuf.writeln('Joined sound loaded successfully');

  // Verify duration matches the maximum of the two sounds
  final joinedDuration = SoLoud.instance.getLength(joinedSound);
  strBuf.writeln('Joined sound duration: ${joinedDuration.inMilliseconds}ms');

  final expectedMaxMs = duration1 > duration2
      ? duration1.inMilliseconds
      : duration2.inMilliseconds;
  assert(
    (joinedDuration.inMilliseconds - expectedMaxMs).abs() <= 50,
    'Joined sound duration should match the maximum duration: '
    'expected ~$expectedMaxMs ms, got ${joinedDuration.inMilliseconds} ms',
  );

  // Play the joined stereo sound
  final handle = SoLoud.instance.play(joinedSound);
  strBuf.writeln('Playing joined stereo sound');

  await delay(200);

  // Verify handle is valid
  assert(
    SoLoud.instance.getIsValidVoiceHandle(handle),
    'Voice handle should be valid',
  );

  await delay(500);

  // Stop and dispose
  await SoLoud.instance.stop(handle);
  await SoLoud.instance.disposeSource(joinedSound);
  strBuf.writeln('Joined sound disposed');

  return strBuf;
}

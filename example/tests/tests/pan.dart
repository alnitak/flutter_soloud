import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test instancing playing handles and their disposal.
Future<OutputBuffer> testPan() async {
  final strBuf = OutputBuffer();

  /// Start audio isolate
  await initialize();

  final song =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  final handle = SoLoud.instance.play(song, volume: 0.5);

  SoLoud.instance.setPan(handle, -0.8);
  var pan = SoLoud.instance.getPan(handle);
  assert(closeTo(pan, -0.8, 0.00001), 'setPan() or getPan() failed!');

  await delay(500);

  SoLoud.instance.setPan(handle, 0.8);
  pan = SoLoud.instance.getPan(handle);
  assert(closeTo(pan, 0.8, 0.00001), 'setPan() or getPan() failed!');
  await delay(500);

  /// Test setPanAbsolute - independent left/right channel control
  SoLoud.instance.setPanAbsolute(handle, 1, 0.3);
  strBuf.writeln('Set absolute pan: left=1.0, right=0.3');
  await delay(400);

  SoLoud.instance.setPanAbsolute(handle, 0.3, 1);
  strBuf.writeln('Set absolute pan: left=0.3, right=1.0');
  await delay(400);

  // Reset to center
  SoLoud.instance.setPan(handle, 0);

  /// Test fadePan
  strBuf.writeln('Testing fadePan');
  SoLoud.instance.fadePan(
    handle,
    -0.8, // Fade to left
    const Duration(milliseconds: 400),
  );
  await delay(500);
  pan = SoLoud.instance.getPan(handle);
  assert(closeTo(pan, -0.8, 0.05), 'fadePan() failed!');
  strBuf
    ..writeln('fadePan completed: pan=$pan')

    /// Test oscillatePan
    ..writeln('Testing oscillatePan');
  SoLoud.instance.oscillatePan(
    handle,
    -0.5,
    0.5,
    const Duration(milliseconds: 600),
  );
  await delay(300);
  final midOscPan = SoLoud.instance.getPan(handle);
  strBuf.writeln('oscillatePan mid-cycle: pan=$midOscPan');
  await delay(400);

  deinit();
  return strBuf;
}

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test advanced panning including setPanAbsolute, fadePan, and oscillatePan.
Future<OutputBuffer> testAdvancedPan() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load and play a sound
  final sound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  final handle = SoLoud.instance.play(sound, looping: true, volume: 0.6);

  // Test setPanAbsolute - control left and right channels independently
  strBuf.writeln('Testing setPanAbsolute');

  // Full left (left channel at 1.0, right at 0.0)
  SoLoud.instance.setPanAbsolute(handle, 1, 0);
  strBuf.writeln('Set absolute pan: left=1.0, right=0.0 (full left)');
  await delay(400);

  // Full right (left channel at 0.0, right at 1.0)
  SoLoud.instance.setPanAbsolute(handle, 0, 1);
  strBuf.writeln('Set absolute pan: left=0.0, right=1.0 (full right)');
  await delay(400);

  // Balanced but quieter (both at 0.5)
  SoLoud.instance.setPanAbsolute(handle, 0.5, 0.5);
  strBuf.writeln('Set absolute pan: left=0.5, right=0.5 (balanced quiet)');
  await delay(400);

  // Back to center using regular setPan
  SoLoud.instance.setPan(handle, 0);
  strBuf.writeln('Reset to center using setPan(0.0)');
  await delay(200);

  // Test fadePan
  strBuf.writeln('Testing fadePan from center to full left over 400ms');
  SoLoud.instance.fadePan(
    handle,
    -1, // Full left
    const Duration(milliseconds: 400),
  );

  await delay(200);
  strBuf.writeln('Mid-fade...');

  await delay(250);
  final pannedLeft = SoLoud.instance.getPan(handle);
  strBuf
    ..writeln('After fade to left, pan: $pannedLeft')

    // Test fade to right
    ..writeln('Fading pan to full right over 400ms');
  SoLoud.instance.fadePan(
    handle,
    1, // Full right
    const Duration(milliseconds: 400),
  );

  await delay(500);
  final pannedRight = SoLoud.instance.getPan(handle);
  strBuf
    ..writeln('After fade to right, pan: $pannedRight')

    // Test oscillatePan
    ..writeln('Testing oscillatePan between -0.8 and 0.8 over 800ms');
  SoLoud.instance.oscillatePan(
    handle,
    -0.8,
    0.8,
    const Duration(milliseconds: 800),
  );

  // Let it oscillate
  await delay(200);
  final pan1 = SoLoud.instance.getPan(handle);
  strBuf.writeln('Pan during oscillation (200ms): $pan1');

  await delay(200);
  final pan2 = SoLoud.instance.getPan(handle);
  strBuf.writeln('Pan during oscillation (400ms): $pan2');

  await delay(200);
  final pan3 = SoLoud.instance.getPan(handle);
  strBuf.writeln('Pan during oscillation (600ms): $pan3');

  await delay(300);

  // Cleanup
  await SoLoud.instance.stop(handle);
  deinit();

  strBuf.writeln('Advanced pan tests completed successfully');
  return strBuf;
}

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test waveform controls including different waveform types,
/// frequency changes, scale, detune, and super wave.
Future<OutputBuffer> testWaveformControls() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Test all waveform types
  for (final waveform in WaveForm.values) {
    strBuf.writeln('Testing waveform: ${waveform.name}');

    // Load waveform with super wave enabled
    final sound = await SoLoud.instance.loadWaveform(
      waveform,
      true, // super wave
      1, // scale
      0.5, // detune
    );

    // Play the waveform
    final handle = SoLoud.instance.play(sound, volume: 0.3);
    strBuf.writeln('  Playing ${waveform.name} with super wave');

    await delay(200);

    // Test setWaveformFreq
    SoLoud.instance.setWaveformFreq(sound, 440);
    strBuf.writeln('  Set frequency to 440Hz');

    // Test changing frequency multiple times
    for (var freq = 200.0; freq <= 800.0; freq += 200.0) {
      SoLoud.instance.setWaveformFreq(sound, freq);
      await delay(100);
    }
    strBuf.writeln('  Frequency sweep completed');

    // Test super wave parameters
    SoLoud.instance.setWaveformSuperWave(sound, true);
    SoLoud.instance.setWaveformScale(sound, 2);
    SoLoud.instance.setWaveformDetune(sound, 0.8);
    strBuf.writeln('  Set super wave params: scale=2.0, detune=0.8');

    await delay(200);

    // Disable super wave
    SoLoud.instance.setWaveformSuperWave(sound, false);
    strBuf.writeln('  Disabled super wave');

    await delay(100);

    // Stop and cleanup
    await SoLoud.instance.stop(handle);
    await SoLoud.instance.disposeSource(sound);
  }

  // Test setWaveform to switch waveform type
  final sound = await SoLoud.instance.loadWaveform(
    WaveForm.sin,
    false,
    1,
    0,
  );

  SoLoud.instance.play(sound, volume: 0.3);
  await delay(200);

  // Switch through different waveforms
  final waveformsToTest = [
    WaveForm.square,
    WaveForm.saw,
    WaveForm.triangle,
    WaveForm.bounce,
  ];

  for (final newWaveform in waveformsToTest) {
    SoLoud.instance.setWaveform(sound, newWaveform);
    strBuf.writeln('Switched to ${newWaveform.name}');
    await delay(200);
  }

  await SoLoud.instance.disposeSource(sound);

  deinit();

  strBuf.writeln('Waveform controls tests completed successfully');
  return strBuf;
}

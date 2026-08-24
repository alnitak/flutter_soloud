import 'dart:typed_data' show Float32List;

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

double sumList(Float32List data) {
  var sum = 0.0;
  for (final value in data) {
    sum += value;
  }
  return sum;
}

/// Test audio visualization including FFT data, wave data,
/// multi-channel, and FFT smoothing via the audioVisualizationEvents stream.
Future<OutputBuffer> testVisualization() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Test FFT smoothing
  SoLoud.instance.setFftSmoothing(0.7);
  strBuf.writeln('FFT smoothing set to 0.7');

  // Load and play a sound
  final sound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  SoLoud.instance.play(sound, volume: 0.5);

  // Wait for playback to start
  await delay(1000);

  // 1. Test Wave data only (window size 256)
  strBuf.writeln('------- Wave data (window size 256) -------');
  SoLoud.instance.setVisualizationEnabled(
    true,
    kind: VisualizationKind.wave,
  );
  assert(
    SoLoud.instance.getVisualizationEnabled(),
    'Visualization should be enabled',
  );

  AudioVisualizationData? wavePacket;
  final waveSub = SoLoud.instance.audioVisualizationEvents.listen((data) {
    wavePacket = data;
  });

  await delay(300);
  await waveSub.cancel();

  assert(wavePacket != null, 'Should have received wave visualization packet');
  assert(wavePacket!.wave.isNotEmpty, 'Wave list should not be empty');
  assert(
    wavePacket!.wave.first.length == 256,
    'Wave data should have 256 samples',
  );
  assert(wavePacket!.fft.isEmpty, 'FFT data should be empty in wave-only mode');
  strBuf
    ..writeln('Wave samples count: ${wavePacket!.wave.first.length}')
    ..writeln('Wave sample [0-10]: ${wavePacket!.wave.first.sublist(0, 10)}')
    ..writeln('------- FFT data (window size 512) -------');

  // 2. Test FFT data only (window size 512)
  SoLoud.instance.setVisualizationEnabled(
    true,
    windowSize: 512,
    kind: VisualizationKind.fft,
  );

  AudioVisualizationData? fftPacket;
  final fftSub = SoLoud.instance.audioVisualizationEvents.listen((data) {
    fftPacket = data;
  });

  await delay(300);
  await fftSub.cancel();

  assert(fftPacket != null, 'Should have received FFT visualization packet');
  assert(fftPacket!.fft.isNotEmpty, 'FFT list should not be empty');
  assert(
    fftPacket!.fft.first.length == 256,
    'FFT data should have windowSize / 2 (256) bins',
  );
  assert(fftPacket!.wave.isEmpty, 'Wave data should be empty in FFT-only mode');
  strBuf
    ..writeln('FFT bins count: ${fftPacket!.fft.first.length}')
    ..writeln('FFT bins [0-10]: ${fftPacket!.fft.first.sublist(0, 10)}')
    ..writeln('------- Wave + FFT (all channels, window size 256) -------');

  // 3. Test Wave + FFT data with all channels
  SoLoud.instance.setVisualizationEnabled(
    true,
    channel: VisualizationChannel.all,
  );

  AudioVisualizationData? combinedPacket;
  final combinedSub = SoLoud.instance.audioVisualizationEvents.listen((data) {
    combinedPacket = data;
  });

  await delay(300);
  await combinedSub.cancel();

  assert(
    combinedPacket != null,
    'Should have received combined visualization packet',
  );
  assert(
    combinedPacket!.wave.isNotEmpty,
    'Combined wave list should not be empty',
  );
  assert(
    combinedPacket!.fft.isNotEmpty,
    'Combined FFT list should not be empty',
  );
  assert(
    combinedPacket!.wave.first.length == 256,
    'Combined wave data should have 256 samples',
  );
  assert(
    combinedPacket!.fft.first.length == 128,
    'Combined FFT data should have 128 bins',
  );
  strBuf
    ..writeln('Active channels count: ${combinedPacket!.channelCount}')
    ..writeln('Wave channel count: ${combinedPacket!.wave.length}')
    ..writeln('FFT channel count: ${combinedPacket!.fft.length}');

  // Disable visualization
  SoLoud.instance.setVisualizationEnabled(false);
  assert(
    !SoLoud.instance.getVisualizationEnabled(),
    'Visualization should be disabled',
  );

  deinit();

  strBuf.writeln('Visualization tests completed successfully');
  return strBuf;
}

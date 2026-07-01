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
/// and FFT smoothing.
Future<OutputBuffer> testVisualization() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Enable visualization
  SoLoud.instance.setVisualizationEnabled(true);
  final isEnabled = SoLoud.instance.getVisualizationEnabled();
  assert(isEnabled, 'setVisualizationEnabled/getVisualizationEnabled failed');
  strBuf.writeln('Visualization enabled: $isEnabled');

  // Test FFT smoothing
  SoLoud.instance.setFftSmoothing(0.7);
  strBuf.writeln('FFT smoothing set to 0.7');

  // Load and play a sound
  final sound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  SoLoud.instance.play(sound, volume: 0.5);

  // Wait for playback to start
  await delay(3000);

  // Test wave data only
  strBuf.writeln('------- Wave data -------');
  final waveData = AudioData(GetSamplesKind.wave)..updateSamples();
  final waveSamples = waveData.getAudioData();
  assert(waveSamples.length == 256, 'Wave data should have 256 samples');
  assert(
    sumList(waveSamples) != 0,
    'Wave samples have 0 value',
  );
  strBuf
    ..writeln('Wave samples count: ${waveSamples.length}')
    ..writeln('Wave sample [0-10]: ${waveSamples.sublist(0, 10)}');
  waveData.dispose();

  // wait a bit for new FFT data to be generated
  await delay(300);

  // Test linear data (FFT + Wave)
  strBuf.writeln('------- Linear data -------');
  final linearData = AudioData(GetSamplesKind.linear)..updateSamples();
  final linearSamples = linearData.getAudioData();
  assert(linearSamples.length == 512, 'Linear data should have 512 samples');
  assert(
    sumList(linearSamples) != 0,
    'Linear samples have 0 value',
  );
  var fftData = linearSamples.sublist(0, 256);
  var linearWaveData = linearSamples.sublist(256, 512);
  strBuf
    ..writeln('Linear samples count: ${linearSamples.length}')
    ..writeln('FFT data [0-10]: ${fftData.sublist(0, 10)}')
    ..writeln('Linear wave data [0-10]: ${linearWaveData.sublist(0, 10)}');
  linearData.dispose();

  // wait a bit for new FFT data to be generated
  await delay(300);

  // Test texture data (2D matrix)
  strBuf.writeln('------- Texture 2D data -------');
  final textureData = AudioData(GetSamplesKind.texture)..updateSamples();
  final textureSamples = textureData.getAudioData();
  assert(
    textureSamples.length == 512 * 256,
    'Linear data should have 512 * 256 samples',
  );
  assert(
    sumList(textureSamples) != 0,
    'Texture samples have 0 value',
  );
  fftData = textureSamples.sublist(0, 256);
  linearWaveData = textureSamples.sublist(256, 512);
  strBuf
    ..writeln(
      'Linear samples count: ${textureSamples.length} (512 * 256 samples)',
    )
    ..writeln('FFT data [0-10]: ${fftData.sublist(0, 10)}')
    ..writeln('Linear wave data [0-10]: ${linearWaveData.sublist(0, 10)}');
  textureData.dispose();

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

import 'dart:typed_data';

/// Audio visualization data produced by the SoLoud audio mixer.
///
/// Contains time-domain waveform samples and/or frequency-domain FFT bins
/// for one or more audio channels.
class AudioVisualizationData {
  /// Creates a visualization data packet.
  const AudioVisualizationData({
    required this.channelCount,
    required this.wave,
    required this.fft,
  });

  /// The number of channels included in this visualization packet.
  final int channelCount;

  /// Time-domain waveform data for each channel.
  ///
  /// If wave visualization is disabled, this list is empty.
  /// For mono/merged visualization, this list contains 1 [Float32List] with
  /// length equal to the chosen `windowSize` (values in range `[-1.0, 1.0]`).
  final List<Float32List> wave;

  /// Frequency-domain FFT magnitude data for each channel.
  ///
  /// If FFT visualization is disabled, this list is empty.
  /// For mono/merged visualization, this list contains 1 [Float32List] with
  /// length equal to `windowSize / 2` (values in range `[0.0, 1.0]`).
  final List<Float32List> fft;

  /// Convenience getter to obtain the first (or merged) channel's wave data.
  ///
  /// Returns `null` if [wave] is empty.
  Float32List? get waveData => wave.isNotEmpty ? wave.first : null;

  /// Convenience getter to obtain the first (or merged) channel's FFT data.
  ///
  /// Returns `null` if [fft] is empty.
  Float32List? get fftData => fft.isNotEmpty ? fft.first : null;
}

A low-level audio plugin for Flutter.

[![Pub Version](https://img.shields.io/pub/v/flutter_soloud?logo=dart)](https://pub.dev/packages/flutter_soloud)
[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

|Linux|Windows|Android|MacOS|iOS|web|
|:-:|:-:|:-:|:-:|:-:|:-:|
|💙|💙|💙|💙|💙|💙|

## Overview

A high-performance audio plugin designed primarily for games and immersive applications, providing low latency and advanced features.

## Key Features

- ⚡ Low latency, high performance audio
- 🎮 3D positional audio with Doppler effect
- 🔄 Gapless looping and streaming
- 📊 Get audio wave and/or FFT audio data in real-time (useful for visualization)
- 🎛️ Rich effects system (reverb, echo, filters, etc.)
- 🌊 Waveform generation and visualization
- 🔊 Multiple voices, playing different or even the same sound multiple times
- 🎵 Support for MP3, WAV, OGG, and FLAC
- 🔄 Stream audio from given audio data with buffering support for PCM and Opus
- 🌊 Generate waveforms in real-time with various types (sine, square, saw, triangle, etc.)

## Documentation

- [Full Documentation](https://docs.page/alnitak/flutter_soloud)
- [API Reference](https://pub.dev/documentation/flutter_soloud/latest/)

## Simple Example

```dart
void example() async {
  final soloud = SoLoud.instance;
  await soloud.init();

  final source = await soloud.loadAsset('assets/sound.mp3');
  final handle = await soloud.play(source);
  
  // Later...
  await soloud.stop(handle);
  await soloud.disposeSource(source);  
}
```

## License

The Dart plugin is covered by the MIT license. For information about the underlying SoLoud engine license, see the [documentation](https://docs.page/alnitak/flutter_soloud/get_started/license).


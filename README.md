A low-level audio plugin for Flutter.

[![Pub Version](https://img.shields.io/pub/v/flutter_soloud?logo=dart)](https://pub.dev/packages/flutter_soloud)
[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

||Linux|Windows|Android|MacOS|iOS|Web|
|-|:-:|:-:|:-:|:-:|:-:|:-:|
|Support|💙|💙|💙|💙|💙|💙|
|Minimum Version|Any|Any|21|10.15|13.0|Any|

## Overview

A high-performance audio plugin designed primarily for games and immersive applications, providing low latency and advanced features.

## Key Features

- ⚡ Low latency, high performance audio
- 🎮 3D positional audio with Doppler effect
- 🔄 Gapless looping
- 🔄 Stream audio with auto-pause for buffering, support for PCM, MP3, Ogg with Opus, Vorbis and FLAC containers
- 📊 Get audio wave and/or FFT audio data in real-time (useful for visualization)
- 🎛️ Rich effects system (reverb, echo, limiter, bassboost, etc.)
- ⚙️ Faders for attributes (e.g. fade out for 2 seconds, then stop)
- 🎚️ Oscillators for attributes
- 🌊 Waveform generation and visualization
- 🔊 Multiple voices, playing different or even the same sound multiple times
- 🎵 Support for MP3, WAV, OGG, and FLAC
- ⏱️ Read audio data samples from a file with a given time range
- 🌊 Generate waveforms in real-time with various types (sine, square, saw, triangle, etc.)

## Getting Started
- Watch Flutter [Package of the Week](https://www.youtube.com/watch?v=2t6Bt04EyLw) video.
- Read CodeLab [Add sound and music to your Flutter game](https://codelabs.developers.google.com/codelabs/flutter-codelab-soloud#0).

If you are looking for a package to visualize audio using shaders or CustomPainter, please check out [audio_flux](https://pub.dev/packages/audio_flux). It uses this plugin for output and [flutter_recorder](https://pub.dev/packages/flutter_recorder) for input.

## Documentation

- [Full Documentation](https://docs.page/alnitak/flutter_soloud_docs)
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

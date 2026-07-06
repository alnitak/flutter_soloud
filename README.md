A low-level audio plugin for Flutter.

[![Pub Version](https://img.shields.io/pub/v/flutter_soloud?logo=dart)](https://pub.dev/packages/flutter_soloud)
[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

||Linux|Windows|Android|macOS|iOS|Web|
|-|:-:|:-:|:-:|:-:|:-:|:-:|
|Support|💙|💙|💙|💙|💙|💙|
|Minimum Version|Any|Any|21+|10.15+|13.0+|iOS 16.4+<br>Safari 16.4+<br>Chrome 91+<br>Edge 91+<br>Firefox 89+|

## Overview

A high-performance audio plugin designed primarily for games and immersive applications, providing low latency and advanced features.

## Key Features

- ⚡ Low latency, high performance audio
- 🎮 3D positional audio with Doppler effect
- 🔄 Gapless looping
- 🔄 Stream audio with auto-pause for buffering, support for PCM, MP3, WAV, Ogg with Opus, Vorbis and FLAC containers
- 📊 Get audio wave and/or FFT audio data in real-time (useful for visualization)
- 🎛️ Rich effects system (reverb, echo, limiter, equalizer, pitch shift, etc.)
- ⚙️ Faders for attributes (e.g. fade out for 2 seconds, then stop)
- 🎚️ Oscillators for attributes
- 🌊 Waveform generation and visualization
- 🔊 Multiple voices, playing different or even the same sound multiple times
- 🎵 Support for MP3, WAV, OGG, and FLAC
- 🔴 Capture the master mixer output as a stream for recording, processing, or streaming (with different PCM formats and Opus, Vorbis, FLAC, WAV encoded stream formats)
- ⏱️ Read audio data samples from a file with a given time range
- 🌊 Generate waveforms in real-time with various types (sine, square, saw, triangle, etc.)

## Getting Started
- Watch the Flutter [Package of the Week](https://www.youtube.com/watch?v=2t6Bt04EyLw) video.
- Especially for web use, please look at the [setup guide docs](https://docs.page/alnitak/flutter_soloud_docs/get_started/setup).

If you are looking for a package to visualize audio using shaders or CustomPainter, please check out [audio_flux](https://pub.dev/packages/audio_flux). It uses this plugin for output and [flutter_recorder](https://pub.dev/packages/flutter_recorder) for input.

Also, if you are building using Swift Package Manager (SPM), please check out [iOS and macOS Configuration](https://docs.page/alnitak/flutter_soloud_docs/get_started/setup#ios-and-macos-configuration).

## Documentation

- [Full Documentation](https://docs.page/alnitak/flutter_soloud_docs)
- [API Reference](https://pub.dev/documentation/flutter_soloud/latest/)

## Simple Example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

void example() async {
  final soloud = SoLoud.instance;
  await soloud.init();

  await soloud.playSource(asset: 'assets/sound.mp3');
  // or
  final sound = await soloud.loadAsset('assets/sound.mp3');
  final handle = soloud.play(sound);
  
  [...]

  soloud.deinit();
}
```

## Apps & Games Using flutter_soloud

A showcase of apps and games built with this plugin:

| App/Game | Developer | Description |
|----------|-----------|-------------|
| GPhil</br>[web](https://app.g-phil.app/) [macOS](https://apps.apple.com/it/app/gphil/id6740543718) [Windows](https://apps.microsoft.com/detail/9pkkz2p2dldg?ocid=webpdpshare)| Vyacheslav Gryaznov | Innovative app designed for musicians to play instrumental concertos with flexible virtual orchestral accompaniment.|
| [Forcebar](https://forcebar.xyz) | Doug Todd | Forcebar is a pure reflex game. |
| [RadioVisualizer](https://radiovisualizer.com) | Marco Bavagnoli | Stream over 35,000 live radio stations from every corner of the globe. |
| Stellar Bastion</br>[web](https://www.crazygames.com/game/stellar-bastion) [Android](https://play.google.com/store/apps/details?id=com.coconutisland.stellar_bastion) [iOS](https://apps.apple.com/us/app/stellar-bastion/id6761073618) | Coconut Island Apps | 2D Tower Defense game. |
| Mortigen</br>[web](https://koldo92.github.io/mortigen/) [Android](https://play.google.com/store/apps/details?id=com.ler.mortigen) [iOS](https://apps.apple.com/us/app/mortigen/id6761758806) | Luis Enrique Ruiz | Roguelite survival shooter. |

*Want to add your app? Feel free to open a PR!*

## License

The Dart plugin is covered by the MIT license. For information about the underlying SoLoud engine license, see the [documentation](https://docs.page/alnitak/flutter_soloud/get_started/license).

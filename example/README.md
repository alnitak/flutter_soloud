---
title: Sample Projects
description: Learn from example implementations using flutter_soloud
---

## Overview

The flutter_soloud package includes several example projects demonstrating various features. These examples can be found in the [example folder](https://github.com/alnitak/flutter_soloud/tree/main/example) of the plugin.

## Basic Examples

### Getting Started
- `lib/main.dart` - Basic setup and usage example
- `lib/output_device/output_device.dart` - How to list and select output devices

### Audio Visualization
- `lib/audio_data/audio_data.dart` - Visualize audio using `AudioData`
- `lib/wave_data/wave_data.dart` - Read and display audio samples from files

## Advanced Features

### Audio Generation
- `lib/waveform/waveform.dart` - Generate and control waveforms in real-time
- `lib/metronome/metronome.dart` - Create a precise metronome

### Streaming Examples
- `lib/buffer_stream/generate.dart` - Generate PCM audio in an `Isolate`
- `lib/buffer_stream/websocket.dart` - Stream PCM and Opus audio via WebSocket

### Audio Effects
| Example | Description |
|---------|-------------|
| `lib/filters/compressor.dart` | Dynamic range compression |
| `lib/filters/limiter.dart` | Peak limiting and volume control |
| `lib/filters/pitchshift.dart` | Real-time pitch shifting |

## Running the Examples

1. Clone the repository:
```bash
git clone https://github.com/alnitak/flutter_soloud.git
```

2. Navigate to the example directory:
```bash
cd flutter_soloud/example
```

3. Install dependencies:
```bash
flutter pub get
```

4. Run a specific example:
```bash
flutter run -t lib/main.dart
# Or any other example file
flutter run -t lib/waveform/waveform.dart
```

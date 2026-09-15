---
title: Sample Projects
description: Learn from example implementations using flutter_soloud
---

## Overview

The flutter_soloud package includes several example projects demonstrating various features. These examples can be found in the [example folder](https://github.com/alnitak/flutter_soloud/tree/main/example) of the plugin.

## Basic Examples

### Getting Started
- `lib/main.dart` - Basic setup and audio playback example
- `lib/output_device/output_device.dart` - How to list, select, and switch audio output devices

### Audio Visualization
- `lib/audio_data/audio_data.dart` - Real-time audio waveform and FFT visualization using `AudioVisualizationData` stream
- `lib/wave_data/wave_data.dart` - Read and display audio waveform samples from files

## Advanced Features

### Audio Context & Background Playback
- `lib/audio_context/audio_context.dart` - How to use `audio_session` and `audio_service` for background audio, notifications, and ducking

### Audio Generation & Scheduling
- `lib/waveform/waveform.dart` - Generate and control real-time synthesizer waveforms
- `lib/metronome/metronome.dart` - Create a sample-accurate metronome using `playClocked()`

### Streaming Examples
#### Push Buffer Streaming (Released & Preserved Modes)
- `lib/buffer_stream/generate.dart` - Procedural PCM audio generation in a worker `Isolate`
- `lib/buffer_stream/websocket.dart` - Stream PCM and Opus audio in real-time via WebSocket
- `lib/buffer_stream/web_radio.dart` - Receive and play live streaming audio from online web radio
- `lib/buffer_stream/simple_noise_stream.dart` - Interactive testing tool for push buffer streaming and underrun handling

#### Pull Buffer Streaming
- `lib/pull_buffer/file_stream.dart` - Callback-driven on-demand pull model with custom seek/progress bar
- `lib/pull_buffer/http_range_stream.dart` - Stream remote audio using HTTP Range requests with seek support

### Mixing Buses
- `lib/mixing_bus/mixing_bus.dart` - Route multiple sounds through sub-mix buses with collective volume and bus filters

### Mixer Output Capture
- `lib/mixer_capture/mixer_capture.dart` - Capture master mixer output as a stream of raw PCM or encoded WAV/FLAC/Opus audio
- `lib/mixer_capture/isolate_capture_test.dart` - Run master mixer output capture inside a separate worker isolate via `SoLoudIsolate`

### Audio Effects
| Example | Description |
|---------|-------------|
| `lib/filters/compressor.dart` | Dynamic range compression |
| `lib/filters/limiter.dart` | Peak limiting and volume control |
| `lib/filters/parametric_eq.dart` | Multi-band parametric equalizer |
| `lib/filters/pitchshift.dart` | Real-time pitch shifting and time stretching |

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

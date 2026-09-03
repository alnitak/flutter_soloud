# Live painter recipe (per-channel wave + FFT)

Adapted from `example/lib/audio_data/audio_data.dart` and `data_widget.dart`.
Setup used there:

```dart
await SoLoud.instance.init(bufferSize: 1024, channels: Channels.stereo);
SoLoud.instance.setVisualizationEnabled(
  true,
  windowSize: 512,
  channel: VisualizationChannel.all, // per-channel lists
);
SoLoud.instance.setFftSmoothing(0.8);
```

A widget that listens to `audioVisualizationEvents` and paints each channel:

```dart
import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

class AudioDataWidget extends StatefulWidget {
  const AudioDataWidget({super.key});

  @override
  State<AudioDataWidget> createState() => AudioDataWidgetState();
}

class AudioDataWidgetState extends State<AudioDataWidget> {
  StreamSubscription<AudioVisualizationData>? _subscription;
  AudioVisualizationData? _latestData;

  static const List<Color> _channelColors = [
    Colors.yellowAccent,
    Colors.greenAccent,
    Colors.cyanAccent,
    Colors.pinkAccent,
  ];

  @override
  void initState() {
    super.initState();
    _subscription = SoLoud.instance.audioVisualizationEvents.listen((data) {
      if (mounted) setState(() => _latestData = data);
    });
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final data = _latestData;
    final waveChannels = data?.wave ?? const <Float32List>[];
    final fftChannels = data?.fft ?? const <Float32List>[];

    return Column(
      children: [
        // Top: wave per channel
        Expanded(
          child: Row(
            children: [
              for (var i = 0; i < waveChannels.length; i++)
                Expanded(
                  child: CustomPaint(
                    painter: WavePainter(
                      waveData: waveChannels[i],
                      color: _channelColors[i % _channelColors.length],
                    ),
                    child: const SizedBox.expand(),
                  ),
                ),
            ],
          ),
        ),
        // Bottom: FFT per channel
        Expanded(
          child: Row(
            children: [
              for (var i = 0; i < fftChannels.length; i++)
                Expanded(
                  child: CustomPaint(
                    painter: FftPainter(
                      fftData: fftChannels[i],
                      color: _channelColors[i % _channelColors.length],
                    ),
                    child: const SizedBox.expand(),
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }
}

/// Time-domain waveform, one vertical line per sample around the center.
class WavePainter extends CustomPainter {
  const WavePainter({required this.waveData, required this.color});

  final Float32List? waveData;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final centerY = size.height / 2;
    final wave = waveData;
    if (wave == null || wave.isEmpty) return;

    final count = wave.length; // == windowSize
    final barWidth = size.width / count;
    final paint = Paint()
      ..color = color
      ..strokeWidth = (barWidth * 0.9).clamp(1.0, double.infinity)
      ..style = PaintingStyle.stroke;

    final halfHeight = size.height * 0.45;
    for (var i = 0; i < count; i++) {
      final x = barWidth * i + barWidth / 2;
      final y = (wave[i] * halfHeight) / 2; // wave is in [-1.0, 1.0]
      canvas.drawLine(Offset(x, centerY - y), Offset(x, centerY + y), paint);
    }
  }

  @override
  bool shouldRepaint(covariant WavePainter oldDelegate) => true;
}

/// FFT magnitudes as bars from the bottom.
class FftPainter extends CustomPainter {
  const FftPainter({required this.fftData, required this.color});

  final Float32List? fftData;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final fft = fftData;
    if (fft == null || fft.isEmpty) return;

    final count = fft.length; // == windowSize / 2
    final barWidth = size.width / count;
    final paint = Paint()
      ..color = color
      ..strokeWidth = (barWidth * 0.9).clamp(1.0, double.infinity)
      ..style = PaintingStyle.stroke;

    for (var i = 0; i < count; i++) {
      final x = barWidth * i + barWidth / 2;
      final barHeight = size.height * fft[i].clamp(0.0, 1.0);
      if (barHeight > 0) {
        canvas.drawLine(
          Offset(x, size.height),
          Offset(x, size.height - barHeight),
          paint,
        );
      }
    }
  }

  @override
  bool shouldRepaint(covariant FftPainter oldDelegate) => true;
}
```

Notes:

- `shouldRepaint => true` is intentional: the stream delivers a *new* `Float32List` instance per packet, but the values change every mixer buffer either way.
- Merged mode (`VisualizationChannel.merged`, the default) gives exactly one entry in `wave`/`fft`; the same painters work with `data.waveData` / `data.fftData`.
- Window size ↔ frequency resolution: 128 → 64 bins (fast, coarse), 256 → 128 bins (good default for 60 fps UIs), 512 → 256 bins (music visualizers), 1024+ for analysis tools.

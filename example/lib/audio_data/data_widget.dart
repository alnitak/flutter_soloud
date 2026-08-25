import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Visualizer for FFT and wave data using the audioVisualizationEvents stream.
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
    Colors.orangeAccent,
    Colors.purpleAccent,
  ];

  @override
  void initState() {
    super.initState();
    _subscription = SoLoud.instance.audioVisualizationEvents.listen((data) {
      if (mounted) {
        setState(() {
          _latestData = data;
        });
      }
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
    final waveChannels = data?.wave ?? const [];
    final fftChannels = data?.fft ?? const [];

    return LayoutBuilder(
      builder: (context, constraints) {
        return Container(
          width: constraints.maxWidth,
          height: constraints.maxHeight,
          padding: const EdgeInsets.all(8),
          color: Colors.black,
          child: Column(
            children: [
              // Upper row: Wave data per channel
              Expanded(
                child: Row(
                  children: [
                    if (waveChannels.isEmpty)
                      const Expanded(
                        child: CustomPaint(
                          painter: WavePainter(
                            waveData: null,
                            color: Colors.yellowAccent,
                          ),
                          child: SizedBox.expand(),
                        ),
                      )
                    else
                      for (var i = 0; i < waveChannels.length; i++) ...[
                        if (i > 0) const SizedBox(width: 8),
                        Expanded(
                          child: ClipRRect(
                            borderRadius: BorderRadius.circular(4),
                            child: ColoredBox(
                              color: const Color(0xFF111111),
                              child: CustomPaint(
                                painter: WavePainter(
                                  waveData: waveChannels[i],
                                  color:
                                      _channelColors[i % _channelColors.length],
                                ),
                                child: const SizedBox.expand(),
                              ),
                            ),
                          ),
                        ),
                      ],
                  ],
                ),
              ),
              const SizedBox(height: 8),
              // Bottom row: FFT data per channel
              Expanded(
                child: Row(
                  children: [
                    if (fftChannels.isEmpty)
                      const Expanded(
                        child: CustomPaint(
                          painter: FftPainter(
                            fftData: null,
                            color: Colors.yellowAccent,
                          ),
                          child: SizedBox.expand(),
                        ),
                      )
                    else
                      for (var i = 0; i < fftChannels.length; i++) ...[
                        if (i > 0) const SizedBox(width: 8),
                        Expanded(
                          child: ClipRRect(
                            borderRadius: BorderRadius.circular(4),
                            child: ColoredBox(
                              color: const Color(0xFF111111),
                              child: CustomPaint(
                                painter: FftPainter(
                                  fftData: fftChannels[i],
                                  color:
                                      _channelColors[i % _channelColors.length],
                                ),
                                child: const SizedBox.expand(),
                              ),
                            ),
                          ),
                        ),
                      ],
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// Custom painter to draw time-domain waveform data.
class WavePainter extends CustomPainter {
  const WavePainter({
    required this.waveData,
    required this.color,
  });

  final Float32List? waveData;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final centerY = size.height / 2;

    // Draw zero-line center reference
    final centerLinePaint = Paint()
      ..color = Colors.white24
      ..strokeWidth = 1.0;
    canvas.drawLine(
      Offset(0, centerY),
      Offset(size.width, centerY),
      centerLinePaint,
    );

    final wave = waveData;
    if (wave == null || wave.isEmpty) {
      return;
    }

    final count = wave.length;
    final barWidth = size.width / count;
    final strokeW = (barWidth * 0.9).clamp(1.0, double.infinity);
    final paint = Paint()
      ..color = color
      ..strokeWidth = strokeW
      ..style = PaintingStyle.stroke;

    final halfHeight = size.height * 0.45;

    for (var i = 0; i < count; i++) {
      final waveVal = wave[i];
      final x = barWidth * i + barWidth / 2;
      final y = (waveVal * halfHeight) / 2;

      canvas.drawLine(
        Offset(x, centerY - y),
        Offset(x, centerY + y),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(covariant WavePainter oldDelegate) {
    return true;
  }
}

/// Custom painter to draw frequency-domain FFT magnitude spectrum.
class FftPainter extends CustomPainter {
  const FftPainter({
    required this.fftData,
    required this.color,
  });

  final Float32List? fftData;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    // Draw baseline
    final baseLinePaint = Paint()
      ..color = Colors.white24
      ..strokeWidth = 1.0;
    canvas.drawLine(
      Offset(0, size.height - 1),
      Offset(size.width, size.height - 1),
      baseLinePaint,
    );

    final fft = fftData;
    if (fft == null || fft.isEmpty) {
      return;
    }

    final count = fft.length;
    final barWidth = size.width / count;
    final strokeW = (barWidth * 0.9).clamp(1.0, double.infinity);
    final paint = Paint()
      ..color = color
      ..strokeWidth = strokeW
      ..style = PaintingStyle.stroke;

    for (var i = 0; i < count; i++) {
      final fftVal = fft[i];
      final x = barWidth * i + barWidth / 2;
      final barHeight = size.height * fftVal.clamp(0.0, 1.0);

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
  bool shouldRepaint(covariant FftPainter oldDelegate) {
    return true;
  }
}

// ignore_for_file: public_member_api_docs, sort_constructors_first
import 'package:flutter/material.dart';

import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/audio_data_extensions.dart';

/// Draw the audio FFT data
///
class BarsFftWidget extends StatelessWidget {
  const BarsFftWidget({
    super.key,
    required this.audioData,
    required this.minFreq,
    required this.maxFreq,
    required this.width,
    required this.height,
  });

  final AudioData audioData;
  final int minFreq;
  final int maxFreq;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    if (audioData.getSamplesKind == GetSamplesKind.wave) {
      return const SizedBox.shrink();
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        ColoredBox(
          color: Colors.black,
          child: RepaintBoundary(
            child: ClipRRect(
              child: CustomPaint(
                size: Size(width, height),
                painter: FftPainter(
                  audioData: audioData,
                  minFreq: minFreq,
                  maxFreq: maxFreq,
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

/// Custom painter to draw the wave in a circle
///
class FftPainter extends CustomPainter {
  const FftPainter({
    required this.audioData,
    required this.minFreq,
    required this.maxFreq,
  });
  final AudioData audioData;
  final int minFreq;
  final int maxFreq;

  @override
  void paint(Canvas canvas, Size size) {
    final barWidth = size.width / (maxFreq - minFreq);
    final paint = Paint()
      ..color = Colors.yellow
      ..strokeWidth = barWidth * 0.8
      ..style = PaintingStyle.stroke;

    for (var i = minFreq; i <= maxFreq; i++) {
      late final double barHeight;
      try {
        barHeight = size.height * audioData.getLinearFft(SampleLinear(i));
      } on Exception {
        barHeight = 0;
      }
      canvas.drawRect(
        Rect.fromLTWH(
          barWidth * (i - minFreq),
          size.height - barHeight,
          barWidth,
          barHeight,
        ),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) {
    return true;
  }
}

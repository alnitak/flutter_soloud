// ignore_for_file: public_member_api_docs
import 'package:flutter/material.dart';

import 'package:flutter_soloud/flutter_soloud.dart';

/// Draw the audio FFT data
///
class BarsFftWidget extends StatelessWidget {
  const BarsFftWidget({
    required this.audioData,
    required this.minFreq,
    required this.maxFreq,
    required this.width,
    required this.height,
    super.key,
  });

  final AudioData audioData;
  final int minFreq;
  final int maxFreq;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    if (audioData.getSamplesKind == GetSamplesKind.wave) {
      return const Placeholder();
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
    final barWidth = size.width / (maxFreq - minFreq).clamp(0, 255);
    final paint = Paint()
      ..color = Colors.yellow
      ..strokeWidth = barWidth * 0.8
      ..style = PaintingStyle.stroke;

    for (var i = minFreq; i <= maxFreq.clamp(0, 255); i++) {
      late final double barHeight;
      try {
        final double data;
        if (audioData.getSamplesKind == GetSamplesKind.linear) {
          data = audioData.getLinearFft(SampleLinear(i));
        } else {
          data = audioData.getTexture(SampleRow(0), SampleColumn(i));
        }
        barHeight = size.height * data;
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

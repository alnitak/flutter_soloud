import 'dart:math' as math;
import 'package:flutter/material.dart';

/// A graph widget that visualizes the frequency response curves of the
/// parametric equalizer bands.
class EqGraph extends StatelessWidget {
  const EqGraph({
    required this.bandsNumber,
    required this.gains,
    required this.freqs,
    required this.qFactors,
    this.minFreq = 20.0,
    this.maxFreq = 20000.0,
    this.minGain = 0.0,
    this.maxGain = 4.0,
    this.height = 200.0,
    super.key,
  });

  final ValueNotifier<int> bandsNumber;
  final List<ValueNotifier<double>> gains;
  final List<ValueNotifier<double>> freqs;
  final List<ValueNotifier<double>> qFactors;
  final double minFreq;
  final double maxFreq;
  final double minGain;
  final double maxGain;
  final double height;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: Listenable.merge([
        bandsNumber,
        ...gains,
        ...freqs,
        ...qFactors,
      ]),
      builder: (context, _) {
        final activeCount = bandsNumber.value.clamp(0, gains.length);
        final bandList = List.generate(
          activeCount,
          (i) => _BandData(
            gain: gains[i].value,
            freq: freqs[i].value,
            q: qFactors[i].value,
          ),
        );

        return Container(
          width: double.infinity,
          height: height,
          decoration: BoxDecoration(
            color: Colors.black,
            borderRadius: BorderRadius.circular(8),
            border: Border.all(color: Colors.white24),
          ),
          clipBehavior: Clip.antiAlias,
          child: CustomPaint(
            size: Size(double.infinity, height),
            painter: _EqGraphPainter(
              bands: bandList,
              minFreq: minFreq,
              maxFreq: maxFreq,
              minGain: minGain,
              maxGain: maxGain,
            ),
          ),
        );
      },
    );
  }
}

class _BandData {
  const _BandData({
    required this.gain,
    required this.freq,
    required this.q,
  });

  final double gain;
  final double freq;
  final double q;
}

class _EqGraphPainter extends CustomPainter {
  _EqGraphPainter({
    required this.bands,
    required this.minFreq,
    required this.maxFreq,
    required this.minGain,
    required this.maxGain,
  })  : logMinF = math.log(minFreq),
        logMaxF = math.log(maxFreq);

  final List<_BandData> bands;
  final double minFreq;
  final double maxFreq;
  final double minGain;
  final double maxGain;
  final double logMinF;
  final double logMaxF;

  // Distinct vibrant color palette for each band
  static Color getBandColor(int index) {
    // Golden angle distribution for high contrast between consecutive bands
    final hue = (index * 137.508) % 360;
    return HSLColor.fromAHSL(1, hue, 0.90, 0.60).toColor();
  }

  double _freqToX(double f, double width) {
    final logF = math.log(f.clamp(minFreq, maxFreq));
    return ((logF - logMinF) / (logMaxF - logMinF)) * width;
  }

  double _xToFreq(double x, double width) {
    final t = (x / width).clamp(0.0, 1.0);
    return math.exp(logMinF + t * (logMaxF - logMinF));
  }

  double _gainToY(double g, double height) {
    // Inverted Y: maxGain at top (y=0), minGain at bottom (y=height)
    final clamped = g.clamp(minGain, maxGain);
    return (1.0 - (clamped - minGain) / (maxGain - minGain)) * height;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    final h = size.height;

    // 1. Draw Grid Lines and Labels
    final gridPaint = Paint()
      ..color = Colors.white12
      ..strokeWidth = 1.0;

    final unityPaint = Paint()
      ..color = Colors.white38
      ..strokeWidth = 1.0
      ..style = PaintingStyle.stroke;

    final textPainter = TextPainter(
      textDirection: TextDirection.ltr,
    );

    // Vertical frequency grid markers
    const freqMarkers = [
      30.0,
      60.0,
      120.0,
      250.0,
      500.0,
      1000.0,
      2000.0,
      4000.0,
      8000.0,
      12000.0,
      16000.0,
    ];
    for (final f in freqMarkers) {
      if (f < minFreq || f > maxFreq) continue;
      final x = _freqToX(f, w);
      canvas.drawLine(Offset(x, 0), Offset(x, h), gridPaint);

      // Label
      final label = f >= 1000
          ? '${(f / 1000).toStringAsFixed(f % 1000 == 0 ? 0 : 1)}k'
          : '${f.toInt()}';
      textPainter
        ..text = TextSpan(
          text: label,
          style: const TextStyle(color: Colors.white30, fontSize: 9),
        )
        ..layout()
        ..paint(canvas, Offset(x + 2, h - 14));
    }

    // Horizontal gain grid lines
    const gainMarkers = [0.0, 0.5, 1.0, 2.0, 3.0, 4.0];
    for (final g in gainMarkers) {
      final y = _gainToY(g, h);
      final paint = (g == 1.0) ? unityPaint : gridPaint;
      canvas.drawLine(Offset(0, y), Offset(w, y), paint);

      // Label
      final label = g == 1.0 ? '0 dB (1x)' : '${g.toStringAsFixed(1)}x';
      textPainter
        ..text = TextSpan(
          text: label,
          style: TextStyle(
            color: g == 1.0 ? Colors.white60 : Colors.white24,
            fontSize: 8,
            fontWeight: g == 1.0 ? FontWeight.bold : FontWeight.normal,
          ),
        )
        ..layout()
        ..paint(canvas, Offset(4, y - 10));
    }

    if (bands.isEmpty) return;

    // 2. Precompute pre-constants for active bands
    final invTwoSigmaSq = List<double>.filled(bands.length, 0);
    final logFcs = List<double>.filled(bands.length, 0);
    for (var b = 0; b < bands.length; b++) {
      final q = bands[b].q.clamp(0.1, 20.0);
      invTwoSigmaSq[b] = 4.16277 * q * q;
      logFcs[b] = math.log(bands[b].freq.clamp(minFreq, maxFreq));
    }

    // 3. Draw individual band bell curves
    const steps = 240;
    final stepX = w / steps;

    for (var b = 0; b < bands.length; b++) {
      final band = bands[b];
      final color = getBandColor(b);
      final curvePaint = Paint()
        ..color = color.withValues(alpha: 0.85)
        ..strokeWidth = 2.0
        ..style = PaintingStyle.stroke;

      final path = Path();
      var started = false;

      for (var s = 0; s <= steps; s++) {
        final x = s * stepX;
        final f = _xToFreq(x, w);
        final logF = math.log(f);

        final d = logF - logFcs[b];
        final exponent = d * d * invTwoSigmaSq[b];
        final weight = exponent > 9.0 ? 0.0 : math.exp(-exponent);
        var bandGain = 1.0 + (band.gain - 1.0) * weight;
        if (bandGain < 0.0) bandGain = 0.0;

        final y = _gainToY(bandGain, h);
        if (!started) {
          path.moveTo(x, y);
          started = true;
        } else {
          path.lineTo(x, y);
        }
      }

      canvas
        ..drawPath(path, curvePaint)
        ..drawCircle(
          Offset(_freqToX(band.freq, w), _gainToY(band.gain, h)),
          4,
          Paint()
            ..color = color
            ..style = PaintingStyle.fill,
        )
        ..drawCircle(
          Offset(_freqToX(band.freq, w), _gainToY(band.gain, h)),
          4,
          Paint()
            ..color = Colors.white
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1.5,
        );
    }

    // 4. Draw Composite / Total Gain curve (multiplicative)
    final compositePath = Path();
    var compStarted = false;

    for (var s = 0; s <= steps; s++) {
      final x = s * stepX;
      final f = _xToFreq(x, w);
      final logF = math.log(f);

      var totalGain = 1.0;
      for (var b = 0; b < bands.length; b++) {
        final d = logF - logFcs[b];
        final exponent = d * d * invTwoSigmaSq[b];
        if (exponent > 9.0) continue;
        final weight = math.exp(-exponent);
        var factor = 1.0 + (bands[b].gain - 1.0) * weight;
        if (factor < 0.0) factor = 0.0;
        totalGain *= factor;
      }

      final y = _gainToY(totalGain, h);
      if (!compStarted) {
        compositePath.moveTo(x, y);
        compStarted = true;
      } else {
        compositePath.lineTo(x, y);
      }
    }

    final compositePaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.9)
      ..strokeWidth = 2.5
      ..style = PaintingStyle.stroke;
    canvas.drawPath(compositePath, compositePaint);
  }

  @override
  bool shouldRepaint(covariant _EqGraphPainter oldDelegate) {
    if (oldDelegate.bands.length != bands.length) return true;
    for (var i = 0; i < bands.length; i++) {
      if (oldDelegate.bands[i].gain != bands[i].gain ||
          oldDelegate.bands[i].freq != bands[i].freq ||
          oldDelegate.bands[i].q != bands[i].q) {
        return true;
      }
    }
    return oldDelegate.minFreq != minFreq ||
        oldDelegate.maxFreq != maxFreq ||
        oldDelegate.minGain != minGain ||
        oldDelegate.maxGain != maxGain;
  }
}

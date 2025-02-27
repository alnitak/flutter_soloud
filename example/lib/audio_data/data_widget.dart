import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Visualizer for FFT and wave data
class AudioDataWidget extends StatefulWidget {
  const AudioDataWidget({super.key});

  @override
  State<AudioDataWidget> createState() => AudioDataWidgetState();
}

class AudioDataWidgetState extends State<AudioDataWidget>
    with SingleTickerProviderStateMixin {
  Ticker? ticker;

  /// Set [AudioData] to use a [GetSamplesKind.linear] data kind.
  /// This is the way to get both wave and FFT data in a single list.
  final AudioData audioData = AudioData(GetSamplesKind.linear);

  @override
  void initState() {
    super.initState();
    ticker = createTicker(_tick);
    ticker?.start();
  }

  @override
  void dispose() {
    ticker?.stop();
    audioData.dispose();
    super.dispose();
  }

  void _tick(Duration elapsed) {
    if (context.mounted) {
      try {
        /// Internally update the audio data to be get later
        /// with `audioData.getLinear*()` in [WavePainter]
        audioData.updateSamples();
        setState(() {});
      } on Exception catch (e) {
        debugPrint('$e');
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        return SizedBox(
          width: constraints.maxWidth,
          height: constraints.maxHeight,
          child: Padding(
            padding: const EdgeInsets.all(6),
            child: ColoredBox(
              color: Colors.black,
              child: RepaintBoundary(
                child: ClipRRect(
                  child: CustomPaint(
                    painter: WavePainter(audioData: audioData),
                  ),
                ),
              ),
            ),
          ),
        );
      },
    );
  }
}

/// Custom painter to draw the wave and the FFT
class WavePainter extends CustomPainter {
  const WavePainter({
    required this.audioData,
  });
  final AudioData audioData;

  @override
  void paint(Canvas canvas, Size size) {
    // ignore: avoid_redundant_argument_values
    final samples = audioData.getAudioData(alwaysReturnData: true);
    // Using `alwaysReturnData: true` this will always return a non-empty list
    // even if the audio data is the same as the previous one.
    if (samples.isEmpty) {
      return;
    }
    final barWidth = size.width / 256;
    final paint = Paint()
      ..strokeWidth = barWidth * 0.8
      ..color = Colors.yellowAccent;

    double waveHeight;
    double fftHeight;

    for (var i = 0; i < 256; i++) {
      try {
        final fftData = samples[i];
        final waveData = samples[i + 256];
        waveHeight = size.height * waveData * 0.5;
        fftHeight = size.height * fftData / 2;
      } on Exception {
        waveHeight = 0;
        fftHeight = 0;
      }

      /// Draw the wave
      canvas
        ..drawRect(
          Rect.fromLTRB(
            barWidth * i,
            size.height / 4 - waveHeight / 2,
            barWidth * (i + 1),
            size.height / 4 + waveHeight / 2,
          ),
          paint,
        )

        /// Draw the fft
        ..drawLine(
          Offset(barWidth * i, size.height - 10),
          Offset(barWidth * i, size.height - 10 - fftHeight),
          paint,
        );
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) {
    return true;
  }
}

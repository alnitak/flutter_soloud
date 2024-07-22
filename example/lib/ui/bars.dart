import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/bars_fft_widget.dart';
import 'package:flutter_soloud_example/visualizer/bars_wave_widget.dart';

/// Visualizer for FFT and wave data
class Bars extends StatefulWidget {
  /// If true get audio data from the player else from the mic

  const Bars({super.key});

  @override
  State<Bars> createState() => BarsState();
}

class BarsState extends State<Bars> with SingleTickerProviderStateMixin {
  late final Ticker ticker;
  final AudioData audioData = AudioData(
    GetSamplesKind.linear,
  );
  @override
  void initState() {
    super.initState();
    ticker = createTicker(_tick);
    ticker.start();
  }

  @override
  void dispose() {
    ticker.stop();
    audioData.dispose();
    super.dispose();
  }

  void _tick(Duration elapsed) {
    if (context.mounted) {
      try {
        audioData.updateSamples();
        setState(() {});
      } on Exception catch (e) {
        debugPrint('$e');
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(6),
      child: Row(
        children: [
          BarsFftWidget(
            audioData: audioData,
            minFreq: 0,
            maxFreq: 128,
            width: MediaQuery.sizeOf(context).width / 2 - 17,
            height: MediaQuery.sizeOf(context).width / 6,
          ),
          const SizedBox(width: 6),
          BarsWaveWidget(
            audioData: audioData,
            width: MediaQuery.sizeOf(context).width / 2 - 17,
            height: MediaQuery.sizeOf(context).width / 6,
          ),
        ],
      ),
    );
  }
}

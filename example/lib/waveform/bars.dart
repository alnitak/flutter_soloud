import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/bars_fft_widget.dart';
import 'package:flutter_soloud_example/visualizer/bars_wave_widget.dart';

/// Visualizer for FFT and wave data
///
class Bars extends StatefulWidget {
  const Bars({super.key});

  @override
  State<Bars> createState() => BarsState();
}

class BarsState extends State<Bars> with SingleTickerProviderStateMixin {
  late final Ticker ticker;
  ffi.Pointer<ffi.Pointer<ffi.Float>> playerData = ffi.nullptr;

  @override
  void initState() {
    super.initState();
    playerData = calloc();
    ticker = createTicker(_tick);
    ticker.start();
  }

  @override
  void dispose() {
    ticker.stop();
    calloc.free(playerData);
    playerData = ffi.nullptr;
    super.dispose();
  }

  void _tick(Duration elapsed) {
    if (mounted) {
      SoLoud.instance.getAudioTexture2D(playerData);
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(6),
      child: Row(
        children: [
          BarsFftWidget(
            audioData: playerData.value,
            minFreq: 0,
            maxFreq: 128,
            width: MediaQuery.sizeOf(context).width / 2 - 17,
            height: MediaQuery.sizeOf(context).width / 6,
          ),
          const SizedBox(width: 6),
          BarsWaveWidget(
            audioData: playerData.value,
            width: MediaQuery.sizeOf(context).width / 2 - 17,
            height: MediaQuery.sizeOf(context).width / 6,
          ),
        ],
      ),
    );
  }
}

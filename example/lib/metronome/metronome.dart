import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Metronome example.
/// 
/// For this example the player is initialized with a buffer size of 256. By
/// default it is set to 2048, this means that for a playback at 44100 Hz the
/// buffer is processed in about 40ms. When we use the `play()` method, the
/// sound will start at the upcoming audio buffer and this could happen with
/// a delay at least of 40ms. If we reduce the buffer size also this gap will
/// be shorter. A value of 256 or 512 will reduce this latency but at the same 
/// time, the smaller the buffer, the more likely the
/// system hits buffer underruns (ie, the play head marches on but there's no
/// data ready to be played) and the sound breaks down horribly.

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  /// Initialize the player.
  await SoLoud.instance.init(bufferSize: 256, channels: Channels.channelMono);

  runApp(
    const MaterialApp(
      home: Metronome(),
    ),
  );
}

class Metronome extends StatefulWidget {
  const Metronome({super.key});

  @override
  State<Metronome> createState() => _MetronomeState();
}

class _MetronomeState extends State<Metronome> {
  /// delay between ticks.
  final delay = ValueNotifier<int>(100);

  /// duration of the tick sound.
  final tickDurationMs = ValueNotifier<int>(45);

  Timer? timer;
  AudioSource? tick1;
  AudioSource? tick2;
  SoundHandle? tick1Handle;
  SoundHandle? tick2Handle;

  @override
  void initState() {
    super.initState();
    SoLoud.instance.loadAsset('assets/audio/tic-1.wav').then((value) async {
      /// start playing the tick in a paused state, so it can be
      /// unpaused/paused in the `Timer` callback.
      tick1 = value;
      tick1Handle = await SoLoud.instance.play(tick1!, paused: true);
    });
    SoLoud.instance.loadAsset('assets/audio/tic-2.wav').then((value) async {
      /// start playing the tick in a paused state, so it can be
      /// unpaused/paused in the `Timer` callback.
      tick2 = value;
      tick2Handle = await SoLoud.instance.play(tick2!, paused: true);
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        children: [
          Padding(
            padding: const EdgeInsets.all(16),
            child: Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text(
                    'Metronome example',
                    textScaler: TextScaler.linear(3),
                  ),
                  const SizedBox(height: 32),
                  ValueListenableBuilder<int>(
                    valueListenable: delay,
                    builder: (_, ms, __) {
                      return Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Slider.adaptive(
                            min: 15,
                            max: 500,
                            value: ms.toDouble(),
                            onChanged: (value) {
                              delay.value = value.toInt();
                              start();
                            },
                          ),
                          Text('delay ms: $ms  BPM: ${60000 ~/ ms}'),
                        ],
                      );
                    },
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  int count = 0;
  void start() {
    timer?.cancel();
    timer = Timer.periodic(Duration(milliseconds: delay.value), (_) {
      if (count % 8 == 0) {
        if (tick2 != null) {
          SoLoud.instance.play(tick2!);
        }
      } else {
        if (tick1 != null) {
          SoLoud.instance.play(tick1!);
        }
      }
      count++;
    });
  }
}

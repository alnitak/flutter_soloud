import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// Waveform example.
///
/// This example demonstrates how to generate a waveform, play it, and change
/// it's frequency on the fly.
void main() {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
  // You can listen to the logs as shown below.
  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
  });

  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Flutter SoLoud waveform demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.deepPurple),
        useMaterial3: true,
      ),
      home: const MyHomePage(title: 'Flutter SoLoud waveform demo'),
    );
  }
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({required this.title, super.key});

  final String title;

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  WaveForm waveForm = WaveForm.sin;
  bool superWave = false;
  double scale = 1;
  double detune = 0.5;
  double frequency = 1000;
  AudioSource? currentSound;
  SoundHandle? soundHandle;
  bool isPlaying = false;

  @override
  void dispose() {
    stop();
    SoLoud.instance.deinit();
    super.dispose();
  }

  Future<void> play(double frequency) async {
    try {
      if (!SoLoud.instance.isInitialized) {
        await SoLoud.instance.init();
      }

      if (isPlaying) {
        await stop();
      }

      currentSound =
          await SoLoud.instance.loadWaveform(WaveForm.sin, true, 1, 0);

      soundHandle = await SoLoud.instance.play(currentSound!);

      if (context.mounted) {
        setState(() {
          isPlaying = true;
        });
      }

      SoLoud.instance.setWaveformSuperWave(currentSound!, superWave);
      SoLoud.instance.setWaveformFreq(currentSound!, frequency);
      SoLoud.instance.setWaveformDetune(currentSound!, detune);
      SoLoud.instance.setWaveformScale(currentSound!, scale);
    } catch (e) {
      debugPrint('Error while trying to play sound: $e');
    }
  }

  Future<void> stop() async {
    try {
      if (soundHandle != null && isPlaying) {
        await SoLoud.instance.stop(soundHandle!);
        setState(() {
          isPlaying = false;
        });
      }
    } catch (e) {
      debugPrint('Error while trying to stop sound: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: Text(widget.title),
      ),
      body: SingleChildScrollView(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('SuperWave : $superWave'),
                Switch(
                  value: superWave,
                  onChanged: (value) {
                    setState(() {
                      superWave = value;
                      if (currentSound != null && isPlaying) {
                        SoLoud.instance
                            .setWaveformSuperWave(currentSound!, value);
                      }
                    });
                  },
                ),
                const SizedBox(height: 20),
                Text('Scale : ${scale.toStringAsFixed(2)}'),
                Slider(
                  value: scale,
                  max: 4,
                  onChanged: !superWave
                      ? null
                      : (value) {
                          setState(() {
                            scale = value;
                            if (currentSound != null && isPlaying) {
                              SoLoud.instance
                                  .setWaveformScale(currentSound!, value);
                            }
                          });
                        },
                  label: 'Scale: ${scale.toStringAsFixed(2)}',
                  activeColor: Colors.green,
                  inactiveColor: Colors.green[100],
                ),
                const SizedBox(height: 20),
                Text('Detune : ${detune.toStringAsFixed(2)}'),
                Slider(
                  value: detune,
                  max: 2,
                  onChanged: !superWave
                      ? null
                      : (value) {
                          setState(() {
                            detune = value;
                            if (currentSound != null && isPlaying) {
                              SoLoud.instance
                                  .setWaveformDetune(currentSound!, value);
                            }
                          });
                        },
                  label: 'Detune: ${detune.toStringAsFixed(2)}',
                  activeColor: Colors.green,
                ),
                const SizedBox(height: 20),
                Text('Freequncy Hz: ${frequency.toInt()}'),
                Slider(
                  value: frequency,
                  min: 20,
                  max: 2000,
                  onChanged: (value) {
                    setState(() {
                      frequency = value;

                      if (currentSound != null && isPlaying) {
                        SoLoud.instance.setWaveformFreq(currentSound!, value);
                      }
                    });
                  },
                  label: 'Frequency: ${frequency.toInt()} Hz',
                  activeColor: Colors.green,
                  inactiveColor: Colors.green[100],
                ),
                const SizedBox(height: 50),

                /// All waveform types.
                Wrap(
                  runSpacing: 4,
                  spacing: 4,
                  alignment: WrapAlignment.center,
                  children: [
                    for (var i = 0; i < WaveForm.values.length; i++)
                      OutlinedButton(
                        onPressed: () {
                          setState(() {
                            waveForm = WaveForm.values[i];
                            SoLoud.instance
                                .setWaveform(currentSound!, waveForm);
                          });
                        },
                        style: ButtonStyle(
                          backgroundColor: WidgetStateProperty.all(
                            waveForm == WaveForm.values[i]
                                ? Colors.green
                                : Colors.transparent,
                          ),
                        ),
                        child: Text(WaveForm.values[i].name),
                      ),
                  ],
                ),
                const SizedBox(height: 50),
                Align(
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      OutlinedButton(
                        onPressed: isPlaying ? null : () => play(frequency),
                        child: const Text('Play'),
                      ),
                      const SizedBox(width: 20),
                      OutlinedButton(
                        onPressed: isPlaying ? stop : null,
                        child: const Text('Stop'),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

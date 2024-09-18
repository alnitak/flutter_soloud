import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Flutter Demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.deepPurple),
        useMaterial3: true,
      ),
      home: const MyHomePage(title: 'Flutter Demo Home Page'),
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

      soundHandle = await SoLoud.instance.play(
        currentSound!,
        loopingStartAt: const Duration(seconds: 5),
        looping: true,
      );

      setState(() {
        isPlaying = true;
      });

      SoLoud.instance.setWaveformFreq(currentSound!, frequency);
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
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text('Freequncy : $frequency'),
            Slider(
              value: frequency,
              min: 20,
              max: 16000,
              onChanged: (value) {
                setState(() {
                  frequency = value;

                  if (currentSound != null && isPlaying) {
                    SoLoud.instance.setWaveformFreq(currentSound!, value);
                  }
                });
              },
              label: 'Frequency: ${frequency.toStringAsFixed(0)} Hz',
              activeColor: Colors.green,
              inactiveColor: Colors.green[100],
            ),
            const SizedBox(height: 50),
            ElevatedButton(
              onPressed: isPlaying ? null : () => play(frequency),
              child: const Text('Play'),
            ),
            const SizedBox(height: 20),
            ElevatedButton(
              onPressed: isPlaying ? stop : null,
              child: const Text('Stop'),
            ),
          ],
        ),
      ),
    );
  }
}

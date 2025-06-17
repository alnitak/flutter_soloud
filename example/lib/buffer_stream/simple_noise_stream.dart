import 'dart:developer' as dev;
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/buffer_stream/ui/buffer_widget.dart';
import 'package:logging/logging.dart';

/// Example of how to generate PCM audio inside an `Isolate` and play them.
///
/// The `setBufferStream`, `addAudioDataStream` and `setDataIsEnded` methods
/// can be used inside an `Isolate`. So you can perform complex operations
/// computing audio inside an `Isolate` without freezing the main Isolate.

void main() async {
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

  WidgetsFlutterBinding.ensureInitialized();

  /// Initialize the player.
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: SimpleNoise(),
    ),
  );
}

class SimpleNoise extends StatefulWidget {
  const SimpleNoise({super.key});

  @override
  State<SimpleNoise> createState() => _SimpleNoiseState();
}

class _SimpleNoiseState extends State<SimpleNoise> {
  AudioSource? noise;
  bool isBuffering = false;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Generate noise')),
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 16,
          children: [
            OutlinedButton(
              onPressed: () async {
                /// Setup the buffer stream
                noise = SoLoud.instance.setBufferStream(
                  maxBufferSizeBytes: 1024 * 1024 * 10,
                  format: BufferType.f32le,
                  bufferingTimeNeeds: 0.1,
                  bufferingType: BufferingType.preserved,
                  onBuffering: (bool buffering, int handle, double time) {
                    isBuffering = buffering;
                    setState(() {});
                  },
                );
                await SoLoud.instance.play(noise!);

                /// Just to rebuild [BufferBar] widgets
                setState(() {});
              },
              child: const Text('set buffer stream'),
            ),
            OutlinedButton(
              onPressed: () async {
                if (noise == null) return;

                final random = Random();
                final randomFloats =
                    Float32List((1024 * 1024) >> 2); // 1 MB of floats
                for (var i = 0; i < randomFloats.length; i++) {
                  // Generate noise in range [-1, 1]
                  randomFloats[i] = random.nextDouble() * 2 - 1;
                }
                // Add 1MB of random noise data to the buffer stream
                SoLoud.instance.addAudioDataStream(
                  noise!,
                  randomFloats.buffer.asUint8List(),
                );
                print(
                    'isPaused: ${SoLoud.instance.getPause(noise!.handles.first)}');
              },
              child: const Text('push 1MB of noise data'),
            ),
            OutlinedButton(
              onPressed: () async {
                if (noise == null) return;
                SoLoud.instance.setDataIsEnded(noise!);
              },
              child: const Text('set data ended'),
            ),
            OutlinedButton(
              onPressed: () async {
                await SoLoud.instance.disposeAllSources();
                noise = null;
                setState(() {});
              },
              child: const Text('dispose all sounds'),
            ),
            BufferBar(
              bufferingType: BufferingType.preserved,
              sound: noise,
              startingMb: 1,
              label: 'noise',
            ),
            if (isBuffering)
              const Text('Buffering...'),
          ],
        ),
      ),
    );
  }
}

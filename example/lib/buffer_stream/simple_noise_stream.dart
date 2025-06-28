import 'dart:developer' as dev;
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/buffer_stream/ui/buffer_widget.dart';
import 'package:logging/logging.dart';

/// Example to demonstrate how to use the buffer stream generating random noise
/// data and how to handle buffering events.
///
/// It also shows how to use the `SoLoud.instance.setBufferStream` method
/// to set up a buffer stream and how to add audio data to it
/// using `SoLoud.instance.addAudioDataStream`.
///
/// As you can see, the playing position of the sound is always at 0 when the
/// buffering type is `BufferingType.released`.
/// When the buffering type is `BufferingType.preserved`, all is normal as for
/// the other "normal" sounds, and the playing position can be get using
/// `SoLoud.instance.getPosition(handle)`.
/// When the buffering type is `BufferingType.released`, the sound is
/// always at the beginning, and the position is get using
/// `SoLoud.instance.getStreamTimeConsumed(sound.)`.

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
  /// The size of the chunks to be sent to the buffer stream in bytes.
  static const chunkSize = 1024 * 1024 / 10; // 0.1 MB

  /// The type of the buffer stream.
  final bufferingType = ValueNotifier<BufferingType>(BufferingType.preserved);

  /// The time needed to wait before unpausing the audio stream.
  final bufferingTimeNeeds = 2.0;

  AudioSource? noise;

  /// Whether the sound is awaiting for new data or not.
  bool isBuffering = false;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Generate noise')),
      body: Align(
        alignment: Alignment.topCenter,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 16,
          children: [
            // Choose between BufferingType.released and
            // BufferingType.preserved.
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Text('BufferingType: '),
                ValueListenableBuilder(
                  valueListenable: bufferingType,
                  builder: (context, value, child) {
                    return DropdownButton<BufferingType>(
                      value: value,
                      items: BufferingType.values.map((type) {
                        return DropdownMenuItem(
                          value: type,
                          child: Text(type.name),
                        );
                      }).toList(),
                      onChanged: (value) {
                        bufferingType.value = value ?? BufferingType.released;
                      },
                    );
                  },
                ),
              ],
            ),
            Text('Buffering time needs: $bufferingTimeNeeds seconds'),
            OutlinedButton(
              onPressed: () async {
                /// Setup the buffer stream
                noise = SoLoud.instance.setBufferStream(
                  format: BufferType.f32le,
                  bufferingTimeNeeds: bufferingTimeNeeds,
                  bufferingType: bufferingType.value,
                  onBuffering: (bool buffering, int handle, double time) {
                    isBuffering = buffering;
                    debugPrint('ON BUFFERING: $buffering, handle: $handle, '
                        'at time: $time');
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
                // floats have 4 bytes each
                final randomFloats = Float32List(chunkSize.toInt() >> 2);

                for (var i = 0; i < randomFloats.length; i++) {
                  // Generate noise in range [-1, 1]
                  randomFloats[i] = random.nextDouble() * 2 - 1;
                }
                // Add [chunkSize] bytes of random noise data to the buffer
                SoLoud.instance.addAudioDataStream(
                  noise!,
                  randomFloats.buffer.asUint8List(),
                );
              },
              child:
                  const Text('push ${chunkSize / 1024 / 1024}MB of noise data'),
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
                isBuffering = false;
                setState(() {});
              },
              child: const Text('dispose all sounds'),
            ),
            BufferBar(
              bufferingType: bufferingType.value,
              isBuffering: isBuffering,
              sound: noise,
              startingMb: 1,
              label: 'noise',
            ),
            if (isBuffering) const Text('Buffering...'),
          ],
        ),
      ),
    );
  }
}

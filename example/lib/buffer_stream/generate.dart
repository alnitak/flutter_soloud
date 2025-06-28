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
      home: Generate(),
    ),
  );
}

@pragma('vm:entry-point')
Future<void> generateTone(Map<String, dynamic> args) async {
  final sound = args['sound'] as AudioSource;

  // Frequency in Hz
  final frequency = args['frequency'] as double;

  // Sampling rate in Hz (samples per second)
  final sampleRate = args['sampleRate'] as int;

  // Duration of the audio in seconds
  final duration = args['duration'] as double;

  // Total number of samples needed for the given duration
  final sampleCount = (sampleRate * duration).ceil();

  // List to hold the audio samples
  final audioData = Int8List(sampleCount);

  // Generate PCM data
  for (var i = 0; i < sampleCount; i++) {
    // Calculate the amplitude of the wave (between -128 and 127 for 8-bit PCM)
    final amplitude = (127 * sin(2 * pi * frequency * i / sampleRate)).toInt();
    // Store the amplitude in the audio data buffer
    audioData[i] = amplitude;
  }

  SoLoud.instance.addAudioDataStream(
    sound,
    audioData.buffer.asUint8List(),
  );
  SoLoud.instance.setDataIsEnded(sound);
}

@pragma('vm:entry-point')
Future<void> generateBouncingSoundPCM(Map<String, dynamic> args) async {
  final sound = args['sound'] as AudioSource;
  // Sampling rate in Hz (samples per second)
  final sampleRate = args['sampleRate'] as int;
  // Duration of the audio in seconds
  final duration = args['duration'] as double;
  // Total number of samples needed for the given duration
  final sampleCount = sampleRate * duration;
  // List to hold the audio samples
  final audioData = Int8List(sampleCount.ceil());

  // Parameters for bouncing effect
  const baseFrequency = 300.0; // Base frequency in Hz
  const bounceFrequency = 8.0; // Frequency of bounce effect
  const bounceDepth = 200.0; // Depth of frequency variation

  // Generate PCM data with bouncing frequency modulation
  for (var i = 0; i < sampleCount; i++) {
    // Calculate time for each sample
    final time = i / sampleRate;

    // Create a bouncing effect by oscillating the frequency
    final modulatedFrequency =
        baseFrequency + bounceDepth * sin(2 * pi * bounceFrequency * time);

    // Apply an oscillation to the amplitude to make it sound more dynamic
    final amplitudeModulation =
        0.8 + 0.2 * sin(2 * pi * bounceFrequency * 0.5 * time);

    // Generate sample using modulated frequency and amplitude
    final amplitude =
        (127 * amplitudeModulation * sin(2 * pi * modulatedFrequency * time))
            .toInt();

    // Store the sample value in the audio data buffer
    audioData[i] = amplitude;
  }

  SoLoud.instance.addAudioDataStream(sound, audioData.buffer.asUint8List());
  SoLoud.instance.setDataIsEnded(sound);
}

@pragma('vm:entry-point')
Future<void> generateWailingSirenSoundPCM(Map<String, dynamic> args) async {
  final sound = args['sound'] as AudioSource;
  // Sampling rate in Hz (samples per second)
  final sampleRate = args['sampleRate'] as int;
  // Duration of the audio in seconds
  final duration = args['duration'] as double;
  // Total number of samples needed for the given duration
  final sampleCount = sampleRate * duration;
  // List to hold the audio samples
  final audioData = Int8List(sampleCount.ceil());

  // Parameters for the siren effect
  const baseFrequency =
      600.0; // Higher base frequency for a more noticeable sound
  const sirenSpeed = 4.0; // Slower frequency oscillation for a siren effect
  const frequencyRange =
      150.0; // Moderate frequency range for smooth oscillation

  // Generate PCM data with a smooth siren-like frequency modulation
  for (var i = 0; i < sampleCount; i++) {
    // Calculate time for each sample
    final time = i / sampleRate;

    // Smoothly oscillate the frequency
    final modulatedFrequency =
        baseFrequency + frequencyRange * sin(2 * pi * sirenSpeed * time);

    // Generate the sample using a sinusoidal wave with modulated frequency
    final amplitude = (127 * sin(2 * pi * modulatedFrequency * time)).toInt();

    // Store the sample value in the audio data buffer
    audioData[i] = amplitude;
  }

  SoLoud.instance.addAudioDataStream(sound, audioData.buffer.asUint8List());
  SoLoud.instance.setDataIsEnded(sound);
}

class Generate extends StatefulWidget {
  const Generate({super.key});

  @override
  State<Generate> createState() => _GenerateState();
}

class _GenerateState extends State<Generate> {
  AudioSource? tone;
  AudioSource? bouncing;
  AudioSource? siren;

  @override
  Widget build(BuildContext context) {
    const gap = SizedBox(height: 16);
    return Scaffold(
      appBar: AppBar(title: const Text('Generate PCM Data')),
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            OutlinedButton(
              onPressed: () async {
                /// Setup the buffer stream for each streams.
                tone = SoLoud.instance.setBufferStream(
                  maxBufferSizeBytes: 1024 * 1024 * 1,
                  format: BufferType.s8,
                );
                bouncing = SoLoud.instance.setBufferStream(
                  maxBufferSizeBytes: 1024 * 1024 * 1,
                  format: BufferType.s8,
                );
                siren = SoLoud.instance.setBufferStream(
                  maxBufferSizeBytes: 1024 * 1024 * 1,
                  format: BufferType.s8,
                );

                /// Generate PCM data inside an Isolate
                await Future.wait([
                  compute(generateTone, {
                    'sound': tone,
                    'frequency': 440.0,
                    'sampleRate': 44100,
                    'duration': 1.0,
                  }),
                  compute(generateBouncingSoundPCM, {
                    'sound': bouncing,
                    'frequency': 200.0,
                    'sampleRate': 44100,
                    'duration': 3.0,
                  }),
                  compute(generateWailingSirenSoundPCM, {
                    'sound': siren,
                    'frequency': 200.0,
                    'sampleRate': 44100,
                    'duration': 2.0,
                  }),
                ]);

                /// Just to rebuild [BufferBar] widgets
                setState(() {});
              },
              child: const Text('generate PCM data'),
            ),
            gap,
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                OutlinedButton(
                  onPressed: () async {
                    if (tone == null) return;
                    await SoLoud.instance.play(tone!, looping: true);
                  },
                  child: const Text('Play tone'),
                ),
                OutlinedButton(
                  onPressed: () async {
                    if (siren == null) return;
                    await SoLoud.instance.play(siren!, looping: true);
                  },
                  child: const Text('Play siren'),
                ),
                OutlinedButton(
                  onPressed: () async {
                    if (bouncing == null) return;
                    await SoLoud.instance.play(bouncing!, looping: true);
                  },
                  child: const Text('Play bouncing'),
                ),
              ],
            ),
            gap,
            OutlinedButton(
              onPressed: () async {
                await SoLoud.instance.disposeAllSources();
                tone = siren = bouncing = null;
                setState(() {});
              },
              child: const Text('dispose all sounds'),
            ),
            gap,
            BufferBar(
              bufferingType: BufferingType.preserved,
              isBuffering: false,
              sound: tone,
              startingMb: 1,
              label: 'tone',
            ),
            BufferBar(
              bufferingType: BufferingType.preserved,
              isBuffering: false,
              sound: siren,
              startingMb: 1,
              label: 'siren',
            ),
            BufferBar(
              bufferingType: BufferingType.preserved,
              isBuffering: false,
              sound: bouncing,
              startingMb: 1,
              label: 'bouncing',
            ),
          ],
        ),
      ),
    );
  }
}

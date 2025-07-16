import 'dart:developer' as dev;
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// Waveform example.
///
/// This example demonstrates how read audio samples from a wave file.
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

  runApp(
    const MaterialApp(
      home: HelloFlutterSoLoud(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  List<PlatformFile>? paths;
  AudioSource? currentSound;
  Float32List? data;
  bool average = false;

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.sizeOf(context).width.toInt();
    return Scaffold(
      body: Center(
        child: Column(
          children: [
            const SizedBox(height: 16),
            const Text(
              'Read N audio samples (here N = width*4) from audio file.',
            ),
            OutlinedButton(
              onPressed: () async {
                paths = (await FilePicker.platform.pickFiles(
                  type: FileType.custom,
                  allowedExtensions: ['mp3', 'wav', 'flac', 'ogg'],
                  onFileLoading: print,
                  dialogTitle: 'Pick audio file',
                ))
                    ?.files;

                await _loadPath(width);
                if (context.mounted) setState(() {});
              },
              child: const Text('pickFile'),
            ),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Text('Average samples  '),
                Switch(
                  value: average,
                  onChanged: (value) async {
                    average = value;
                    await _loadPath(width);
                    if (context.mounted) setState(() {});
                  },
                ),
              ],
            ),
            if (data != null)
              Expanded(
                child: Container(
                  color: Colors.black,
                  width: width.toDouble(),
                  child: RepaintBoundary(
                    child: ClipRRect(
                      child: CustomPaint(
                        key: UniqueKey(),
                        painter: WavePainter(
                          data: data!,
                        ),
                      ),
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  Future<void> _loadPath(int width) async {
    if (paths != null) {
      if (kIsWeb) {
        // on web we can't read the bytes from the file.
        data = await SoLoud.instance.readSamplesFromMem(
          paths!.first.bytes!,
          width * 4,
          average: average,
        );
      } else {
        final f = File(paths!.first.path!);
        final bytes = f.readAsBytesSync();
        data = await SoLoud.instance.readSamplesFromMem(
          bytes,
          width * 4,
          average: average,
        );
      }
    }
  }
}

/// Custom painter to draw the wave data.
class WavePainter extends CustomPainter {
  const WavePainter({
    required this.data,
  });

  final Float32List data;

  @override
  void paint(Canvas canvas, Size size) {
    final barWidth = size.width / data.length;
    final paint = Paint()
      ..color = Colors.yellowAccent
      ..strokeWidth = barWidth;

    for (var i = 0; i < data.length; i++) {
      final barHeight = size.height * data[i] * 2;
      canvas.drawLine(
        Offset(barWidth * i, (size.height - barHeight) / 2),
        Offset(barWidth * i, (size.height + barHeight) / 2),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(WavePainter oldDelegate) {
    return true;
  }
}

import 'dart:developer' as dev;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

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
  AudioSource? currentSound;
  Float32List? data;

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    final width = MediaQuery.sizeOf(context).width.toInt();
    return Scaffold(
      body: Center(
        child: Column(
          children: [
            const SizedBox(height: 16),
            const Text(
                'Read N audio samples (here N = width) from audio file.'),
            ElevatedButton(
              onPressed: () async {
                final paths = (await FilePicker.platform.pickFiles(
                  type: FileType.custom,
                  allowedExtensions: ['mp3', 'wav', 'flac'],
                  onFileLoading: print,
                  dialogTitle: 'Pick audio file',
                ))
                    ?.files;

                if (paths != null) {
                  if (kIsWeb) {
                    data = await SoLoud.instance.readSamplesFromMem(
                      paths.first.bytes!,
                      width * 2,
                    );
                  } else {
                    data = await SoLoud.instance.readSamplesFromFile(
                      paths.first.path!,
                      width * 2,
                    );
                  }
                }
                if (context.mounted) setState(() {});
              },
              child: const Text('pickFile'),
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
      ..strokeWidth = barWidth * 1.5;

    for (var i = 0; i < data.length; i++) {
      final barHeight = size.height * data[i];
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

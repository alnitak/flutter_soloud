import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:path_provider/path_provider.dart';

class Page4 extends StatefulWidget {
  const Page4({super.key});

  @override
  State<Page4> createState() => _Page4State();
}

class _Page4State extends State<Page4> {
  SoundProps? currentSound;
  late DateTime initialTime;
  Timer? timer;
  Duration elapsed = Duration.zero;
  double soundSpeed = 0.3;
  double elapsedDistance = 0;
  double circleRadius = 100;
  double posX = 0;
  double posY = 100;

  @override
  void initState() {
    super.initState();
    initialTime = DateTime.now();
  }

  void _startTimer() {
    timer?.cancel();
    timer = Timer.periodic(const Duration(milliseconds: 50), (_) {
      if (currentSound != null) {
        final now = DateTime.now();
        SoLoud().set3dSourceParameters(
          currentSound!.handle.first,
          posX,
          posY,
          0,
          0,
          0,
          0,
        );
        setState(() {
          elapsed = now.difference(initialTime);
          elapsedDistance += soundSpeed / 20;
          final angle = 2 * pi * elapsedDistance;
          posX = circleRadius * cos(angle);
          posY = circleRadius * sin(angle);
        });
      }
    });
  }

  @override
  void dispose() {
    SoLoud().stopIsolate();
    SoLoud().stopCapture();
    timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        ElevatedButton(
          onPressed: () async {
            if (currentSound == null) {
              await play(maxDistance: 100);
            } else {
              timer?.cancel();
              // await SoLoud().stop(currentSound!.handle.first);
              await SoLoud().stopSound(currentSound!);
              currentSound = null;
            }
            setState(() {});
          },
          child: Text(currentSound == null ? 'play' : 'stop'),
        ),
        Text(elapsed.toString()),
        const SizedBox(
          height: 50,
        ),
        CustomPaint(
          size: const Size.square(200),
          painter: CircleWithDotPainter(x: posX, y: posY),
        ),
      ],
    );
  }

  Future<void> play({required double maxDistance}) async {
    /// Start audio engine if not already
    if (!SoLoud().isIsolateRunning()) {
      await SoLoud().startIsolate().then((value) {
        if (value == PlayerErrors.noError) {
          debugPrint('isolate started');
        } else {
          debugPrint('isolate starting error: $value');
          return;
        }
      });
    }

    /// stop any previous sound loaded
    if (currentSound != null) {
      if (await SoLoud().stopSound(currentSound!) != PlayerErrors.noError) {
        return;
      }
    }

    /// load the audio file
    final f = await getAssetFile('assets/audio/8_bit_mentality.mp3');
    final loadRet = await SoLoud().loadFile(f.path);
    if (loadRet.error != PlayerErrors.noError) return;
    currentSound = loadRet.sound;

    /// play it
    final playRet = await SoLoud().play3d(currentSound!, 0, 0, 0);
    if (loadRet.error != PlayerErrors.noError) return;
    SoLoud().setLooping(playRet.newHandle, true);
    SoLoud().set3dSourceMinMaxDistance(
      playRet.newHandle,
      50,
      maxDistance,
    );
    SoLoud().set3dSourceAttenuation(playRet.newHandle, 1, 1);
    currentSound = playRet.sound;

    _startTimer();
    if (mounted) setState(() {});
  }

  /// get the assets file and copy it to the temp dir
  Future<File> getAssetFile(String assetsFile) async {
    final tempDir = await getTemporaryDirectory();
    final tempPath = tempDir.path;
    final filePath = '$tempPath/$assetsFile';
    final file = File(filePath);
    if (file.existsSync()) {
      return file;
    } else {
      final byteData = await rootBundle.load(assetsFile);
      final buffer = byteData.buffer;
      await file.create(recursive: true);
      return file.writeAsBytes(
        buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
      );
    }
  }

  void stopSound() {
    SoLoud().pauseSwitch(currentSound!.handle.first);
  }
}

class CircleWithDotPainter extends CustomPainter {
  CircleWithDotPainter({required this.x, required this.y});

  final double x;
  final double y;

  @override
  void paint(Canvas canvas, Size size) {
    final radius = size.width / 2;
    final circlePaint = Paint()
      ..color = Colors.blue
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.0;

    final center = Offset(size.width / 2, size.height / 2);
    canvas.drawCircle(center, radius, circlePaint);

    final dotX = center.dx + x;
    final dotY = center.dy + y;
    final dotPaint = Paint()
      ..color = Colors.red
      ..style = PaintingStyle.fill;

    final dotRadius = size.width / 50;
    canvas.drawCircle(Offset(dotX, dotY), dotRadius, dotPaint);
  }

  @override
  bool shouldRepaint(CustomPainter oldDelegate) {
    return false;
  }
}

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:path_provider/path_provider.dart';

class Page3 extends StatefulWidget {
  const Page3({super.key});

  @override
  State<Page3> createState() => _Page3State();
}

class _Page3State extends State<Page3> {
  SoundProps? currentSound;

  @override
  void dispose() {
    AudioIsolate().stopIsolate();
    AudioIsolate().stopCapture();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          children: [
            ElevatedButton(
              onPressed: play,
              child: const Text('play'),
            ),
            const SizedBox(height: 16),
            Audio3DWidget(
              sound: currentSound,
              width: 400,
              height: 400,
            ),
          ],
        ),
      ),
    );
  }

  Future<void> play() async {
    /// Start audio engine if not already
    if (!AudioIsolate().isIsolateRunning()) {
      await AudioIsolate().startIsolate().then((value) {
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
      if ((await AudioIsolate().stopSound(currentSound!)) !=
          PlayerErrors.noError) return;
    }

    /// load the audio file
    final f = await getAssetFile('assets/audio/siren.mp3');
    final loadRet = await AudioIsolate().loadFile(f.path);
    if (loadRet.error != PlayerErrors.noError) return;
    currentSound = loadRet.sound;

    /// play it
    final playRet = await AudioIsolate().play3d(currentSound!, 0, 0, 0);
    if (loadRet.error != PlayerErrors.noError) return;
    AudioIsolate().setLooping(playRet.newHandle, true);
    currentSound = playRet.sound;

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
}

class Audio3DWidget extends StatefulWidget {
  const Audio3DWidget({
    required this.width,
    required this.height,
    this.sound,
    super.key,
  });

  final SoundProps? sound;
  final double width;
  final double height;

  @override
  State<Audio3DWidget> createState() => _Audio3DWidgetState();
}

class _Audio3DWidgetState extends State<Audio3DWidget> {
  final doppler = ValueNotifier(true);
  Duration precTime = Duration.zero;
  double posX = 0;
  double posY = 0;
  double posZ = 0;
  double velX = 0;
  double velY = 0;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Listener(
          onPointerDown: (event) {
            precTime = Duration.zero;
          },
          onPointerMove: (event) {
            posX = event.localPosition.dx - widget.width / 2;
            posY = event.localPosition.dy - widget.height / 2;
            velX = 100 *
                event.delta.dx /
                (event.timeStamp.inMilliseconds - precTime.inMilliseconds);
            velY = 100 *
                event.delta.dy /
                (event.timeStamp.inMilliseconds - precTime.inMilliseconds);
            if (widget.sound != null) {
              AudioIsolate().set3dSourceParameters(
                widget.sound!.handle.first,
                posX,
                posY,
                posZ,
                doppler.value ? velX : 0,
                doppler.value ? velY : 0,
                0,
              );
            }

            precTime = event.timeStamp;

            if (mounted) {
              setState(() {});
            }
          },
          child: Stack(
            alignment: Alignment.center,
            children: [
              RepaintBoundary(
                child: CustomPaint(
                  size: Size(widget.width, widget.height),
                  painter: Audio3DPainter(
                    posX: posX,
                    posY: posY,
                    posZ: posZ,
                  ),
                ),
              ),
              const Icon(Icons.accessibility),
            ],
          ),
        ),
        const SizedBox(height: 16),
        ValueListenableBuilder(
          valueListenable: doppler,
          builder: (_, d, __) {
            return Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Checkbox(
                  value: d,
                  onChanged: (value) {
                    doppler.value = value!;
                    if (mounted) setState(() {});
                  },
                ),
                const SizedBox(width: 12),
                const Text('enable/disable doppler effect'),
              ],
            );
          },
        ),
        Text('Pos x: ${posX.toStringAsFixed(1)}'),
        Text('Pos y: ${posY.toStringAsFixed(1)}'),
        Text('Pos z: ${posZ.toStringAsFixed(1)}'),
        Text('Velocity x: ${velX.toStringAsFixed(1)}'),
        Text('Velocity y: ${velY.toStringAsFixed(1)}'),
        const Text('Velocity z: 0'),
      ],
    );
  }
}

/// Custom painter to draw the wave in a circle
///
class Audio3DPainter extends CustomPainter {
  Audio3DPainter({
    required this.posX,
    required this.posY,
    required this.posZ,
  });

  double posX;
  double posY;
  double posZ;

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Colors.green
      ..style = PaintingStyle.stroke;

    /// draw background circle
    canvas
      ..drawCircle(
        Offset(size.width / 2, size.height / 2),
        size.height / 2,
        paint,
      )

      /// draw cross axis
      ..drawLine(
        Offset(size.width / 2, 0),
        Offset(size.width / 2, size.height),
        paint,
      )
      ..drawLine(
        Offset(0, size.height / 2),
        Offset(size.width, size.height / 2),
        paint,
      )

      /// draw sound position
      ..drawCircle(
        Offset(posX + size.width / 2, posY + size.height / 2),
        5,
        paint,
      );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) {
    return true;
  }
}

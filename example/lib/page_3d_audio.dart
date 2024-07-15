import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

class Page3DAudio extends StatefulWidget {
  const Page3DAudio({super.key});

  @override
  State<Page3DAudio> createState() => _Page3DAudioState();
}

class _Page3DAudioState extends State<Page3DAudio> {
  static final Logger _log = Logger('_Page3DAudioState');

  AudioSource? currentSound;
  bool spinAround = false;

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: Column(
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                ElevatedButton(
                  onPressed: playFromUrl,
                  child: const Text('spin around\nsound from network'),
                ),
                const SizedBox(width: 16),
                ElevatedButton(
                  onPressed: () =>
                      play((MediaQuery.sizeOf(context).width - 20) / 2),
                  child: const Text('play'),
                ),
              ],
            ),
            const SizedBox(height: 16),
            Audio3DWidget(
              key: UniqueKey(),
              spinAround: spinAround,
              sound: currentSound,
              width: MediaQuery.sizeOf(context).width * .8 - 20,
              height: MediaQuery.sizeOf(context).width * .8 - 20,
            ),
          ],
        ),
      ),
    );
  }

  /// Play an audio downloaded from url
  ///
  Future<void> playFromUrl() async {
    /// stop any previous sound loaded
    if (currentSound != null) {
      try {
        await SoLoud.instance.disposeSource(currentSound!);
      } catch (e) {
        _log.severe('error disposing sound: $e');
        return;
      }
    }

    /// load the audio file
    currentSound = await SoLoud.instance.loadUrl(
      // From https://freetestdata.com/audio-files/mp3/
      'https://marcobavagnoli.com/Free_Test_Data_500KB_MP3.mp3',
    );

    /// play it
    await SoLoud.instance.play3d(currentSound!, 0, 0, 0, looping: true);

    spinAround = true;

    if (mounted) setState(() {});
  }

  /// Play the audio setting min and max distance and attenuation
  ///
  Future<void> play(double maxDistance) async {
    /// stop any previous sound loaded
    if (currentSound != null) {
      try {
        await SoLoud.instance.disposeSource(currentSound!);
      } catch (e) {
        _log.severe('error disposing sound: $e');
        return;
      }
    }

    /// load the audio file
    currentSound = await SoLoud.instance.loadAsset('assets/audio/siren.mp3');

    /// play it
    final newHandle = await SoLoud.instance.play3d(
      currentSound!,
      0,
      0,
      0,
      looping: true,
    );

    SoLoud.instance.set3dSourceMinMaxDistance(
      newHandle,
      50,
      maxDistance,
    );
    SoLoud.instance.set3dSourceAttenuation(newHandle, 1, 0.5);

    spinAround = false;

    if (mounted) setState(() {});
  }
}

class Audio3DWidget extends StatefulWidget {
  const Audio3DWidget({
    required this.spinAround,
    required this.width,
    required this.height,
    this.sound,
    super.key,
  });

  final bool spinAround;
  final AudioSource? sound;
  final double width;
  final double height;

  @override
  State<Audio3DWidget> createState() => _Audio3DWidgetState();
}

class _Audio3DWidgetState extends State<Audio3DWidget>
    with SingleTickerProviderStateMixin {
  late Ticker ticker;
  final doppler = ValueNotifier(true);
  Duration precTime = Duration.zero;
  double posX = 0;
  double posY = -50;
  double posZ = 0;
  double velX = 0;
  double velY = 0;
  double animVel = 2;
  double angle = 0;
  Offset center = Offset.zero;
  late double dx;

  @override
  void initState() {
    super.initState();
    dx = animVel;
    ticker = Ticker(_tick);
    if (widget.sound != null) ticker.start();
  }

  @override
  void dispose() {
    ticker.dispose();
    super.dispose();
  }

  void _tick(Duration elapsed) {
    final prevX = posX;
    final prevY = posY;
    if (widget.spinAround) {
      final circleRadius = Offset(posX - center.dx, posY - center.dy).distance;
      angle += pi / (animVel / (circleRadius / widget.width)) / 50;
      posX = circleRadius * cos(angle);
      posY = circleRadius * sin(angle);
    } else {
      if (posX + dx > widget.width / 2) dx = -animVel;
      if (posX + dx < -widget.width / 2) dx = animVel;
      posX += dx;
    }
    updatePos(Offset(posX - prevX, posY - prevY), elapsed);
  }

  void updatePos(Offset delta, Duration timeStamp) {
    velX =
        -100 * delta.dx / (timeStamp.inMilliseconds - precTime.inMilliseconds);
    velY =
        -100 * delta.dy / (timeStamp.inMilliseconds - precTime.inMilliseconds);
    if (widget.sound != null) {
      SoLoud.instance.set3dSourceParameters(
        widget.sound!.handles.first,
        posX,
        posY,
        posZ,
        doppler.value ? velX : 0,
        doppler.value ? velY : 0,
        0,
      );
    }

    precTime = timeStamp;
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Listener(
          onPointerDown: (event) {
            precTime = Duration.zero;
            ticker.stop();
          },
          onPointerUp: (event) => ticker.start(),
          onPointerMove: (event) {
            posX = event.localPosition.dx - widget.width / 2;
            posY = event.localPosition.dy - widget.height / 2;
            updatePos(event.delta, event.timeStamp);
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
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5;

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
        size.width / 50,
        Paint()
          ..color = Colors.red
          ..style = PaintingStyle.fill,
      );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) {
    return true;
  }
}

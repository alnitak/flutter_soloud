import 'dart:ui' as ui;

import 'package:flutter/material.dart';

/// Draw the image painted using audio data
///
class PaintTexture extends StatelessWidget {
  const PaintTexture({
    required this.width,
    required this.height,
    required this.image,
    super.key,
  });

  final double width;
  final double height;
  final ui.Image image;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        RepaintBoundary(
          child: CustomPaint(
            size: Size(width, height),
            painter: DisplayUiImagePainter(
              image,
            ),
          ),
        ),
      ],
    );
  }
}

/// CustomPainter to draw the ui.Image
///
class DisplayUiImagePainter extends CustomPainter {
  DisplayUiImagePainter(
    this.image,
  );

  final ui.Image image;

  @override
  void paint(Canvas canvas, ui.Size size) {
    // canvas.drawImage(image, Offset.zero, Paint());
    paintImage(
      canvas: canvas,
      rect: Rect.fromLTRB(0, 0, size.width, size.height),
      image: image,
      fit: BoxFit.fill,
      filterQuality: FilterQuality.none,
    );
  }

  @override
  bool shouldRepaint(covariant DisplayUiImagePainter oldDelegate) {
    // return oldDelegate.shader != shader;
    return true;
  }
}

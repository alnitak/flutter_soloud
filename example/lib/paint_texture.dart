import 'package:flutter/material.dart';
import 'dart:ui' as ui;

class PaintTexture extends StatelessWidget {
  const PaintTexture({
    super.key,
    required this.text,
    required this.width,
    required this.height,
    required this.image,
  });

  final String text;
  final double width;
  final double height;
  final ui.Image image;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (text.isNotEmpty)
          Text(
            text,
            style: const TextStyle(fontWeight: FontWeight.bold),
          ),
        CustomPaint(
          size: Size(width, height),
          painter: DisplayUiImagePainter(
            image,
          ),
        ),
      ],
    );
  }
}

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

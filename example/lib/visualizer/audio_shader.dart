import 'dart:ui' as ui;

import 'package:flutter/material.dart';

/// Draw the the shader using the built image as a parameter
///
class AudioShader extends StatelessWidget {
  const AudioShader({
    required this.image,
    required this.shader,
    required this.iTime,
    super.key,
    this.width,
    this.height,
  });

  final double? width;
  final double? height;
  final ui.Image image;
  final ui.FragmentShader shader;
  final double iTime;

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: width,
          height: height,
          child: CustomPaint(
            // size: Size(width, height),
            painter: ShaderPainter(
              shader,
              image,
              iTime,
            ),
          ),
        ),
      ],
    );
  }
}

///
class ShaderPainter extends CustomPainter {
  ShaderPainter(
    this.shader,
    this.image,
    this.iTime,
  );

  final ui.Image image;
  final ui.FragmentShader shader;
  final double iTime;

  @override
  void paint(Canvas canvas, ui.Size size) {
    shader
      ..setImageSampler(0, image)
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setFloat(2, iTime);

    canvas.drawRect(
      Rect.fromLTWH(0, 0, size.width, size.height),
      Paint()..shader = shader,
    );
  }

  @override
  bool shouldRepaint(covariant ShaderPainter oldDelegate) {
    return oldDelegate.iTime != iTime;
  }
}

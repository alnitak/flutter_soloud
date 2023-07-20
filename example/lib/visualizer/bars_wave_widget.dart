import 'dart:ffi' as ffi;

import 'package:flutter/material.dart';

class BarsWaveWidget extends StatelessWidget {
  const BarsWaveWidget({
    required this.audioData,
    required this.width,
    required this.height,
    super.key,
  });

  final ffi.Pointer<ffi.Float> audioData;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    final barWidth = width / 256;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: width,
          height: height,
          color: Colors.black,
          child: Row(
            children: [
              for (int i = 0; i < 256; i++)
                Container(
                  width: barWidth,
                  height: (height * audioData[i + 256]).abs(),
                  color: Colors.yellow,
                )
            ],
          ),
        ),
      ],
    );
  }
}

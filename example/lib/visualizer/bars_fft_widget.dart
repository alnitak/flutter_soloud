import 'dart:ffi' as ffi;

import 'package:flutter/material.dart';

class BarsFftWidget extends StatelessWidget {
  const BarsFftWidget({
    required this.audioData,
    required this.minFreq,
    required this.maxFreq,
    required this.width,
    required this.height,
    super.key,
  });

  final ffi.Pointer<ffi.Float> audioData;
  final int minFreq;
  final int maxFreq;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    final barWidth = width / (maxFreq - minFreq);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: width,
          height: height,
          color: Colors.black,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              for (int i = minFreq; i < maxFreq; i++)
                Container(
                  width: barWidth,
                  height: (height * audioData[i]).abs(),
                  color: Colors.yellow,
                )
            ],
          ),
        ),
      ],
    );
  }
}

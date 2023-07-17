import 'dart:ffi' as ffi;

import 'package:flutter/material.dart';

class BarsWidget extends StatelessWidget {
  const BarsWidget({
    super.key,
    this.text = '',
    required this.audioData,
    required this.n,
    required this.width,
    required this.height,
    this.useFftData = false,
  });

  final String text;
  final ffi.Pointer<ffi.Float> audioData;
  final int n;
  final bool useFftData;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    final barWidth = width / n;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (text.isNotEmpty)
          Text(
            text,
            style: const TextStyle(fontWeight: FontWeight.bold),
          ),
        Container(
          width: width,
          height: height,
          color: Colors.black,
          child: Row(
            crossAxisAlignment:
                useFftData ? CrossAxisAlignment.end : CrossAxisAlignment.center,
            children: [
              for (int i = 0; i < n; i++)
                Container(
                  width: barWidth,
                  height: (height * audioData[i + (useFftData ? 0 : n)]).abs(),
                  color: Colors.yellow,
                )
            ],
          ),
        ),
      ],
    );
  }
}

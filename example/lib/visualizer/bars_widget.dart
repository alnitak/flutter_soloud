import 'dart:ffi' as ffi;

import 'package:flutter/material.dart';

class BarsWidget extends StatelessWidget {
  const BarsWidget({
    required this.audioData,
    required this.minFreq,
    required this.maxFreq,
    required this.width,
    required this.height,
    super.key,
    this.text = '',
    this.useFftData = false,
  });

  final String text;
  final ffi.Pointer<ffi.Float> audioData;
  final int minFreq;
  final int maxFreq;
  final bool useFftData;
  final double width;
  final double height;

  @override
  Widget build(BuildContext context) {
    final barWidth = width / (useFftData ? (maxFreq - minFreq) : 256);

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
              for (int i = useFftData ? minFreq : 0;
                  i < (useFftData ? maxFreq : 255);
                  i++)
                Container(
                  width: barWidth,
                  height:
                      (height * audioData[i + (useFftData ? 0 : 256)]).abs(),
                  color: Colors.yellow,
                )
            ],
          ),
        ),
      ],
    );
  }
}

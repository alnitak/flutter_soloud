// ignore_for_file: public_member_api_docs, sort_constructors_first
import 'dart:async';

import 'package:flutter/material.dart';

import 'package:flutter_soloud/flutter_soloud.dart';
class BufferBar extends StatefulWidget {
  const BufferBar({
    super.key,
    this.sound,
  });

  final AudioSource? sound;

  @override
  State<BufferBar> createState() => _BufferBarState();
}

class _BufferBarState extends State<BufferBar> {
  final height = 30.0;
  Timer? timer;
  int currentMaxBytes = 1024 * 1024 * 10; // 10 MB

  @override
  void initState() {
    super.initState();
    timer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      setState(() {});
    });
  }

  @override
  Widget build(BuildContext context) {
    if (widget.sound == null) {
      return const SizedBox.shrink();
    }

    final int bufferSize;
    try {
      bufferSize = SoLoud.instance.getBufferSize(widget.sound!);
    } on Exception catch (_) {
      return const SizedBox.shrink();
    }
    if (bufferSize >= currentMaxBytes) {
      currentMaxBytes += 1024 * 1024 * 10;
    }

    /// [soundLength] reflects the value of [progressValue].
    final soundLength = SoLoud.instance.getLength(widget.sound!);
    final humanLength = 
        '${soundLength.inMinutes % 60}:'
        '${(soundLength.inSeconds % 60).toString().padLeft(2, '0')}.'
        '${(soundLength.inMilliseconds % 1000).toString().padLeft(3, '0')}';

    /// [handlesPos] reflects the handles position between start and
    /// [progressValue] in percentage.
    final handlesPos = <double>[];
    for (var i = 0; i < widget.sound!.handles.length; i++) {
      handlesPos.add(
        SoLoud.instance
                .getPosition(widget.sound!.handles.elementAt(i))
                .inMilliseconds /
            soundLength.inMilliseconds,
      );
    }

    final progressValue = bufferSize > 0.0 ? bufferSize / currentMaxBytes : 0.0;

    return Padding(
      padding: const EdgeInsets.all(8),
      child: Row(
        children: [
          SizedBox(
            width: 90,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('${(bufferSize / 1024 / 1024).toStringAsFixed(1)} MB'),
                Text(humanLength),
              ],
            ),
          ),
          Stack(
            children: [
              SizedBox(
                height: height,
                width: 400,
                child: LinearProgressIndicator(
                  value: progressValue,
                  backgroundColor: Colors.black,
                  valueColor: const AlwaysStoppedAnimation(Colors.red),
                  minHeight: height,
                  borderRadius: BorderRadius.circular(height / 3),
                ),
              ),
              for (var i = 0; i < handlesPos.length; i++)
                Positioned(
                  left: handlesPos[i] * progressValue * 400,
                  child: SizedBox(
                    height: height,
                    width: 3,
                    child: const ColoredBox(color: Colors.yellowAccent),
                  ),
                ),
            ],
          ),
        ],
      ),
    );
  }
}

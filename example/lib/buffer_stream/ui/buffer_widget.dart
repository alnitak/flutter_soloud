// ignore_for_file: public_member_api_docs, sort_constructors_first
import 'dart:async';

import 'package:flutter/material.dart';

import 'package:flutter_soloud/flutter_soloud.dart';

class BufferBar extends StatefulWidget {
  const BufferBar({
    super.key,
    this.label = '',
    this.sound,
    this.startingMb = 10,
  });

  final String? label;
  final AudioSource? sound;
  final int startingMb;

  @override
  State<BufferBar> createState() => _BufferBarState();
}

class _BufferBarState extends State<BufferBar> {
  final width = 250.0;
  final height = 30.0;
  Timer? timer;
  int currentMaxBytes = 1024 * 1024; // 1 MB

  @override
  void initState() {
    currentMaxBytes *= widget.startingMb;
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

    /// Increase the widget width when the buffer size increases.
    if (bufferSize >= currentMaxBytes) {
      currentMaxBytes += 1024 * 1024 * widget.startingMb;
    }

    /// [soundLength] reflects the value of [progressValue].
    final soundLength = SoLoud.instance.getLength(widget.sound!);
    final humanDuration = '${soundLength.inMinutes % 60}:'
        '${(soundLength.inSeconds % 60).toString().padLeft(2, '0')}.'
        '${(soundLength.inMilliseconds % 1000).toString().padLeft(3, '0')}';

    /// The current progress value
    final progressValue = bufferSize > 0.0 ? bufferSize / currentMaxBytes : 0.0;

    /// [handlesPos] reflects the handles position between start and
    /// [soundLength] in percentage.
    final handlesPos = <double>[];
    for (var i = 0; i < widget.sound!.handles.length; i++) {
      final hp =
          SoLoud.instance.getPosition(widget.sound!.handles.elementAt(i));
      handlesPos.add(
        soundLength.inMilliseconds == 0
            ? 0
            : hp.inMilliseconds / soundLength.inMilliseconds,
      );
    }

    final mb = (bufferSize / 1024 / 1024).toStringAsFixed(1);

    return Padding(
      padding: const EdgeInsets.all(8),
      child: Row(
        children: [
          SizedBox(
            width: 150,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                if (widget.label != null && widget.label!.isNotEmpty)
                  Text(
                    widget.label!,
                    style: const TextStyle(fontWeight: FontWeight.bold),
                  ),
                Text('using $mb MB'),
                Text('length $humanDuration'),
              ],
            ),
          ),
          ColoredBox(
            color: Colors.grey,
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Stack(
                children: [
                  SizedBox(
                    height: height,
                    width: width,
                    child: LinearProgressIndicator(
                      value: progressValue,
                      backgroundColor: Colors.black,
                      valueColor: const AlwaysStoppedAnimation(Colors.red),
                      minHeight: height,
                    ),
                  ),
                  for (var i = 0; i < handlesPos.length; i++)
                    Positioned(
                      left: handlesPos[i] * progressValue * width,
                      child: SizedBox(
                        height: height,
                        width: 3,
                        child: const ColoredBox(color: Colors.yellowAccent),
                      ),
                    ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

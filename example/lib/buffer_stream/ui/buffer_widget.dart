// ignore_for_file: public_member_api_docs, sort_constructors_first
import 'dart:async';

import 'package:flutter/material.dart';

import 'package:flutter_soloud/flutter_soloud.dart';

class BufferBar extends StatefulWidget {
  const BufferBar({
    required this.bufferingType,
    required this.isBuffering,
    super.key,
    this.label = '',
    this.sound,
    this.startingMb = 10,
  });

  final String? label;
  final AudioSource? sound;
  final int startingMb;
  final BufferingType bufferingType;
  final bool isBuffering;

  @override
  State<BufferBar> createState() => _BufferBarState();
}

class _BufferBarState extends State<BufferBar> {
  final width = 300.0;
  final height = 30.0;
  Timer? timer;
  int currentMaxBytes = 1024 * 1024; // 1 MB
  String firstHandleHumanPos = '';
  final pause = ValueNotifier<bool>(true);

  @override
  void initState() {
    currentMaxBytes *= widget.startingMb;
    super.initState();
    timer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (context.mounted) setState(() {});
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
    final humanDuration = toHuman(soundLength);
    final firstHandlePos = widget.bufferingType == BufferingType.preserved
        ? (widget.sound!.handles.isNotEmpty
            ? SoLoud.instance.getPosition(widget.sound!.handles.first)
            : Duration.zero)
        : SoLoud.instance.getStreamTimeConsumed(widget.sound!);
    firstHandleHumanPos = toHuman(firstHandlePos);

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

    return Column(
      mainAxisSize: MainAxisSize.min,
      spacing: 8,
      children: [
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            if (widget.label != null && widget.label!.isNotEmpty)
              Text(
                widget.label!,
                style: const TextStyle(fontWeight: FontWeight.bold),
              ),
            Text('using: $bufferSize b = $mb MB'),
            Text('length: $humanDuration'),
            Text('pos: $firstHandleHumanPos'),
          ],
        ),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            ColoredBox(
              color: Colors.grey,
              child: Padding(
                padding: const EdgeInsets.all(10),
                child: Stack(
                  children: [
                    GestureDetector(
                      onTapDown: (details) {
                        final bufferedWidth = progressValue * width;
                        if (bufferedWidth <= 0) return;

                        final relative =
                            (details.localPosition.dx / bufferedWidth)
                                .clamp(0.0, 1.0);
                        final touchPosition = Duration(
                          milliseconds:
                              (relative * soundLength.inMilliseconds).round(),
                        );
                        debugPrint(
                          'Touched progress bar at '
                          '${relative.toStringAsFixed(3)} -> $touchPosition',
                        );
                        if (widget.sound!.handles.isNotEmpty) {
                          SoLoud.instance.seek(
                            widget.sound!.handles.first,
                            touchPosition,
                          );
                        }
                      },
                      child: SizedBox(
                        height: height,
                        width: width,
                        child: LinearProgressIndicator(
                          value: progressValue,
                          backgroundColor: Colors.black,
                          valueColor: const AlwaysStoppedAnimation(Colors.red),
                          minHeight: height,
                          borderRadius: BorderRadius.circular(5),
                        ),
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

            // Play/pause button
            if (widget.sound!.handles.isNotEmpty)
              ValueListenableBuilder(
                valueListenable: pause,
                builder: (context, value, child) {
                  return IconButton(
                    onPressed: () async {
                      SoLoud.instance.setPause(
                        widget.sound!.handles.first,
                        value,
                      );
                      pause.value = !value;
                    },
                    icon: Icon(value ? Icons.pause : Icons.play_arrow),
                  );
                },
              ),
          ],
        ),
      ],
    );
  }

  String toHuman(Duration time) {
    return '${time.inMinutes % 60}:'
        '${(time.inSeconds % 60).toString().padLeft(2, '0')}.'
        '${(time.inMilliseconds % 1000).toString().padLeft(3, '0')}';
  }
}

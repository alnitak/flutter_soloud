import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

class SeekBar extends StatefulWidget {
  const SeekBar({required this.source, super.key});

  final AudioSource? source;

  @override
  State<SeekBar> createState() => _SeekBarState();
}

class _SeekBarState extends State<SeekBar> with SingleTickerProviderStateMixin {
  late Ticker _ticker;
  int _position = 0;
  int _length = 0;

  @override
  void initState() {
    super.initState();
    _ticker = Ticker(_onTick);
    _ticker.start();
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }

  void _onTick(Duration elapsed) {
    if (widget.source == null) return;
    setState(() {
      _position = SoLoud.instance
          .getPosition(widget.source!.handles.first)
          .inMilliseconds;
      _length = SoLoud.instance.getLength(widget.source!).inMilliseconds;
    });
  }

  @override
  Widget build(BuildContext context) {
    if (widget.source == null) return const SizedBox.shrink();

    return Row(
      children: [
        Text(
          '$_position',
          style: const TextStyle(fontWeight: FontWeight.bold),
        ),
        Expanded(
          child: Slider.adaptive(
            value: _position.toDouble(),
            max: _length.toDouble(),
            onChanged: (value) {
              SoLoud.instance.seek(
                widget.source!.handles.first,
                Duration(milliseconds: value.toInt()),
              );
            },
          ),
        ),
        Text(
          '$_length',
          style: const TextStyle(fontWeight: FontWeight.bold),
        ),
      ],
    );
  }
}

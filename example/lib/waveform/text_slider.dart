import 'package:flutter/material.dart';

/// Custom slider with text
///
class TextSlider extends StatelessWidget {
  const TextSlider({
    required this.text,
    required this.min,
    required this.max,
    required this.value,
    required this.enabled,
    required this.onChanged,
    this.isDivided = false,
    super.key,
  });

  final String text;
  final double min;
  final double max;
  final double value;
  final bool enabled;
  final bool isDivided;
  final void Function(double) onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Text(
          '$text: ${isDivided ? value.toInt() : value.toStringAsFixed(2)}\t',
        ),
        Expanded(
          child: Slider.adaptive(
            value: value,
            min: min,
            max: max,
            divisions: isDivided ? 4 : null,
            onChanged: enabled ? onChanged : null,
          ),
        ),
      ],
    );
  }
}

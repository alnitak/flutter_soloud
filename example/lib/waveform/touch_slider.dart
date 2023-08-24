import 'package:flutter/material.dart';

class TouchSlider extends StatelessWidget {
  const TouchSlider({
    required this.diameter,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.text = '',
    super.key,
  });

  final String text;
  final double diameter;
  final double value;
  final double min;
  final double max;
  final void Function(double newValue) onChanged;

  @override
  Widget build(BuildContext context) {
    final newValue = ValueNotifier<double>(value);
    final step = max == double.maxFinite ? 0.1 : (max - min) / (diameter * 3);
    newValue.value = value;
    return GestureDetector(
      onPanUpdate: (event) {
        newValue.value += event.delta.dx * step;
        if (newValue.value > max) newValue.value = max;
        if (newValue.value < min) newValue.value = min;
        onChanged(newValue.value);
      },
      child: ValueListenableBuilder<double>(
        valueListenable: newValue,
        builder: (_, val, __) {
          return Container(
            width: diameter,
            height: diameter,
            decoration: BoxDecoration(
              color: Colors.white,
              border: Border.all(width: 3),
              borderRadius: BorderRadius.circular(diameter / 2),
            ),
            child: FittedBox(
              child: SizedBox(
                width: 130,
                height: 130,
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      text,
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                        fontWeight: FontWeight.bold,
                        fontSize: 18,
                        color: Colors.black,
                      ),
                    ),
                    const Text('<-->', style: TextStyle(color: Colors.black)),
                    const SizedBox(height: 4),
                    Text(
                      val.toStringAsFixed(3),
                      style: const TextStyle(
                        fontWeight: FontWeight.bold,
                        color: Colors.black,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}

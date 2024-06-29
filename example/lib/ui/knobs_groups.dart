import 'package:flutter/material.dart';
import 'package:flutter_soloud_example/ui/touch_slider.dart';

class KnobsGroup extends StatefulWidget {
  const KnobsGroup({
    required this.texts,
    required this.values,
    required this.mins,
    required this.maxs,
    required this.onChanges,
    super.key,
  }) : assert(
          texts.length == values.length &&
              texts.length == mins.length &&
              texts.length == maxs.length &&
              texts.length == onChanges.length,
          'Sizes mismatch!',
        );

  final List<String> texts;
  final List<double> values;
  final List<double> mins;
  final List<double> maxs;
  final List<void Function(double value)> onChanges;

  @override
  State<KnobsGroup> createState() => _KnobsGroupState();
}

class _KnobsGroupState extends State<KnobsGroup> {
  late List<double> values;
  @override
  void initState() {
    super.initState();
    values = widget.values;
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: List.generate(widget.texts.length, (i) {
        return TouchSlider(
          text: widget.texts[i],
          diameter: 80,
          min: widget.mins[i],
          max: widget.maxs[i],
          value: values[i],
          onChanged: (value) {
            widget.onChanges[i](value);
            values[i] = value;
            if (mounted) setState(() {});
          },
        );
      }),
    );
  }
}

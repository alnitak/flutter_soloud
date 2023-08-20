import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/waveform/touch_slider.dart';

class FilterFx extends StatefulWidget {
  const FilterFx({
    required this.filterType,
    super.key,
  });

  final FilterType filterType;
  @override
  State<FilterFx> createState() => _FilterFxState();
}

class _FilterFxState extends State<FilterFx> {
  late bool enabled;
  late FxParams fxParams;
  late List<double> params;

  @override
  void initState() {
    super.initState();
    enabled = SoLoud().isFilterActive(widget.filterType).index != -1;

    switch (widget.filterType) {
      case FilterType.biquadResonantFilter:
        params = List.from(fxBiquadResonant.defs);
        fxParams = fxBiquadResonant;
      case FilterType.eqFilter:
        params = List.from(fxEq.defs);
        fxParams = fxEq;
      case FilterType.echoFilter:
        params = List.from(fxEcho.defs);
        fxParams = fxEcho;
      case FilterType.lofiFilter:
        params = List.from(fxLofi.defs);
        fxParams = fxLofi;
      case FilterType.flangerFilter:
        params = List.from(fxFlanger.defs);
        fxParams = fxFlanger;
      case FilterType.dcRemovalFilter:
        params = List.from(fxDCRemoval.defs);
        fxParams = fxDCRemoval;
      case FilterType.bassboostFilter:
        params = List.from(fxBassboost.defs);
        fxParams = fxBassboost;
      case FilterType.waveShaperFilter:
        params = List.from(fxWaveShaper.defs);
        fxParams = fxWaveShaper;
      case FilterType.robotizeFilter:
        params = List.from(fxRobotize.defs);
        fxParams = fxRobotize;
      case FilterType.freeverbFilter:
        params = List.from(fxFreeverb.defs);
        fxParams = fxFreeverb;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        /// enable / disable FX
        Row(
          children: [
            Text(fxParams.title),
            Checkbox.adaptive(
              value: enabled,
              onChanged: (value) {
                enabled = value!;
                if (enabled) {
                  if (SoLoud().addGlobalFilter(widget.filterType) !=
                      PlayerErrors.noError) {
                    enabled = false;
                  } else {
                    for (var i = 0; i < params.length; i++) {
                      params[i] = SoLoud().getFxParams(widget.filterType, i);
                    }
                  }
                } else {
                  if (SoLoud().removeGlobalFilter(widget.filterType) !=
                      PlayerErrors.noError) enabled = true;
                }
                if (mounted) setState(() {});
              },
            ),
          ],
        ),

        /// Params knobs
        Wrap(
          children: List.generate(params.length, (index) {
            return TouchSlider(
              text: fxParams.names[index],
              diameter: 120,
              min: fxParams.mins[index],
              max: fxParams.maxs[index],
              value: params[index],
              onChanged: (value) async {
                params[index] = value;
                SoLoud().setFxParams(widget.filterType, index, value);
                if (mounted) setState(() {});
              },
            );
          }),
        ),
      ],
    );
  }
}

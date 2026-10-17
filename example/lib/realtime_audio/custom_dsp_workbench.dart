// ignore_for_file: experimental_member_use

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Example 3: Multi-Instance CustomFilter & CustomAudioSource Workbench
///
/// Demonstrates:
/// - Chaining multiple `CustomFilter` instances (`customFilter(0)`,
///   `customFilter(1)`) on the global mixer simultaneously.
/// - Registering custom parameter metadata with `CustomFilterParamSpec`.
/// - Controlling the hardcoded 3-branch `wet` (`param[0]`) behavior:
///   * `wet == 0.0` -> hard bypass (callback is not called).
///   * `0.0 < wet < 1.0` -> automatic dry/wet blend.
///   * `wet == 1.0` -> zero-copy in-place processing.
/// - Automating custom filter parameters via `fadeFilterParameter` and
///   `oscillateFilterParameter`.
/// - Creating a procedural `CustomAudioSource` via `createCustomAudioSource`.
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init();
  runApp(const MaterialApp(home: CustomDspWorkbenchExample()));
}

class CustomDspWorkbenchExample extends StatefulWidget {
  const CustomDspWorkbenchExample({super.key});

  @override
  State<CustomDspWorkbenchExample> createState() =>
      _CustomDspWorkbenchExampleState();
}

class _CustomDspWorkbenchExampleState extends State<CustomDspWorkbenchExample> {
  final SoLoud _soloud = SoLoud.instance;

  AudioSource? _waveSource;
  AudioSource? _customSynthSource;
  SoundHandle? _activeHandle;
  SoundHandle? _customSynthHandle;

  double _slot0Wet = 1;
  double _slot0Drive = 2.5;
  double _slot1Wet = 0.5;
  double _slot1Cutoff = 1200;

  @override
  void initState() {
    super.initState();
    _initWorkbench();
  }

  Future<void> _initWorkbench() async {
    // 1. Load a built-in waveform source to test the custom filter chain.
    final wave = await _soloud.loadWaveform(
      WaveForm.fSquare,
      true,
      1,
      0.5,
    );
    SoLoud.instance.setWaveformFreq(wave, 440);
    // final wave = await _soloud.loadAsset('assets/audio/8_bit_mentality.mp3');

    // 2. Create a CustomAudioSource (registered with a Web JS callback name or
    //    native C callback pointer when provided by a native plugin).
    final customSynth = _soloud.createCustomAudioSource(
      webJsFunctionName: 'soloudCustomSynthCallback',
    );

    // 3. Configure Slot 0: Soft-Clipping Overdrive CustomFilter
    final slot0 = _soloud.filters.customFilter(0)
      ..configureParameters(const <CustomFilterParamSpec>[
        CustomFilterParamSpec(
          name: 'Drive',
          min: 1,
          max: 10,
          defaultValue: 2.5,
        ),
        CustomFilterParamSpec(
          name: 'Asymmetry',
          min: -0.5,
          max: 0.5,
          defaultValue: 0,
        ),
      ])
      ..setNativeCallback(webJsFunctionName: 'soloudOverdriveFilterCallback')
      ..activate();

    // 4. Configure Slot 1: State-Variable Filter CustomFilter
    final slot1 = _soloud.filters.customFilter(1)
      ..configureParameters(const <CustomFilterParamSpec>[
        CustomFilterParamSpec(
          name: 'CutoffHz',
          min: 80,
          max: 16000,
          defaultValue: 1200,
        ),
        CustomFilterParamSpec(
          name: 'Resonance',
          min: 0.1,
          max: 0.95,
          defaultValue: 0.4,
        ),
      ])
      ..setNativeCallback(webJsFunctionName: 'soloudSvfFilterCallback')
      ..activate();

    slot0.wet.value = _slot0Wet;
    slot0.param(1, min: 1, max: 10).value = _slot0Drive;
    slot1.wet.value = _slot1Wet;
    slot1.param(1, min: 80, max: 16000).value = _slot1Cutoff;

    if (mounted) {
      setState(() {
        _waveSource = wave;
        _customSynthSource = customSynth;
      });
    }
  }

  Future<void> _togglePlayback() async {
    final wave = _waveSource;
    if (wave == null) return;

    if (_activeHandle != null &&
        _soloud.getIsValidVoiceHandle(_activeHandle!)) {
      await _soloud.stop(_activeHandle!);
      setState(() => _activeHandle = null);
    } else {
      final h = _soloud.play(wave, looping: true, volume: 0.6);
      setState(() => _activeHandle = h);
    }
  }

  Future<void> _toggleCustomSynthPlayback() async {
    final customSynth = _customSynthSource;
    if (customSynth == null) return;

    if (_customSynthHandle != null &&
        _soloud.getIsValidVoiceHandle(_customSynthHandle!)) {
      await _soloud.stop(_customSynthHandle!);
      setState(() => _customSynthHandle = null);
    } else {
      final h = _soloud.play(customSynth, volume: 0.7);
      setState(() => _customSynthHandle = h);
    }
  }

  @override
  void dispose() {
    for (var i = 0; i < 2; i++) {
      final slot = _soloud.filters.customFilter(i);
      if (slot.isActive) {
        slot.deactivate();
      }
    }
    if (_waveSource != null) {
      unawaited(_soloud.disposeSource(_waveSource!));
    }
    if (_customSynthSource != null) {
      unawaited(_soloud.disposeSource(_customSynthSource!));
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final slot0 = _soloud.filters.customFilter(0);
    final slot1 = _soloud.filters.customFilter(1);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Multi-Instance CustomFilter Workbench'),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 16,
          children: [
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                ElevatedButton.icon(
                  onPressed: _togglePlayback,
                  icon: Icon(
                    _activeHandle != null ? Icons.stop : Icons.play_arrow,
                  ),
                  label: Text(
                    _activeHandle != null
                        ? 'Stop Test Synth'
                        : 'Play Test Synth',
                  ),
                ),
                ElevatedButton.icon(
                  onPressed: _toggleCustomSynthPlayback,
                  icon: Icon(
                    _customSynthHandle != null ? Icons.stop : Icons.graphic_eq,
                  ),
                  label: Text(
                    _customSynthHandle != null
                        ? 'Stop CustomAudioSource'
                        : 'Play CustomAudioSource',
                  ),
                ),
                OutlinedButton(
                  onPressed: () {
                    slot0.wet.fadeFilterParameter(
                      to: _slot0Wet > 0 ? 0 : 1,
                      time: const Duration(milliseconds: 600),
                    );
                    setState(() => _slot0Wet = _slot0Wet > 0 ? 0 : 1);
                  },
                  child: const Text('Fade Slot 0 Wet (600 ms)'),
                ),
                OutlinedButton(
                  onPressed: () {
                    slot1
                        .param(1, min: 80, max: 16000)
                        .oscillateFilterParameter(
                          from: 200,
                          to: 6000,
                          time: const Duration(seconds: 2),
                        );
                  },
                  child: const Text('Oscillate Slot 1 Cutoff (2 s)'),
                ),
              ],
            ),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  spacing: 8,
                  children: [
                    Text(
                      'CustomFilter Slot 0 (Filter index: ${slot0.index})',
                      style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    Text(
                      _slot0Wet <= 0
                          ? 'Mode: Hard Bypass (callback skipped)'
                          : (_slot0Wet >= 1
                              ? 'Mode: 100% Wet (zero-copy in-place)'
                              : 'Mode: Dry/Wet Blend '
                                  '(${(_slot0Wet * 100).round()}% wet)'),
                    ),
                    Row(
                      children: [
                        const SizedBox(
                          width: 100,
                          child: Text('Wet (Param 0)'),
                        ),
                        Expanded(
                          child: Slider(
                            value: _slot0Wet,
                            onChanged: (v) {
                              slot0.wet.value = v;
                              setState(() => _slot0Wet = v);
                            },
                          ),
                        ),
                        SizedBox(
                          width: 60,
                          child: Text(_slot0Wet.toStringAsFixed(2)),
                        ),
                      ],
                    ),
                    Row(
                      children: [
                        const SizedBox(
                          width: 100,
                          child: Text('Drive (Param 1)'),
                        ),
                        Expanded(
                          child: Slider(
                            min: 1,
                            max: 10,
                            value: _slot0Drive,
                            onChanged: (v) {
                              slot0.param(1, min: 1, max: 10).value = v;
                              setState(() => _slot0Drive = v);
                            },
                          ),
                        ),
                        SizedBox(
                          width: 60,
                          child: Text(_slot0Drive.toStringAsFixed(2)),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  spacing: 8,
                  children: [
                    Text(
                      'CustomFilter Slot 1 (Filter index: ${slot1.index})',
                      style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    Row(
                      children: [
                        const SizedBox(
                          width: 100,
                          child: Text('Wet (Param 0)'),
                        ),
                        Expanded(
                          child: Slider(
                            value: _slot1Wet,
                            onChanged: (v) {
                              slot1.wet.value = v;
                              setState(() => _slot1Wet = v);
                            },
                          ),
                        ),
                        SizedBox(
                          width: 60,
                          child: Text(_slot1Wet.toStringAsFixed(2)),
                        ),
                      ],
                    ),
                    Row(
                      children: [
                        const SizedBox(
                          width: 100,
                          child: Text('Cutoff Hz'),
                        ),
                        Expanded(
                          child: Slider(
                            min: 80,
                            max: 16000,
                            value: _slot1Cutoff,
                            onChanged: (v) {
                              slot1.param(1, min: 80, max: 16000).value = v;
                              setState(() => _slot1Cutoff = v);
                            },
                          ),
                        ),
                        SizedBox(
                          width: 60,
                          child: Text('${_slot1Cutoff.round()} Hz'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

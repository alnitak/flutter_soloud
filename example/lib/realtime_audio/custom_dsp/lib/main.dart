// ignore_for_file: experimental_member_use

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'custom_dsp.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init();
  runApp(const MaterialApp(
    debugShowCheckedModeBanner: false,
    home: CustomDspExampleApp(),
  ));
}

class CustomDspExampleApp extends StatefulWidget {
  const CustomDspExampleApp({super.key});

  @override
  State<CustomDspExampleApp> createState() => _CustomDspExampleAppState();
}

class _CustomDspExampleAppState extends State<CustomDspExampleApp> {
  final SoLoud _soloud = SoLoud.instance;

  AudioSource? _customSource;
  SoundHandle? _activeHandle;

  bool _filterActive = true;
  double _wet = 1.0;
  double _drive = 2.5;
  String _platformInfo = 'Loading...';

  @override
  void initState() {
    super.initState();
    _initCustomDsp();
  }

  void _initCustomDsp() {
    try {
      // 1. Resolve custom DSP callbacks (Native FFI or Web JS)
      final cbs = loadCustomDspCallbacks();

      // 2. Create the procedural CustomAudioSource
      final source = cbs.createSource(_soloud);

      // 3. Configure the CustomFilter on global mixer slot 0
      final slot0 = _soloud.filters.customFilter(0);
      cbs.configureFilter(slot0, drive: _drive, wet: _wet);

      setState(() {
        _customSource = source;
        _platformInfo = cbs.isWeb
            ? 'Web JS Callbacks: ${cbs.sourceJsFunctionName} / ${cbs.filterJsFunctionName}'
            : 'Native C Callbacks: 0x${cbs.sourceCallbackAddress.toRadixString(16)}';
      });
    } catch (e) {
      setState(() {
        _platformInfo = 'Init Error: $e';
      });
    }
  }

  @override
  void dispose() {
    final handle = _activeHandle;
    if (handle != null && _soloud.getIsValidVoiceHandle(handle)) {
      _soloud.stop(handle);
    }
    final source = _customSource;
    if (source != null) {
      _soloud.disposeSource(source);
    }
    _soloud.filters.customFilter(0).deactivate();
    super.dispose();
  }

  void _togglePlayback() {
    final source = _customSource;
    if (source == null) return;

    final handle = _activeHandle;
    if (handle != null && _soloud.getIsValidVoiceHandle(handle)) {
      _soloud.stop(handle);
      setState(() => _activeHandle = null);
    } else {
      final newHandle = _soloud.play(source, volume: 0.7);
      setState(() => _activeHandle = newHandle);
    }
  }

  void _onWetChanged(double value) {
    setState(() => _wet = value);
    _soloud.filters.customFilter(0).wet.value = value;
  }

  void _onDriveChanged(double value) {
    setState(() => _drive = value);
    _soloud.filters.customFilter(0).param(1, min: 1, max: 10).value = value;
  }

  void _onFilterToggled(bool active) {
    setState(() => _filterActive = active);
    final slot = _soloud.filters.customFilter(0);
    if (active) {
      if (!slot.isActive) slot.activate();
      slot.wet.value = _wet;
    } else {
      slot.wet.value = 0.0; // Hard bypass without de-allocating
    }
  }

  @override
  Widget build(BuildContext context) {
    final isPlaying =
        _activeHandle != null && _soloud.getIsValidVoiceHandle(_activeHandle!);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Custom DSP Callbacks Example'),
        backgroundColor: Colors.blueGrey.shade900,
        foregroundColor: Colors.white,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(20),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 550),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Platform Info Badge
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                  decoration: BoxDecoration(
                    color: Colors.blueGrey.shade50,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.blueGrey.shade200),
                  ),
                  child: Row(
                    children: [
                      Icon(Icons.memory, size: 20, color: Colors.blueGrey.shade700),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          _platformInfo,
                          style: TextStyle(
                            fontFamily: 'monospace',
                            fontSize: 12,
                            color: Colors.blueGrey.shade900,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 20),

                // Card 1: Custom Audio Source
                Card(
                  elevation: 2,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Padding(
                    padding: const EdgeInsets.all(20),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(Icons.graphic_eq, color: Colors.indigo.shade700),
                            const SizedBox(width: 8),
                            const Text(
                              'Custom Audio Source',
                              style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 8),
                        Text(
                          'Generates a lush A-minor 9th ambient pad with stereo chorus modulation directly on the audio thread.',
                          style: TextStyle(color: Colors.grey.shade600, fontSize: 13),
                        ),
                        const SizedBox(height: 20),
                        SizedBox(
                          width: double.infinity,
                          height: 48,
                          child: ElevatedButton.icon(
                            style: ElevatedButton.styleFrom(
                              backgroundColor: isPlaying ? Colors.red.shade700 : Colors.indigo.shade700,
                              foregroundColor: Colors.white,
                              shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(8),
                              ),
                            ),
                            icon: Icon(isPlaying ? Icons.stop : Icons.play_arrow),
                            label: Text(
                              isPlaying ? 'Stop Custom Synth' : 'Play Custom Synth',
                              style: const TextStyle(fontWeight: FontWeight.bold),
                            ),
                            onPressed: _customSource == null ? null : _togglePlayback,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 20),

                // Card 2: Custom Filter
                Card(
                  elevation: 2,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Padding(
                    padding: const EdgeInsets.all(20),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Row(
                              children: [
                                Icon(Icons.tune, color: Colors.teal.shade700),
                                const SizedBox(width: 8),
                                const Text(
                                  'Custom Overdrive Filter',
                                  style: TextStyle(
                                    fontSize: 18,
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                              ],
                            ),
                            Switch(
                              value: _filterActive,
                              onChanged: _onFilterToggled,
                            ),
                          ],
                        ),
                        const SizedBox(height: 8),
                        Text(
                          'Applies real-time tanh soft-clipping saturation in-place.',
                          style: TextStyle(color: Colors.grey.shade600, fontSize: 13),
                        ),
                        const SizedBox(height: 16),

                        // Wet Slider
                        Text(
                          'Wet / Dry: ${(_wet * 100).toInt()}%',
                          style: const TextStyle(fontWeight: FontWeight.w600),
                        ),
                        Slider(
                          value: _wet,
                          min: 0.0,
                          max: 1.0,
                          divisions: 100,
                          label: '${(_wet * 100).toInt()}%',
                          onChanged: _filterActive ? _onWetChanged : null,
                        ),

                        // Drive Slider
                        Text(
                          'Drive: ${_drive.toStringAsFixed(1)}x',
                          style: const TextStyle(fontWeight: FontWeight.w600),
                        ),
                        Slider(
                          value: _drive,
                          min: 1.0,
                          max: 10.0,
                          divisions: 90,
                          label: _drive.toStringAsFixed(1),
                          onChanged: _filterActive ? _onDriveChanged : null,
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

import 'dart:developer' as dev;
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// This example shows the use of the amplitude modulator (ring modulator)
/// filter.
///
/// Amplitude modulation (specifically bipolar / suppressed-carrier ring
/// modulation) multiplies the incoming audio signal by a sine wave carrier
/// at a specified frequency:
///
///   output(t) = input(t) * sin(2π * frequency * t)
///
/// Because the carrier has no DC offset (unlike unipolar tremolo), the original
/// carrier and input fundamental frequencies are suppressed when wet is 1.0.
/// Instead, sum and difference sideband frequencies are generated
/// (`frequency - f_input` and `frequency + f_input`).
///
/// **Common Use Cases**:
/// - **Robotic & Dalek Voices**: Modulating human speech (e.g. vocal samples)
///   with a carrier between 30 Hz and 500 Hz creates classic sci-fi cyborg and
///   Dalek-style robotic speech.
/// - **Metallic & Bell-like Sound Effects**: Processing synthesizers, bells,
///   percussion, or music with carrier frequencies between 800 Hz and 3000 Hz
///   produces inharmonic, clangorous, and alien metallic timbres.
/// - **Tremolo & Flutter**: Low carrier frequencies (0.5 Hz to 20 Hz) combined
///   with a mixed wet/dry ratio produce pulsing, tremolo-like fluttering
///   effects.
/// - **Sci-Fi Frequency Sweeps**: Dynamically fading or oscillating the carrier
///   frequency (`fadeFilterParameter` / `oscillateFilterParameter`) produces
///   smooth retro-futuristic laser warps and sweeps without audio clicks,
///   thanks to continuous phase accumulation across buffers.
///
/// In this example we can load vocal speech or music samples, activate the
/// amplitude modulator filter, adjust wet mix and carrier frequency, or test
/// quick presets for the use-cases described above.
///
/// **Amplitude Modulator parameters**:
/// - `wet`: Wet/dry mix ratio, 1.0 means fully modulated (100% wet ring
///   modulation), 0.0 means bypass / dry original signal.
/// - `frequency`: Carrier frequency in Hz (0.1 Hz to 20,000 Hz, default:
///   440 Hz). Frequencies requested at or above the Nyquist limit of the
///   active sample rate are safely clamped just below it during processing
///   to prevent aliasing.

void main() async {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
  // You can listen to the logs as shown below.
  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
  });

  WidgetsFlutterBinding.ensureInitialized();

  /// Initialize the player.
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: AmplitudeModulatorExample(),
    ),
  );
}

/// Simple usecase of the AmplitudeModulatorFilter in flutter_soloud
class AmplitudeModulatorExample extends StatefulWidget {
  const AmplitudeModulatorExample({super.key});

  @override
  State<AmplitudeModulatorExample> createState() =>
      _AmplitudeModulatorExampleState();
}

class _AmplitudeModulatorExampleState extends State<AmplitudeModulatorExample> {
  final am = SoLoud.instance.filters.amplitudeModulatorFilter;
  AudioSource? voiceSound;
  AudioSource? musicSound;
  late double wet;
  late double frequency;
  bool isFilterActive = false;
  bool isOscillating = false;

  @override
  void initState() {
    super.initState();

    wet = am.queryWet.def;
    frequency = am.queryFrequency.def;
  }

  @override
  void dispose() {
    SoLoud.instance.stopAll();
    if (voiceSound != null) {
      SoLoud.instance.disposeSource(voiceSound!);
    }
    if (musicSound != null) {
      SoLoud.instance.disposeSource(musicSound!);
    }
    am.deactivate();
    SoLoud.instance.deinit();
    super.dispose();
  }

  void _applyParameters() {
    if (isFilterActive) {
      if (!am.isActive) {
        am.activate();
      }
      am.wet.value = wet;
      if (isOscillating) {
        am.frequency.oscillateFilterParameter(
          from: 100,
          to: 1500,
          time: const Duration(milliseconds: 2500),
        );
      } else {
        am.frequency.value = frequency;
      }
    }
  }

  void _setPreset({
    required double newFreq,
    required double newWet,
    bool oscillate = false,
  }) {
    setState(() {
      frequency = newFreq;
      wet = newWet;
      isOscillating = oscillate;
      if (!isFilterActive) {
        isFilterActive = true;
      }
    });

    if (!am.isActive) {
      am.activate();
    }
    am.wet.value = wet;
    if (oscillate) {
      am.frequency.oscillateFilterParameter(
        from: 100,
        to: 1500,
        time: const Duration(milliseconds: 2500),
      );
    } else {
      am.frequency.value = frequency;
    }
  }

  Future<void> _playVoice() async {
    SoLoud.instance.stopAll();
    voiceSound ??=
        await SoLoud.instance.loadAsset('assets/audio/IveSeenThings.mp3');
    if (isFilterActive) {
      if (!am.isActive) {
        am.activate();
      }
      _applyParameters();
    }
    SoLoud.instance.play(voiceSound!, looping: true);
  }

  Future<void> _playMusic() async {
    SoLoud.instance.stopAll();
    musicSound ??=
        await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
    if (isFilterActive) {
      if (!am.isActive) {
        am.activate();
      }
      _applyParameters();
    }
    SoLoud.instance.play(musicSound!, looping: true);
  }

  void _stopAll() {
    SoLoud.instance.stopAll();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    // Map frequency linearly between log10(min) and log10(max) for smoother
    // slider response.
    final minLog = math.log(am.queryFrequency.min) / math.ln10;
    final maxLog = math.log(am.queryFrequency.max) / math.ln10;
    final currentLog =
        (math.log(math.max(am.queryFrequency.min, frequency)) / math.ln10)
            .clamp(minLog, maxLog);

    final freqStr = frequency >= 100
        ? frequency.toStringAsFixed(0)
        : frequency.toStringAsFixed(1);
    final freqLabel = isOscillating ? 'Freq: Oscillating' : 'Freq: $freqStr Hz';

    return Scaffold(
      appBar: AppBar(
        title: const Text('Amplitude Modulator Filter Example'),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Text(
                  'Activate Filter',
                  style: TextStyle(fontWeight: FontWeight.bold),
                ),
                Checkbox(
                  value: isFilterActive,
                  onChanged: (value) {
                    final active = value ?? false;
                    if (active) {
                      am.activate();
                      _applyParameters();
                    } else {
                      am.deactivate();
                      isOscillating = false;
                    }
                    setState(() {
                      isFilterActive = active;
                    });
                  },
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _playVoice,
                    icon: const Icon(Icons.record_voice_over),
                    label: const Text('Play Voice'),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _playMusic,
                    icon: const Icon(Icons.music_note),
                    label: const Text('Play Music'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            ElevatedButton.icon(
              onPressed: _stopAll,
              icon: const Icon(Icons.stop),
              label: const Text('Stop All'),
            ),
            const Divider(height: 32),
            const Text(
              'Use-Case Presets',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 6,
              children: [
                ActionChip(
                  label: const Text('Dalek Voice (30 Hz)'),
                  onPressed: () => _setPreset(newFreq: 30, newWet: 1),
                ),
                ActionChip(
                  label: const Text('Robot Voice (440 Hz)'),
                  onPressed: () => _setPreset(newFreq: 440, newWet: 1),
                ),
                ActionChip(
                  label: const Text('Flutter / Tremolo (8 Hz)'),
                  onPressed: () => _setPreset(newFreq: 8, newWet: 0.6),
                ),
                ActionChip(
                  label: const Text('Metallic Bell (1200 Hz)'),
                  onPressed: () => _setPreset(newFreq: 1200, newWet: 1),
                ),
                ActionChip(
                  label: const Text('Sci-Fi Sweep (Oscillate)'),
                  onPressed: () => _setPreset(
                    newFreq: 500,
                    newWet: 1,
                    oscillate: true,
                  ),
                ),
              ],
            ),
            const Divider(height: 32),
            Row(
              children: [
                SizedBox(
                  width: 140,
                  child: Text('Wet: ${wet.toStringAsFixed(2)}'),
                ),
                Expanded(
                  child: Slider(
                    value: wet,
                    min: am.queryWet.min,
                    max: am.queryWet.max,
                    onChanged: (value) {
                      setState(() {
                        wet = value;
                        if (isFilterActive) {
                          am.wet.value = value;
                        }
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                SizedBox(
                  width: 140,
                  child: Text(freqLabel),
                ),
                Expanded(
                  child: Slider(
                    value: currentLog,
                    min: minLog,
                    max: maxLog,
                    onChanged: (value) {
                      final calculatedFreq = math.pow(10, value).toDouble();
                      setState(() {
                        frequency = calculatedFreq;
                        isOscillating = false;
                        if (isFilterActive) {
                          am.frequency.value = calculatedFreq;
                        }
                      });
                    },
                  ),
                ),
              ],
            ),
            Center(
              child: Text(
                'Frequency range: ${am.queryFrequency.min} Hz to '
                '${am.queryFrequency.max.toInt()} Hz (log scale)',
                style: const TextStyle(fontSize: 12, color: Colors.grey),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

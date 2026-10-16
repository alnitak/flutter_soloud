import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Example 1: Real-Time Zero-Latency PCM Synthesizer (`RealtimeStreamHandle`)
///
/// Demonstrates:
/// - Creating a lock-free real-time PCM stream via `setRealtimeStream`.
/// - Allocating a reusable zero-GC `RealtimeNativeBuffer`.
/// - Instantaneous injection into the currently playing mixer window when
///   triggering synth notes (`pushNativeFloat32`).
/// - Monitoring `availableToRead`, `availableToWrite`, and `underrunCount`.
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init(
    sampleRate: 48000,
    bufferSize: 512,
  );
  runApp(const MaterialApp(home: RealtimePcmSynthExample()));
}

class RealtimePcmSynthExample extends StatefulWidget {
  const RealtimePcmSynthExample({super.key});

  @override
  State<RealtimePcmSynthExample> createState() =>
      _RealtimePcmSynthExampleState();
}

class _RealtimePcmSynthExampleState extends State<RealtimePcmSynthExample> {
  final SoLoud _soloud = SoLoud.instance;

  RealtimeStreamHandle? _stream;
  RealtimeNativeBuffer? _nativeBuffer;
  Timer? _feedTimer;

  bool _droneEnabled = false;
  double _frequency = 220;
  double _detune = 0.01;
  double _phaseL = 0;
  double _phaseR = 0;

  static const int _sampleRate = 48000;
  static const int _chunkFrames = 960; // 20 ms chunks

  @override
  void initState() {
    super.initState();
    _initRealtimeStream();
  }

  void _initRealtimeStream() {
    final stream = _soloud.setRealtimeStream(
      bufferFrames: 8192,
    );
    final handle = _soloud.play(stream.source);
    stream.bindActiveVoice(handle);

    final nativeBuf = stream.allocateNativeBuffer(maxFrames: 4096);

    _feedTimer = Timer.periodic(
      const Duration(milliseconds: 10),
      (_) => _onFeedTick(),
    );

    if (mounted) {
      setState(() {
        _stream = stream;
        _nativeBuffer = nativeBuf;
      });
    }
  }

  void _onFeedTick() {
    final stream = _stream;
    final buf = _nativeBuffer;
    if (stream == null || buf == null) return;

    if (_droneEnabled) {
      // Keep ~40 ms buffered ahead when continuous drone mode is active.
      while (stream.availableToRead < _chunkFrames * 2 &&
          stream.availableToWrite >= _chunkFrames) {
        _synthesizeStereoSaw(buf, _chunkFrames, _frequency, _detune);
        stream.pushNativeFloat32(buf, _chunkFrames);
      }
    }

    if (mounted) {
      setState(() {});
    }
  }

  void _synthesizeStereoSaw(
    RealtimeNativeBuffer buf,
    int frames,
    double freq,
    double detuneRatio,
  ) {
    final out = buf.float32View;
    final incL = freq / _sampleRate;
    final incR = (freq * (1.0 + detuneRatio)) / _sampleRate;

    var pL = _phaseL;
    var pR = _phaseR;
    for (var i = 0; i < frames; i++) {
      // Soft-saturated stereo sawtooth
      final sL = (2.0 * pL - 1.0) * 0.25;
      final sR = (2.0 * pR - 1.0) * 0.25;
      out[i * 2] = sL;
      out[i * 2 + 1] = sR;

      pL += incL;
      if (pL >= 1.0) pL -= 1.0;
      pR += incR;
      if (pR >= 1.0) pR -= 1.0;
    }
    _phaseL = pL;
    _phaseR = pR;
  }

  /// Immediately synthesizes and injects a percussive pluck transient into the
  /// currently playing mixer window.
  void _triggerInstantPluck(double noteFreq) {
    final stream = _stream;
    final buf = _nativeBuffer;
    if (stream == null || buf == null) return;

    // Reset any stale queued drone audio so the transient lands immediately at
    // the playhead.
    stream.reset();

    const pluckFrames = 4096; // ~85 ms transient burst
    final out = buf.float32View;
    var phase = 0.0;
    final inc = noteFreq / _sampleRate;

    for (var i = 0; i < pluckFrames; i++) {
      final env = math.exp(-i / (pluckFrames * 0.28));
      final harmonic =
          math.sin(2.0 * math.pi * phase) +
          0.5 * math.sin(4.0 * math.pi * phase) +
          0.25 * math.sin(6.0 * math.pi * phase);
      final sample = (harmonic * 0.25 * env).clamp(-1.0, 1.0);
      out[i * 2] = sample;
      out[i * 2 + 1] = sample;
      phase += inc;
      if (phase >= 1.0) phase -= 1.0;
    }

    stream.pushNativeFloat32(buf, pluckFrames);
    setState(() {});
  }

  @override
  void dispose() {
    _feedTimer?.cancel();
    _nativeBuffer?.dispose();
    final src = _stream?.source;
    if (src != null) {
      unawaited(_soloud.disposeSource(src));
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final stream = _stream;
    const notes = <String, double>{
      'A3': 220,
      'C4': 261.63,
      'E4': 329.63,
      'G4': 392,
      'A4': 440,
      'C5': 523.25,
    };

    return Scaffold(
      appBar: AppBar(
        title: const Text('Real-Time PCM Synth (RealtimeStream)'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 16,
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  spacing: 8,
                  children: [
                    const Text(
                      'Ring Buffer Telemetry',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    Text(
                      'Available to read: '
                      '${stream?.availableToRead ?? 0} frames',
                    ),
                    Text(
                      'Available to write: '
                      '${stream?.availableToWrite ?? 0} frames',
                    ),
                    Text('Underrun count: ${stream?.underrunCount ?? 0}'),
                  ],
                ),
              ),
            ),
            const Text(
              'Instantaneous Playing-Window Pluck Injection',
              style: TextStyle(fontWeight: FontWeight.bold),
            ),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                for (final entry in notes.entries)
                  ElevatedButton(
                    onPressed: () => _triggerInstantPluck(entry.value),
                    child: Text('${entry.key} (${entry.value.round()} Hz)'),
                  ),
              ],
            ),
            const Divider(),
            SwitchListTile(
              title: const Text('Continuous Stereo Saw Drone'),
              subtitle: const Text(
                'Pushes 20 ms chunks via zero-GC RealtimeNativeBuffer',
              ),
              value: _droneEnabled,
              onChanged: (v) => setState(() => _droneEnabled = v),
            ),
            Row(
              children: [
                const SizedBox(width: 110, child: Text('Frequency')),
                Expanded(
                  child: Slider(
                    min: 55,
                    max: 880,
                    value: _frequency,
                    onChanged: (v) => setState(() => _frequency = v),
                  ),
                ),
                SizedBox(
                  width: 80,
                  child: Text('${_frequency.toStringAsFixed(1)} Hz'),
                ),
              ],
            ),
            Row(
              children: [
                const SizedBox(width: 110, child: Text('Stereo Detune')),
                Expanded(
                  child: Slider(
                    max: 0.05,
                    value: _detune,
                    onChanged: (v) => setState(() => _detune = v),
                  ),
                ),
                SizedBox(
                  width: 80,
                  child: Text('${(_detune * 100).toStringAsFixed(1)}%'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

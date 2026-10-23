// ignore_for_file: experimental_member_use

import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Example 2: Worker-Isolate Real-Time Drum Sequencer
///
/// Demonstrates:
/// - Synthesizing a 4-track 16-step drum machine using
///   `RealtimeStreamHandle.startFloat32Feeder`
///   (`RealtimeStreamFeeder<DrumSynthState>`), which automatically runs in a
///   background Dart `Isolate` on native platforms and seamlessly falls back to
///   a zero-GC timer loop on Web.
/// - Routing the real-time stream through a SoLoud `Bus` with `freeverbFilter`
///   and `compressorFilter`.
///
/// ### Web Platform Considerations & Drawbacks
/// - **Threading & Timers**: On native platforms, the feeder runs in a
///   dedicated background Dart `Isolate` alongside low-latency OS hardware
///   periods (256 frames). On Web, Dart is single-threaded (no background
///   `Isolate`), so the feeder runs on the main browser thread via
///   `Timer.periodic`. The HTML5 spec clamps nested timers to >= 4 ms, and
///   Flutter UI rendering (60-120 Hz) introduces 10-16 ms event-loop jitter.
/// - **Buffer Cushion**: If the ring buffer cushion (`targetBufferedFrames`)
///   is too small (e.g. 256 frames = 5.33 ms), the Web Audio device callback
///   drains the buffer faster than the main-thread timer can refill it,
///   causing massive underruns and slow-motion playback (< 1 BPM). On Web, a
///   larger cushion (e.g. 2048 frames) and 15 ms ticks avoid starvation.
/// - **Render-Ahead**: The native retroactive render-ahead ring
///   (`renderAheadFrames`) is native-only (direct-to-device mixing on Web).
/// - **Alternative for Web**: For zero-latency, glitch-free procedural
///   synthesis on Web without ring-buffer queuing jitter, consider using a
///   pull-based `SoLoud.instance.createCustomAudioSource`
///   (see `custom_dsp_workbench.dart`).
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Initialized with Option B low-latency lookahead configuration:
  // - Native: `bufferSize: 4096` ensures mixer headroom, `renderAheadFrames:
  //   4096` enables the native render-ahead ring (`mRenderRing`) and mix
  //   checkpoints, and `devicePeriodFrames: 256` requests ~5.33 ms callbacks.
  // - Web: Retroactive render-ahead is native-only (direct-to-device on Web).
  //   `bufferSize: 512` is used for balanced Web Audio callback granularity.
  await SoLoud.instance.init(
    sampleRate: 48000,
    bufferSize: kIsWeb ? 512 : 1024,
    renderAheadFrames: kIsWeb ? 0 : 1024,
    devicePeriodFrames: kIsWeb ? 0 : 256,
  );
  runApp(const MaterialApp(home: IsolateDrumSequencerExample()));
}

/// Holds both UI-controlled sequencer parameters and internal DSP synthesis
/// state across audio chunks.
final class DrumSynthState {
  DrumSynthState({
    required this.running,
    required this.bpm,
    required this.grid,
  });

  // UI-controlled parameters:
  bool running;
  double bpm;
  List<List<bool>> grid;

  // Internal DSP synthesis state (preserved across chunks and UI updates):
  int sampleCounter = 0;
  int currentStep = -1;
  double kickEnv = 0;
  double kickPhase = 0;
  double snareEnv = 0;
  double hatEnv = 0;
  double rimEnv = 0;
  double rimPhase = 0;
  final math.Random rng = math.Random(42);
}

/// Top-level callback invoked inside the feeder when `feeder.updateState(...)`
/// is called from the UI, updating parameters without resetting DSP phases.
void _updateDrumSynthState(DrumSynthState current, DrumSynthState next) {
  current
    ..running = next.running
    ..bpm = next.bpm
    ..grid = next.grid;
}

/// Top-level real-time PCM generator callback invoked by
/// [RealtimeStreamFeeder].
int _drumSynthGenerateChunk(
  Float32List out,
  int framesRequested,
  DrumSynthState state,
  void Function(Object? event) emitToUi,
) {
  if (!state.running) {
    // Returning 0 tells the feeder no audio frames are ready right now;
    // the C++ mixer smoothly cosine-ramps to silence.
    return 0;
  }

  final samplesPerStep = ((60.0 / state.bpm / 4.0) * 48000).round();

  for (var i = 0; i < framesRequested; i++) {
    if (state.sampleCounter % samplesPerStep == 0) {
      state.currentStep = (state.currentStep + 1) & 15;
      emitToUi(state.currentStep);
      final step = state.currentStep;
      if (state.grid[0][step]) {
        state
          ..kickEnv = 1.0
          ..kickPhase = 0.0;
      }
      if (state.grid[1][step]) state.snareEnv = 1.0;
      if (state.grid[2][step]) state.hatEnv = 1.0;
      if (state.grid[3][step]) {
        state
          ..rimEnv = 1.0
          ..rimPhase = 0.0;
      }
    }
    state.sampleCounter++;

    var mix = 0.0;
    if (state.kickEnv > 1e-4) {
      final freq = 48.0 + 110.0 * state.kickEnv * state.kickEnv;
      mix += math.sin(2.0 * math.pi * state.kickPhase) * state.kickEnv * 0.7;
      state
        ..kickPhase += freq / 48000.0
        ..kickEnv *= 0.9992;
    }
    if (state.snareEnv > 1e-4) {
      final noise = state.rng.nextDouble() * 2.0 - 1.0;
      mix += noise * state.snareEnv * 0.45;
      state.snareEnv *= 0.9985;
    }
    if (state.hatEnv > 1e-4) {
      final noise = state.rng.nextDouble() * 2.0 - 1.0;
      mix += noise * state.hatEnv * 0.25;
      state.hatEnv *= 0.9965;
    }
    if (state.rimEnv > 1e-4) {
      mix += math.sin(2.0 * math.pi * state.rimPhase) * state.rimEnv * 0.35;
      state
        ..rimPhase += 820.0 / 48000.0
        ..rimEnv *= 0.9975;
    }

    final clamped = mix.clamp(-1.0, 1.0);
    out[i * 2] = clamped;
    out[i * 2 + 1] = clamped;
  }

  return framesRequested;
}

class IsolateDrumSequencerExample extends StatefulWidget {
  const IsolateDrumSequencerExample({super.key});

  @override
  State<IsolateDrumSequencerExample> createState() =>
      _IsolateDrumSequencerExampleState();
}

class _IsolateDrumSequencerExampleState
    extends State<IsolateDrumSequencerExample> {
  final SoLoud _soloud = SoLoud.instance;

  RealtimeStreamHandle? _stream;
  RealtimeStreamFeeder<DrumSynthState>? _feeder;
  Bus? _drumBus;

  bool _isRunning = false;
  double _bpm = 120;
  double _reverbWet = 0.15;
  int _currentStep = 0;

  // 4 tracks x 16 steps: [Kick, Snare, Closed Hat, Rimshot]
  final List<List<bool>> _grid = <List<bool>>[
    <bool>[
      true, false, false, false, true, false, false, false, //
      true, false, false, false, true, false, true, false,
    ],
    <bool>[
      false, false, false, false, true, false, false, false, //
      false, false, false, false, true, false, false, true,
    ],
    <bool>[
      true, false, true, false, true, false, true, true, //
      true, false, true, false, true, false, true, true,
    ],
    <bool>[
      false, false, false, true, false, false, true, false, //
      false, true, false, false, false, false, true, false,
    ],
  ];

  static const List<String> _trackNames = <String>[
    'Kick',
    'Snare',
    'Hi-Hat',
    'Rim',
  ];

  @override
  void initState() {
    super.initState();
    _setupEngine();
  }

  DrumSynthState _buildStateSnapshot() => DrumSynthState(
        running: _isRunning,
        bpm: _bpm,
        grid: _grid.map(List<bool>.from).toList(),
      );

  Future<void> _setupEngine() async {
    final bus = _soloud.createMixingBus(name: 'Drum Bus')..playOnEngine();
    bus.filters.freeverbFilter.activate();
    bus.filters.freeverbFilter.wet().value = _reverbWet;
    bus.filters.compressorFilter.activate();

    final stream = _soloud.setRealtimeStream(
      bufferFrames: 8192,
    );
    final handle = _soloud.play(stream.source, busId: bus.busId);
    stream.bindActiveVoice(handle);

    final feeder = await stream.startFloat32Feeder<DrumSynthState>(
      initialState: _buildStateSnapshot(),
      onFeed: _drumSynthGenerateChunk,
      onStateUpdate: _updateDrumSynthState,
      // Buffer sizing & refill interval:
      // - Native: 128 frames (2.67 ms) chunk, 256 frames (5.33 ms) cushion,
      //   2 ms tick in a background Isolate for ~5-10 ms latency.
      // - Web: HTML5 timers on the main JS thread are clamped to >= 4 ms and
      //   jitter up to 10-16 ms with UI rendering. A 2048-frame (~42.6 ms)
      //   cushion, 512-frame chunk, and 15 ms ticks prevent buffer starvation
      //   and underruns.
      chunkFrames: kIsWeb ? 512 : 128,
      targetBufferedFrames: kIsWeb ? 2048 : 256,
      tickInterval: const Duration(milliseconds: kIsWeb ? 15 : 2),
      onUiEvent: (Object? step) {
        if (step is int && mounted) {
          setState(() => _currentStep = step);
        }
      },
    );

    if (mounted) {
      setState(() {
        _drumBus = bus;
        _stream = stream;
        _feeder = feeder;
      });
    } else {
      await feeder.dispose();
    }
  }

  void _syncWorkerState() {
    _feeder?.updateState(_buildStateSnapshot());
  }

  @override
  void dispose() {
    unawaited(_feeder?.dispose());
    final src = _stream?.source;
    if (src != null) {
      unawaited(_soloud.disposeSource(src));
    }
    _drumBus?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Worker-Isolate Drum Sequencer'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 16,
          children: [
            Row(
              spacing: 16,
              children: [
                ElevatedButton.icon(
                  onPressed: () {
                    setState(() => _isRunning = !_isRunning);
                    _syncWorkerState();
                  },
                  icon: Icon(_isRunning ? Icons.stop : Icons.play_arrow),
                  label: Text(_isRunning ? 'Stop' : 'Start'),
                ),
                Text(
                  'Step: ${_currentStep + 1} / 16',
                  style: const TextStyle(fontWeight: FontWeight.bold),
                ),
                const Spacer(),
                // Note: `underrunCount` increments once whenever the ring
                // buffer transitions from having audio frames to empty (e.g.
                // 1 at initial startup while stopped, and +1 each time Stop is
                // pressed). During continuous playback it stays constant.
                Tooltip(
                  message: 'Underruns count transitions to an empty buffer '
                      '(1 at startup and +1 on Stop; stays constant while '
                      'running).',
                  child: Text(
                    'Buffer: ${_stream?.availableToRead ?? 0} frames | '
                    'Underruns: ${_stream?.underrunCount ?? 0}',
                  ),
                ),
              ],
            ),
            Row(
              children: [
                const SizedBox(width: 90, child: Text('Tempo')),
                Expanded(
                  child: Slider(
                    min: 60,
                    max: 180,
                    value: _bpm,
                    onChanged: (v) {
                      setState(() => _bpm = v);
                      _syncWorkerState();
                    },
                  ),
                ),
                SizedBox(width: 75, child: Text('${_bpm.round()} BPM')),
              ],
            ),
            Row(
              children: [
                const SizedBox(width: 90, child: Text('Bus Reverb')),
                Expanded(
                  child: Slider(
                    value: _reverbWet,
                    onChanged: (v) {
                      setState(() => _reverbWet = v);
                      _drumBus?.filters.freeverbFilter.wet().value = v;
                    },
                  ),
                ),
                SizedBox(
                  width: 75,
                  child: Text('${(_reverbWet * 100).round()}%'),
                ),
              ],
            ),
            const Divider(),
            for (var track = 0; track < _grid.length; track++)
              Row(
                children: [
                  SizedBox(
                    width: 64,
                    child: Text(
                      _trackNames[track],
                      style: const TextStyle(fontWeight: FontWeight.w600),
                    ),
                  ),
                  for (var step = 0; step < 16; step++)
                    Expanded(
                      child: Padding(
                        padding: const EdgeInsets.all(2),
                        child: InkWell(
                          onTap: () {
                            setState(() {
                              _grid[track][step] = !_grid[track][step];
                            });
                            _syncWorkerState();
                          },
                          child: Container(
                            height: 36,
                            decoration: BoxDecoration(
                              color: _grid[track][step]
                                  ? (step == _currentStep && _isRunning
                                      ? Colors.amber
                                      : Colors.indigo)
                                  : (step == _currentStep && _isRunning
                                      ? Colors.amber.withValues(alpha: 0.35)
                                      : Colors.grey.shade300),
                              borderRadius: BorderRadius.circular(4),
                            ),
                          ),
                        ),
                      ),
                    ),
                ],
              ),
          ],
        ),
      ),
    );
  }
}

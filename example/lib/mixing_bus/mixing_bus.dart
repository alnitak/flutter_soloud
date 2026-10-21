import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// Mixing Bus Example - Grouped Audio Control and Routing
///
/// This example demonstrates how to use mixing buses to group and control
/// multiple audio sources together. Mixing buses act as virtual audio devices
/// that allow you to:
///
/// 1. **Group audio sources**: Route multiple sounds through a single bus
/// 2. **Control volume collectively**: Adjust the volume of all sounds on a bus
///    with a single call
/// 3. **Apply shared effects**: Apply filters (like pitch shift, echo, etc.)
///    to all sounds routed through the bus
/// 4. **Dynamic routing**: Move live sounds between buses
///    at runtime using [Bus.annexSound]
/// 5. **Metering**: Read the level of each bus with [Bus.getLevel] after
///    [Bus.setVisualizationEnabled]
///
/// ## Key Concepts
///
/// - **Mixing Bus**: A special audio source that plays other audio sources
///   through it. Think of it as a virtual audio mixer channel.
///
/// - **Bus Creation**: Create buses using [SoLoud.instance.createMixingBus()]
///
/// - **Playing the Bus**: A bus must be "played" on the main engine using
///   [Bus.playOnEngine()] before its output becomes audible. This is different
///   from playing regular sounds.
///
/// - **Routing Sounds**: Play sounds through a bus by calling [Bus.play()] or
///   by passing the `busId` parameter to [SoLoud.instance.play()]
///
/// - **Annexing Sounds**: Dynamically reparent a live sound to a bus using
///   [Bus.annexSound()]. Useful for moving sounds between buses at runtime.
///
/// ## Typical Workflow
///
/// ```dart
/// // 1. Create a mixing bus
/// final sfxBus = SoLoud.instance.createMixingBus(name: 'SFX');
///
/// // 2. Play the bus on the engine (required for audio output)
/// sfxBus.playOnEngine();
///
/// // 3. Load and play sounds through the bus
/// final explosion = await SoLoud.instance.loadAsset('explosion.mp3');
/// sfxBus.play(explosion);
///
/// // 4. Control all sounds on the bus together
/// SoLoud.instance.setVolume(sfxBus.soundHandle!, 0.5); // Half volume for all SFX
/// sfxBus.filters.echoFilter.activate(); // Echo effect on all SFX
/// ```
///
/// ## Use Cases
///
/// - **Game Audio Layers**: Separate buses for music, SFX, voiceovers, ambience
/// - **Dynamic Audio Mixing**: Duck music when voiceover plays, apply effects
///   to specific sound categories
/// - **Volume Categories**: Allow users to adjust volume per sound category
/// - **Runtime Effects**: Apply filters to groups of sounds (underwater effect
///   for all SFX, radio filter for voice communications, etc.)
///
/// See also:
/// - [Bus] - The mixing bus class with full API documentation
/// - [Buses] - Helper singleton for managing all active buses
/// - https://solhsa.com/soloud/mixbus.html - Official SoLoud mixing bus docs

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

  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: HelloFlutterSoLoud(),
    ),
  );
}

class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  AudioSource? currentSound;

  @override
  void initState() {
    super.initState();
    SoLoud.instance
        .loadAsset('assets/audio/IveSeenThings.mp3')
        .then((sound) => currentSound = sound);
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SingleChildScrollView(
        child: SafeArea(
          child: Center(
            child: Column(
              spacing: 8,
              children: [
                ElevatedButton(
                  onPressed: () {
                    // Create a new mixing bus via the Core instance.
                    // The newly created bus acts like a virtual audio device
                    // where other sounds can be routed into.
                    SoLoud.instance.createMixingBus(
                      name: 'bus ${Buses().buses.length + 1}',
                    );
                    setState(() {});
                  },
                  child: const Text('create bus'),
                ),
                Row(
                  mainAxisSize: MainAxisSize.min,
                  spacing: 8,
                  children: [
                    ElevatedButton(
                      onPressed: () async {
                        SoLoud.instance.play(currentSound!, looping: true);
                      },
                      child: const Text('play sound on engine'),
                    ),
                    ElevatedButton(
                      onPressed: () {
                        // Annex the first handle of the sound to the first bus.
                        // (the bus must be already created and playing)
                        Buses()
                            .buses
                            .first
                            .annexSound(currentSound!.handles.first);
                        setState(() {});
                      },
                      child: const Text('move 1st handle of sound on bus 1'),
                    ),
                  ],
                ),
                Wrap(
                  children: [
                    for (final bus in Buses().buses)
                      BusControls(
                        key: ValueKey(bus.name),
                        bus: bus,
                        onMustRefresh: () => setState(() {}),
                      ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class BusControls extends StatefulWidget {
  const BusControls({required this.bus, this.onMustRefresh, super.key});

  final Bus bus;

  final VoidCallback? onMustRefresh;

  @override
  State<BusControls> createState() => _BusControlsState();
}

class _BusControlsState extends State<BusControls> {
  /// The volume of the bus.
  late final ValueNotifier<double> volumeNotifier;

  /// The voice count of the bus.
  late final ValueNotifier<int> voiceCountNotifier;

  AudioSource? background;
  AudioSource? explosion;
  AudioSource? iVeSeenThings;

  @override
  void initState() {
    super.initState();
    volumeNotifier = ValueNotifier<double>(1);
    voiceCountNotifier = ValueNotifier<int>(widget.bus.getActiveVoiceCount());

    if (widget.bus.soundHandle != null) {
      volumeNotifier.value = SoLoud.instance.getVolume(widget.bus.soundHandle!);
    }

    if (widget.bus.soundHandle != null) {
      voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
    }

    SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3').then((sound) {
      background = sound;
      // Update the voice count UI when sounds finish playing
      background?.soundEvents.listen((event) {
        dev.log('background sound event $event');
        voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
      });
    });

    SoLoud.instance.loadAsset('assets/audio/explosion.mp3').then((sound) {
      explosion = sound;
      // Update the voice count UI when sounds finish playing
      explosion?.soundEvents.listen((event) {
        dev.log('explosion sound event $event');
        voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
      });
    });

    SoLoud.instance.loadAsset('assets/audio/IveSeenThings.mp3').then((sound) {
      iVeSeenThings = sound;
      // Update the voice count UI when sounds finish playing
      iVeSeenThings?.soundEvents.listen((event) {
        dev.log('ive seen things sound event $event');
        voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.all(8),
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(22),
        border: Border.all(color: Colors.blue),
      ),
      child: Column(
        spacing: 8,
        children: [
          ValueListenableBuilder(
            valueListenable: voiceCountNotifier,
            builder: (context, value, child) {
              return Text(
                '${widget.bus.name} - voices: $value',
                style:
                    const TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
              );
            },
          ),
          BusLevelMeter(bus: widget.bus),
          Row(
            mainAxisSize: MainAxisSize.min,
            spacing: 8,
            children: [
              ElevatedButton(
                // Play the bus on the engine.
                // Like any SoundHandle, a bus must be played on the main
                // SoLoud engine in order for its output to be audible.
                onPressed: () =>
                    widget.bus.playOnEngine(volume: volumeNotifier.value),
                child: const Text('play bus'),
              ),
              ElevatedButton(
                onPressed: () {
                  widget.bus.dispose();
                  widget.onMustRefresh?.call();
                },
                child: const Text('dispose bus'),
              ),
            ],
          ),
          const SizedBox(height: 8),
          ElevatedButton(
            onPressed: () async {
              if (background == null) return;
              // Route sounds through the bus.
              // By calling `play()` on the Bus instance itself, the audio
              // source is automatically routed through this mixing bus.
              widget.bus.play(background!, volume: 0.2);
              voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
            },
            child: const Text('play background sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              if (explosion == null) return;
              widget.bus.play(explosion!);
              voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
            },
            child: const Text('play explosion sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              if (iVeSeenThings == null) return;
              widget.bus.play(iVeSeenThings!);
              voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
            },
            child: const Text('play ive seen things sound'),
          ),
          const SizedBox(height: 8),
          ValueListenableBuilder<double>(
            valueListenable: volumeNotifier,
            builder: (context, volume, child) {
              return Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text('Volume: ${volume.toStringAsFixed(2)}'),
                  Slider(
                    value: volume,
                    max: 3,
                    onChanged: (value) {
                      volumeNotifier.value = value;
                      if (widget.bus.soundHandle != null) {
                        // Adjust volume of the entire bus.
                        // Setting the volume on the bus's SoundHandle affects
                        // all audio sources currently routed through it.
                        SoLoud.instance
                            .setVolume(widget.bus.soundHandle!, value);
                      }
                    },
                  ),
                ],
              );
            },
          ),
          ElevatedButton(
            onPressed: () async {
              // Apply effects to the entire bus.
              // Activating a filter on the bus will automatically apply
              // the effect to all audio sources routed through it.
              if (widget.bus.filters.pitchShiftFilter.isActive) {
                widget.bus.filters.pitchShiftFilter.deactivate();
              } else {
                widget.bus.filters.pitchShiftFilter.activate();
                widget.bus.filters.pitchShiftFilter
                    .shift(soundHandle: widget.bus.soundHandle)
                    .value = 2.5;
              }
            },
            child: const Text('pitch shift filter'),
          ),
        ],
      ),
    );
  }
}

/// A level meter for [bus], polled once per frame with [Bus.getLevel].
///
/// The level is taken before the bus's own filters, volume and pan, so the
/// volume slider doesn't move it.
class BusLevelMeter extends StatefulWidget {
  const BusLevelMeter({required this.bus, super.key});

  final Bus bus;

  @override
  State<BusLevelMeter> createState() => _BusLevelMeterState();
}

class _BusLevelMeterState extends State<BusLevelMeter>
    with SingleTickerProviderStateMixin {
  late final Ticker ticker;
  double rms = 0;
  double peak = 0;

  @override
  void initState() {
    super.initState();
    widget.bus.setVisualizationEnabled(true);
    ticker = createTicker((_) => poll())..start();
  }

  void poll() {
    final BusLevel level;
    try {
      level = widget.bus.getLevel();
    } on SoLoudBusDisposedDartException {
      ticker.stop();
      return;
    }
    setState(() {
      if (level.frames == 0) {
        // Nothing was mixed since the last frame: the bus is paused or not
        // playing. Let the meter fall instead of freezing it.
        rms *= 0.8;
        peak *= 0.8;
      } else {
        rms = level.rms;
        // Hold the peak and let it fall slowly.
        peak = level.peak > peak ? level.peak : peak * 0.95;
      }
    });
  }

  @override
  void dispose() {
    ticker.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 200,
      height: 12,
      child: Stack(
        children: [
          Container(color: Colors.blue.withValues(alpha: 0.15)),
          FractionallySizedBox(
            alignment: Alignment.centerLeft,
            widthFactor: rms.clamp(0, 1),
            child: Container(color: Colors.blue),
          ),
          Align(
            alignment: Alignment(peak.clamp(0, 1) * 2 - 1, 0),
            child: Container(width: 2, color: Colors.red),
          ),
        ],
      ),
    );
  }
}

import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

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
                        await SoLoud.instance
                            .play(currentSound!, looping: true);
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
          Row(
            mainAxisSize: MainAxisSize.min,
            spacing: 8,
            children: [
              ElevatedButton(
                // Play the bus on the engine.
                // Like any SoundHandle, a bus must be played on the main
                // SoLoud engine in order for its output to be audible.
                onPressed: () => widget.bus.playOnEngine(volume: volumeNotifier.value),
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
              await widget.bus.play(background!, volume: 0.2);
              voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
            },
            child: const Text('play background sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              if (explosion == null) return;
              await widget.bus.play(explosion!);
              voiceCountNotifier.value = widget.bus.getActiveVoiceCount();
            },
            child: const Text('play explosion sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              if (iVeSeenThings == null) return;
              await widget.bus.play(iVeSeenThings!);
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
                        SoLoud.instance.setVolume(widget.bus.soundHandle!, value);
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

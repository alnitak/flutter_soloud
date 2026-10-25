import 'dart:async';
import 'dart:typed_data' show Float32List;

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

bool _isSilent(Float32List data) => data.every((sample) => sample == 0);

/// Collects the packets [bus] emits during [ms] milliseconds.
Future<List<AudioVisualizationData>> _collect(Bus bus, int ms) async {
  final packets = <AudioVisualizationData>[];
  final sub = bus.audioVisualizationEvents.listen(packets.add);
  await delay(ms);
  await sub.cancel();
  return packets;
}

/// Waits for packets already on their way to be delivered, then collects.
Future<List<AudioVisualizationData>> _collectAfterSettling(
  Bus bus,
  int ms,
) async {
  await delay(150);
  return _collect(bus, ms);
}

/// Test per-bus visualization: `Bus.setVisualizationEnabled`,
/// `Bus.audioVisualizationEvents` and `Bus.getChannelVolume`.
Future<OutputBuffer> testBusVisualization() async {
  final strBuf = OutputBuffer();
  await initialize();

  final sound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  // ========================================================================
  strBuf.writeln('=== Enabled before the bus plays ===');
  final bus = SoLoud.instance.createMixingBus(name: 'visualized')
    ..setVisualizationEnabled(true);
  assert(bus.getVisualizationEnabled(), 'Bus visualization should be enabled');

  var packets = await _collect(bus, 300);
  assert(
    packets.isEmpty,
    'Nothing should be emitted before the bus plays, got ${packets.length}',
  );

  // ========================================================================
  strBuf.writeln('=== A playing empty bus emits silence ===');
  final busHandle = bus.playOnEngine();
  packets = await _collectAfterSettling(bus, 300);
  assert(packets.isNotEmpty, 'An empty playing bus should emit packets');
  assert(
    packets.every((p) => _isSilent(p.waveData!) && _isSilent(p.fftData!)),
    'The packets of an empty bus should be silent',
  );
  strBuf
    ..writeln('Silent packets: ${packets.length}')
    // ======================================================================
    ..writeln('=== Sound through the bus ===');
  bus.play(sound, looping: true);
  packets = await _collectAfterSettling(bus, 300);
  assert(packets.isNotEmpty, 'Packets should be emitted while playing');
  final packet = packets.last;
  assert(packet.channelCount == 1, 'Merged should send one channel');
  assert(packet.waveData!.length == 256, 'Wave should have 256 samples');
  assert(packet.fftData!.length == 128, 'FFT should have 128 bins');
  assert(
    packets.any((p) => !_isSilent(p.waveData!)),
    'The packets should carry the sound',
  );
  final left = bus.getChannelVolume(0);
  final right = bus.getChannelVolume(1);
  assert(left > 0 && right > 0, 'Channel volumes should be > 0');
  strBuf
    ..writeln('Packets in 300 ms: ${packets.length}')
    ..writeln('Channel volumes - Left: $left, Right: $right')
    // ======================================================================
    ..writeln('=== Taken before the bus volume ===');
  SoLoud.instance.setVolume(busHandle, 0);
  packets = await _collectAfterSettling(bus, 300);
  assert(
    packets.any((p) => !_isSilent(p.waveData!)),
    'A muted bus should still be visualized: the data is pre-volume',
  );
  SoLoud.instance.setVolume(busHandle, 1);

  // ========================================================================
  strBuf.writeln('=== Nothing while paused ===');
  SoLoud.instance.setPause(busHandle, true);
  packets = await _collectAfterSettling(bus, 300);
  assert(
    packets.isEmpty,
    'Nothing should be emitted while the bus is paused, got '
    '${packets.length}',
  );
  SoLoud.instance.setPause(busHandle, false);
  packets = await _collectAfterSettling(bus, 300);
  assert(packets.isNotEmpty, 'Packets should resume once unpaused');

  // ========================================================================
  strBuf.writeln('=== Each source keeps its own stream ===');
  final emptyBus = SoLoud.instance.createMixingBus(name: 'empty')
    ..playOnEngine()
    ..setVisualizationEnabled(true, channel: VisualizationChannel.all);
  SoLoud.instance.setVisualizationEnabled(true, kind: VisualizationKind.wave);
  final enginePackets = <AudioVisualizationData>[];
  final emptyBusPackets = <AudioVisualizationData>[];
  final busPackets = <AudioVisualizationData>[];
  final subs = [
    SoLoud.instance.audioVisualizationEvents.listen(enginePackets.add),
    emptyBus.audioVisualizationEvents.listen(emptyBusPackets.add),
    bus.audioVisualizationEvents.listen(busPackets.add),
  ];
  await delay(400);
  for (final sub in subs) {
    await sub.cancel();
  }
  assert(enginePackets.isNotEmpty, 'The engine output should emit packets');
  assert(
    enginePackets.every((p) => p.fft.isEmpty),
    'The engine packets should follow the engine settings (wave only)',
  );
  assert(emptyBusPackets.isNotEmpty, 'The empty bus should emit packets');
  assert(
    emptyBusPackets
        .every((p) => p.channelCount == 2 && p.wave.every(_isSilent)),
    'The empty bus should emit its own silent, two-channel packets',
  );
  assert(
    busPackets.any((p) => !_isSilent(p.waveData!)),
    'The playing bus should emit its own packets',
  );
  SoLoud.instance.setVisualizationEnabled(false);
  strBuf
    ..writeln('Engine: ${enginePackets.length}, empty bus: '
        '${emptyBusPackets.length}, bus: ${busPackets.length}')
    // ======================================================================
    ..writeln('=== The engine deleting the bus voice ===');
  SoLoud.instance.stopAll();
  packets = await _collectAfterSettling(bus, 300);
  assert(
    packets.isEmpty,
    'Nothing should be emitted once the bus voice is gone, got '
    '${packets.length}',
  );
  assert(
    bus.getVisualizationEnabled(),
    'The visualization belongs to the bus and should survive its voice',
  );
  bus
    ..playOnEngine()
    ..play(sound, looping: true);
  packets = await _collectAfterSettling(bus, 300);
  assert(
    packets.any((p) => !_isSilent(p.waveData!)),
    'The replayed bus should be visualized again',
  );

  // ========================================================================
  strBuf.writeln('=== Invalid settings ===');
  var threw = false;
  try {
    bus.setVisualizationEnabled(true, windowSize: 100);
  } on SoLoudCppException {
    threw = true;
  }
  assert(threw, 'A window size that is not a power of two should throw');
  assert(
    bus.getVisualizationEnabled(),
    'Invalid settings should leave the visualization running',
  );
  threw = false;
  try {
    bus.setVisualizationEnabled(true, channel: 2);
  } on SoLoudCppException {
    threw = true;
  }
  assert(threw, 'A channel the stereo bus does not have should throw');
  assert(
    bus.getVisualizationEnabled(),
    'An invalid channel should leave the visualization running',
  );

  // ========================================================================
  strBuf.writeln('=== Changing the channel count ===');
  bus.setChannels(channels: Channels.mono);
  assert(
    bus.getVisualizationEnabled(),
    'A merged visualization should restart for a mono bus',
  );
  bus
    ..setChannels()
    ..setVisualizationEnabled(true, channel: 1)
    ..setChannels(channels: Channels.mono);
  assert(
    !bus.getVisualizationEnabled(),
    'A visualization of channel 1 should be disabled once the bus is mono',
  );
  bus
    ..setChannels()
    ..setVisualizationEnabled(true);

  // ========================================================================
  strBuf.writeln('=== Disabling ===');
  bus.setVisualizationEnabled(false);
  assert(!bus.getVisualizationEnabled(), 'Visualization should be disabled');
  packets = await _collectAfterSettling(bus, 300);
  assert(packets.isEmpty, 'Nothing should be emitted once disabled');
  assert(
    bus.getChannelVolume(0) == 0,
    'Channel volumes should read 0 once disabled',
  );

  // ========================================================================
  strBuf.writeln('=== Disposing closes the stream ===');
  final closed = Completer<void>();
  emptyBus.audioVisualizationEvents.listen(null, onDone: closed.complete);
  emptyBus.dispose();
  await closed.future.timeout(const Duration(seconds: 1));
  bus.dispose();

  deinit();

  strBuf.writeln('Bus visualization tests completed successfully');
  return strBuf;
}

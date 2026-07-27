import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test mixing bus functionality including basic usage,
/// nested buses, and multiple buses with different effects.
Future<OutputBuffer> testMixingBus() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load sounds
  final background =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  final iveSeenThings =
      await SoLoud.instance.loadAsset('assets/audio/IveSeenThings.mp3');
  final sample = await SoLoud.instance.loadAsset('assets/audio/sample-vorbis.ogg');

  // Get initial bus count
  final initialBusCount = Buses().buses.length;
  strBuf
    ..writeln('Initial bus count: $initialBusCount')

    // ========================================================================
    // Test 1: Basic mixing bus functionality
    // ========================================================================
    ..writeln('=== Test 1: Basic Mixing Bus ===');

  // Create a mixing bus
  final bus1 = SoLoud.instance.createMixingBus(name: 'SFX Bus');
  strBuf.writeln('Created bus: ${bus1.name}, ID: ${bus1.busId}');

  // Verify bus was added to Buses singleton
  assert(
    Buses().buses.length == initialBusCount + 1,
    'Bus should be added to Buses',
  );
  assert(Buses().byName('SFX Bus') != null, 'Should find bus by name');
  assert(Buses().byId(bus1.busId) != null, 'Should find bus by ID');
  assert(bus1.busId > 0, 'Bus should have valid ID');
  assert(!bus1.isActive, 'Bus should not be active before playOnEngine');

  // Play the bus on the engine (required for audio output)
  final busHandle1 = bus1.playOnEngine(volume: 0.8);
  strBuf.writeln('Bus playing on engine with handle: $busHandle1');

  assert(!busHandle1.isError, 'Bus handle should be valid');
  assert(bus1.isActive, 'Bus should be active after playOnEngine');
  assert(bus1.soundHandle != null, 'Bus should have a sound handle');
  assert(bus1.soundHandle == busHandle1, 'Bus handle should match');

  // Play sounds through the bus
  final handle1 = bus1.play(background, volume: 0.5, looping: true);
  strBuf.writeln('Playing background sound through bus, handle: $handle1');

  assert(!handle1.isError, 'Sound handle should be valid');

  await delay(500);

  // Check active voice count on bus
  final voiceCount = bus1.getActiveVoiceCount();
  strBuf.writeln('Active voices on bus1: $voiceCount');
  assert(voiceCount >= 1, 'Bus should have at least 1 active voice');

  // Control volume of entire bus
  SoLoud.instance.setVolume(bus1.soundHandle!, 0.5);
  strBuf.writeln('Reduced bus volume to 0.5');

  // Verify volume was set
  final busVolume = SoLoud.instance.getVolume(bus1.soundHandle!);
  assert(closeTo(busVolume, 0.5, 0.01), 'Bus volume should be 0.5');

  await delay(500);

  // ========================================================================
  // Test 2: Bus with filter (pitch shift)
  // ========================================================================
  strBuf.writeln('\n=== Test 2: Bus with Pitch Shift Filter ===');

  // Create second bus with pitch shift filter
  final bus2 = SoLoud.instance.createMixingBus(name: 'Music Bus');
  strBuf.writeln('Created bus: ${bus2.name}, ID: ${bus2.busId}');

  assert(bus2.busId != bus1.busId, 'Each bus should have unique ID');
  assert(
    Buses().buses.length == initialBusCount + 2,
    'Second bus should be added',
  );

  // Activate pitch shift filter on the bus BEFORE playing
  if (!kIsWeb && !kIsWasm) {
    bus2.filters.pitchShiftFilter.activate();
    assert(
      bus2.filters.pitchShiftFilter.isActive,
      'Pitch shift filter should be active',
    );
    strBuf.writeln('Activated pitch shift filter on bus2');
  }

  // Play bus on engine with louder volume
  final busHandle2 = bus2.playOnEngine(volume: 1.2);
  strBuf.writeln('Bus playing on engine with louder volume (1.2)');

  assert(!busHandle2.isError, 'Bus2 handle should be valid');
  assert(bus2.isActive, 'Bus2 should be active');

  // Set pitch shift parameters using the bus handle
  if (!kIsWeb && !kIsWasm) {
    bus2.filters.pitchShiftFilter.shift(soundHandle: busHandle2).value = 1.5;

    // Verify pitch shift value
    final shiftValue =
        bus2.filters.pitchShiftFilter.shift(soundHandle: busHandle2).value;
    assert(closeTo(shiftValue, 1.5, 0.01), 'Pitch shift should be 1.5');
    strBuf.writeln('Set pitch shift to 1.5');
  }

  // Play sound through this bus
  final handle2 = bus2.play(background, volume: 0.6, looping: true);
  strBuf.writeln('Playing background through filtered bus, handle: $handle2');

  assert(!handle2.isError, 'Sound handle on bus2 should be valid');

  await delay(1000);

  // Verify both buses have active voices
  assert(
    bus1.getActiveVoiceCount() >= 1,
    'Bus1 should still have active voices',
  );
  assert(
    bus2.getActiveVoiceCount() >= 1,
    'Bus2 should have active voices',
  );

  // ========================================================================
  // Test 3: Two buses playing simultaneously
  // ========================================================================
  strBuf.writeln('\n=== Test 3: Two Buses Playing Simultaneously ===');

  // Play different sounds on each bus
  final handle3 = bus1.play(iveSeenThings, volume: 0.7);
  strBuf.writeln('Playing IveSeenThings on bus1, handle: $handle3');

  assert(!handle3.isError, 'Explosion handle should be valid');

  await delay(300);

  final handle4 = bus2.play(sample, volume: 0.8);
  strBuf.writeln('Playing sample sound on bus2, handle: $handle4');

  assert(!handle4.isError, 'Tic handle should be valid');

  await delay(1000);

  // Check voice counts
  final bus1Voices = bus1.getActiveVoiceCount();
  final bus2Voices = bus2.getActiveVoiceCount();
  strBuf
    ..writeln('Bus1 active voices: $bus1Voices')
    ..writeln('Bus2 active voices: $bus2Voices');

  assert(bus1Voices >= 2, 'Bus1 should have at least 2 voices');
  assert(bus2Voices >= 2, 'Bus2 should have at least 2 voices');

  // ========================================================================
  // Test 4: Bus annexing (moving a sound to a bus)
  // ========================================================================
  strBuf.writeln('\n=== Test 4: Bus Annexing ===');

  // Get voice count before annexing
  final voicesBeforeAnnex = bus1.getActiveVoiceCount();

  // Play a sound directly on the engine (not through a bus)
  final directHandle = SoLoud.instance.play(sample, volume: 0.5);
  strBuf.writeln('Playing sound directly on engine, handle: $directHandle');

  assert(!directHandle.isError, 'Direct handle should be valid');
  assert(
    SoLoud.instance.getIsValidVoiceHandle(directHandle),
    'Direct sound should be playing',
  );

  await delay(300);

  // Move (annex) the sound to bus1
  bus1.annexSound(directHandle);
  strBuf.writeln('Annexed direct sound to bus1');

  await delay(500);

  // Verify voice count increased
  final voicesAfterAnnex = bus1.getActiveVoiceCount();
  assert(
    voicesAfterAnnex > voicesBeforeAnnex,
    'Bus1 voice count should increase after annexing',
  );

  // ========================================================================
  // Test 5: A bus playing something added to another bus
  // ========================================================================
  strBuf.writeln('\n=== Test 5: Nested Bus (Bus playing on another Bus) ===');

  // Create a third bus that will play ON bus1
  final bus3 = SoLoud.instance.createMixingBus(name: 'Nested Bus');
  strBuf.writeln('Created nested bus: ${bus3.name}, ID: ${bus3.busId}');

  assert(
    Buses().buses.length == initialBusCount + 3,
    'Third bus should be added',
  );

  // Play bus3 on the engine
  bus3.playOnEngine(volume: 0.9);
  strBuf.writeln('Nested bus playing on engine');

  assert(bus3.isActive, 'Nested bus should be active');

  // Play a sound through bus3
  final handle5 = bus3.play(iveSeenThings, volume: 0.6);
  strBuf.writeln('Playing IveSeenThings through nested bus, handle: $handle5');

  assert(!handle5.isError, 'Nested bus sound handle should be valid');
  assert(
    bus3.getActiveVoiceCount() >= 1,
    'Nested bus should have active voices',
  );

  await delay(800);

  // ========================================================================
  // Test 6: Bus with channel volume metering (if visualization enabled)
  // ========================================================================
  strBuf.writeln('\n=== Test 6: Bus Channel Volume ===');

  // Enable visualization for channel volume to work
  SoLoud.instance.setVisualizationEnabled(true);
  assert(
    SoLoud.instance.getVisualizationEnabled(),
    'Visualization should be enabled',
  );

  await delay(100);

  // Get approximate volume for channel 0 (left)
  final leftVolume = bus1.getChannelVolume(0);
  final rightVolume = bus1.getChannelVolume(1);
  strBuf
      .writeln('Bus1 channel volumes - Left: $leftVolume, Right: $rightVolume');

  // Channel volumes should be reasonable (not negative, not extremely high)
  assert(leftVolume >= 0, 'Left channel volume should be non-negative');
  assert(rightVolume >= 0, 'Right channel volume should be non-negative');
  assert(leftVolume <= 10, 'Left channel volume should be reasonable');
  assert(rightVolume <= 10, 'Right channel volume should be reasonable');

  // Disable visualization
  SoLoud.instance.setVisualizationEnabled(false);
  assert(
    !SoLoud.instance.getVisualizationEnabled(),
    'Visualization should be disabled',
  );

  // ========================================================================
  // Test 7: Bus lookup via Buses singleton
  // ========================================================================
  strBuf.writeln('\n=== Test 7: Bus Lookup ===');

  // Test finding buses
  final foundByName = Buses().byName('SFX Bus');
  final foundById = Buses().byId(bus2.busId);
  final notFound = Buses().byName('Nonexistent Bus', orElse: () => bus1);

  assert(foundByName == bus1, 'Should find SFX Bus by name');
  assert(foundById == bus2, 'Should find bus2 by ID');
  assert(notFound == bus1, 'orElse should return default bus');
  strBuf
    ..writeln('Bus lookup tests passed')

    // ========================================================================
    // Cleanup
    // ========================================================================
    ..writeln('\n=== Cleanup ===');

  // Dispose all buses
  bus1.dispose();
  assert(!bus1.isActive, 'Bus1 should not be active after dispose');
  strBuf.writeln('Disposed bus1');

  bus2.dispose();
  assert(!bus2.isActive, 'Bus2 should not be active after dispose');
  strBuf.writeln('Disposed bus2');

  bus3.dispose();
  assert(!bus3.isActive, 'Bus3 should not be active after dispose');
  strBuf.writeln('Disposed bus3');

  // Verify buses are removed from Buses singleton
  final remainingBuses = Buses().buses.length;
  assert(
    remainingBuses == initialBusCount,
    'All buses should be removed, expected $initialBusCount, '
    'got $remainingBuses',
  );
  strBuf.writeln('Remaining buses: $remainingBuses');

  // Verify buses can't be used after dispose
  try {
    bus1.playOnEngine();
    assert(false, 'Should throw when playing disposed bus');
  } on SoLoudBusDisposedDartException {
    strBuf.writeln('Correctly threw when using disposed bus');
  }

  deinit();

  strBuf.writeln('\nMixing bus tests completed successfully');
  return strBuf;
}

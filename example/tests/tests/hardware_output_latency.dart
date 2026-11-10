import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Set to true to run this test indefinitely and continuously poll latency.
///
/// When true, watch the terminal / debug console for live updates.
/// When false, collects [sampleCount] samples and finishes normally so the
/// test passes in automated test runners and the Flutter test GUI.
const bool runIndefinitely = false;

/// The interval in milliseconds between latency queries in the output console.
const int pollIntervalMs = 500;

/// Number of samples to collect when [runIndefinitely] is false.
const int sampleCount = 6;

/// Visualizes and validates [SoLoud.getHardwareOutputLatency] on the current
/// platform.
///
/// Tests:
/// 1. Behavior when the engine is not yet started (expecting exception).
/// 2. Initial values immediately after [SoLoud.init] while idle.
/// 3. Latency values during active looping audio playback.
/// 4. Continuous periodic output to the console (indefinitely if
///    [runIndefinitely] is set to true).
Future<OutputBuffer> testHardwareOutputLatency() async {
  final strBuf = OutputBuffer();

  // Ensure engine is in uninitialized state before the first check.
  if (SoLoud.instance.isInitialized) {
    await SoLoud.instance.deinitAsync();
  }

  // 1. Verify behavior when engine is not yet started.
  strBuf.writeln('Checking getHardwareOutputLatency() before init...');
  debugPrint(
    '[HardwareOutputLatency] Checking getHardwareOutputLatency() '
    'before init...',
  );

  var caughtNotInitialized = false;
  try {
    final uninitLatency = SoLoud.instance.getHardwareOutputLatency();
    strBuf.writeln(
      'UNEXPECTED: getHardwareOutputLatency() returned $uninitLatency '
      'before init!',
    );
    debugPrint(
      '[HardwareOutputLatency] UNEXPECTED: returned $uninitLatency '
      'before init!',
    );
  } on SoLoudNotInitializedException catch (e) {
    caughtNotInitialized = true;
    strBuf.writeln('Correctly threw SoLoudNotInitializedException: $e');
    debugPrint(
      '[HardwareOutputLatency] Correctly caught before init: $e',
    );
  }

  assert(
    caughtNotInitialized,
    'getHardwareOutputLatency() must throw SoLoudNotInitializedException '
    'when engine is not initialized',
  );

  // 2. Initialize SoLoud engine with render-ahead enabled so both
  // getHardwareOutputLatency() and getOutputLatency() are active.
  strBuf.writeln('\nInitializing SoLoud engine...');
  debugPrint('[HardwareOutputLatency] Initializing SoLoud engine...');
  await SoLoud.instance.init(
    renderAheadFrames: 2048,
    devicePeriodFrames: 256,
  );
  if (!kIsWeb) {
    SoLoud.instance.setGlobalVolume(0.2);
  }

  // 3. Inspect values immediately after init while idle.
  final idleHw = SoLoud.instance.getHardwareOutputLatency();
  final idleStream = SoLoud.instance.getOutputLatency();

  strBuf
    ..writeln('\nEngine initialized (idle):')
    ..writeln(
      '  Hardware output latency: ${idleHw.inMicroseconds} µs '
      '(${(idleHw.inMicroseconds / 1000).toStringAsFixed(2)} ms)',
    )
    ..writeln(
      '  Stream output latency:   ${idleStream.inMicroseconds} µs '
      '(${(idleStream.inMicroseconds / 1000).toStringAsFixed(2)} ms)',
    );

  debugPrint(
    '[HardwareOutputLatency] Idle after init -> '
    'hw: ${idleHw.inMicroseconds} µs '
    '(${(idleHw.inMicroseconds / 1000).toStringAsFixed(2)} ms), '
    'stream: ${idleStream.inMicroseconds} µs '
    '(${(idleStream.inMicroseconds / 1000).toStringAsFixed(2)} ms)',
  );

  // 4. Start active looping audio playback so the hardware device stream is
  // actively running and processing audio frames.
  strBuf.writeln('\nStarting active playback (assets/audio/explosion.mp3)...');
  debugPrint(
    '[HardwareOutputLatency] Starting playback (assets/audio/explosion.mp3)...',
  );

  final sound = await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final handle = SoLoud.instance.play(sound, looping: true, volume: 0.2);

  // Wait briefly for the audio device pipeline to start cycling buffers.
  await delay(100);

  // 5. Query latency periodically.
  if (runIndefinitely) {
    strBuf.writeln(
      '\nRunning indefinitely (polling every ${pollIntervalMs}ms)...\n'
      'Watch the output console for live updates.',
    );
    debugPrint(
      '=== HardwareOutputLatency running indefinitely '
      '(polling every ${pollIntervalMs}ms) ===',
    );

    var iteration = 0;
    while (true) {
      await delay(pollIntervalMs);
      iteration++;
      final hw = SoLoud.instance.getHardwareOutputLatency();
      final stream = SoLoud.instance.getOutputLatency();
      debugPrint(
        '[HardwareOutputLatency #$iteration] '
        'hw: ${hw.inMicroseconds} µs '
        '(${(hw.inMicroseconds / 1000).toStringAsFixed(2)} ms) | '
        'stream: ${stream.inMicroseconds} µs '
        '(${(stream.inMicroseconds / 1000).toStringAsFixed(2)} ms)',
      );
    }
  } else {
    strBuf.writeln(
      '\nCollecting $sampleCount samples (every ${pollIntervalMs}ms):',
    );
    for (var i = 1; i <= sampleCount; i++) {
      await delay(pollIntervalMs);
      final hw = SoLoud.instance.getHardwareOutputLatency();
      final stream = SoLoud.instance.getOutputLatency();
      final sampleLog = '[Sample $i/$sampleCount] '
          'hw: ${hw.inMicroseconds} µs '
          '(${(hw.inMicroseconds / 1000).toStringAsFixed(2)} ms) | '
          'stream: ${stream.inMicroseconds} µs '
          '(${(stream.inMicroseconds / 1000).toStringAsFixed(2)} ms)';

      strBuf.writeln('  $sampleLog');
      debugPrint('[HardwareOutputLatency] $sampleLog');
    }
  }

  // 6. Cleanup
  strBuf.writeln('\nStopping playback and deinitializing...');
  debugPrint(
    '[HardwareOutputLatency] Stopping playback and deinitializing...',
  );
  await SoLoud.instance.stop(handle);
  await SoLoud.instance.disposeSource(sound);
  deinit();

  strBuf.writeln('Hardware output latency test finished successfully.');
  debugPrint('[HardwareOutputLatency] Test finished successfully.');
  return strBuf;
}

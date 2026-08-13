import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

import 'common.dart';

/// Test instancing playing handles and their disposal.
Future<OutputBuffer> testStopFutures() async {
  final output = OutputBuffer();
  final severeLogs = <LogRecord>[];
  final strBuf = OutputBuffer();

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

    if (record.level > Level.INFO) {
      output.writeln(record.message);
      if (record.level >= Level.SEVERE) {
        severeLogs.add(record);
      }
    }
  });

  /// Start audio isolate
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  /// Fast call to `stop` after `play`
  var handle = SoLoud.instance.play(currentSound);
  output
    ..writeln('fast play/stop')
    ..writeln('$handle started');
  unawaited(
    SoLoud.instance.stop(handle).then((_) => output.writeln('$handle stopped')),
  );

  await delay(500);

  /// Schedule a stop and call `stop` after the scheduled time
  handle = SoLoud.instance.play(currentSound);
  output
    ..writeln('\nscheduleStop')
    ..writeln('$handle started');
  SoLoud.instance.scheduleStop(handle, const Duration(milliseconds: 500));
  await delay(1000);
  unawaited(
    SoLoud.instance.stop(handle).then((_) => output.writeln('$handle stopped')),
  );

  /// Wait a bit.
  await delay(1000);

  /// Test schedulePause
  strBuf.writeln('\ntesting schedulePause');
  handle = SoLoud.instance.play(currentSound);
  output
    ..writeln('schedulePause')
    ..writeln('$handle started');
  SoLoud.instance.schedulePause(handle, const Duration(milliseconds: 400));
  await delay(200);
  // Should still be playing
  assert(
    !SoLoud.instance.getPause(handle),
    'Sound should not be paused yet',
  );
  output.writeln('200ms: not paused yet');
  await delay(300);
  // Should now be paused
  assert(
    SoLoud.instance.getPause(handle),
    'Sound should be paused after schedulePause',
  );
  output.writeln('500ms: paused as expected');

  /// Test that manual stop cancels scheduled stop
  strBuf.writeln('\ntesting scheduled stop cancellation');
  handle = SoLoud.instance.play(currentSound);
  SoLoud.instance.scheduleStop(handle, const Duration(seconds: 5));
  await delay(100);
  // Manually stop before scheduled time
  await SoLoud.instance.stop(handle);
  output.writeln('Manual stop before scheduled time');
  // Handle should be invalid
  assert(
    !SoLoud.instance.getIsValidVoiceHandle(handle),
    'Handle should be invalid after manual stop',
  );

  deinit();

  if (severeLogs.isNotEmpty) {
    throw Exception('Severe logs produced:\n'
        '${severeLogs.map((r) => '[${r.level}] ${r.message}').join('\n')}');
  }

  return output;
}

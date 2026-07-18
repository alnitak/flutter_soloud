// ignore_for_file: experimental_member_use

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test that `onMoreDataIsNeeded` is called when the playback position is at
/// approximately the `bufferTriggerPosition` normalized position inside the
/// decoded circular buffer.
///
/// The callback is fired when the remaining decoded audio drops below
/// `(1 - bufferTriggerPosition) * bufferCapacity`. For a full buffer, this
/// means the playback position should be around
/// `start + bufferTriggerPosition * (end - start)`.
Future<OutputBuffer> testPullBufferRange() async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes = (await rootBundle.load('assets/audio/sample-MP3.mp3'))
      .buffer
      .asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  final refDuration = SoLoud.instance.getLength(refSource);
  await SoLoud.instance.disposeSource(refSource);

  final durationCompleter = Completer<double>();
  final fetchedOffsets = <int>{};
  final samples = <({
    double position,
    double expected,
    double start,
    double end,
  })>[];
  var ended = false;
  SoundHandle? handle;
  const triggerPosition = 0.75;
  const chunkSize = 1024;
  // 400 KB decoded buffer gives frequent triggers while keeping the test
  // reasonably fast.
  const bufferSizeBytes = 400 * 1024;
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    bufferSizeBytes: bufferSizeBytes,
    bufferTriggerPosition: triggerPosition,
    audioSizeBytes: bytes.length,
    onAudioDuration: (duration) {
      strBuf.writeln('audioDuration=${(duration * 1000).round()}ms');
      if (!durationCompleter.isCompleted) {
        durationCompleter.complete(duration);
      }
    },
    onMoreDataIsNeeded: (offset) {
      strBuf.writeln('onMoreDataIsNeeded($offset)');
      if (offset < 0 || offset >= bytes.length) {
        strBuf.writeln('  offset out of range, ignoring');
        return;
      }
      if (!fetchedOffsets.add(offset)) {
        strBuf.writeln('  duplicate offset, ignoring');
        return;
      }

      // Measure the buffer range and the current playback position *before*
      // feeding the new chunk, so the sample reflects the state that caused
      // the callback to fire.
      final range = SoLoud.instance.getPullBufferTimeRange(source);
      if (handle != null) {
        final position = SoLoud.instance.getPosition(handle!);
        final positionSec = position.inMilliseconds / 1000.0;
        final startSec = range.startTime.inMilliseconds / 1000.0;
        final endSec = range.endTime.inMilliseconds / 1000.0;
        // At the trigger moment the playback position should be at roughly
        // `bufferTriggerPosition` through the currently buffered range.
        final expectedSec = startSec + triggerPosition * (endSec - startSec);
        samples.add(
          (
            position: positionSec,
            expected: expectedSec,
            start: startSec,
            end: endSec,
          ),
        );
        strBuf.writeln(
          '  pos=${(positionSec * 1000).round()}ms '
          'expected=${(expectedSec * 1000).round()}ms '
          'buffer=[${(startSec * 1000).round()}ms, '
          '${(endSec * 1000).round()}ms]',
        );
      } else {
        strBuf.writeln(
          '  buffer=[${range.startTime.inMilliseconds}ms, '
          '${range.endTime.inMilliseconds}ms] (no handle yet)',
        );
      }

      final end = (offset + chunkSize).clamp(0, bytes.length);
      final chunk = bytes.sublist(offset, end);
      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
      if (end >= bytes.length) {
        ended = true;
      }
    },
  );

  final duration = await durationCompleter.future.timeout(
    const Duration(seconds: 5),
    onTimeout: () => -1.0,
  );
  strBuf.writeln('duration reported: ${(duration * 1000).round()}ms');
  assert(
    duration > 0.0,
    'Expected a positive duration to be reported via onAudioDuration',
  );

  final playHandle = SoLoud.instance.play(source);
  strBuf.writeln('play returned');
  assert(!playHandle.isError, 'play() failed');
  handle = playHandle;

  final completed = Completer<void>();
  source.soundEvents.listen((event) {
    strBuf.writeln('soundEvent: ${event.event} handle=${event.handle}');
    if (event.event == SoundEventType.handleIsNoMoreValid &&
        event.handle == handle) {
      if (!completed.isCompleted) completed.complete();
    }
  });

  await completed.future.timeout(
    const Duration(seconds: 120),
    onTimeout: () {
      throw Exception('playback did not complete before timeout');
    },
  );

  // Skip the first few triggers: the buffer is still filling up and the
  // trigger-position math assumes the buffer is at its working capacity.
  final validSamples = samples.skip(5).toList();
  assert(
    validSamples.length >= 5,
    'Expected at least 5 trigger samples after warmup, got '
    '${validSamples.length}',
  );

  var totalError = 0.0;
  var maxError = 0.0;
  for (final sample in validSamples) {
    final error = (sample.position - sample.expected).abs();
    totalError += error;
    if (error > maxError) maxError = error;
  }
  final meanError = totalError / validSamples.length;

  strBuf
    ..writeln(
      'PullBufferRange MP3 (${bytes.length} bytes): '
      'ref=${refDuration.inMilliseconds}ms',
    )
    ..writeln(
      'samples=${validSamples.length} '
      'meanError=${(meanError * 1000).round()}ms '
      'maxError=${(maxError * 1000).round()}ms',
    )
    ..writeln('ended=$ended fetchedChunks=${fetchedOffsets.length}');

  if (!ended) {
    throw Exception('end of file was never reached');
  }

  // Allow 400 ms of timing jitter between the audio thread and the Dart
  // callback measurement. This is well below the ~1.1 s decoded buffer
  // duration for a 400 KB stereo float32 buffer at 44.1 kHz.
  const toleranceMs = 400.0;
  assert(
    meanError * 1000 <= toleranceMs,
    'Mean trigger position error ${(meanError * 1000).round()}ms '
    'exceeds tolerance ${toleranceMs.round()}ms',
  );
  assert(
    maxError * 1000 <= toleranceMs * 2,
    'Max trigger position error ${(maxError * 1000).round()}ms '
    'exceeds tolerance ${(toleranceMs * 2).round()}ms',
  );

  deinit();
  return strBuf;
}

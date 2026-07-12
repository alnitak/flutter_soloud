import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test pull-buffer streaming with very small encoded chunks and a tiny
/// decoded circular buffer.
///
/// Verifies that playback starts and continues when the encoded chunk size
/// (1 KB) is much smaller than the decoded buffer (100 KB) and the decoder
/// temporarily backlogs decoded samples.
Future<OutputBuffer> testPullBufferTiny() async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes = (await rootBundle.load('assets/audio/sample-MP3.mp3'))
      .buffer
      .asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  final refDuration = SoLoud.instance.getLength(refSource);
  await SoLoud.instance.disposeSource(refSource);

  final firstOffsetCompleter = Completer<int>();
  final durationCompleter = Completer<double>();
  final fetchedOffsets = <int>{};
  var ended = false;
  late final AudioSource source;

  const chunkSize = 1024;
  source = SoLoud.instance.setPullBufferStream(
    // Tiny decoded buffer: 100 KB total with a 0.5 trigger position.
    bufferSizeBytes: 100 * 1024,
    bufferTriggerPosition: 0.5,
    audioSizeBytes: bytes.length,
    onAudioDuration: (duration) {
      strBuf.writeln('audioDuration=${(duration * 1000).round()}ms');
      if (!durationCompleter.isCompleted) {
        durationCompleter.complete(duration);
      }
    },
    onMoreDataIsNeeded: (offset) {
      strBuf.writeln('moreDataIsNeeded($offset)');
      if (offset < 0 || offset >= bytes.length) {
        strBuf.writeln('  offset out of range, ignoring');
        return;
      }
      if (!firstOffsetCompleter.isCompleted) {
        firstOffsetCompleter.complete(offset);
      }
      if (!fetchedOffsets.add(offset)) {
        strBuf.writeln('  duplicate offset, ignoring');
        return;
      }
      final end = (offset + chunkSize).clamp(0, bytes.length);
      final chunk = bytes.sublist(offset, end);
      strBuf.writeln('feeding chunk $offset-$end');
      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
      if (end >= bytes.length) {
        strBuf.writeln('setting data ended');
        SoLoud.instance.setPullBufferDataIsEnded(source);
        ended = true;
      }
    },
  );

  final firstOffset = await firstOffsetCompleter.future;
  assert(
    firstOffset == 0,
    'Expected first request at offset 0, got $firstOffset',
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

  final stopwatch = Stopwatch()..start();
  final handle = SoLoud.instance.play(source);
  strBuf.writeln('play returned');
  assert(!handle.isError, 'play() failed');

  // Give the tiny buffer a chance to start. If the backlog is not counted as
  // available, playback will be paused indefinitely and the position will
  // stay at zero.
  await delay(1000);
  final position = SoLoud.instance.getPosition(handle);
  strBuf.writeln('position after 1s: $position');
  assert(
    position > Duration.zero,
    'Playback did not start with 1 KB chunks and 100 KB buffer; '
    'position is $position',
  );

  final completed = Completer<int>();
  source.soundEvents.listen((event) {
    strBuf.writeln('soundEvent: ${event.event} handle=${event.handle}');
    if (event.event == SoundEventType.handleIsNoMoreValid &&
        event.handle == handle) {
      final elapsed = stopwatch.elapsedMilliseconds;
      if (!completed.isCompleted) completed.complete(elapsed);
    }
  });

  final elapsed = await completed.future.timeout(
    const Duration(seconds: 120),
    onTimeout: () => -1,
  );

  strBuf
    ..writeln(
      'PullBufferTiny MP3 (${bytes.length} bytes): '
      'stream=${elapsed}ms, ref=${refDuration.inMilliseconds}ms',
    )
    ..writeln('ended=$ended fetchedChunks=${fetchedOffsets.length}');
  if (!ended) {
    throw Exception('setPullBufferDataIsEnded was never reached');
  }
  if (elapsed < 0) {
    throw Exception('playback did not complete before timeout');
  }

  deinit();
  return strBuf;
}

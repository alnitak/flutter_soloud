import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test pull-buffer streaming by feeding a local MP3 file in chunks via the
/// callback-driven pull model.
///
/// Verifies that:
/// - `onMoreDataIsNeeded(0)` fires immediately after creating the stream.
/// - The stream can be fed with `addPullBufferDataStream` and played.
/// - The playback duration matches the reference duration from `loadMem`.
/// - Seeking inside the buffered range updates the play position.
Future<OutputBuffer> testPullBuffer() async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes = (await rootBundle.load('assets/audio/sample-OPUS.opus'))
      .buffer
      .asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  final refDuration = SoLoud.instance.getLength(refSource);
  await SoLoud.instance.disposeSource(refSource);

  final firstOffsetCompleter = Completer<int>();
  final fetchedOffsets = <int>{};
  const chunkSize = 1024 * 10;
  var startBufferTime = Duration.zero;
  var endBufferTime = Duration.zero;
  SoundHandle? handle;
  final durationCompleter = Completer<double>();
  var ended = false;
  var nextSequentialOffset = 0;
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    // Use a short 1 MB buffer with a 0.75 ahead fraction to verify the
    // callback-driven pull model keeps playback fed without unbounded memory.
    bufferSizeBytes: 1024 * 1024,
    bufferTriggerPosition: 0.75,
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
      if (handle != null) {
        strBuf.writeln(
          '*********************************************************** '
          'bufferedStart=${startBufferTime.inMilliseconds}ms '
          'bufferedEnd=${endBufferTime.inMilliseconds}ms '
          'position='
          '${SoLoud.instance.getPosition(handle!).inMilliseconds}ms',
        );
      }

      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
      final range = SoLoud.instance.getPullBufferTimeRange(source);
      startBufferTime = range.startTime;
      endBufferTime = range.endTime;

      strBuf.writeln(
        '  startTime=${startBufferTime.inMilliseconds}ms '
        'endTime=${endBufferTime.inMilliseconds}ms',
      );
      // The C++ duration probe for Ogg requests the last 64 KB of the file.
      // Those requests are not part of the sequential playback stream, so do
      // not mark the stream as ended until the sequential playback data reaches
      // the end of the file.
      const tailProbeSize = 65536;
      final isTailProbe = offset >= bytes.length - tailProbeSize &&
          offset != nextSequentialOffset &&
          nextSequentialOffset >= 0;
      if (offset == nextSequentialOffset || nextSequentialOffset < 0) {
        nextSequentialOffset = end;
      }
      if (end >= bytes.length && !isTailProbe) {
        strBuf.writeln('reached end of file');
        ended = true;
      }
    },
  );

  final firstOffset = await firstOffsetCompleter.future;
  assert(
    firstOffset == 0,
    'Expected first request at offset 0, got $firstOffset',
  );

  // Wait for the duration to be reported (should be near the reference).
  final duration = await durationCompleter.future.timeout(
    const Duration(seconds: 5),
    onTimeout: () => -1.0,
  );
  strBuf.writeln('duration reported: ${(duration * 1000).round()}ms');
  assert(
    duration > 0.0,
    'Expected a positive duration to be reported via onAudioDuration',
  );
  assert(
    (duration * 1000 - refDuration.inMilliseconds).abs() <
        refDuration.inMilliseconds * 0.1 + 1000,
    'Reported duration ${(duration * 1000).round()}ms differs from reference '
    '${refDuration.inMilliseconds}ms',
  );

  handle = SoLoud.instance.play(source);
  strBuf.writeln('play returned');
  assert(!handle.isError, 'play() failed');

  final startTime = DateTime.now();
  final completed = Completer<int>();
  source.soundEvents.listen((event) {
    strBuf.writeln('soundEvent: ${event.event} handle=${event.handle}');
    if (event.event == SoundEventType.handleIsNoMoreValid &&
        event.handle == handle) {
      final elapsed = DateTime.now().difference(startTime).inMilliseconds;
      if (!completed.isCompleted) completed.complete(elapsed);
    }
  });

  // Seek test: wait for playback to advance, seek back, then verify
  // position advances from the new location.
  await delay(500);
  final beforeSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('before seek position: $beforeSeek');
  assert(
    beforeSeek > Duration.zero,
    'Playback did not advance before seek',
  );

  const seekTarget = Duration(seconds: 1);
  SoLoud.instance.seek(handle, seekTarget);
  // After seeking, the plugin may request data at offsets that were already
  // fed before the seek. Clear the set so the seek target can be served again.
  fetchedOffsets.clear();
  nextSequentialOffset = -1;
  await delay(100); // Allow the seek to propagate to the audio thread.
  final afterSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('after seek position: $afterSeek');
  // The mixer may have already advanced the position by the mix buffer
  // duration when we read it, so allow a generous tolerance.
  assert(
    (afterSeek - seekTarget).abs() < const Duration(milliseconds: 500),
    'Seek target was $seekTarget but position is $afterSeek',
  );

  await delay(500);
  final afterSeekPlayback = SoLoud.instance.getPosition(handle);
  strBuf.writeln('after seek playback position: $afterSeekPlayback');
  assert(
    afterSeekPlayback > afterSeek,
    'Playback did not advance after seek',
  );

  await delay(5000);
  strBuf
    ..writeln('after 5s position: ${SoLoud.instance.getPosition(handle)}')
    ..writeln(
      'after 5s active voices: ${SoLoud.instance.getActiveVoiceCount()}',
    );

  final elapsed = await completed.future.timeout(
    const Duration(seconds: 120),
    onTimeout: () => -1,
  );

  strBuf
    ..writeln(
      'PullBuffer MP3 (${bytes.length} bytes): '
      'stream=${elapsed}ms, ref=${refDuration.inMilliseconds}ms',
    )
    ..writeln('ended=$ended fetchedChunks=${fetchedOffsets.length}');
  if (!ended) {
    throw Exception('end of file was never reached');
  }

  assert(
    elapsed > refDuration.inMilliseconds * 0.5,
    'PullBuffer MP3 decoded only ${elapsed}ms '
    'but reference is ${refDuration.inMilliseconds}ms.',
  );

  deinit();
  return strBuf;
}

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Pull-buffer test for Ogg Vorbis, using the same chunked feeding pattern as
/// the Opus test.
Future<OutputBuffer> testPullBufferVorbis() async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes = (await rootBundle.load('assets/audio/sample-vorbis.ogg'))
      .buffer
      .asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  final refDuration = SoLoud.instance.getLength(refSource);
  await SoLoud.instance.disposeSource(refSource);

  final firstOffsetCompleter = Completer<int>();
  final fetchedOffsets = <int>{};
  const chunkSize = 1024;
  final durationCompleter = Completer<double>();
  var ended = false;
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    bufferSizeBytes: 1024 * 100 * 1,
    bufferTriggerPosition: 2 / 3,
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
      final range = SoLoud.instance.getPullBufferTimeRange(source);
      strBuf.writeln(
        '  startTime=${range.startTime.inMilliseconds}ms '
        'endTime=${range.endTime.inMilliseconds}ms',
      );
      if (end >= bytes.length) {
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

  final handle = SoLoud.instance.play(source);
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

  await delay(500);
  final beforeSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('before seek position: $beforeSeek');
  assert(
    beforeSeek > Duration.zero,
    'Playback did not advance before seek',
  );

  const seekTarget = Duration(seconds: 1);
  SoLoud.instance.seek(handle, seekTarget);
  // After seeking, the engine may request data at offsets that were already
  // fed before the seek. Clear the set so the seek target can be served again.
  fetchedOffsets.clear();
  await delay(100);
  final afterSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('after seek position: $afterSeek');
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
      'PullBuffer Vorbis (${bytes.length} bytes): '
      'stream=${elapsed}ms, ref=${refDuration.inMilliseconds}ms',
    )
    ..writeln('ended=$ended fetchedChunks=${fetchedOffsets.length}');
  if (!ended) {
    throw Exception('end of file was never reached');
  }

  assert(
    elapsed > refDuration.inMilliseconds * 0.5,
    'PullBuffer Vorbis decoded only ${elapsed}ms '
    'but reference is ${refDuration.inMilliseconds}ms.',
  );

  deinit();
  return strBuf;
}

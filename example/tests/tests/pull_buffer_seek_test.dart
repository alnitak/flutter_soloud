import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test out-of-buffer seeking on a pull-buffer stream.
///
/// A small-ish decoded buffer is used so that seeking to a later position in
/// the stream must throw away the buffered data and re-fetch from the encoded
/// byte offset. Verifies that the seek lands near the target and playback
/// continues from there.
Future<OutputBuffer> testPullBufferSeek() async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes = (await rootBundle.load('assets/audio/sample-MP3.mp3'))
      .buffer
      .asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  final refDuration = SoLoud.instance.getLength(refSource);
  await SoLoud.instance.disposeSource(refSource);

  final firstOffsetCompleter = Completer<int>();
  final fetchedOffsets = <int>{};
  final durationCompleter = Completer<double>();
  var ended = false;
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    // Use a decoded buffer that is large enough to keep playback fed but small
    // enough that a seek to ~70% of the file is outside the decoded window.
    bufferSizeBytes: 1024 * 1024,
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
      // Feed the whole file in one chunk so the seek table is built
      // immediately and the offset after the seek can be served from memory.
      final end = bytes.length;
      final chunk = bytes.sublist(offset, end);
      strBuf.writeln('feeding chunk $offset-$end');
      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
      strBuf.writeln('setting data ended');
      SoLoud.instance.setPullBufferDataIsEnded(source);
      ended = true;
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

  final handle = SoLoud.instance.play(source);
  strBuf.writeln('play returned');
  assert(!handle.isError, 'play() failed');

  // Give playback time to advance from the start.
  await delay(1500);
  final beforeSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('position before seek: $beforeSeek');
  assert(
    beforeSeek > Duration.zero,
    'Playback did not advance before seek',
  );

  // Seek to a position well outside the small decoded buffer.
  final seekTarget = Duration(
    seconds: (refDuration.inSeconds * 0.7)
        .clamp(1, refDuration.inSeconds - 2)
        .toInt(),
  );
  SoLoud.instance.seek(handle, seekTarget);
  strBuf.writeln('seeked to $seekTarget');
  await delay(200);
  final afterSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('position after seek: $afterSeek');
  assert(
    (afterSeek - seekTarget).abs() < const Duration(seconds: 1),
    'Seek target was $seekTarget but position is $afterSeek',
  );

  // Ensure playback continues from the new position.
  await delay(1000);
  final afterSeekPlayback = SoLoud.instance.getPosition(handle);
  strBuf.writeln('position after seek + 1s: $afterSeekPlayback');
  assert(
    afterSeekPlayback > afterSeek,
    'Playback did not advance after seek',
  );

  // Let the stream run for a while so we can verify it stays alive.
  await delay(3000);
  strBuf.writeln('final position: ${SoLoud.instance.getPosition(handle)}');

  if (!ended) {
    throw Exception('setPullBufferDataIsEnded was never reached');
  }

  deinit();
  return strBuf;
}

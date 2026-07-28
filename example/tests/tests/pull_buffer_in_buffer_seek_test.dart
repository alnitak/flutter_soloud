import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test in-buffer seeking across the whole reported buffered range.
///
/// The whole file is fed into a large decoded buffer so the entire red bar is
/// in-buffer seekable. Verifies that clicks at several positions inside the
/// currently reported range all take the in-buffer path and do not request new
/// encoded data. After each seek the reported range is read again, because the
/// circular buffer is a sliding window: the next seek must be inside the
/// updated range.
Future<OutputBuffer> testInBufferSeek() async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes = (await rootBundle.load('assets/audio/sample-MP3.mp3'))
      .buffer
      .asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  await SoLoud.instance.disposeSource(refSource);

  final firstOffsetCompleter = Completer<int>();
  final durationCompleter = Completer<double>();
  var requestCountAfterSeek = 0;
  var seekStage = 0; // 0 = before any seek, 1+ after a seek
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    // Large decoded buffer so the whole file fits and seeking stays in-buffer.
    bufferSizeBytes: 20 * 1024 * 1024,
    bufferTriggerPosition: 0.7,
    audioSizeBytes: bytes.length,
    onAudioDuration: (duration) {
      strBuf.writeln('audioDuration=${(duration * 1000).round()}ms');
      if (!durationCompleter.isCompleted) {
        durationCompleter.complete(duration);
      }
    },
    onMoreDataIsNeeded: (offset) {
      if (seekStage > 0) {
        requestCountAfterSeek++;
        strBuf.writeln(
          'ERROR: onMoreDataIsNeeded($offset) after seek stage $seekStage',
        );
      } else {
        strBuf.writeln('onMoreDataIsNeeded($offset) before seek');
      }
      if (offset < 0 || offset >= bytes.length) {
        return;
      }
      if (!firstOffsetCompleter.isCompleted) {
        firstOffsetCompleter.complete(offset);
      }
      // Feed the whole file in one chunk.
      final end = bytes.length;
      final chunk = bytes.sublist(offset, end);
      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
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
  assert(duration > 0.0, 'Expected a positive duration');

  final handle = SoLoud.instance.play(source);
  assert(!handle.isError, 'play() failed');

  // Let playback advance a bit so the buffered window is not pinned to zero.
  await delay(1500);
  final beforeSeek = SoLoud.instance.getPosition(handle);
  strBuf.writeln('position before seek: $beforeSeek');
  assert(beforeSeek > Duration.zero, 'Playback did not advance');

  // Helper to seek to a position and verify it stays in-buffer.
  Future<void> doSeek(Duration target, String label) async {
    strBuf.writeln('$label: seeking to $target');
    SoLoud.instance.seek(handle, target);
    seekStage++;
    await delay(200);
    final afterSeek = SoLoud.instance.getPosition(handle);
    strBuf.writeln('$label: position after seek: $afterSeek');
    assert(
      (afterSeek - target).abs() < const Duration(seconds: 1),
      '$label: expected $target, got $afterSeek',
    );
  }

  // Seek to a point about 30% into the current red bar.
  var range = SoLoud.instance.getPullBufferTimeRange(source);
  strBuf.writeln(
    'reported range before seek: ${range.startTime} - ${range.endTime}',
  );
  assert(range.startTime >= Duration.zero, 'Invalid buffered start');
  assert(range.endTime > range.startTime, 'Buffered range is empty');
  final middle = Duration(
    milliseconds: range.startTime.inMilliseconds +
        (range.endTime.inMilliseconds - range.startTime.inMilliseconds) *
            3 ~/
            10,
  );
  await doSeek(middle, 'middle');

  // Seek to a point near the right edge of the *new* red bar.
  range = SoLoud.instance.getPullBufferTimeRange(source);
  strBuf.writeln(
    'reported range after middle seek: ${range.startTime} - ${range.endTime}',
  );
  final rightEdge = range.endTime - const Duration(milliseconds: 200);
  if (rightEdge > range.startTime) {
    await doSeek(rightEdge, 'rightEdge');
  }

  // Seek to a point just inside the left edge of the *new* red bar.
  range = SoLoud.instance.getPullBufferTimeRange(source);
  strBuf.writeln(
    'reported range after rightEdge seek: ${range.startTime} - '
    '${range.endTime}',
  );
  final leftEdge = range.startTime + const Duration(milliseconds: 100);
  if (leftEdge < range.endTime) {
    await doSeek(leftEdge, 'leftEdge');
  }

  // Ensure no new encoded data was requested for any in-buffer seek.
  assert(
    requestCountAfterSeek == 0,
    'In-buffer seeks should not request new data, got '
    '$requestCountAfterSeek requests after seek',
  );

  // Ensure playback continues from the last position.
  final lastSeekPosition = SoLoud.instance.getPosition(handle);
  await delay(1000);
  final afterSeekPlayback = SoLoud.instance.getPosition(handle);
  strBuf.writeln('position after seek + 1s: $afterSeekPlayback');
  assert(
    afterSeekPlayback > lastSeekPosition,
    'Playback did not advance after seek',
  );

  deinit();
  return strBuf;
}

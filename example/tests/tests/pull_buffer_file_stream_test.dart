import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test callback-driven pull-buffer streaming with the bundled MP3 asset,
/// similar to [example/lib/pull_buffer/file_stream.dart].
///
/// Verifies that:
/// - `onMoreDataIsNeeded` fires with monotonically increasing offsets.
/// - Feeding chunks via `addPullBufferDataStream` drives playback.
/// - The engine detects the end of the stream automatically when the total
///   number of bytes has been fed and playback completes.
Future<OutputBuffer> testPullBufferFileStream() async {
  final strBuf = OutputBuffer();
  await initialize();

  const asset = 'assets/audio/sample-MP3.mp3';
  final bytes = (await rootBundle.load(asset)).buffer.asUint8List();
  strBuf.writeln('loaded ${bytes.length} bytes from $asset');

  const chunkSize = 1024 * 1024;
  final fetchedOffsets = <int>{};
  final durationCompleter = Completer<double>();
  var ended = false;
  late final AudioSource source;

  // The bundled MP3 is ~96 s. Its decoded stereo f32 PCM is ~34 MB.
  source = SoLoud.instance.setPullBufferStream(
    bufferSizeBytes: 1024 * 1024 * 1,
    bufferTriggerPosition: 0.5,
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
      final end = (offset + chunkSize).clamp(0, bytes.length);
      final chunk = bytes.sublist(offset, end);
      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
      final range = SoLoud.instance.getPullBufferTimeRange(source);
      strBuf.writeln(
        '  fed $end / ${bytes.length} bytes '
        'startTime=${range.startTime.inMilliseconds}ms '
        'endTime=${range.endTime.inMilliseconds}ms',
      );
      if (end >= bytes.length) {
        strBuf.writeln('  reached end of file');
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

  final handle = SoLoud.instance.play(source);
  strBuf.writeln('play returned handle=$handle');

  final completer = Completer<void>();
  source.soundEvents.listen((event) {
    strBuf.writeln('soundEvent: ${event.event} handle=${event.handle}');
    if (event.event == SoundEventType.handleIsNoMoreValid &&
        event.handle == handle) {
      if (!completer.isCompleted) completer.complete();
    }
  });

  var timedOut = false;
  await completer.future.timeout(
    const Duration(seconds: 120),
    onTimeout: () {
      timedOut = true;
      strBuf.writeln('TIMEOUT waiting for playback end');
    },
  );

  strBuf.writeln('ended=$ended fetchedChunks=${fetchedOffsets.length} '
      'timedOut=$timedOut');
  if (!ended) {
    throw Exception('end of file was never reached');
  }
  if (timedOut) {
    throw Exception('playback did not complete before timeout');
  }

  deinit();
  return strBuf;
}

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test that setBufferStream correctly decodes small MP3 files (< 32 KB).
///
/// Reproduces a bug where MP3 files below the 32 KB buffering threshold
/// in addData() only decoded ~0.17 s of audio instead of the full duration.
/// The end-of-stream signal from setDataIsEnded() was lost because the
/// decoder wrapper had not been created yet (format detection is deferred
/// until the buffer exceeds 32 KB). Without the signal, the MP3 decode
/// loop breaks prematurely.
///
/// The test loads a small MPEG2 24 kHz mono MP3 (typical TTS output),
/// feeds it to setBufferStream, and verifies the decoded duration matches
/// the reference duration from loadMem.
///
/// Also tests a larger MPEG1 44.1 kHz MP3 (typical music) to verify
/// no regression for files that exceed the 32 KB threshold.
Future<OutputBuffer> testBufferStreamSmallMp3() async {
  final strBuf = OutputBuffer();
  await initialize();

  Future<int> measureBufferStreamMs(Uint8List bytes) async {
    final source = SoLoud.instance.setBufferStream(
      format: BufferType.auto,
      bufferingTimeNeeds: 0.3,
      maxBufferSizeBytes: 1024 * 1024 * 5,
    );
    final handle = SoLoud.instance.play(source);
    final startTime = DateTime.now();

    final completed = Completer<int>();
    source.soundEvents.listen((event) {
      if (event.event == SoundEventType.handleIsNoMoreValid &&
          event.handle == handle) {
        final elapsed = DateTime.now().difference(startTime).inMilliseconds;
        if (!completed.isCompleted) completed.complete(elapsed);
      }
    });

    await delay(50);
    SoLoud.instance.addAudioDataStream(source, bytes);
    SoLoud.instance.setDataIsEnded(source);

    final elapsed = await completed.future.timeout(
      const Duration(seconds: 15),
      onTimeout: () => -1,
    );
    await SoLoud.instance.disposeSource(source);
    return elapsed;
  }

  // --- Test 1: Small MPEG2 24 kHz mono MP3 (< 32 KB) ---
  // This is the format commonly output by TTS services (Deepgram, etc.).
  final smallMp3 = (await rootBundle.load(
    'assets/audio/test_mpeg2_24khz_mono.mp3',
  ))
      .buffer
      .asUint8List();

  final smallRefSource = await SoLoud.instance.loadMem('small_ref', smallMp3);
  final smallRefDuration = SoLoud.instance.getLength(smallRefSource);
  await SoLoud.instance.disposeSource(smallRefSource);

  final smallStreamMs = await measureBufferStreamMs(smallMp3);

  strBuf.writeln(
    'Small MP3 (${smallMp3.length} bytes): '
    'stream=${smallStreamMs}ms, ref=${smallRefDuration.inMilliseconds}ms',
  );

  // The streamed duration should be at least 50% of the reference.
  // Before the fix, it was ~170 ms for a ~3000 ms file.
  assert(
    smallStreamMs > smallRefDuration.inMilliseconds * 0.5,
    'Small MP3 buffer stream decoded only ${smallStreamMs}ms '
    'but reference is ${smallRefDuration.inMilliseconds}ms. '
    'End-of-stream signal likely lost for small files.',
  );

  // --- Test 2: Larger MPEG1 44.1 kHz MP3 (> 32 KB, regression check) ---
  final largeMp3 = (await rootBundle.load(
    'assets/audio/explosion.mp3',
  ))
      .buffer
      .asUint8List();

  final largeRefSource = await SoLoud.instance.loadMem('large_ref', largeMp3);
  final largeRefDuration = SoLoud.instance.getLength(largeRefSource);
  await SoLoud.instance.disposeSource(largeRefSource);

  final largeStreamMs = await measureBufferStreamMs(largeMp3);

  strBuf.writeln(
    'Large MP3 (${largeMp3.length} bytes): '
    'stream=${largeStreamMs}ms, ref=${largeRefDuration.inMilliseconds}ms',
  );

  assert(
    largeStreamMs > largeRefDuration.inMilliseconds * 0.5,
    'Large MP3 buffer stream decoded only ${largeStreamMs}ms '
    'but reference is ${largeRefDuration.inMilliseconds}ms. '
    'Regression in large file streaming.',
  );

  deinit();
  return strBuf;
}

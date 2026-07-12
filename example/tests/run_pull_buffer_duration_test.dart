import 'dart:io' if (dart.library.js_interop) 'dart_io_stub.dart';

import 'package:flutter/material.dart';

import 'tests/pull_buffer_duration_test.dart' as pull_buffer_duration;

/// Fast duration-probe test for Opus and Vorbis pull-buffer streams.
void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  try {
    final opusOutput = await pull_buffer_duration.testPullBufferDuration(
      'sample-OPUS.opus',
    );
    print('PULL_BUFFER_DURATION_OPUS_PASSED'); // ignore: avoid_print
    print(opusOutput); // ignore: avoid_print

    final vorbisOutput = await pull_buffer_duration.testPullBufferDuration(
      'sample-vorbis.ogg',
    );
    print('PULL_BUFFER_DURATION_VORBIS_PASSED'); // ignore: avoid_print
    print(vorbisOutput); // ignore: avoid_print

    exit(0);
  } catch (e, st) {
    // ignore: avoid_print
    print('PULL_BUFFER_DURATION_TEST_FAILED: $e');
    // ignore: avoid_print
    print(st);
    exit(1);
  }
}

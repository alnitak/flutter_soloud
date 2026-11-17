// speechText() copies its text into native memory for the call and must free
// that copy. Measured as process memory growth through the real native
// library. Without an engine the export returns backendNotInited before it
// reads the text, so nothing is synthesized and the Dart-side copy is all
// that is allocated.
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter_soloud/src/bindings/bindings_player_ffi.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

const int _mb = 1024 * 1024;

void main() {
  test('speechText frees its copy of the text', () {
    final bindings = FlutterSoLoudFfi();
    final text = 'a' * (16 * _mb);

    void call() {
      expect(bindings.speechText(text).error, PlayerErrors.backendNotInited);
    }

    // The first call grows the heap the later ones reuse.
    call();
    final before = ProcessInfo.currentRss;
    for (var i = 0; i < 12; i++) {
      call();
    }
    final growth = ProcessInfo.currentRss - before;
    expect(
      growth,
      lessThan(64 * _mb),
      reason:
          '12 calls with a 16 MB text grew the process by '
          '${growth ~/ _mb} MB: the native copy of the text leaks',
    );
  });
}

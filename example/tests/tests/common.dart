import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> initialize() async {
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(0.2);
}

void deinit() {
  SoLoud.instance.deinit();
}

Future<void> delay(int ms) async {
  await Future.delayed(Duration(milliseconds: ms), () {});
}

bool closeTo(num value, num expected, num epsilon) {
  return (value - expected).abs() <= epsilon.abs();
}

Future<AudioSource> loadAsset() async {
  return SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
}

/// A buffer that accumulates output and also prints each line to the console.
///
/// Use this in tests instead of `StringBuffer` and `print`, so that output is
/// visible in real-time both in the console and in the GUI test harness.
class OutputBuffer extends StringBuffer {
  /// Write a line to the buffer and print it to the console.
  @override
  void writeln([Object? object]) {
    super.writeln(object);
    // ignore: avoid_print
    print(object);
  }

  /// Write a raw string to the buffer and print it without an added newline.
  @override
  void write(Object? object) {
    super.write(object);
    // ignore: avoid_print
    print(object);
  }
}

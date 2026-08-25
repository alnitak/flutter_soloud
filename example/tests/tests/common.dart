import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> initialize() async {
  debugPrint('>>> initialize()');
  await SoLoud.instance.init();
  debugPrint('>>> initialize() done');
  if (!kIsWeb) {
    SoLoud.instance.setGlobalVolume(0.2);
  }
}

void deinit() {
  debugPrint('>>> deinit()');
  SoLoud.instance.deinit();
  debugPrint('>>> deinit() done');
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

/// A buffer that accumulates test output.
class OutputBuffer extends StringBuffer {}

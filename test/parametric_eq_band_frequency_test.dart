// Reads parametric EQ band frequencies through bandFrequency(), which reads
// them from an active EQ and reports the band defaults for one that is not
// active.
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUpAll(() async {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          const MethodChannel('plugins.flutter.io/path_provider'),
          (MethodCall methodCall) async => Directory.systemTemp.path,
        );
    await SoLoud.instance.init();
  });

  tearDownAll(() {
    if (SoLoud.instance.isInitialized) SoLoud.instance.deinit();
  });

  test('a global EQ that is not active reports the default frequencies', () {
    final eq = SoLoud.instance.filters.parametricEqFilter;
    expect(eq.isActive, isFalse);
    expect(eq.bandFrequency(0), 30);
    expect(eq.bandFrequency(1), 600);
    expect(eq.bandFrequency(2), 12000);
    expect(eq.bandFrequency(63), 1000);
  });

  test('a bus EQ that is not active reports the default frequencies', () {
    final bus = SoLoud.instance.createMixingBus();
    addTearDown(bus.dispose);
    final eq = bus.filters.parametricEqFilter;
    expect(eq.isActive, isFalse);
    expect(eq.bandFrequency(0), 30);
    expect(eq.bandFrequency(1), 600);
    expect(eq.bandFrequency(2), 12000);
    expect(eq.bandFrequency(63), 1000);
  });

  test('an active global EQ reports the frequency it was given', () {
    final eq = SoLoud.instance.filters.parametricEqFilter..activate();
    addTearDown(() {
      if (eq.isActive) eq.deactivate();
    });
    eq.bandFreq(1).value = 9999;
    expect(eq.bandFrequency(1), 9999);

    eq.deactivate();
    expect(eq.bandFrequency(1), 600);
  });
}

// Reads filter parameter names through the FFI binding, which asks the
// native side how many there are before it allocates room for them (the
// parametric EQ has 195).
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/filters/parametric_eq.dart';
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

  test('the parametric EQ reports all of its parameter names', () {
    final ret = SoLoudController().soLoudFFI.getFilterParamNames(
      FilterType.parametricEq,
    );
    expect(ret.error, PlayerErrors.noError);
    expect(ret.names, hasLength(ParametricEqParam.maxParams));
    expect(ret.names.first, 'Wet');
    expect(ret.names[ParametricEqParam.bandFreq(0)], 'Band 0 Frequency');
    expect(ret.names.last, 'Band 63 Q');
  });

  test('every filter reports its parameter names', () {
    for (final type in FilterType.values) {
      final ret = SoLoudController().soLoudFFI.getFilterParamNames(type);
      expect(ret.error, PlayerErrors.noError, reason: '$type');
      expect(ret.names, isNotEmpty, reason: '$type');
      expect(ret.names, everyElement(isNotEmpty), reason: '$type');
    }
  });
}

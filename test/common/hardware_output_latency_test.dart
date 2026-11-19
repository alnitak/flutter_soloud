import 'dart:io';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('Hardware output latency', () {
    test('throws SoLoudNotInitializedException when not initialized', () {
      expect(SoLoud.instance.isInitialized, isFalse);
      expect(
        SoLoud.instance.getHardwareOutputLatency,
        throwsA(isA<SoLoudNotInitializedException>()),
      );
      expect(
        SoLoud.instance.getOutputLatency,
        throwsA(isA<SoLoudNotInitializedException>()),
      );
    });

    test('is declared across C++ headers', () {
      final bindingsH = File('src/bindings.h').readAsStringSync();
      expect(
        bindingsH.contains('double getHardwareOutputLatency()'),
        isTrue,
        reason: 'src/bindings.h must declare getHardwareOutputLatency',
      );

      final soloudH = File('src/soloud/include/soloud.h').readAsStringSync();
      expect(
        soloudH.contains('time getHardwareOutputLatency()'),
        isTrue,
        reason:
            'src/soloud/include/soloud.h must declare getHardwareOutputLatency',
      );

      final internalH = File(
        'src/soloud/include/soloud_internal.h',
      ).readAsStringSync();
      expect(
        internalH.contains('miniaudio_getHardwareOutputLatency()'),
        isTrue,
        reason:
            'src/soloud/include/soloud_internal.h must declare '
            'miniaudio_getHardwareOutputLatency',
      );
    });
  });
}

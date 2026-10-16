import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

void main() {
  group('CustomFilterParamSpec', () {
    test('stores parameter metadata and validates bounds', () {
      const spec = CustomFilterParamSpec(
        name: 'Cutoff',
        min: 20,
        max: 20000,
        defaultValue: 1000,
      );
      expect(spec.name, 'Cutoff');
      expect(spec.min, 20.0);
      expect(spec.max, 20000.0);
      expect(spec.defaultValue, 1000.0);
    });

    test('FilterType.customFilter has expected paramcount and index', () {
      expect(FilterType.customFilter.index, 12);
      expect(FilterType.customFilter.numParameters, 8);
      expect(FilterType.customFilter.toString(), 'Custom Filter');
    });
  });

  group('BufferType & PlayerErrors for RealtimeStream', () {
    test('BufferType.f32lePlanar is exposed with value 5', () {
      expect(BufferType.f32lePlanar.value, 5);
    });

    test('PlayerErrors.hashIsNotARealtimeStream maps to exception', () {
      final ex = SoLoudCppException.fromPlayerError(
        PlayerErrors.hashIsNotARealtimeStream,
      );
      expect(ex, isA<SoLoudHashIsNotARealtimeStreamCppException>());
      expect(ex.description, contains('setRealtimeStream'));
    });
  });
}

import 'package:flutter_soloud/src/filters/parametric_eq.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('ParametricEq frequency calculation', () {
    const eq = ParametricEqGlobal();

    test('single band returns 1000 Hz', () {
      expect(eq.calculateBandFrequency(0, 1), 1000.0);
    });

    test('3 bands distribute between 30 Hz and 12000 Hz', () {
      expect(eq.calculateBandFrequency(0, 3), 30.0);
      expect(eq.calculateBandFrequency(1, 3), closeTo(600.0, 1e-6));
      expect(eq.calculateBandFrequency(2, 3), closeTo(12000.0, 1e-6));
    });

    test('upper bound is 12000 Hz for various band counts', () {
      for (final nBands in [2, 4, 8, 10, 16, 32, 64]) {
        expect(eq.calculateBandFrequency(0, nBands), 30.0);
        expect(
          eq.calculateBandFrequency(nBands - 1, nBands),
          closeTo(12000.0, 1e-6),
        );
      }
    });

    test('invalid bandIndex or nBands throws ArgumentError', () {
      expect(() => eq.calculateBandFrequency(-1, 3), throwsArgumentError);
      expect(() => eq.calculateBandFrequency(3, 3), throwsArgumentError);
      expect(() => eq.calculateBandFrequency(0, 0), throwsArgumentError);
      expect(() => eq.calculateBandFrequency(0, 65), throwsArgumentError);
    });
  });
}

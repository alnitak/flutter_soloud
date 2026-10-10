import 'package:flutter_soloud/src/filters/filters.dart';
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

  group('ParametricEqParam indexing and bounds', () {
    test('parameter offsets are correct and non-overlapping', () {
      expect(ParametricEqParam.wet, 0);
      expect(ParametricEqParam.stftWindowSize, 1);
      expect(ParametricEqParam.numBands, 2);
      expect(ParametricEqParam.bandGainOffset, 3);
      expect(ParametricEqParam.bandFreqOffset, 67);
      expect(ParametricEqParam.bandQOffset, 131);
      expect(ParametricEqParam.maxParams, 195);
      expect(FilterType.parametricEq.numParameters, 195);

      for (var i = 0; i < 64; i++) {
        expect(ParametricEqParam.bandGain(i), 3 + i);
        expect(ParametricEqParam.bandFreq(i), 67 + i);
        expect(ParametricEqParam.bandQ(i), 131 + i);
      }

      expect(() => ParametricEqParam.bandGain(-1), throwsArgumentError);
      expect(() => ParametricEqParam.bandGain(64), throwsArgumentError);
      expect(() => ParametricEqParam.bandFreq(-1), throwsArgumentError);
      expect(() => ParametricEqParam.bandFreq(64), throwsArgumentError);
      expect(() => ParametricEqParam.bandQ(-1), throwsArgumentError);
      expect(() => ParametricEqParam.bandQ(64), throwsArgumentError);
    });

    test('min and max parameter bounds are valid', () {
      expect(ParametricEqParam.getMin(ParametricEqParam.wet), 0.0);
      expect(ParametricEqParam.getMax(ParametricEqParam.wet), 1.0);

      expect(ParametricEqParam.getMin(ParametricEqParam.stftWindowSize), 32.0);
      expect(
        ParametricEqParam.getMax(ParametricEqParam.stftWindowSize),
        4096.0,
      );

      expect(ParametricEqParam.getMin(ParametricEqParam.numBands), 1.0);
      expect(ParametricEqParam.getMax(ParametricEqParam.numBands), 64.0);

      for (var i = 0; i < 64; i++) {
        final gainIdx = ParametricEqParam.bandGain(i);
        expect(ParametricEqParam.getMin(gainIdx), 0.0);
        expect(ParametricEqParam.getMax(gainIdx), 4.0);

        final freqIdx = ParametricEqParam.bandFreq(i);
        expect(ParametricEqParam.getMin(freqIdx), 10.0);
        expect(ParametricEqParam.getMax(freqIdx), 24000.0);

        final qIdx = ParametricEqParam.bandQ(i);
        expect(ParametricEqParam.getMin(qIdx), 0.1);
        expect(ParametricEqParam.getMax(qIdx), 20.0);
      }
    });

    test('parameter names are properly formatted', () {
      expect(ParametricEqParam.getName(ParametricEqParam.wet), 'Wet');
      expect(
        ParametricEqParam.getName(ParametricEqParam.stftWindowSize),
        'STFT Window Size',
      );
      expect(
        ParametricEqParam.getName(ParametricEqParam.numBands),
        'Number of Bands',
      );
      expect(
        ParametricEqParam.getName(ParametricEqParam.bandGain(0)),
        'Band 0 Gain',
      );
      expect(
        ParametricEqParam.getName(ParametricEqParam.bandFreq(0)),
        'Band 0 Frequency',
      );
      expect(ParametricEqParam.getName(ParametricEqParam.bandQ(0)), 'Band 0 Q');
    });

    test(
      'ParametricEqGlobal creates FilterParam instances with matching indices',
      () {
        const eq = ParametricEqGlobal();
        expect(eq.filterType, FilterType.parametricEq);
        expect(eq.wet.attributeId, ParametricEqParam.wet);
        expect(eq.stftWindowSize.attributeId, ParametricEqParam.stftWindowSize);
        expect(eq.numBands.attributeId, ParametricEqParam.numBands);

        for (var i = 0; i < 3; i++) {
          expect(eq.bandGain(i).attributeId, ParametricEqParam.bandGain(i));
          expect(eq.bandFreq(i).attributeId, ParametricEqParam.bandFreq(i));
          expect(
            eq.bandFrequencyParam(i).attributeId,
            ParametricEqParam.bandFreq(i),
          );
          expect(eq.bandQ(i).attributeId, ParametricEqParam.bandQ(i));
        }
      },
    );
  });
}

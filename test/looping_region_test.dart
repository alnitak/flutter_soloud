import 'package:flutter_soloud/src/helpers/looping_region.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('validateLoopRegion', () {
    test('accepts natural EOF and a later endpoint', () {
      expect(() => validateLoopRegion(start: Duration.zero), returnsNormally);
      expect(
        () => validateLoopRegion(
          start: const Duration(milliseconds: 250),
          end: const Duration(milliseconds: 750),
        ),
        returnsNormally,
      );
    });

    test('rejects a negative start', () {
      expect(
        () => validateLoopRegion(start: const Duration(microseconds: -1)),
        throwsArgumentError,
      );
    });

    test('rejects an endpoint equal to or before the start', () {
      expect(
        () => validateLoopRegion(
          start: const Duration(milliseconds: 500),
          end: const Duration(milliseconds: 500),
        ),
        throwsArgumentError,
      );
      expect(
        () => validateLoopRegion(
          start: const Duration(milliseconds: 500),
          end: const Duration(milliseconds: 499),
        ),
        throwsArgumentError,
      );
    });
    test('accepts valid frame offsets', () {
      expect(
        () => validateLoopRegion(
          start: Duration.zero,
          startOffset: 100,
          endOffset: 500,
        ),
        returnsNormally,
      );
    });

    test('rejects negative start offset or end offset <= start offset', () {
      expect(
        () => validateLoopRegion(start: Duration.zero, startOffset: -5),
        throwsArgumentError,
      );
      expect(
        () => validateLoopRegion(
          start: Duration.zero,
          startOffset: 100,
          endOffset: 100,
        ),
        throwsArgumentError,
      );
      expect(
        () => validateLoopRegion(
          start: Duration.zero,
          startOffset: 100,
          endOffset: 99,
        ),
        throwsArgumentError,
      );
    });

    test('asserts when both Duration and frame offsets are provided', () {
      expect(
        () => validateLoopRegion(
          start: const Duration(milliseconds: 100),
          startOffset: 100,
        ),
        throwsA(isA<AssertionError>()),
      );
      expect(
        () => validateLoopRegion(
          start: Duration.zero,
          end: const Duration(milliseconds: 500),
          endOffset: 500,
        ),
        throwsA(isA<AssertionError>()),
      );
    });
  });
}

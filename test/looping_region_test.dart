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
  });
}

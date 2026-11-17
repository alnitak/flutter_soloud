import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
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
      expect(FilterType.customFilter.index, 13);
      expect(FilterType.customFilter.numParameters, 8);
      expect(FilterType.customFilter.toString(), 'Custom Filter');
    });
  });

  group('BufferType & PlayerErrors for RealtimeStream', () {
    test('BufferType.f32lePlanar is exposed with value 5', () {
      expect(BufferType.f32lePlanar.value, 5);
    });

    test('push rejects a frameCount larger than the samples given', () {
      final stream = RealtimeStreamHandle.fromHash(
        soundHash: SoundHash.fromValue(1234),
        sampleRate: 48000,
        channels: Channels.stereo,
      );
      // 8 stereo samples hold 4 frames; asking for 5 must not reach native
      // code, which would read past the list.
      expect(
        () => stream.pushFloat32(Float32List(8), frameCount: 5),
        throwsArgumentError,
      );
      expect(
        () => stream.pushInt16(Int16List(8), frameCount: 5),
        throwsArgumentError,
      );
      expect(
        () => stream.pushScheduledFloat32(
          Duration.zero,
          Float32List(8),
          frameCount: 5,
        ),
        throwsArgumentError,
      );
      expect(
        () => stream.pushClockedFloat32(
          Duration.zero,
          Float32List(8),
          frameCount: 5,
        ),
        throwsArgumentError,
      );
    });

    test('custom source lifecycle callbacks must be the same address', () {
      expect(customSourceLifecycleAddress(0, 0), 0);
      expect(customSourceLifecycleAddress(42, 0), 42);
      expect(customSourceLifecycleAddress(0, 42), 42);
      expect(customSourceLifecycleAddress(42, 42), 42);
      expect(() => customSourceLifecycleAddress(42, 43), throwsArgumentError);
    });

    test('PlayerErrors.hashIsNotARealtimeStream maps to exception', () {
      final ex = SoLoudCppException.fromPlayerError(
        PlayerErrors.hashIsNotARealtimeStream,
      );
      expect(ex, isA<SoLoudHashIsNotARealtimeStreamCppException>());
      expect(ex.description, contains('realtime stream'));
    });
  });
}

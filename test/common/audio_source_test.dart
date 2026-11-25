import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:test/test.dart';

void main() {
  group('AudioSource', () {
    test('defaults to empty strings for soundPath and tempFilePath', () {
      final source = AudioSource(const SoundHash(123));

      expect(source.soundPath, isEmpty);
      expect(source.tempFilePath, isEmpty);
      expect(source.toString(), 'soundHash: 123 has 0 active handles');
    });

    test('stores soundPath and tempFilePath via constructor', () {
      final source = AudioSource(
        const SoundHash(456),
        soundPath: 'assets/audio/laser.mp3',
        tempFilePath: '/tmp/temp-sound-asset-1.mp3',
      );

      expect(source.soundPath, 'assets/audio/laser.mp3');
      expect(source.tempFilePath, '/tmp/temp-sound-asset-1.mp3');
      expect(
        source.toString(),
        'soundHash: 456 (assets/audio/laser.mp3) has 0 active handles',
      );
    });

    test('allows mutating soundPath and tempFilePath', () {
      final source = AudioSource(const SoundHash(789))
        ..soundPath = 'https://example.com/stream.mp3'
        ..tempFilePath = '/tmp/temp-sound-url-1.mp3';

      expect(source.soundPath, 'https://example.com/stream.mp3');
      expect(source.tempFilePath, '/tmp/temp-sound-url-1.mp3');
      expect(
        source.toString(),
        'soundHash: 789 (https://example.com/stream.mp3) has 0 active handles',
      );
    });
  });
}

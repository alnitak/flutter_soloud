import 'dart:io';

import 'package:flutter_soloud/src/metadata.dart';
import 'package:test/test.dart';

void main() {
  group('AacMetadata, Ac3Metadata, Eac3Metadata models and formatting', () {
    test('AacMetadata stores properties correctly', () {
      final aac = AacMetadata(
        sampleRate: 44100,
        channels: 2,
        profile: 'LC',
        bitrate: 128000,
        frameLength: 304,
        title: 'Radio Song',
        artist: 'Radio Artist',
        album: 'Radio Album',
      );

      expect(aac.sampleRate, 44100);
      expect(aac.channels, 2);
      expect(aac.profile, 'LC');
      expect(aac.bitrate, 128000);
      expect(aac.frameLength, 304);
      expect(aac.title, 'Radio Song');
      expect(aac.artist, 'Radio Artist');
      expect(aac.album, 'Radio Album');
    });

    test('Ac3Metadata stores properties correctly', () {
      final ac3 = Ac3Metadata(
        sampleRate: 48000,
        channels: 6,
        bitrate: 384000,
        bsid: 8,
        bsmod: 0,
        acmod: 7,
        lfeOn: true,
        frameSize: 1536,
      );

      expect(ac3.sampleRate, 48000);
      expect(ac3.channels, 6);
      expect(ac3.bitrate, 384000);
      expect(ac3.bsid, 8);
      expect(ac3.bsmod, 0);
      expect(ac3.acmod, 7);
      expect(ac3.lfeOn, isTrue);
      expect(ac3.frameSize, 1536);
    });

    test('Eac3Metadata stores properties correctly', () {
      final eac3 = Eac3Metadata(
        sampleRate: 48000,
        channels: 8,
        bitrate: 448000,
        bsid: 16,
        streamType: 0,
        substreamId: 0,
        acmod: 7,
        lfeOn: true,
        frameSize: 2048,
        numBlocks: 6,
      );

      expect(eac3.sampleRate, 48000);
      expect(eac3.channels, 8);
      expect(eac3.bitrate, 448000);
      expect(eac3.bsid, 16);
      expect(eac3.streamType, 0);
      expect(eac3.substreamId, 0);
      expect(eac3.acmod, 7);
      expect(eac3.lfeOn, isTrue);
      expect(eac3.frameSize, 2048);
      expect(eac3.numBlocks, 6);
    });

    test('AudioMetadata.toString() formats AAC metadata properly', () {
      final metadata = AudioMetadata(
        detectedType: DetectedType.aac,
        aacMetadata: AacMetadata(
          sampleRate: 44100,
          channels: 2,
          profile: 'LC',
          bitrate: 128000,
          frameLength: 304,
          title: 'Radio Song',
          artist: 'Radio Artist',
          album: 'Radio Album',
        ),
      );

      final str = metadata.toString();
      expect(str, contains('|---------- AAC ----------|'));
      expect(str, contains('Profile: LC'));
      expect(str, contains('Bitrate: 128000'));
      expect(str, contains('FrameLength: 304'));
      expect(str, contains('Title: Radio Song'));
      expect(str, contains('Artist: Radio Artist'));
      expect(str, contains('Album: Radio Album'));
    });

    test('AudioMetadata.toString() formats AC3 metadata properly', () {
      final metadata = AudioMetadata(
        detectedType: DetectedType.ac3,
        ac3Metadata: Ac3Metadata(
          sampleRate: 48000,
          channels: 6,
          bitrate: 384000,
          bsid: 8,
          bsmod: 0,
          acmod: 7,
          lfeOn: true,
          frameSize: 1536,
        ),
      );

      final str = metadata.toString();
      expect(str, contains('|---------- AC3 ----------|'));
      expect(str, contains('Bitrate: 384000'));
      expect(str, contains('Bsid: 8'));
      expect(str, contains('Acmod: 7'));
      expect(str, contains('LfeOn: true'));
      expect(str, contains('FrameSize: 1536'));
    });

    test('AudioMetadata.toString() formats EAC3 metadata properly', () {
      final metadata = AudioMetadata(
        detectedType: DetectedType.eac3,
        eac3Metadata: Eac3Metadata(
          sampleRate: 48000,
          channels: 8,
          bitrate: 448000,
          bsid: 16,
          streamType: 0,
          substreamId: 0,
          acmod: 7,
          lfeOn: true,
          frameSize: 2048,
          numBlocks: 6,
        ),
      );

      final str = metadata.toString();
      expect(str, contains('|---------- EAC3 ----------|'));
      expect(str, contains('Bsid: 16'));
      expect(str, contains('StreamType: 0'));
      expect(str, contains('NumBlocks: 6'));
      expect(str, contains('FrameSize: 2048'));
    });

    test('metadata_ffi.h defines aacMetadata, ac3Metadata, eac3Metadata', () {
      final header = File('src/audiobuffer/metadata_ffi.h').readAsStringSync();
      expect(header, contains('struct AacMetadataFFI'));
      expect(header, contains('struct Ac3MetadataFFI'));
      expect(header, contains('struct Eac3MetadataFFI'));
      expect(header, contains('AacMetadataFFI aacMetadata;'));
      expect(header, contains('Ac3MetadataFFI ac3Metadata;'));
      expect(header, contains('Eac3MetadataFFI eac3Metadata;'));
    });
  });
}

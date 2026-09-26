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

    test('Mp3Metadata stores all extended properties correctly', () {
      final mp3 = Mp3Metadata(
        title: 'Song Title',
        artist: 'Lead Artist',
        albumArtist: 'Band Artist',
        album: 'Album Name',
        date: '2024-01-01',
        genre: 'Rock',
        composer: 'Song Composer',
        comment: 'Studio Mix',
        track: '3/12',
        disc: '1/2',
        streamUrl: 'https://example.com/stream',
        sampleRate: 44100,
        channels: 2,
        bitrate: 320000,
      );

      expect(mp3.title, 'Song Title');
      expect(mp3.artist, 'Lead Artist');
      expect(mp3.albumArtist, 'Band Artist');
      expect(mp3.album, 'Album Name');
      expect(mp3.date, '2024-01-01');
      expect(mp3.genre, 'Rock');
      expect(mp3.composer, 'Song Composer');
      expect(mp3.comment, 'Studio Mix');
      expect(mp3.track, '3/12');
      expect(mp3.disc, '1/2');
      expect(mp3.streamUrl, 'https://example.com/stream');
      expect(mp3.sampleRate, 44100);
      expect(mp3.channels, 2);
      expect(mp3.bitrate, 320000);
    });

    test('M4aMetadata stores properties and formats correctly', () {
      final m4a = M4aMetadata(
        title: 'M4A Song',
        artist: 'M4A Artist',
        albumArtist: 'M4A Album Artist',
        album: 'M4A Album',
        date: '2024',
        genre: 'Pop',
        composer: 'M4A Composer',
        comment: 'Nice track',
        track: '1/10',
        disc: '1/1',
        codec: 'mp4a',
        sampleRate: 48000,
        channels: 2,
        bitrate: 256000,
      );

      expect(m4a.title, 'M4A Song');
      expect(m4a.artist, 'M4A Artist');
      expect(m4a.albumArtist, 'M4A Album Artist');
      expect(m4a.album, 'M4A Album');
      expect(m4a.date, '2024');
      expect(m4a.genre, 'Pop');
      expect(m4a.composer, 'M4A Composer');
      expect(m4a.comment, 'Nice track');
      expect(m4a.track, '1/10');
      expect(m4a.disc, '1/1');
      expect(m4a.codec, 'mp4a');
      expect(m4a.sampleRate, 48000);
      expect(m4a.channels, 2);
      expect(m4a.bitrate, 256000);

      final metadata = AudioMetadata(
        detectedType: DetectedType.m4a,
        m4aMetadata: m4a,
      );

      final str = metadata.toString();
      expect(str, contains('|---------- M4A ----------|'));
      expect(str, contains('Title: M4A Song'));
      expect(str, contains('Artist: M4A Artist'));
      expect(str, contains('AlbumArtist: M4A Album Artist'));
      expect(str, contains('Album: M4A Album'));
      expect(str, contains('Date: 2024'));
      expect(str, contains('Genre: Pop'));
      expect(str, contains('Composer: M4A Composer'));
      expect(str, contains('Comment: Nice track'));
      expect(str, contains('Track: 1/10'));
      expect(str, contains('Disc: 1/1'));
      expect(str, contains('Codec: mp4a'));
      expect(str, contains('SampleRate: 48000'));
      expect(str, contains('Channels: 2'));
      expect(str, contains('Bitrate: 256000'));
    });

    test('AudioMetadata.toString() formats extended MP3 properly', () {
      final metadata = AudioMetadata(
        detectedType: DetectedType.mp3WithId3,
        mp3Metadata: Mp3Metadata(
          title: 'MP3 Title',
          artist: 'MP3 Artist',
          albumArtist: 'MP3 Album Artist',
          album: 'MP3 Album',
          date: '2023',
          genre: 'Jazz',
          composer: 'MP3 Composer',
          comment: 'Live session',
          track: '4/8',
          disc: '1/1',
          streamUrl: 'https://stream.example.com',
          sampleRate: 44100,
          channels: 2,
          bitrate: 192000,
        ),
      );

      final str = metadata.toString();
      expect(str, contains('|---------- MP3 with ID3 ----------|'));
      expect(str, contains('Title: MP3 Title'));
      expect(str, contains('Artist: MP3 Artist'));
      expect(str, contains('AlbumArtist: MP3 Album Artist'));
      expect(str, contains('Album: MP3 Album'));
      expect(str, contains('Date: 2023'));
      expect(str, contains('Genre: Jazz'));
      expect(str, contains('Composer: MP3 Composer'));
      expect(str, contains('Comment: Live session'));
      expect(str, contains('Track: 4/8'));
      expect(str, contains('Disc: 1/1'));
      expect(str, contains('StreamUrl: https://stream.example.com'));
      expect(str, contains('SampleRate: 44100'));
      expect(str, contains('Channels: 2'));
      expect(str, contains('Bitrate: 192000'));
    });

    test('metadata_ffi.h defines metadata structs', () {
      final header = File('src/audiobuffer/metadata_ffi.h').readAsStringSync();
      expect(header, contains('struct AacMetadataFFI'));
      expect(header, contains('struct Ac3MetadataFFI'));
      expect(header, contains('struct Eac3MetadataFFI'));
      expect(header, contains('struct M4aMetadataFFI'));
      expect(header, contains('AacMetadataFFI aacMetadata;'));
      expect(header, contains('Ac3MetadataFFI ac3Metadata;'));
      expect(header, contains('Eac3MetadataFFI eac3Metadata;'));
      expect(header, contains('M4aMetadataFFI m4aMetadata;'));
      expect(header, contains('char album_artist[MAX_STRING_LENGTH];'));
      expect(header, contains('char composer[MAX_STRING_LENGTH];'));
      expect(header, contains('char track[MAX_STRING_LENGTH];'));
      expect(header, contains('char disc[MAX_STRING_LENGTH];'));
    });
  });
}

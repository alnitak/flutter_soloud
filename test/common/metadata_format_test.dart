// The metadata classes of lib/src/metadata.dart, without the engine:
// DetectedType conversions, the toString of every metadata class (fields set,
// empty fields left out, non-ASCII text), OggMetadata.toStringWithFormat for
// every DetectedType, AudioMetadata.toString for every DetectedType with and
// without its format-specific metadata, and equality.

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

VorbisInfo _vorbis() => VorbisInfo(
  version: 0,
  channels: 2,
  rate: 44100,
  bitrateUpper: 192000,
  bitrateNominal: 128000,
  bitrateLower: 64000,
  bitrateWindow: 0,
);

OpusInfo _opus() => OpusInfo(
  version: 1,
  channels: 2,
  preSkip: 312,
  inputSampleRate: 48000,
  outputGain: -256,
  mappingFamily: 0,
  streamCount: 1,
  coupledCount: 1,
  channelMapping: [0, 1],
  channelMappingSize: 2,
);

FlacInfo _flac() => FlacInfo(
  minBlockSize: 4096,
  maxBlockSize: 4096,
  minFrameSize: 14,
  maxFrameSize: 9000,
  sampleRate: 96000,
  channels: 2,
  bitsPerSample: 24,
  totalSamples: 960000,
);

OggMetadata _ogg({Map<String, String>? comments}) => OggMetadata(
  vendor: 'Xiph.Org libVorbis I 20200704',
  commentsCount: (comments ?? const {'TITLE': 'Song', 'ARTIST': 'Band'}).length,
  comments: comments ?? {'TITLE': 'Song', 'ARTIST': 'Band'},
  vorbisInfo: _vorbis(),
  opusInfo: _opus(),
  flacInfo: _flac(),
);

/// The lines of [s] without their leading tab.
List<String> _lines(String s) =>
    s.split('\n').map((l) => l.replaceFirst('\t', '')).toList();

void main() {
  group('DetectedType', () {
    test('fromInt maps each index to its value', () {
      for (final type in DetectedType.values) {
        expect(DetectedType.fromInt(type.index), type);
      }
    });

    test('fromInt maps out-of-range values to unknown', () {
      for (final value in [-1, DetectedType.values.length, 255, 1 << 31]) {
        expect(DetectedType.fromInt(value), DetectedType.unknown);
      }
    });

    test('toString gives a distinct, non-empty name for every value', () {
      final names = DetectedType.values.map((t) => t.toString()).toList();
      expect(names, everyElement(isNotEmpty));
      expect(names.toSet().length, names.length);
      expect(DetectedType.wav.toString(), 'WAV');
      expect(DetectedType.oggOpus.toString(), 'Ogg Opus');
      expect(DetectedType.mp3Stream.toString(), 'MP3 Stream');
    });
  });

  group('Mp3Metadata', () {
    test('toString lists every field that is set, one per line', () {
      final mp3 = Mp3Metadata(
        title: 'T',
        artist: 'A',
        album: 'Al',
        date: '1999',
        genre: 'G',
        albumArtist: 'AA',
        composer: 'C',
        comment: 'Co',
        track: '1/9',
        disc: '2/2',
        streamUrl: 'http://x',
        sampleRate: 44100,
        channels: 2,
        bitrate: 128000,
      );
      expect(_lines(mp3.toString()), [
        'SampleRate: 44100',
        'Channels: 2',
        'Bitrate: 128000',
        'Title: T',
        'Artist: A',
        'AlbumArtist: AA',
        'Album: Al',
        'Date: 1999',
        'Genre: G',
        'Composer: C',
        'Comment: Co',
        'Track: 1/9',
        'Disc: 2/2',
        'StreamUrl: http://x',
      ]);
    });

    test('empty strings and zero numbers are left out', () {
      final mp3 = Mp3Metadata(
        title: '',
        artist: '',
        album: '',
        date: '',
        genre: '',
      );
      expect(mp3.toString(), isEmpty);

      mp3.artist = 'Only';
      expect(mp3.toString(), '\tArtist: Only');
    });

    test('non-ASCII text is kept as is', () {
      final mp3 = Mp3Metadata(
        title: 'Città 夜 🎵',
        artist: 'Ægir & Zoë',
        album: 'Ελληνικά',
        date: '',
        genre: 'Ünïcödé',
      );
      final s = mp3.toString();
      expect(s, contains('Title: Città 夜 🎵'));
      expect(s, contains('Artist: Ægir & Zoë'));
      expect(s, contains('Album: Ελληνικά'));
      expect(s, contains('Genre: Ünïcödé'));
    });
  });

  group('Ogg info classes', () {
    test('VorbisInfo.toString lists every field', () {
      expect(_lines(_vorbis().toString()), [
        'Version: 0',
        'Channels: 2',
        'Rate: 44100',
        'Upper: 192000',
        'Nominal: 128000',
        'Lower: 64000',
        'Window: 0',
      ]);
    });

    test('OpusInfo.toString lists every field', () {
      expect(_lines(_opus().toString()), [
        'Opus version: 1',
        'Channels: 2',
        'PreSkip: 312',
        'InputSampleRate: 48000',
        'OutputGain: -256',
        'MappingFamily: 0',
        'StreamCount: 1',
        'CoupledCount: 1',
        'ChannelMapping: [0, 1]',
        'ChannelMappingSize: 2',
      ]);
    });

    test('FlacInfo.toString lists every field', () {
      final s = _flac().toString();
      for (final line in [
        'MinBlockSize: 4096',
        'MaxBlockSize: 4096',
        'MinFrameSize: 14',
        'MaxFrameSize: 9000',
        'SampleRate: 96000',
        'Channels: 2',
        'TotalSamples: 960000',
      ]) {
        expect(_lines(s), contains(line));
      }
      expect(s, contains('24'));
    });

    test('FlacInfo.toString does not call the bits per sample a bitrate', () {
      // 24 bits per sample is not a bitrate: printed as "Bitrate: 24" it
      // reads as 24 bit/s.
      expect(_lines(_flac().toString()), isNot(contains('Bitrate: 24')));
    });
  });

  group('OggMetadata.toStringWithFormat', () {
    for (final format in DetectedType.values) {
      test('$format', () {
        final s = _ogg().toStringWithFormat(format);
        final lines = _lines(s);
        expect(lines.first, 'Vendor: Xiph.Org libVorbis I 20200704');
        expect(lines, contains('CommentsCount: 2'));
        expect(lines, contains('TITLE: Song'));
        expect(lines, contains('ARTIST: Band'));
        final info = switch (format) {
          DetectedType.oggOpus => 'OpusInfo',
          DetectedType.oggVorbis => 'Vorbis Info',
          DetectedType.oggFlac => 'Flac Info',
          _ => null,
        };
        for (final header in ['OpusInfo', 'Vorbis Info', 'Flac Info']) {
          expect(
            lines.contains(header),
            header == info,
            reason: '$header section for $format',
          );
        }
        switch (format) {
          case DetectedType.oggOpus:
            expect(s, endsWith(_opus().toString()));
          case DetectedType.oggVorbis:
            expect(s, endsWith(_vorbis().toString()));
          case DetectedType.oggFlac:
            expect(s, endsWith(_flac().toString()));
          case DetectedType.unknown ||
              DetectedType.mp3WithId3 ||
              DetectedType.mp3Stream ||
              DetectedType.wav ||
              DetectedType.m4a ||
              DetectedType.aac ||
              DetectedType.ac3 ||
              DetectedType.eac3:
            expect(s, isNot(contains('Channels:')));
        }
      });
    }

    test('without comments there are no comment lines', () {
      final s = _ogg(comments: {}).toStringWithFormat(DetectedType.unknown);
      expect(s, 'Vendor: Xiph.Org libVorbis I 20200704\nCommentsCount: 0\n');
    });

    test('non-ASCII comments and empty values are written as is', () {
      final s = _ogg(
        comments: {'TITLE': '東京 — ✓', 'EMPTY': '', 'ÄRTIST': 'Ñandú'},
      ).toStringWithFormat(DetectedType.oggVorbis);
      expect(s, contains('\tTITLE: 東京 — ✓\n'));
      expect(s, contains('\tEMPTY: \n'));
      expect(s, contains('\tÄRTIST: Ñandú\n'));
    });
  });

  group('AacMetadata, M4aMetadata, Ac3Metadata, Eac3Metadata', () {
    test('AacMetadata leaves out empty tags but always has stream info', () {
      final aac = AacMetadata(
        sampleRate: 0,
        channels: 0,
        profile: '',
        bitrate: 0,
        frameLength: 0,
      );
      expect(_lines(aac.toString()), [
        'SampleRate: 0',
        'Channels: 0',
        'Profile: ',
        'Bitrate: 0',
        'FrameLength: 0',
      ]);
      aac.title = 'Señal';
      expect(aac.toString(), endsWith('\tTitle: Señal'));
    });

    test('M4aMetadata with nothing set prints nothing', () {
      expect(M4aMetadata().toString(), isEmpty);
      expect(
        M4aMetadata(codec: 'alac', title: 'Ök').toString(),
        '\tCodec: alac\n\tTitle: Ök',
      );
    });

    test('Ac3Metadata and Eac3Metadata list every field', () {
      final ac3 = Ac3Metadata(
        sampleRate: 48000,
        channels: 6,
        bitrate: 448000,
        bsid: 8,
        bsmod: 0,
        acmod: 7,
        lfeOn: true,
        frameSize: 1792,
      );
      expect(_lines(ac3.toString()), hasLength(8));
      expect(ac3.toString(), endsWith('\tFrameSize: 1792'));
      final eac3 = Eac3Metadata(
        sampleRate: 48000,
        channels: 2,
        bitrate: 96000,
        bsid: 16,
        streamType: 0,
        substreamId: 0,
        acmod: 2,
        lfeOn: false,
        frameSize: 384,
        numBlocks: 6,
      );
      expect(_lines(eac3.toString()), hasLength(10));
      expect(eac3.toString(), contains('\tLfeOn: false\n'));
    });
  });

  group('AudioMetadata.toString', () {
    AudioMetadata full(DetectedType type) => AudioMetadata(
      detectedType: type,
      mp3Metadata: Mp3Metadata(
        title: 'Mp3Title',
        artist: '',
        album: '',
        date: '',
        genre: '',
      ),
      oggMetadata: _ogg(),
      aacMetadata: AacMetadata(
        sampleRate: 44100,
        channels: 2,
        profile: 'LC',
        bitrate: 1,
        frameLength: 2,
      ),
      ac3Metadata: Ac3Metadata(
        sampleRate: 48000,
        channels: 6,
        bitrate: 1,
        bsid: 8,
        bsmod: 0,
        acmod: 7,
        lfeOn: true,
        frameSize: 3,
      ),
      eac3Metadata: Eac3Metadata(
        sampleRate: 48000,
        channels: 2,
        bitrate: 1,
        bsid: 16,
        streamType: 0,
        substreamId: 0,
        acmod: 2,
        lfeOn: false,
        frameSize: 4,
        numBlocks: 6,
      ),
      m4aMetadata: M4aMetadata(codec: 'mp4a'),
    );

    /// A line that only the metadata of [type] prints.
    String? marker(DetectedType type) => switch (type) {
      DetectedType.mp3WithId3 || DetectedType.mp3Stream => 'Title: Mp3Title',
      DetectedType.oggOpus ||
      DetectedType.oggVorbis ||
      DetectedType.oggFlac => 'Vendor: Xiph.Org libVorbis I 20200704',
      DetectedType.aac => 'Profile: LC',
      DetectedType.ac3 => 'Bsmod: 0',
      DetectedType.eac3 => 'NumBlocks: 6',
      DetectedType.m4a => 'Codec: mp4a',
      DetectedType.wav || DetectedType.unknown => null,
    };

    for (final type in DetectedType.values) {
      test('$type prints only its own metadata between header and footer', () {
        final s = full(type).toString();
        final lines = _lines(s);
        expect(lines.first, '|---------- $type ----------|');
        expect(lines.last, '|-------------------------------|');
        final own = marker(type);
        for (final other in DetectedType.values) {
          final m = marker(other);
          if (m == null || m == own) continue;
          expect(lines, isNot(contains(m)), reason: '$other metadata leaked');
        }
        if (own != null) expect(lines, contains(own));
        if (type == DetectedType.wav) {
          expect(lines, contains('WAV audio format'));
        }
        if (type == DetectedType.unknown) {
          expect(lines, contains('Unknown audio format'));
        }
      });
    }

    for (final type in [
      DetectedType.wav,
      DetectedType.m4a,
      DetectedType.aac,
      DetectedType.ac3,
      DetectedType.eac3,
      DetectedType.unknown,
      DetectedType.oggOpus,
      DetectedType.oggVorbis,
      DetectedType.oggFlac,
    ]) {
      test('$type without its metadata prints no "null"', () {
        final s = AudioMetadata(detectedType: type).toString();
        expect(s, isNot(contains('null')));
        expect(s, startsWith('|---------- $type ----------|'));
      });
    }

    test('MP3 without its metadata prints no "null"', () {
      for (final type in [DetectedType.mp3WithId3, DetectedType.mp3Stream]) {
        expect(
          AudioMetadata(detectedType: type).toString(),
          isNot(contains('null')),
        );
      }
    });
  });

  group('equality', () {
    test('metadata objects have identity equality', () {
      // None of the classes overrides ==: two objects with the same content
      // are different, and each object equals itself.
      final a = _ogg();
      final b = _ogg();
      expect(a, same(a));
      expect(a == b, isFalse);
      expect(
        Mp3Metadata(title: '', artist: '', album: '', date: '', genre: '') ==
            Mp3Metadata(title: '', artist: '', album: '', date: '', genre: ''),
        isFalse,
      );
    });

    test('equal content gives equal strings', () {
      for (final type in DetectedType.values) {
        expect(
          _ogg().toStringWithFormat(type),
          _ogg().toStringWithFormat(type),
        );
      }
      expect(_flac().toString(), _flac().toString());
      expect(
        AudioMetadata(
          detectedType: DetectedType.oggOpus,
          oggMetadata: _ogg(),
        ).toString(),
        AudioMetadata(
          detectedType: DetectedType.oggOpus,
          oggMetadata: _ogg(),
        ).toString(),
      );
    });

    test('fields are mutable and toString follows them', () {
      final info = _vorbis()..channels = 6;
      expect(info.toString(), contains('\tChannels: 6\n'));
      final ogg = _ogg()..comments['NEW'] = 'ünï';
      expect(
        ogg.toStringWithFormat(DetectedType.unknown),
        contains('\tNEW: ünï\n'),
      );
    });
  });
}

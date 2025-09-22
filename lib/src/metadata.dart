/// Enum representing different types of detected stream audio formats
enum DetectedType {
  /// Unknown audio format
  unknown,

  /// Ogg Opus audio format
  oggOpus,

  /// Ogg Vorbis audio format
  oggVorbis,

  /// FLAC audio format
  oggFlac,

  /// MP3 audio format
  mp3WithId3,

  /// MP3 Stream audio format
  mp3Stream;

  /// Converts an integer value to a [DetectedType] enum value
  static DetectedType fromInt(int value) {
    switch (value) {
      case 0:
        return DetectedType.unknown;
      case 1:
        return DetectedType.oggOpus;
      case 2:
        return DetectedType.oggVorbis;
      case 3:
        return DetectedType.oggFlac;
      case 4:
        return DetectedType.mp3WithId3;
      case 5:
        return DetectedType.mp3Stream;
      default:
        return DetectedType.unknown;
    }
  }

  /// Converts a [DetectedType] enum value to a human-readable string
  @override
  String toString() {
    switch (this) {
      case DetectedType.unknown:
        return 'Unknown';
      case DetectedType.oggOpus:
        return 'Ogg Opus';
      case DetectedType.oggVorbis:
        return 'Ogg Vorbis';
      case DetectedType.oggFlac:
        return 'Ogg FLAC';
      case DetectedType.mp3WithId3:
        return 'MP3 with ID3';
      case DetectedType.mp3Stream:
        return 'MP3 Stream';
    }
  }
}

/// Represents metadata information extracted from an MP3 audio file
final class Mp3Metadata {
  /// Creates a new [Mp3Metadata] instance with the metadata fields
  Mp3Metadata({
    required this.title,
    required this.artist,
    required this.album,
    required this.date,
    required this.genre,
  });

  /// The title of the audio track
  String title;

  /// The artist or performer of the audio track
  String artist;

  /// The album name containing this audio track
  String album;

  /// The release date or year of the audio track
  String date;

  /// The musical genre or style of the audio track
  String genre;

  @override
  String toString() {
    final buffer = StringBuffer()
      ..writeln('Title: $title')
      ..writeln('Artist: $artist')
      ..writeln('Album: $album')
      ..writeln('Date: $date')
      ..write('Genre: $genre');
    return buffer.toString();
  }
}

/// Contains detailed information about a Vorbis audio stream
final class VorbisInfo {
  /// Creates a new [VorbisInfo] instance with the metadata fields
  VorbisInfo({
    required this.version,
    required this.channels,
    required this.rate,
    required this.bitrateUpper,
    required this.bitrateNominal,
    required this.bitrateLower,
    required this.bitrateWindow,
  });

  /// The version of the Vorbis stream format
  int version;

  /// The number of audio channels in the stream
  int channels;

  /// The sampling rate of the audio in Hz
  int rate;

  /// The upper limit of the bitrate for variable bitrate streams.
  /// For fixed bitrate streams, this will be equal to [bitrateNominal]
  /// and [bitrateLower].
  int bitrateUpper;

  /// The target or average bitrate for the stream.
  /// For fixed bitrate streams, this will be equal to [bitrateUpper]
  /// and [bitrateLower].
  int bitrateNominal;

  /// The lower limit of the bitrate for variable bitrate streams.
  /// For fixed bitrate streams, this will be equal to [bitrateUpper]
  /// and [bitrateNominal].
  int bitrateLower;

  /// The size of the window used for bitrate tracking
  int bitrateWindow;

  @override
  String toString() {
    final buffer = StringBuffer()
      ..writeln('\tVersion: $version')
      ..writeln('\tChannels: $channels')
      ..writeln('\tRate: $rate')
      ..writeln('\tUpper: $bitrateUpper')
      ..writeln('\tNominal: $bitrateNominal')
      ..writeln('\tLower: $bitrateLower')
      ..write('\tWindow: $bitrateWindow');
    return buffer.toString();
  }
}

/// Contains detailed information about an Opus audio stream
final class OpusInfo {
  /// Creates a new [OpusInfo] instance with the metadata fields
  OpusInfo({
    required this.version,
    required this.channels,
    required this.preSkip,
    required this.inputSampleRate,
    required this.outputGain,
    required this.mappingFamily,
    required this.streamCount,
    required this.coupledCount,
    required this.channelMapping,
    required this.channelMappingSize,
  });

  /// Version number for the Opus header format
  int version;

  /// Number of channels in the Opus stream
  int channels;

  /// Number of samples to skip at the start of the stream for encoder
  /// delay compensation
  int preSkip;

  /// Original input sampling rate in Hz before encoding
  int inputSampleRate;

  /// Output gain in Q8 format (signed fixed-point value in dB)
  int outputGain;

  /// Mapping family type used for channel configuration
  int mappingFamily;

  /// Number of independent Opus streams
  /// Only used if [mappingFamily] is not 0
  int streamCount;

  /// Number of stereo Opus streams
  /// Only used if [mappingFamily] is not 0
  int coupledCount;

  /// Channel mapping array defining how output channels are ordered
  List<int> channelMapping;

  /// Size of the channel mapping configuration
  int channelMappingSize;

  @override
  String toString() {
    final buffer = StringBuffer()
      ..writeln('\tOpus version: $version')
      ..writeln('\tChannels: $channels')
      ..writeln('\tPreSkip: $preSkip')
      ..writeln('\tInputSampleRate: $inputSampleRate')
      ..writeln('\tOutputGain: $outputGain')
      ..writeln('\tMappingFamily: $mappingFamily')
      ..writeln('\tStreamCount: $streamCount')
      ..writeln('\tCoupledCount: $coupledCount')
      ..writeln('\tChannelMapping: $channelMapping')
      ..write('\tChannelMappingSize: $channelMappingSize');
    return buffer.toString();
  }
}

/// Contains detailed information about an Flac audio stream
final class FlacInfo {
  /// Creates a new [FlacInfo] instance with the metadata fields
  FlacInfo({
    required this.minBlockSize,
    required this.maxBlockSize,
    required this.minFrameSize,
    required this.maxFrameSize,
    required this.sampleRate,
    required this.channels,
    required this.bitsPerSample,
    required this.totalSamples,
  });

  /// The minimum block size in samples
  int minBlockSize;

  /// The maximum block size in samples
  int maxBlockSize;

  /// The minimum frame size in bytes
  int minFrameSize;

  /// The maximum frame size in bytes
  int maxFrameSize;

  /// The sampling rate of the audio in Hz
  int sampleRate;

  /// The number of audio channels in the stream
  int channels;

  /// The bitrate of the audio stream
  int bitsPerSample;

  /// The total number of samples in the audio stream
  int totalSamples;

  @override
  String toString() {
    final buffer = StringBuffer()
      ..writeln('\tMinBlockSize: $minBlockSize')
      ..writeln('\tMaxBlockSize: $maxBlockSize')
      ..writeln('\tMinFrameSize: $minFrameSize')
      ..writeln('\tMaxFrameSize: $maxFrameSize')
      ..writeln('\tSampleRate: $sampleRate')
      ..writeln('\tChannels: $channels')
      ..writeln('\tBitrate: $bitsPerSample')
      ..writeln('\tTotalSamples: $totalSamples');
    return buffer.toString();
  }
}

/// Combines metadata from Ogg container format, including Opus and Vorbis
/// specific information
final class OggMetadata {
  /// Creates a new [OggMetadata] instance with the metadata fields
  OggMetadata({
    required this.vendor,
    required this.commentsCount,
    required this.comments,
    required this.vorbisInfo,
    required this.opusInfo,
    required this.flacInfo,
  });

  /// The vendor string identifying the software used to encode the audio
  String vendor;

  /// Number of metadata comments in the Ogg container
  int commentsCount;

  /// Key-value pairs of metadata comments (e.g., ARTIST, TITLE, etc.)
  Map<String, String> comments;

  /// Detailed information about the Vorbis audio stream configuration
  VorbisInfo vorbisInfo;

  /// Detailed information about the Opus audio stream configuration
  OpusInfo opusInfo;

  /// Detailed information about the Flac audio stream configuration
  FlacInfo flacInfo;

  /// Returns a string representation of the metadata with the specified format
  String toStringWithFormat(DetectedType format) {
    final buffer = StringBuffer()
      ..writeln('Vendor: $vendor')
      ..writeln('CommentsCount: $commentsCount');

    // Write comments in lines
    for (final comment in comments.entries) {
      buffer.writeln('\t${comment.key}: ${comment.value}');
    }
    switch (format) {
      case DetectedType.oggOpus:
        buffer
          ..writeln('OpusInfo')
          ..write(opusInfo);
      case DetectedType.oggVorbis:
        buffer
          ..writeln('Vorbis Info')
          ..write(vorbisInfo);
      case DetectedType.oggFlac:
        buffer
          ..writeln('Flac Info')
          ..write(flacInfo);
      case DetectedType.unknown:
      case DetectedType.mp3WithId3:
      case DetectedType.mp3Stream:
        break;
    }

    return buffer.toString();
  }
}

/// The main container for all audio metadata, supporting various audio formats
final class AudioMetadata {
  /// Creates a new [AudioMetadata] instance with metadata for different
  /// audio formats
  AudioMetadata({
    required this.detectedType,
    this.mp3Metadata,
    this.oggMetadata,
  });

  /// The detected audio format type (e.g., "MP3", "OGG", etc.)
  DetectedType detectedType;

  /// Metadata specific to MP3 audio format
  Mp3Metadata? mp3Metadata;

  /// Metadata specific to OGG container format, including Opus and Vorbis
  /// information
  OggMetadata? oggMetadata;

  /// Returns a string representation of the [AudioMetadata] object
  @override
  String toString() {
    final buffer = StringBuffer()
      ..writeln('|---------- $detectedType ----------|');

    switch (detectedType) {
      case DetectedType.mp3WithId3:
      case DetectedType.mp3Stream:
        buffer.writeln(mp3Metadata.toString());
      case DetectedType.oggOpus:
      case DetectedType.oggVorbis:
      case DetectedType.oggFlac:
        if (oggMetadata != null) {
          buffer.writeln(oggMetadata!.toStringWithFormat(detectedType));
        }
      case DetectedType.unknown:
        buffer.writeln('Unknown audio format');
    }
    buffer.write('|-------------------------------|');
    return buffer.toString();
  }
}

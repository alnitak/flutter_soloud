// ignore_for_file: public_member_api_docs, constant_identifier_names
// ignore_for_file: non_constant_identifier_names, camel_case_types

import 'dart:ffi' as ffi;
import 'package:flutter_soloud/src/metadata.dart';

/// Reflection of metadata_ffi.h

enum NativeDetectedType {
  UNKNOWN(0),
  OGG_OPUS(1),
  OGG_VORBIS(2),
  OGG_FLAC(3),
  MP3_WITH_ID3(4),
  MP3_STREAM(5);

  const NativeDetectedType(this.value);
  final int value;

  static NativeDetectedType fromValue(int value) => switch (value) {
        0 => UNKNOWN,
        1 => OGG_OPUS,
        2 => OGG_VORBIS,
        3 => OGG_FLAC,
        4 => MP3_WITH_ID3,
        5 => MP3_STREAM,
        _ => throw ArgumentError('Unknown value for DetectedTypeFFI: $value'),
      };

  DetectedType toDart() {
    switch (this) {
      case UNKNOWN:
        return DetectedType.unknown;
      case OGG_OPUS:
        return DetectedType.oggOpus;
      case OGG_VORBIS:
        return DetectedType.oggVorbis;
      case OGG_FLAC:
        return DetectedType.oggFlac;
      case MP3_WITH_ID3:
        return DetectedType.mp3WithId3;
      case MP3_STREAM:
        return DetectedType.mp3Stream;
    }
  }
}

final class NativeMp3Metadata extends ffi.Struct {
  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> title;

  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> artist;

  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> album;

  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> date;

  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> genre;
}

/// Comment key-value pair structure
final class NativeCommentPair extends ffi.Struct {
  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> key;

  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> value;
}

/// Structure to hold track metadata
final class NativeVorbisInfo extends ffi.Struct {
  /// Dont' use here the vorbis_info struct because the lib could be not linked
  @ffi.Int()
  external int version;

  @ffi.Int()
  external int channels;

  @ffi.Long()
  external int rate;

  /// The below bitrate declarations are *hints*.
  /// Combinations of the three values carry the following implications:
  ///
  /// all three set to the same value:
  /// implies a fixed rate bitstream
  /// only nominal set:
  /// implies a VBR stream that averages the nominal bitrate.  No hard
  /// upper/lower limit
  /// upper and or lower set:
  /// implies a VBR bitstream that obeys the bitrate limits. nominal
  /// may also be set to give a nominal rate.
  /// none set:
  /// the coder does not care to speculate.
  @ffi.Long()
  external int bitrate_upper;

  @ffi.Long()
  external int bitrate_nominal;

  @ffi.Long()
  external int bitrate_lower;

  @ffi.Long()
  external int bitrate_window;
}

final class NativeOpusInfo extends ffi.Struct {
  @ffi.Uint8()
  external int version;

  @ffi.Uint8()
  external int channels;

  @ffi.Uint16()
  external int pre_skip;

  @ffi.Uint32()
  external int input_sample_rate;

  /// signed, Q8 dB
  @ffi.Int16()
  external int output_gain;

  @ffi.Uint8()
  external int mapping_family;

  /// Optional fields if mapping_family != 0
  @ffi.Uint8()
  external int stream_count;

  @ffi.Uint8()
  external int coupled_count;

  @ffi.Array.multi([8])
  external ffi.Array<ffi.Uint8> channel_mapping;

  @ffi.Int()
  external int channel_mapping_size;
}

final class FlacInfoFFI extends ffi.Struct {
  @ffi.Uint32()
  external int min_blocksize;

  @ffi.Uint32()
  external int max_blocksize;

  @ffi.Uint32()
  external int min_framesize;

  @ffi.Uint32()
  external int max_framesize;

  @ffi.Uint32()
  external int sample_rate;

  @ffi.Uint32()
  external int channels;

  @ffi.Uint32()
  external int bits_per_sample;

  @ffi.Uint32()
  external int total_samples;
}

final class NativeOggMetadata extends ffi.Struct {
  @ffi.Array.multi([1024])
  external ffi.Array<ffi.Char> vendor;

  @ffi.Int()
  external int commentsCount;

  @ffi.Array.multi([32])
  external ffi.Array<NativeCommentPair> comments;

  external NativeVorbisInfo vorbisInfo;

  external NativeOpusInfo opusInfo;

  external FlacInfoFFI flacInfo;
}

final class NativeAudioMetadata extends ffi.Struct {
  @ffi.UnsignedInt()
  external int detectedTypeAsInt;

  NativeDetectedType get detectedTypeFFI =>
      NativeDetectedType.fromValue(detectedTypeAsInt);

  external NativeMp3Metadata mp3Metadata;

  external NativeOggMetadata oggMetadata;

  /// Helper function to convert FFI char array to String
  String _arrayToString(ffi.Array<ffi.Char> array) {
    final buffer = StringBuffer();
    for (var i = 0; i < 1024; i++) {
      // Using 1024 as it's the size we defined for our arrays
      if (array[i] <= 0) break; // Stop at null terminator
      buffer.writeCharCode(array[i]);
    }
    return buffer.toString().trim();
  }

  // Dummy method to reflect the web implementation. Not used with FFI.
  static AudioMetadata fromJSPointer(dynamic metadataPtr) {
    return (metadataPtr as NativeAudioMetadata).toAudioMetadata();
  }

  /// Convert native FFI or Web metadata to the Dart-friendly
  /// [AudioMetadata] class.
  AudioMetadata toAudioMetadata() {
    final mp3Meta = Mp3Metadata(
      title: _arrayToString(mp3Metadata.title),
      artist: _arrayToString(mp3Metadata.artist),
      album: _arrayToString(mp3Metadata.album),
      date: _arrayToString(mp3Metadata.date),
      genre: _arrayToString(mp3Metadata.genre),
    );

    // Process comments from OGG metadata
    final comments = <String, String>{};
    for (var i = 0; i < oggMetadata.commentsCount; i++) {
      final key = _arrayToString(oggMetadata.comments[i].key);
      final value = _arrayToString(oggMetadata.comments[i].value);
      comments[key] = value;
    }

    // Process channel mapping
    final channelMapping = <int>[];
    for (var i = 0; i < 8; i++) {
      channelMapping.add(oggMetadata.opusInfo.channel_mapping[i]);
    }

    final oggMeta = OggMetadata(
      vendor: _arrayToString(oggMetadata.vendor),
      commentsCount: oggMetadata.commentsCount,
      comments: comments,
      vorbisInfo: VorbisInfo(
        version: oggMetadata.vorbisInfo.version,
        channels: oggMetadata.vorbisInfo.channels,
        rate: oggMetadata.vorbisInfo.rate,
        bitrateUpper: oggMetadata.vorbisInfo.bitrate_upper,
        bitrateNominal: oggMetadata.vorbisInfo.bitrate_nominal,
        bitrateLower: oggMetadata.vorbisInfo.bitrate_lower,
        bitrateWindow: oggMetadata.vorbisInfo.bitrate_window,
      ),
      opusInfo: OpusInfo(
        version: oggMetadata.opusInfo.version,
        channels: oggMetadata.opusInfo.channels,
        preSkip: oggMetadata.opusInfo.pre_skip,
        inputSampleRate: oggMetadata.opusInfo.input_sample_rate,
        outputGain: oggMetadata.opusInfo.output_gain,
        mappingFamily: oggMetadata.opusInfo.mapping_family,
        streamCount: oggMetadata.opusInfo.stream_count,
        coupledCount: oggMetadata.opusInfo.coupled_count,
        channelMapping: channelMapping,
        channelMappingSize: oggMetadata.opusInfo.channel_mapping_size,
      ),
      flacInfo: FlacInfo(
        minBlockSize: oggMetadata.flacInfo.min_blocksize,
        maxBlockSize: oggMetadata.flacInfo.max_blocksize,
        minFrameSize: oggMetadata.flacInfo.min_framesize,
        maxFrameSize: oggMetadata.flacInfo.max_framesize,
        sampleRate: oggMetadata.flacInfo.sample_rate,
        channels: oggMetadata.flacInfo.channels,
        bitsPerSample: oggMetadata.flacInfo.bits_per_sample,
        totalSamples: oggMetadata.flacInfo.total_samples,
      ),
    );

    return AudioMetadata(
      detectedType: detectedTypeFFI.toDart(),
      mp3Metadata: mp3Meta,
      oggMetadata: oggMeta,
    );
  }
}

typedef dartOnMetadataCallback_tFunction = ffi.Void Function(
  NativeAudioMetadata metadata,
);
typedef DartdartOnMetadataCallback_tFunction = void Function(
  NativeAudioMetadata metadata,
);

/// callback to tell dart the metadata
typedef dartOnMetadataCallback_t
    = ffi.Pointer<ffi.NativeFunction<dartOnMetadataCallback_tFunction>>;

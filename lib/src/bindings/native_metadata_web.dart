// ignore_for_file: public_member_api_docs

import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/metadata.dart';

/// Reflection of metadata_ffi.h
enum NativeDetectedType {
  unknown(0),
  oggOpus(1),
  oggVorbis(2),
  oggFlac(3),
  mp3WithId3(4),
  mp3Stream(5),
  wav(6),
  m4a(7),
  aac(8),
  ac3(9),
  eac3(10);

  const NativeDetectedType(this.value);
  final int value;

  static NativeDetectedType fromValue(int value) => switch (value) {
    0 => unknown,
    1 => oggOpus,
    2 => oggVorbis,
    3 => oggFlac,
    4 => mp3WithId3,
    5 => mp3Stream,
    6 => wav,
    7 => m4a,
    8 => aac,
    9 => ac3,
    10 => eac3,
    _ => throw ArgumentError(r'Unknown value for DetectedTypeJS: $value'),
  };

  DetectedType toDart() {
    switch (this) {
      case unknown:
        return DetectedType.unknown;
      case oggOpus:
        return DetectedType.oggOpus;
      case oggVorbis:
        return DetectedType.oggVorbis;
      case oggFlac:
        return DetectedType.oggFlac;
      case mp3WithId3:
        return DetectedType.mp3WithId3;
      case mp3Stream:
        return DetectedType.mp3Stream;
      case wav:
        return DetectedType.wav;
      case m4a:
        return DetectedType.m4a;
      case aac:
        return DetectedType.aac;
      case ac3:
        return DetectedType.ac3;
      case eac3:
        return DetectedType.eac3;
    }
  }
}

/// Base class for JS metadata
abstract class _MetadataJS {
  _MetadataJS(this.ptr);
  final int ptr;

  String _readString(int offset, int length) {
    final bytes = <int>[];
    for (var i = 0; i < length; i++) {
      final charCode = wasmGetI32Value(ptr + offset + i, 'i8');
      if (charCode == 0) break;
      bytes.add(charCode);
    }
    return String.fromCharCodes(bytes);
  }
}

/// MP3 metadata from stream
class NativeMp3Metadata extends _MetadataJS {
  NativeMp3Metadata(super.ptr);

  String get title => _readString(0, 1024);
  String get artist => _readString(1024, 1024);
  String get album => _readString(1024 * 2, 1024);
  String get date => _readString(1024 * 3, 1024);
  String get genre => _readString(1024 * 4, 1024);
}

/// Comment key-value pair structure
class NativeCommentPair {
  const NativeCommentPair(this.key, this.value);
  final String key;
  final String value;
}

/// Structure to hold track metadata
class NativeVorbisInfo extends _MetadataJS {
  NativeVorbisInfo(super.ptr);

  int get version => wasmGetI32Value(ptr, 'i32');
  int get channels => wasmGetI32Value(ptr + 4, 'i32');
  int get rate => wasmGetI32Value(ptr + 8, 'i32');
  int get bitrateUpper => wasmGetI32Value(ptr + 12, 'i32');
  int get bitrateNominal => wasmGetI32Value(ptr + 16, 'i32');
  int get bitrateLower => wasmGetI32Value(ptr + 20, 'i32');
  int get bitrateWindow => wasmGetI32Value(ptr + 24, 'i32');
}

/// Ogg/Opus info
class NativeOpusInfo extends _MetadataJS {
  NativeOpusInfo(super.ptr);

  int get version => wasmGetI32Value(ptr, 'i8');
  int get channels => wasmGetI32Value(ptr + 1, 'i8');
  int get preSkip => wasmGetI32Value(ptr + 2, 'i16');
  int get inputSampleRate => wasmGetI32Value(ptr + 4, 'i32');
  int get outputGain => wasmGetI32Value(ptr + 8, 'i16');
  int get mappingFamily => wasmGetI32Value(ptr + 10, 'i8');
  int get streamCount => wasmGetI32Value(ptr + 11, 'i8');
  int get coupledCount => wasmGetI32Value(ptr + 12, 'i8');
  List<int> get channelMapping {
    final mapping = <int>[];
    for (var i = 0; i < 8; i++) {
      mapping.add(wasmGetI32Value(ptr + 13 + i, 'i8'));
    }
    return mapping;
  }

  int get channelMappingSize => wasmGetI32Value(ptr + 24, 'i32');
}

/// Ogg/Flac info
class NativeFlacInfo extends _MetadataJS {
  NativeFlacInfo(super.ptr);

  int get minBlockSize => wasmGetI32Value(ptr, 'i32');
  int get maxBlockSize => wasmGetI32Value(ptr + 4, 'i32');
  int get minFrameSize => wasmGetI32Value(ptr + 8, 'i32');
  int get maxFrameSize => wasmGetI32Value(ptr + 12, 'i32');
  int get sampleRate => wasmGetI32Value(ptr + 16, 'i32');
  int get channels => wasmGetI32Value(ptr + 20, 'i32');
  int get bitsPerSample => wasmGetI32Value(ptr + 24, 'i32');
  int get totalSamples => wasmGetI32Value(ptr + 28, 'i32');
}

/// OGG metadata from stream
class NativeOggMetadata extends _MetadataJS {
  NativeOggMetadata(super.ptr);

  String get vendor => _readString(0, 1024);
  int get commentsCount => wasmGetI32Value(ptr + 1024, 'i32');
  List<NativeCommentPair> get comments {
    final commentsList = <NativeCommentPair>[];
    const commentsOffset = 1028;
    for (var i = 0; i < commentsCount; i++) {
      final commentOffset = commentsOffset + i * 2048;
      final key = _readString(commentOffset, 1024);
      final value = _readString(commentOffset + 1024, 1024);
      commentsList.add(NativeCommentPair(key, value));
    }
    return commentsList;
  }

  NativeVorbisInfo get vorbisInfo => NativeVorbisInfo(ptr + 1028 + 32 * 2048);
  NativeOpusInfo get opusInfo => NativeOpusInfo(ptr + 1028 + 32 * 2048 + 28);
  NativeFlacInfo get flacInfo => NativeFlacInfo(ptr + 1028 + 32 * 2048 + 64);
}

/// AAC metadata from stream
class NativeAacMetadata extends _MetadataJS {
  NativeAacMetadata(super.ptr);

  String get title => _readString(0, 1024);
  String get artist => _readString(1024, 1024);
  String get album => _readString(2048, 1024);
  int get sampleRate => wasmGetI32Value(ptr + 3072, 'i32');
  int get channels => wasmGetI32Value(ptr + 3076, 'i32');
  String get profile => _readString(3080, 32);
  int get bitrate => wasmGetI32Value(ptr + 3112, 'i32');
  int get frameLength => wasmGetI32Value(ptr + 3116, 'i32');
}

/// AC-3 metadata from stream
class NativeAc3Metadata extends _MetadataJS {
  NativeAc3Metadata(super.ptr);

  int get sampleRate => wasmGetI32Value(ptr, 'i32');
  int get channels => wasmGetI32Value(ptr + 4, 'i32');
  int get bitrate => wasmGetI32Value(ptr + 8, 'i32');
  int get bsid => wasmGetI32Value(ptr + 12, 'i32');
  int get bsmod => wasmGetI32Value(ptr + 16, 'i32');
  int get acmod => wasmGetI32Value(ptr + 20, 'i32');
  int get lfeon => wasmGetI32Value(ptr + 24, 'i32');
  int get frameSize => wasmGetI32Value(ptr + 28, 'i32');
}

/// E-AC-3 metadata from stream
class NativeEac3Metadata extends _MetadataJS {
  NativeEac3Metadata(super.ptr);

  int get sampleRate => wasmGetI32Value(ptr, 'i32');
  int get channels => wasmGetI32Value(ptr + 4, 'i32');
  int get bitrate => wasmGetI32Value(ptr + 8, 'i32');
  int get bsid => wasmGetI32Value(ptr + 12, 'i32');
  int get streamType => wasmGetI32Value(ptr + 16, 'i32');
  int get substreamId => wasmGetI32Value(ptr + 20, 'i32');
  int get acmod => wasmGetI32Value(ptr + 24, 'i32');
  int get lfeon => wasmGetI32Value(ptr + 28, 'i32');
  int get frameSize => wasmGetI32Value(ptr + 32, 'i32');
  int get numBlocks => wasmGetI32Value(ptr + 36, 'i32');
}

/// Both MP3 and OGG metadata
class NativeAudioMetadata extends _MetadataJS {
  NativeAudioMetadata(super.ptr);

  NativeDetectedType get detectedType =>
      NativeDetectedType.fromValue(wasmGetI32Value(ptr, 'i32'));

  NativeMp3Metadata get mp3Metadata => NativeMp3Metadata(ptr + 4);

  NativeOggMetadata get oggMetadata => NativeOggMetadata(ptr + 4 + 5 * 1024);

  NativeAacMetadata get aacMetadata => NativeAacMetadata(ptr + 71776);

  NativeAc3Metadata get ac3Metadata => NativeAc3Metadata(ptr + 74896);

  NativeEac3Metadata get eac3Metadata => NativeEac3Metadata(ptr + 74928);

  // Dummy method to reflect the FFI implementation. Not used with Web.
  AudioMetadata toAudioMetadata() {
    return AudioMetadata(detectedType: DetectedType.unknown);
  }

  /// Converts the struct pointed by [metadataPtr] to the Dart-friendly
  /// [AudioMetadata] class.
  static AudioMetadata fromJSPointer(dynamic metadataPtr) {
    final instance = NativeAudioMetadata(metadataPtr as int);
    final mp3Meta = Mp3Metadata(
      title: instance.mp3Metadata.title,
      artist: instance.mp3Metadata.artist,
      album: instance.mp3Metadata.album,
      date: instance.mp3Metadata.date,
      genre: instance.mp3Metadata.genre,
    );

    // Process comments from OGG metadata
    final comments = <String, String>{};
    for (final comment in instance.oggMetadata.comments) {
      comments[comment.key] = comment.value;
    }

    final oggMeta = OggMetadata(
      vendor: instance.oggMetadata.vendor,
      commentsCount: instance.oggMetadata.commentsCount,
      comments: comments,
      vorbisInfo: VorbisInfo(
        version: instance.oggMetadata.vorbisInfo.version,
        channels: instance.oggMetadata.vorbisInfo.channels,
        rate: instance.oggMetadata.vorbisInfo.rate,
        bitrateUpper: instance.oggMetadata.vorbisInfo.bitrateUpper,
        bitrateNominal: instance.oggMetadata.vorbisInfo.bitrateNominal,
        bitrateLower: instance.oggMetadata.vorbisInfo.bitrateLower,
        bitrateWindow: instance.oggMetadata.vorbisInfo.bitrateWindow,
      ),
      opusInfo: OpusInfo(
        version: instance.oggMetadata.opusInfo.version,
        channels: instance.oggMetadata.opusInfo.channels,
        preSkip: instance.oggMetadata.opusInfo.preSkip,
        inputSampleRate: instance.oggMetadata.opusInfo.inputSampleRate,
        outputGain: instance.oggMetadata.opusInfo.outputGain,
        mappingFamily: instance.oggMetadata.opusInfo.mappingFamily,
        streamCount: instance.oggMetadata.opusInfo.streamCount,
        coupledCount: instance.oggMetadata.opusInfo.coupledCount,
        channelMapping: instance.oggMetadata.opusInfo.channelMapping,
        channelMappingSize: instance.oggMetadata.opusInfo.channelMappingSize,
      ),
      flacInfo: FlacInfo(
        minBlockSize: instance.oggMetadata.flacInfo.minBlockSize,
        maxBlockSize: instance.oggMetadata.flacInfo.maxBlockSize,
        minFrameSize: instance.oggMetadata.flacInfo.minFrameSize,
        maxFrameSize: instance.oggMetadata.flacInfo.maxFrameSize,
        sampleRate: instance.oggMetadata.flacInfo.sampleRate,
        channels: instance.oggMetadata.flacInfo.channels,
        bitsPerSample: instance.oggMetadata.flacInfo.bitsPerSample,
        totalSamples: instance.oggMetadata.flacInfo.totalSamples,
      ),
    );

    final aacMeta = AacMetadata(
      title: instance.aacMetadata.title,
      artist: instance.aacMetadata.artist,
      album: instance.aacMetadata.album,
      sampleRate: instance.aacMetadata.sampleRate,
      channels: instance.aacMetadata.channels,
      profile: instance.aacMetadata.profile,
      bitrate: instance.aacMetadata.bitrate,
      frameLength: instance.aacMetadata.frameLength,
    );

    final ac3Meta = Ac3Metadata(
      sampleRate: instance.ac3Metadata.sampleRate,
      channels: instance.ac3Metadata.channels,
      bitrate: instance.ac3Metadata.bitrate,
      bsid: instance.ac3Metadata.bsid,
      bsmod: instance.ac3Metadata.bsmod,
      acmod: instance.ac3Metadata.acmod,
      lfeOn: instance.ac3Metadata.lfeon != 0,
      frameSize: instance.ac3Metadata.frameSize,
    );

    final eac3Meta = Eac3Metadata(
      sampleRate: instance.eac3Metadata.sampleRate,
      channels: instance.eac3Metadata.channels,
      bitrate: instance.eac3Metadata.bitrate,
      bsid: instance.eac3Metadata.bsid,
      streamType: instance.eac3Metadata.streamType,
      substreamId: instance.eac3Metadata.substreamId,
      acmod: instance.eac3Metadata.acmod,
      lfeOn: instance.eac3Metadata.lfeon != 0,
      frameSize: instance.eac3Metadata.frameSize,
      numBlocks: instance.eac3Metadata.numBlocks,
    );

    return AudioMetadata(
      detectedType: instance.detectedType.toDart(),
      mp3Metadata: mp3Meta,
      oggMetadata: oggMeta,
      aacMetadata: aacMeta,
      ac3Metadata: ac3Meta,
      eac3Metadata: eac3Meta,
    );
  }
}

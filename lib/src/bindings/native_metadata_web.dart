// ignore_for_file: public_member_api_docs

import 'dart:js_interop';

import 'package:flutter_soloud/src/bindings/js_extension.dart';
import 'package:flutter_soloud/src/metadata.dart';

/// Mirror of the C enum `DetectedTypeFFI`
enum NativeDetectedType {
  unknown(0),
  oggOpus(1),
  oggVorbis(2),
  mp3WithId3(3),
  mp3Stream(4);

  const NativeDetectedType(this.value);
  final int value;

  static NativeDetectedType fromValue(int value) => switch (value) {
        0 => unknown,
        1 => oggOpus,
        2 => oggVorbis,
        3 => mp3WithId3,
        4 => mp3Stream,
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
      case mp3WithId3:
        return DetectedType.mp3WithId3;
      case mp3Stream:
        return DetectedType.mp3Stream;
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
      final charCode = wasmGetI32Value(ptr + offset + i, 'i8') as int;
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
class NativeCommentPair extends _MetadataJS {
  NativeCommentPair(super.ptr);

  String get key => _readString(0, 1024);
  String get value => _readString(1024, 1024);
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

/// OGG metadata from stream
class NativeOggMetadata extends _MetadataJS {
  NativeOggMetadata(super.ptr);

  String get vendor => _readString(0, 1024);
  int get commentsCount => wasmGetI32Value(ptr + 1024, 'i32');
  List<NativeCommentPair> get comments {
    final commentsList = <NativeCommentPair>[];
    final commentsPtr = ptr + 1028;
    for (var i = 0; i < commentsCount; i++) {
      commentsList.add(NativeCommentPair(commentsPtr + i * 2048));
    }
    return commentsList;
  }

  NativeVorbisInfo get vorbisInfo => NativeVorbisInfo(ptr + 1028 + 32 * 2048);
  NativeOpusInfo get opusInfo => NativeOpusInfo(ptr + 1028 + 32 * 2048 + 28);
}

/// Both MP3 and OGG metadata
class NativeAudioMetadata extends _MetadataJS {
  NativeAudioMetadata(super.ptr);

  NativeDetectedType get detectedType =>
      NativeDetectedType.fromValue(wasmGetI32Value(ptr, 'i32'));

  NativeMp3Metadata get mp3Metadata => NativeMp3Metadata(ptr + 4);

  NativeOggMetadata get oggMetadata => NativeOggMetadata(ptr + 4 + 5 * 1024);

  // Dummy method to reflect the FFI implementation. Not used with Web
  AudioMetadata toAudioMetadata() {
    return AudioMetadata(
      detectedType: DetectedType.unknown,
    );
  }
  
  /// Converts the struct pointed by [metadataPtr] to the Dart-friendly
  /// [AudioMetadata] class
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
    );

    return AudioMetadata(
      detectedType: instance.detectedType.toDart(),
      mp3Metadata: mp3Meta,
      oggMetadata: oggMeta,
    );
  }
}

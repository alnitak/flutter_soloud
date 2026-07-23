// ignore_for_file: public_member_api_docs

import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:meta/meta.dart';

/// Internal helper that owns the mixer output capture stream lifecycle.
///
/// This class is used by both SoLoud (main isolate) and SoLoudIsolate
/// (worker isolates) so the capture logic lives in a single place. It is
/// parameterized with the [FlutterSoLoud] backend to use and a readiness
/// predicate that decides whether the engine is in a state where capture can
/// start.
@internal
class MixerOutputStreamManager {
  MixerOutputStreamManager({required this.bindings, required this.isReady});

  /// The platform-specific bindings used to start/stop capture and read the
  /// circular buffer.
  final FlutterSoLoud bindings;

  /// Called before starting capture. Should return `true` when the native
  /// engine is initialized and ready to capture.
  final bool Function() isReady;

  /// Subscription that forwards copied mixer output chunks to the broadcast
  /// stream.
  StreamSubscription<Uint8List>? _subscription;

  /// The broadcast stream of captured mixer output data.
  StreamController<Uint8List>? _streamController;

  /// Captures the master mixer output as a [Stream] of [Uint8List] chunks.
  ///
  /// The signature mirrors SoLoud.startMixerOutputStream. See the public
  /// documentation there for parameter details.
  Stream<Uint8List> start({
    MixerOutputFormat format = MixerOutputFormat.pcmF32le,
    int sampleRate = -1,
    int channels = -1,
    int bufferSizeBytes = 1024 * 1024,
    int notificationThresholdBytes = 4096,
    int chunkPCMFrames = -1,
  }) {
    if (!isReady()) {
      throw const SoLoudNotInitializedException();
    }

    if (format.isPcm) {
      assert(
        chunkPCMFrames == -1 || chunkPCMFrames >= 2048,
        'chunkPCMFrames must be at least 2048 when provided',
      );
    } else {
      assert(
        chunkPCMFrames == -1,
        'chunkPCMFrames is only supported for PCM formats',
      );
    }

    if (_streamController != null) {
      return _streamController!.stream;
    }

    // Use a synchronous controller so that the tail data flushed in
    // [_flushRemaining] during [stop] is delivered to listeners before the
    // stream is closed. With an async broadcast controller the flush event can
    // be scheduled after the stream has already been closed and canceled,
    // which causes compressed formats (especially FLAC) to lose their final
    // header chunk and report zero captured bytes.
    _streamController = StreamController<Uint8List>.broadcast(
      sync: true,
    );

    // Ensure the mixer output callback is registered in this isolate so that
    // [bindings.mixerOutputChunkEvents] receives the copied chunks. On FFI
    // this creates a native callable for the current isolate; on web it is a
    // no-op because the worker plumbing is already shared. Calling this before
    // [startMixerOutputCapture] is safe and idempotent.
    bindings.registerMixerOutputCallback();

    final error = bindings.startMixerOutputCapture(
      format,
      sampleRate,
      channels,
      bufferSizeBytes,
      notificationThresholdBytes,
      chunkPCMFrames,
    );

    if (error != PlayerErrors.noError) {
      _streamController!.close();
      _streamController = null;
      throw SoLoudCppException.fromPlayerError(error);
    }

    _subscription = bindings.mixerOutputChunkEvents.listen(_emitChunk);

    return _streamController!.stream;
  }

  void _emitChunk(Uint8List chunk) {
    final controller = _streamController;
    if (controller == null || controller.isClosed) {
      return;
    }

    if (chunk.isEmpty) {
      return;
    }

    controller.add(chunk);
  }

  /// Stops the mixer output capture stream and releases associated resources.
  void stop() {
    _subscription?.cancel();
    _subscription = null;
    bindings.stopMixerOutputCapture();

    // Flush any captured data that did not reach the notification threshold.
    // The native side keeps the buffer alive after stopping so we can copy the
    // tail synchronously and avoid losing compressed-format headers.
    _flushRemaining();

    _streamController?.close();
    _streamController = null;
  }

  void _flushRemaining() {
    final controller = _streamController;
    if (controller == null || controller.isClosed) {
      return;
    }

    final available = bindings.getMixerOutputAvailableBytes();
    if (available <= 0) {
      return;
    }

    final bufferSize = bindings.getMixerOutputBufferSize();
    final readOffset = bindings.getMixerOutputReadOffset();
    final firstLength = available < bufferSize - readOffset
        ? available
        : bufferSize - readOffset;

    if (firstLength > 0) {
      final chunk = bindings.copyMixerOutputBuffer(readOffset, firstLength);
      if (chunk.isNotEmpty) {
        controller.add(chunk);
      }
    }

    if (available > firstLength) {
      final secondLength = available - firstLength;
      final chunk = bindings.copyMixerOutputBuffer(0, secondLength);
      if (chunk.isNotEmpty) {
        controller.add(chunk);
      }
    }
  }

  /// Whether mixer output capture is currently active.
  bool get isRunning => bindings.isMixerOutputCaptureRunning();

  /// Returns the current 44-byte WAV header for the active mixer output
  /// capture, or an empty [Uint8List] if unavailable.
  Uint8List getWavHeader() {
    if (!isReady()) {
      throw const SoLoudNotInitializedException();
    }
    return bindings.getMixerOutputWavHeader();
  }
}

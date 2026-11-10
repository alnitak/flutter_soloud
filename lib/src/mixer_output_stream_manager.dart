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

  /// The controller delivering an event right now, if any: a listener that
  /// stops the stream runs inside that delivery.
  StreamController<Uint8List>? _dispatchingController;

  /// Whether this manager has a stream open, even one whose capture has
  /// stopped.
  bool get hasStream => _streamController != null;

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
      if (bindings.isCurrentMixerOutputCapture()) {
        return _streamController!.stream;
      }
      // The capture behind this stream was stopped without this manager, by
      // an engine deinitialization in another isolate, say: end that stream
      // first.
      stop();
    }

    // The controller must be synchronous: [stop] flushes the captured tail
    // into the stream and callers typically cancel their subscription right
    // after [stop] returns. With the default asynchronous delivery those
    // pending events would be discarded by the cancellation.
    _streamController = StreamController<Uint8List>.broadcast(sync: true);

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
    if (controller != null) {
      _emitTo(controller, chunk);
    }
  }

  void _emitTo(StreamController<Uint8List> controller, Uint8List chunk) {
    if (controller.isClosed || chunk.isEmpty) {
      return;
    }
    final previous = _dispatchingController;
    _dispatchingController = controller;
    try {
      controller.add(chunk);
    } finally {
      _dispatchingController = previous;
    }
  }

  /// Stops the mixer output capture stream and releases associated resources.
  void stop() {
    // Detached before anything reaches a listener, so a start from a
    // listener gets a stream of its own, which never sees this one's end.
    final controller = _streamController;
    _streamController = null;
    _subscription?.cancel();
    _subscription = null;
    // Only one capture runs at a time: a manager without one of its own (a
    // worker isolate stopping defensively, say) must not end another's.
    bindings.stopMixerOutputCapture(onlyOwned: true);
    if (controller == null) {
      // A manager without a stream leaves what the bindings collected for
      // the one that has.
      return;
    }

    // What the capture still owed this stream when it stopped, copied by the
    // bindings before another isolate could restart it: the chunks whose
    // notifications had not been handled yet, then the tail that never
    // reached the notification threshold or filled a chunk (for compressed
    // formats, the end of the stream).
    final stopped = bindings.takeStoppedMixerOutput();
    void finish() {
      for (final chunk in stopped) {
        _emitTo(controller, chunk);
      }
      controller.close();
    }

    if (identical(_dispatchingController, controller)) {
      // A listener is stopping its own stream: the controller can neither
      // add nor close while it delivers that listener's event. Finish once
      // the delivery is over.
      scheduleMicrotask(finish);
    } else {
      finish();
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

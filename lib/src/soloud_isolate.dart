import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/mixer_output_stream_manager.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/utils/loader.dart';
import 'package:meta/meta.dart';

/// Isolate-safe access to the SoLoud engine for tasks that do not require the
/// main isolate's state.
///
/// The SoLoud engine is a singleton in C++ land and can be initialized only
/// from the main isolate via [SoLoud]. Call `SoLoud.instance.init` in the
/// main isolate before using this class from another isolate. Once initialized,
/// operations can be safely performed from another isolate because they do not
/// touch the main isolate's [SoLoudLoader], [AudioSource] registry, filters, or
/// playback-event streams.
///
/// [SoLoudIsolate] exposes only those operations. It does not expose
/// `init()`/`deinit()`, loading, playback, filters, or the
/// `voiceEnded`/`fileLoaded`/`stateChanged` event streams. Those must remain on
/// the isolate that initialized the engine.
///
/// The primary use case is running the mixer output capture stream in a worker
/// isolate so the main isolate stays free for UI and other tasks.
///
/// Example:
/// ```dart
/// final isolateCapture = SoLoudIsolate.instance.startMixerOutputStream(
///   format: MixerOutputFormat.pcmS16le,
///   chunkPCMFrames: 2048,
/// );
/// isolateCapture.listen((chunk) { /* process audio bytes */ });
/// ```
///
/// See also the `example/lib/mixer_capture/isolate_capture_test.dart` for
/// a complete example of running mixer output capture in a separate isolate.
@experimental
class SoLoudIsolate {
  SoLoudIsolate._();

  /// The singleton instance of [SoLoudIsolate].
  static final SoLoudIsolate instance = SoLoudIsolate._();

  final _controller = SoLoudController();

  /// The low-level platform bindings. Advanced users can use this to call
  /// methods that are safe to invoke from a non-main isolate.
  FlutterSoLoud get bindings => _controller.soLoudFFI;

  /// Whether the native engine is initialized.
  ///
  /// This is `true` when [SoLoud] has been initialized in the main isolate via
  /// `SoLoud.instance.init` and the engine is still active. It intentionally
  /// does not depend on the main isolate's loader or callback state.
  bool get isInitialized => bindings.isInited();

  late final _mixerOutputStreamManager = MixerOutputStreamManager(
    bindings: bindings,
    isReady: () => isInitialized,
  );

  /// Captures the master mixer output as a [Stream] of [Uint8List] chunks.
  ///
  /// This method has the same behavior as [SoLoud.startMixerOutputStream] but
  /// can be called from a non-main isolate. The engine must have been
  /// initialized in the main isolate before calling this method.
  ///
  /// See [SoLoud.startMixerOutputStream] for parameter details.
  @experimental
  Stream<Uint8List> startMixerOutputStream({
    MixerOutputFormat format = MixerOutputFormat.pcmF32le,
    int sampleRate = -1,
    int channels = -1,
    int bufferSizeBytes = 1024 * 1024,
    int notificationThresholdBytes = 4096,
    int chunkPCMFrames = -1,
  }) => _mixerOutputStreamManager.start(
    format: format,
    sampleRate: sampleRate,
    channels: channels,
    bufferSizeBytes: bufferSizeBytes,
    notificationThresholdBytes: notificationThresholdBytes,
    chunkPCMFrames: chunkPCMFrames,
  );

  /// Stops the mixer output capture stream and releases associated resources.
  @experimental
  void stopMixerOutputStream() => _mixerOutputStreamManager.stop();

  /// Whether mixer output capture is currently active.
  @experimental
  bool get isMixerOutputStreamRunning => _mixerOutputStreamManager.isRunning;

  /// Returns the current 44-byte WAV header for the active mixer output
  /// capture.
  ///
  /// This is only meaningful when the capture format is
  /// [MixerOutputFormat.wav]. See [SoLoud.getMixerOutputWavHeader] for details.
  @experimental
  Uint8List getMixerOutputWavHeader() {
    return _mixerOutputStreamManager.getWavHeader();
  }

  ///////////////////////////////////////////////////
  /// Read samples from file and memory.
  ///////////////////////////////////////////////////

  /// This method has the same behavior as [SoLoud.readSamplesFromFile] (refer
  /// to it for a complete documentation) and can be safely called from a
  /// non-main isolate. Is not mandatory that the engine is initialized in
  /// the main isolate to use this method.
  ///
  /// See also [readSamplesFromMem].
  Future<Float32List> readSamplesFromFile(
    String completeFileName,
    int numSamplesNeeded, {
    double startTime = 0,
    double endTime = -1,
    bool average = false,
  }) async {
    assert(
      endTime == -1 || endTime > startTime,
      '[endTime] must be greater than [startTime].',
    );
    assert(startTime >= 0, '[startTime] must be greater than or equal to 0.');
    return bindings.readSamplesFromFile(
      completeFileName,
      numSamplesNeeded,
      startTime: startTime,
      endTime: endTime,
      average: average,
    );
  }

  /// This method has the same behavior as [SoLoud.readSamplesFromMem] (refer
  /// to it for a complete documentation) and can be safely called from a
  /// non-main isolate. Is not mandatory that the engine is initialized in
  /// the main isolate to use this method.
  ///
  /// See also [readSamplesFromFile].
  Future<Float32List> readSamplesFromMem(
    Uint8List buffer,
    int numSamplesNeeded, {
    double startTime = 0,
    double endTime = -1,
    bool average = false,
  }) async {
    assert(
      endTime == -1 || endTime > startTime,
      '[endTime] must be greater than [startTime].',
    );
    assert(startTime >= 0, '[startTime] must be greater than or equal to 0.');
    final samples = bindings.readSamplesFromMem(
      buffer,
      numSamplesNeeded,
      startTime: startTime,
      endTime: endTime,
      average: average,
    );

    return samples;
  }
}

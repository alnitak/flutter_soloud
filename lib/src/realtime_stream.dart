import 'dart:async';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:meta/meta.dart';

const bool _kIsWeb = bool.fromEnvironment('dart.library.js_interop');

/// Signature for a real-time 32-bit float PCM generator callback used by
/// [RealtimeStreamFeeder], [RealtimeStreamHandle.startFloat32Feeder].
///
/// When `useIsolate: true` (the default on native platforms), this must be a
/// **top-level or static function** so it can be sent to the worker isolate.
///
/// Write up to [framesRequested] multi-channel frames (`framesRequested *
/// channels`, normalized in `-1.0..1.0`) into [outBuffer] and return the
/// actual number of frames written (`0..framesRequested`).
///
/// - If your generator is paused or temporarily has no audio data ready, return
///   `0`. The feeder will stop requesting chunks for the current tick, and if
///   the C++ ring buffer runs dry, the mixer automatically applies a 64-frame
///   raised-cosine anti-click decay ramp and outputs silence until future ticks
///   return frames again.
/// - If your generator writes fewer than [framesRequested] frames, those frames
///   are pushed immediately and the feeder yields until the next tick.
/// - Use [emitToUi] to send lightweight telemetry or step notifications back to
///   the UI thread ([RealtimeStreamFeeder.events] / `onUiEvent`).
@experimental
typedef RealtimeFloat32FeedCallback<TState> =
    int Function(
      Float32List outBuffer,
      int framesRequested,
      TState state,
      void Function(Object? event) emitToUi,
    );

/// Signature for a real-time 16-bit signed integer PCM generator callback used
/// by [RealtimeStreamFeeder] and [RealtimeStreamHandle.startInt16Feeder].
///
/// When `useIsolate: true` (the default on native platforms), this must be a
/// **top-level or static function** so it can be sent to the worker isolate.
///
/// Write up to [framesRequested] interleaved multi-channel 16-bit signed
/// integer frames (`framesRequested * channels`, in the range
/// `-32768..32767`) into [outBuffer] and return the actual number of frames
/// written (`0..framesRequested`).
///
/// On the C++ side, pushed `int16` frames are converted to normalized planar
/// 32-bit floats (`* (1.0f / 32768.0f)`) and de-interleaved on the fly using
/// hardware SIMD (`NEON` on ARM64/ARMv7, `SSE2` on x86_64).
///
/// - Returning `0` yields for the current tick; if the C++ ring buffer drains,
///   the mixer smoothly applies a 64-frame raised-cosine anti-click decay ramp
///   to silence.
/// - Returning `< framesRequested` pushes those available frames and yields
///   until the next tick.
/// - Use [emitToUi] to send lightweight telemetry or step notifications back to
///   the UI thread ([RealtimeStreamFeeder.events] / `onUiEvent`).
@experimental
typedef RealtimeInt16FeedCallback<TState> =
    int Function(
      Int16List outBuffer,
      int framesRequested,
      TState state,
      void Function(Object? event) emitToUi,
    );

/// Optional callback invoked inside the feeder whenever
/// [RealtimeStreamFeeder.updateState] is called from the UI thread.
///
/// Useful when [TState] holds both UI-controlled parameters (e.g. BPM, step
/// grid) and internal DSP synthesis state (e.g. oscillator phases, envelopes)
/// that should be preserved across parameter updates.
///
/// If omitted, [RealtimeStreamFeeder.updateState] simply replaces the active
/// `TState` reference with `newState`.
@experimental
typedef RealtimeStateUpdateCallback<TState> =
    void Function(TState currentState, TState newState);

/// Optional callback invoked inside the feeder whenever
/// [RealtimeStreamFeeder.sendMessage] is called from the UI thread.
@experimental
typedef RealtimeMessageCallback<TState> =
    void Function(TState currentState, Object? message);

/// A pre-allocated native/WASM memory buffer for zero-GC real-time audio
/// pushes into a [RealtimeStreamHandle].
///
/// Write audio samples directly into [float32View] or [int16View] on every
/// synthesis or DSP tick, then pass this buffer to
/// [RealtimeStreamHandle.pushNativeFloat32],
/// [RealtimeStreamHandle.pushNativeInt16],
/// [RealtimeStreamHandle.pushNativeClockedFloat32], or
/// [RealtimeStreamHandle.pushNativeScheduledFloat32].
///
/// Always call [dispose] when the buffer is no longer needed to release the
/// underlying native/WASM allocation.
@experimental
final class RealtimeNativeBuffer {
  /// Allocates a native/WASM buffer capable of holding [maxFrames] multi-channel
  /// frames of 32-bit float (or 16-bit integer) PCM audio.
  @experimental
  factory RealtimeNativeBuffer({
    required int maxFrames,
    required Channels channels,
  }) {
    if (maxFrames <= 0) {
      throw ArgumentError.value(maxFrames, 'maxFrames', 'Must be > 0');
    }
    final totalSamples = maxFrames * channels.count;
    final byteLength = totalSamples * Float32List.bytesPerElement;
    final address = SoLoudController().soLoudFFI.allocateRealtimeNativeBuffer(
      byteLength,
    );
    return RealtimeNativeBuffer._(
      address: address,
      maxFrames: maxFrames,
      channels: channels,
      totalSamples: totalSamples,
    );
  }

  RealtimeNativeBuffer._({
    required int address,
    required this.maxFrames,
    required this.channels,
    required int totalSamples,
  }) : _address = address,
       _totalSamples = totalSamples;

  int _address;
  final int _totalSamples;
  Float32List? _cachedFloat32View;
  Int16List? _cachedInt16View;

  /// Maximum number of multi-channel frames this buffer can hold.
  final int maxFrames;

  /// Channel configuration of this buffer.
  final Channels channels;

  /// Native pointer or WASM heap byte offset of the underlying allocation.
  int get address => _address;

  /// Whether [dispose] has been called on this buffer.
  bool get isDisposed => _address == 0;

  /// Returns a mutable [Float32List] view backed directly by the native/WASM
  /// memory allocation (`maxFrames * channels.count` elements).
  ///
  /// Note: On Web, if WASM memory grows between ticks, re-querying this getter
  /// returns a fresh view over the updated `HEAPF32` buffer.
  Float32List get float32View {
    if (isDisposed) {
      throw StateError('RealtimeNativeBuffer has already been disposed.');
    }
    final cached = _cachedFloat32View;
    if (cached != null) {
      if (!_kIsWeb || cached.buffer.lengthInBytes > 0) {
        return cached;
      }
    }
    return _cachedFloat32View = SoLoudController().soLoudFFI
        .viewRealtimeFloat32Buffer(_address, _totalSamples);
  }

  /// Returns a mutable [Int16List] view backed directly by the native/WASM
  /// memory allocation (`maxFrames * channels.count` elements).
  Int16List get int16View {
    if (isDisposed) {
      throw StateError('RealtimeNativeBuffer has already been disposed.');
    }
    final cached = _cachedInt16View;
    if (cached != null) {
      if (!_kIsWeb || cached.buffer.lengthInBytes > 0) {
        return cached;
      }
    }
    return _cachedInt16View = SoLoudController().soLoudFFI
        .viewRealtimeInt16Buffer(_address, _totalSamples);
  }

  /// Frees the underlying native/WASM memory allocation. Idempotent.
  void dispose() {
    if (_address != 0) {
      SoLoudController().soLoudFFI.freeRealtimeNativeBuffer(_address);
      _address = 0;
      _cachedFloat32View = null;
      _cachedInt16View = null;
    }
  }
}

/// High-throughput, lock-free real-time PCM stream handle with support for
/// immediate, clocked, and scheduled playing-window mixer injection.
///
/// Created on the main isolate via `SoLoud.instance.setRealtimeStream(...)`, or
/// reconstructed inside a worker isolate via `SoLoudIsolate` or
/// [RealtimeStreamHandle.fromHash].
///
/// Unlike `setBufferStream` (which is designed for media/network streaming),
/// [RealtimeStreamHandle] uses a wait-free SPSC planar ring buffer with zero
/// mutex contention on the audio thread, and can splice newly pushed samples
/// directly into the currently playing mixer window when `renderAheadFrames`
/// is enabled in `SoLoud.instance.init(...)`.
@experimental
final class RealtimeStreamHandle {
  /// Creates a [RealtimeStreamHandle] wrapping an [AudioSource].
  @experimental
  RealtimeStreamHandle({
    required this.source,
    required this.sampleRate,
    required this.channels,
    required this.format,
    required this.bufferFrames,
    required this.historyFrames,
    this.activeHandle,
  }) : soundHash = source.soundHash;

  /// Reconstructs a lightweight [RealtimeStreamHandle] from a [soundHash]
  /// (useful when feeding the stream from a background Dart Isolate).
  @experimental
  RealtimeStreamHandle.fromHash({
    required this.soundHash,
    required this.sampleRate,
    required this.channels,
    this.format = BufferType.f32le,
    this.bufferFrames = 2048,
    this.historyFrames = 4096,
    this.activeHandle,
  }) : source = AudioSource(soundHash);

  /// The underlying [AudioSource] that can be passed to `SoLoud.instance.play`,
  /// `play3d`, `playClocked`, or `playScheduled`, or routed to a mixing `Bus`.
  final AudioSource source;

  /// The unique [SoundHash] of this real-time stream.
  final SoundHash soundHash;

  /// Sample rate of the stream in Hz.
  final int sampleRate;

  /// Channel count of the stream.
  final Channels channels;

  /// Input PCM format (`BufferType.f32le`, `BufferType.s16le`, or
  /// `BufferType.f32lePlanar`).
  final BufferType format;

  /// Forward ring buffer capacity in frames (rounded up to a power of two in
  /// native code).
  final int bufferFrames;

  /// Historical rewind window in frames retained for retroactive mixer
  /// re-mixing.
  final int historyFrames;

  /// The currently bound active voice handle used for instantaneous
  /// playing-window mixer injection, if any.
  SoundHandle? activeHandle;

  RealtimeStreamFeeder<dynamic>? _activeFeeder;

  /// Binds the active voice [handle] returned by
  /// `SoLoud.instance.play(source)`.
  // ignore: use_setters_to_change_properties
  void bindActiveVoice(SoundHandle? handle) {
    activeHandle = handle;
    _activeFeeder?.bindActiveVoice(handle);
  }

  /// Starts a managed real-time 32-bit float PCM feeder for this stream.
  ///
  /// On native platforms (`!kIsWeb`), when [useIsolate] is `true` (default),
  /// this automatically spawns a dedicated background Dart [Isolate], attaches
  /// to the lock-free C++ ring buffer, pre-allocates a zero-GC
  /// [RealtimeNativeBuffer], and runs a watermark refill loop calling [onFeed].
  ///
  /// On the Web platform (where `Isolate.spawn` is not available) or when
  /// [useIsolate] is `false`, this automatically runs the exact same [onFeed]
  /// refill loop on the current isolate's event loop using a pre-allocated
  /// WASM/native [RealtimeNativeBuffer].
  ///
  /// Parameters:
  /// - [initialState]: Initial state/configuration object passed to [onFeed].
  ///   When [useIsolate] is `true`, [initialState] is copied once into the
  ///   worker isolate at startup.
  /// - [onFeed]: Top-level or static function that fills up to
  ///   `framesRequested` frames (`framesRequested * channels.count` floats,
  ///   interleaved or planar according to [format]) into `outBuffer` and
  ///   returns the number of frames actually written (`0..framesRequested`).
  ///   Returning `0` (e.g. when paused or waiting for data) yields until the
  ///   next tick, and the C++ mixer smoothly outputs cosine-ramped silence if
  ///   the ring buffer drains.
  /// - [onStateUpdate]: Optional top-level or static function called when
  ///   [RealtimeStreamFeeder.updateState] is invoked, allowing you to copy
  ///   UI-controlled fields into the live `TState` without resetting internal
  ///   DSP fields (such as oscillator phases, step counters, or envelopes).
  ///   If omitted, `updateState(newState)` replaces `state` with `newState`.
  /// - [onMessage]: Optional top-level or static function called when
  ///   [RealtimeStreamFeeder.sendMessage] is invoked from the UI isolate.
  /// - [onUiEvent]: Optional listener invoked on the UI isolate whenever
  ///   [onFeed] calls `emitToUi(event)` (also exposed via
  ///   [RealtimeStreamFeeder.events]).
  /// - [chunkFrames]: Number of multi-channel frames requested per [onFeed]
  ///   invocation (default `480`, i.e. 10 ms at 48 kHz). Must be `> 0` and
  ///   `<= bufferFrames`.
  /// - [targetBufferedFrames]: Low-watermark target fill level in frames
  ///   (default `chunkFrames * 3`, i.e. 30 ms at 48 kHz). Whenever
  ///   [availableToRead] drops below this threshold, the feeder invokes
  ///   [onFeed] in `chunkFrames` increments until the buffer reaches
  ///   [targetBufferedFrames] or [onFeed] returns `< chunkFrames`.
  /// - [tickInterval]: Refill check interval (default 5 ms). Should be shorter
  ///   than the duration of `chunkFrames` so the watermark is checked before
  ///   the ring buffer drains.
  /// - [useIsolate]: Whether to run the feeder inside a dedicated background
  ///   Dart [Isolate] on native platforms (default `true`). Automatically
  ///   ignored on Web (`false`).
  Future<RealtimeStreamFeeder<TState>> startFloat32Feeder<TState>({
    required TState initialState,
    required RealtimeFloat32FeedCallback<TState> onFeed,
    RealtimeStateUpdateCallback<TState>? onStateUpdate,
    RealtimeMessageCallback<TState>? onMessage,
    void Function(Object? event)? onUiEvent,
    int chunkFrames = 480,
    int? targetBufferedFrames,
    Duration tickInterval = const Duration(milliseconds: 5),
    bool useIsolate = true,
  }) async {
    await _activeFeeder?.dispose();
    final feeder = await RealtimeStreamFeeder._start<TState>(
      stream: this,
      initialState: initialState,
      onFeedFloat32: onFeed,
      onStateUpdate: onStateUpdate,
      onMessage: onMessage,
      onUiEvent: onUiEvent,
      chunkFrames: chunkFrames,
      targetBufferedFrames: targetBufferedFrames ?? (chunkFrames * 3),
      tickInterval: tickInterval,
      useIsolate: useIsolate && !_kIsWeb,
      isInt16: false,
    );
    _activeFeeder = feeder;
    return feeder;
  }

  /// Starts a managed real-time 16-bit signed integer PCM feeder for this
  /// stream.
  ///
  /// On native platforms (`!kIsWeb`), when [useIsolate] is `true` (default),
  /// this automatically spawns a dedicated background Dart [Isolate], attaches
  /// to the lock-free C++ ring buffer, pre-allocates a zero-GC
  /// [RealtimeNativeBuffer], and runs a watermark refill loop calling [onFeed]
  /// with a mutable [Int16List] view. On the C++ side, pushed 16-bit integer
  /// samples (`-32768..32767`) are converted to normalized 32-bit floats and
  /// de-interleaved into planar channels on the fly using SIMD (`NEON`/`SSE2`).
  ///
  /// On the Web platform (where `Isolate.spawn` is not available) or when
  /// [useIsolate] is `false`, this automatically runs the exact same [onFeed]
  /// refill loop on the current isolate's event loop using a pre-allocated
  /// WASM/native [RealtimeNativeBuffer].
  ///
  /// Parameters:
  /// - [initialState]: Initial state/configuration object passed to [onFeed].
  ///   When [useIsolate] is `true`, [initialState] is copied once into the
  ///   worker isolate at startup.
  /// - [onFeed]: Top-level or static function that fills up to
  ///   `framesRequested` interleaved multi-channel 16-bit signed integer frames
  ///   (`framesRequested * channels.count` elements in `-32768..32767`) into
  ///   `outBuffer` and returns the number of frames actually written
  ///   (`0..framesRequested`). Returning `0` (e.g. when paused or waiting for
  ///   data) yields until the next tick, and the C++ mixer smoothly outputs
  ///   cosine-ramped silence if the ring buffer drains.
  /// - [onStateUpdate]: Optional top-level or static function called when
  ///   [RealtimeStreamFeeder.updateState] is invoked, allowing you to copy
  ///   UI-controlled fields into the live `TState` without resetting internal
  ///   DSP fields (such as oscillator phases, step counters, or envelopes).
  ///   If omitted, `updateState(newState)` replaces `state` with `newState`.
  /// - [onMessage]: Optional top-level or static function called when
  ///   [RealtimeStreamFeeder.sendMessage] is invoked from the UI isolate.
  /// - [onUiEvent]: Optional listener invoked on the UI isolate whenever
  ///   [onFeed] calls `emitToUi(event)` (also exposed via
  ///   [RealtimeStreamFeeder.events]).
  /// - [chunkFrames]: Number of multi-channel frames requested per [onFeed]
  ///   invocation (default `480`, i.e. 10 ms at 48 kHz). Must be `> 0` and
  ///   `<= bufferFrames`.
  /// - [targetBufferedFrames]: Low-watermark target fill level in frames
  ///   (default `chunkFrames * 3`, i.e. 30 ms at 48 kHz). Whenever
  ///   [availableToRead] drops below this threshold, the feeder invokes
  ///   [onFeed] in `chunkFrames` increments until the buffer reaches
  ///   [targetBufferedFrames] or [onFeed] returns `< chunkFrames`.
  /// - [tickInterval]: Refill check interval (default 5 ms). Should be shorter
  ///   than the duration of `chunkFrames` so the watermark is checked before
  ///   the ring buffer drains.
  /// - [useIsolate]: Whether to run the feeder inside a dedicated background
  ///   Dart [Isolate] on native platforms (default `true`). Automatically
  ///   ignored on Web (`false`).
  Future<RealtimeStreamFeeder<TState>> startInt16Feeder<TState>({
    required TState initialState,
    required RealtimeInt16FeedCallback<TState> onFeed,
    RealtimeStateUpdateCallback<TState>? onStateUpdate,
    RealtimeMessageCallback<TState>? onMessage,
    void Function(Object? event)? onUiEvent,
    int chunkFrames = 480,
    int? targetBufferedFrames,
    Duration tickInterval = const Duration(milliseconds: 5),
    bool useIsolate = true,
  }) async {
    await _activeFeeder?.dispose();
    final feeder = await RealtimeStreamFeeder._start<TState>(
      stream: this,
      initialState: initialState,
      onFeedInt16: onFeed,
      onStateUpdate: onStateUpdate,
      onMessage: onMessage,
      onUiEvent: onUiEvent,
      chunkFrames: chunkFrames,
      targetBufferedFrames: targetBufferedFrames ?? (chunkFrames * 3),
      tickInterval: tickInterval,
      useIsolate: useIsolate && !_kIsWeb,
      isInt16: true,
    );
    _activeFeeder = feeder;
    return feeder;
  }

  /// Number of unread frames currently queued in the lock-free ring buffer.
  int get availableToRead =>
      SoLoudController().soLoudFFI.getRealtimeAvailableToRead(soundHash.hash);

  /// Number of free frames currently available to write without dropping data.
  int get availableToWrite =>
      SoLoudController().soLoudFFI.getRealtimeAvailableToWrite(soundHash.hash);

  /// Total number of ring-buffer underrun transitions observed since stream
  /// creation or the last [reset] call.
  ///
  /// ### What is an underrun?
  /// On every hardware audio callback, SoLoud's mixer requests a block of audio
  /// frames from this stream's lock-free ring buffer. If the ring buffer holds
  /// fewer unread frames than the mixer requested (`availableToRead <
  /// requestedFrames`), the buffer has "run dry" and the stream enters the
  /// **underrun (starved)** state:
  /// 1. [underrunCount] is incremented by `1` on the transition from active
  ///    playback into the starved state.
  /// 2. To prevent an audible speaker click or pop from a sudden step change,
  ///    the C++ mixer applies a 64-frame (~1.3 ms at 48 kHz) raised-cosine
  ///    decay ramp from the last output sample smoothly down to `0.0`, and
  ///    fills the remainder of the callback block with silence (`0.0`).
  /// 3. Unlike `setBufferStream`, the voice never auto-terminates or pauses on
  ///    underrun; while the stream remains idle/empty, [underrunCount] does
  ///    **not** keep incrementing on subsequent silent callbacks.
  /// 4. As soon as new frames are pushed again, the stream exits the underrun
  ///    state and (when `renderAheadFrames > 0`) splices the new audio
  ///    instantaneously into the currently playing mixer window.
  ///
  /// ### Interpreting [underrunCount]
  /// - **At startup (`1`)**: Calling `SoLoud.instance.play(stream.source)`
  ///   before pushing initial audio frames causes the first mixer callback to
  ///   observe an empty buffer (`underrunCount == 1`), which is normal.
  /// - **When stopping a generator (`+1`)**: Intentionally stopping sample
  ///   production (e.g. pausing a sequencer or finishing a one-shot note) lets
  ///   the remaining buffered frames play out to `0`, incrementing
  ///   [underrunCount] once.
  /// - **During continuous playback**: [underrunCount] should remain constant.
  ///   If it increases while audio is actively being fed, the producer is not
  ///   pushing frames fast enough (increase `targetBufferedFrames` or
  ///   `bufferFrames`, or reduce `tickInterval`).
  int get underrunCount =>
      SoLoudController().soLoudFFI.getRealtimeUnderrunCount(soundHash.hash);

  /// Clears all unread frames in the ring buffer and resets the underrun count.
  void reset() {
    final error = SoLoudController().soLoudFFI.resetRealtimeStream(soundHash);
    if (error != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Allocates a persistent [RealtimeNativeBuffer] sized for [maxFrames] and
  /// this stream's [channels].
  RealtimeNativeBuffer allocateNativeBuffer({int maxFrames = 1024}) =>
      RealtimeNativeBuffer(maxFrames: maxFrames, channels: channels);

  /// Pushes 32-bit float PCM [samples] (interleaved, or planar if [format] is
  /// [BufferType.f32lePlanar]) into the real-time stream immediately.
  ///
  /// If [voiceHandle] or [activeHandle] is set and the stream was previously
  /// starving while `renderAheadFrames > 0` is enabled, the pushed samples are
  /// spliced instantaneously into the currently playing mixer window at the
  /// current device playhead.
  ///
  /// Returns the number of frames actually written (`0..frameCount`).
  int pushFloat32(
    Float32List samples, {
    int? frameCount,
    SoundHandle? voiceHandle,
  }) {
    final frames = frameCount ?? (samples.length ~/ channels.count);
    if (frames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeFloat32(
      soundHash.hash,
      handleId,
      samples,
      frames,
    );
  }

  /// Pushes 16-bit signed integer PCM [samples] (interleaved) into the
  /// real-time stream immediately, converting and de-interleaving into planar
  /// floats on the fly using SIMD.
  ///
  /// Returns the number of frames actually written (`0..frameCount`).
  int pushInt16(
    Int16List samples, {
    int? frameCount,
    SoundHandle? voiceHandle,
  }) {
    final frames = frameCount ?? (samples.length ~/ channels.count);
    if (frames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeInt16(
      soundHash.hash,
      handleId,
      samples,
      frames,
    );
  }

  /// Pushes 32-bit float PCM [samples] anchored to app/physics clock
  /// [soundTime] (analogous to `playClocked`).
  ///
  /// Rapidly pushed chunks within the same frame or across frames preserve
  /// their sub-buffer timing offsets and are spliced into the currently playing
  /// mixer window when `renderAheadFrames > 0` is enabled.
  ///
  /// Returns the number of frames actually written (`0..frameCount`).
  int pushClockedFloat32(
    Duration soundTime,
    Float32List samples, {
    int? frameCount,
    SoundHandle? voiceHandle,
  }) {
    final frames = frameCount ?? (samples.length ~/ channels.count);
    if (frames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeClockedFloat32(
      soundHash.hash,
      handleId,
      soundTime.toDouble(),
      samples,
      frames,
    );
  }

  /// Pushes 32-bit float PCM [samples] scheduled at absolute engine time
  /// [atTime] (see `SoLoud.instance.getEngineTime()` / `getPlayheadTime()`,
  /// analogous to `playScheduled`).
  ///
  /// If [atTime] falls inside the currently playing render-ahead window, the
  /// mixer rewinds to [atTime] and splices the samples with sample accuracy.
  ///
  /// Returns the number of frames actually written (`0..frameCount`).
  int pushScheduledFloat32(
    Duration atTime,
    Float32List samples, {
    int? frameCount,
    SoundHandle? voiceHandle,
  }) {
    final frames = frameCount ?? (samples.length ~/ channels.count);
    if (frames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeScheduledFloat32(
      soundHash.hash,
      handleId,
      atTime.toDouble(),
      samples,
      frames,
    );
  }

  /// Zero-copy push of [frameCount] float32 frames from a pre-allocated
  /// [RealtimeNativeBuffer].
  int pushNativeFloat32(
    RealtimeNativeBuffer buffer,
    int frameCount, {
    SoundHandle? voiceHandle,
  }) {
    if (buffer.isDisposed) {
      throw StateError('RealtimeNativeBuffer has already been disposed.');
    }
    final clampedFrames = frameCount > buffer.maxFrames
        ? buffer.maxFrames
        : frameCount;
    if (clampedFrames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeFloat32Address(
      soundHash.hash,
      handleId,
      buffer.address,
      clampedFrames,
    );
  }

  /// Zero-copy push of [frameCount] int16 frames from a pre-allocated
  /// [RealtimeNativeBuffer].
  int pushNativeInt16(
    RealtimeNativeBuffer buffer,
    int frameCount, {
    SoundHandle? voiceHandle,
  }) {
    if (buffer.isDisposed) {
      throw StateError('RealtimeNativeBuffer has already been disposed.');
    }
    final clampedFrames = frameCount > buffer.maxFrames
        ? buffer.maxFrames
        : frameCount;
    if (clampedFrames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeInt16Address(
      soundHash.hash,
      handleId,
      buffer.address,
      clampedFrames,
    );
  }

  /// Zero-copy clocked push of [frameCount] float32 frames from a
  /// pre-allocated [RealtimeNativeBuffer].
  int pushNativeClockedFloat32(
    Duration soundTime,
    RealtimeNativeBuffer buffer,
    int frameCount, {
    SoundHandle? voiceHandle,
  }) {
    if (buffer.isDisposed) {
      throw StateError('RealtimeNativeBuffer has already been disposed.');
    }
    final clampedFrames = frameCount > buffer.maxFrames
        ? buffer.maxFrames
        : frameCount;
    if (clampedFrames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeClockedFloat32Address(
      soundHash.hash,
      handleId,
      soundTime.toDouble(),
      buffer.address,
      clampedFrames,
    );
  }

  /// Zero-copy scheduled push of [frameCount] float32 frames from a
  /// pre-allocated [RealtimeNativeBuffer] at absolute engine time [atTime].
  int pushNativeScheduledFloat32(
    Duration atTime,
    RealtimeNativeBuffer buffer,
    int frameCount, {
    SoundHandle? voiceHandle,
  }) {
    if (buffer.isDisposed) {
      throw StateError('RealtimeNativeBuffer has already been disposed.');
    }
    final clampedFrames = frameCount > buffer.maxFrames
        ? buffer.maxFrames
        : frameCount;
    if (clampedFrames <= 0) return 0;
    final handleId = (voiceHandle ?? activeHandle)?.id ?? 0;
    return SoLoudController().soLoudFFI.pushRealtimeScheduledFloat32Address(
      soundHash.hash,
      handleId,
      atTime.toDouble(),
      buffer.address,
      clampedFrames,
    );
  }
}

/// Manages automated PCM feeding for a [RealtimeStreamHandle], either inside a
/// dedicated background Dart [Isolate] (default on native platforms) or on the
/// current isolate's event loop (automatic fallback on Web).
///
/// Created via [RealtimeStreamHandle.startFloat32Feeder] or
/// [RealtimeStreamHandle.startInt16Feeder].
///
/// This helper eliminates all manual `Isolate.spawn`, `ReceivePort`/`SendPort`,
/// `SoLoudIsolate.attachRealtimeStream`, [RealtimeNativeBuffer] lifecycle, and
/// watermark polling boilerplate while keeping the exact same code working on
/// both Native and Web.
@experimental
final class RealtimeStreamFeeder<TState> {
  RealtimeStreamFeeder._({
    required this.stream,
    required this.isUsingIsolate,
    required Stream<Object?> events,
    required void Function(TState newState) onUpdateState,
    required void Function(Object? message) onSendMessage,
    required void Function(SoundHandle? handle) onBindVoice,
    required Future<void> Function() onDispose,
  }) : _events = events,
       _onUpdateState = onUpdateState,
       _onSendMessage = onSendMessage,
       _onBindVoice = onBindVoice,
       _onDispose = onDispose;

  /// The [RealtimeStreamHandle] being fed by this feeder.
  final RealtimeStreamHandle stream;

  /// Whether this feeder is running inside a separate background Dart [Isolate]
  /// (`true` on native platforms by default; `false` on Web or when
  /// `useIsolate: false`).
  final bool isUsingIsolate;

  final Stream<Object?> _events;
  final void Function(TState newState) _onUpdateState;
  final void Function(Object? message) _onSendMessage;
  final void Function(SoundHandle? handle) _onBindVoice;
  final Future<void> Function() _onDispose;

  bool _isDisposed = false;

  /// Whether [dispose] has been called on this feeder.
  bool get isDisposed => _isDisposed;

  /// Broadcast stream of events emitted by the generator callback via
  /// `emitToUi(event)`.
  Stream<Object?> get events => _events;

  /// Sends an updated [newState] to the feeder.
  ///
  /// If `onStateUpdate` was provided to
  /// [RealtimeStreamHandle.startFloat32Feeder], `onStateUpdate(currentState,
  /// newState)` is invoked inside the feeder so internal DSP fields on
  /// `currentState` are preserved. Otherwise, the feeder's state reference is
  /// replaced with [newState].
  void updateState(TState newState) {
    if (_isDisposed) return;
    _onUpdateState(newState);
  }

  /// Sends a lightweight custom [message] to the feeder's `onMessage` callback.
  void sendMessage(Object? message) {
    if (_isDisposed) return;
    _onSendMessage(message);
  }

  /// Updates the bound active voice [handle] inside the feeder.
  // ignore: use_setters_to_change_properties
  void bindActiveVoice(SoundHandle? handle) {
    if (_isDisposed) return;
    _onBindVoice(handle);
  }

  /// Stops the feeder, releases its pre-allocated [RealtimeNativeBuffer], and
  /// terminates the background [Isolate] (if any). Idempotent.
  Future<void> dispose() async {
    if (_isDisposed) return;
    _isDisposed = true;
    if (identical(stream._activeFeeder, this)) {
      stream._activeFeeder = null;
    }
    await _onDispose();
  }

  static Future<RealtimeStreamFeeder<TState>> _start<TState>({
    required RealtimeStreamHandle stream,
    required TState initialState,
    required int chunkFrames,
    required int targetBufferedFrames,
    required Duration tickInterval,
    required bool useIsolate,
    required bool isInt16,
    RealtimeFloat32FeedCallback<TState>? onFeedFloat32,
    RealtimeInt16FeedCallback<TState>? onFeedInt16,
    RealtimeStateUpdateCallback<TState>? onStateUpdate,
    RealtimeMessageCallback<TState>? onMessage,
    void Function(Object? event)? onUiEvent,
  }) async {
    if (chunkFrames <= 0) {
      throw ArgumentError.value(chunkFrames, 'chunkFrames', 'Must be > 0');
    }
    if (targetBufferedFrames < chunkFrames) {
      throw ArgumentError.value(
        targetBufferedFrames,
        'targetBufferedFrames',
        'Must be >= chunkFrames ($chunkFrames)',
      );
    }

    final eventController = StreamController<Object?>.broadcast();
    void dispatchUiEvent(Object? event) {
      onUiEvent?.call(event);
      if (!eventController.isClosed) {
        eventController.add(event);
      }
    }

    if (useIsolate) {
      final uiReceivePort = ReceivePort();
      final handshakeCompleter = Completer<SendPort>();
      final disposedAckCompleter = Completer<void>();

      final portSub = uiReceivePort.listen((Object? message) {
        if (message is _FeederHandshakeMsg) {
          if (!handshakeCompleter.isCompleted) {
            handshakeCompleter.complete(message.workerSendPort);
          }
        } else if (message is _FeederDisposedAckMsg) {
          if (!disposedAckCompleter.isCompleted) {
            disposedAckCompleter.complete();
          }
        } else {
          dispatchUiEvent(message);
        }
      });

      final initPayload = _FeederIsolateInit<TState>(
        uiSendPort: uiReceivePort.sendPort,
        soundHash: stream.soundHash,
        sampleRate: stream.sampleRate,
        channels: stream.channels,
        format: stream.format,
        bufferFrames: stream.bufferFrames,
        historyFrames: stream.historyFrames,
        activeHandle: stream.activeHandle,
        initialState: initialState,
        chunkFrames: chunkFrames,
        targetBufferedFrames: targetBufferedFrames,
        tickInterval: tickInterval,
        isInt16: isInt16,
        onFeedFloat32: onFeedFloat32,
        onFeedInt16: onFeedInt16,
        onStateUpdate: onStateUpdate,
        onMessage: onMessage,
      );

      final isolate = await Isolate.spawn<_FeederIsolateInit<dynamic>>(
        _realtimeFeederIsolateMain,
        initPayload,
        debugName: 'SoLoudRealtimeFeeder(${stream.soundHash.hash})',
      );

      final workerSendPort = await handshakeCompleter.future;

      return RealtimeStreamFeeder<TState>._(
        stream: stream,
        isUsingIsolate: true,
        events: eventController.stream,
        onUpdateState: (TState newState) {
          workerSendPort.send(_FeederStateUpdateMsg<TState>(newState));
        },
        onSendMessage: (Object? message) {
          workerSendPort.send(_FeederCustomMsg(message));
        },
        onBindVoice: (SoundHandle? handle) {
          workerSendPort.send(_FeederBindVoiceMsg(handle));
        },
        onDispose: () async {
          workerSendPort.send(const _FeederDisposeMsg());
          await disposedAckCompleter.future.timeout(
            const Duration(milliseconds: 100),
            onTimeout: () {},
          );
          isolate.kill(priority: Isolate.immediate);
          await portSub.cancel();
          uiReceivePort.close();
          await eventController.close();
        },
      );
    }

    // In-process feeder (used automatically on Web or when useIsolate: false).
    var state = initialState;
    final nativeBuf = stream.allocateNativeBuffer(maxFrames: chunkFrames);

    final timer = Timer.periodic(
      tickInterval,
      (_) => _refillBuffer<TState>(
        stream: stream,
        nativeBuf: nativeBuf,
        targetBufferedFrames: targetBufferedFrames,
        chunkFrames: chunkFrames,
        isInt16: isInt16,
        onFeedFloat32: onFeedFloat32,
        onFeedInt16: onFeedInt16,
        state: state,
        emitEvent: dispatchUiEvent,
      ),
    );

    return RealtimeStreamFeeder<TState>._(
      stream: stream,
      isUsingIsolate: false,
      events: eventController.stream,
      onUpdateState: (TState newState) {
        if (onStateUpdate != null) {
          onStateUpdate(state, newState);
        } else {
          state = newState;
        }
        _refillBuffer<TState>(
          stream: stream,
          nativeBuf: nativeBuf,
          targetBufferedFrames: targetBufferedFrames,
          chunkFrames: chunkFrames,
          isInt16: isInt16,
          onFeedFloat32: onFeedFloat32,
          onFeedInt16: onFeedInt16,
          state: state,
          emitEvent: dispatchUiEvent,
        );
      },
      onSendMessage: (Object? message) {
        onMessage?.call(state, message);
        _refillBuffer<TState>(
          stream: stream,
          nativeBuf: nativeBuf,
          targetBufferedFrames: targetBufferedFrames,
          chunkFrames: chunkFrames,
          isInt16: isInt16,
          onFeedFloat32: onFeedFloat32,
          onFeedInt16: onFeedInt16,
          state: state,
          emitEvent: dispatchUiEvent,
        );
      },
      onBindVoice: (SoundHandle? handle) {
        stream.activeHandle = handle;
      },
      onDispose: () async {
        timer.cancel();
        nativeBuf.dispose();
        await eventController.close();
      },
    );
  }

  static void _refillBuffer<TState>({
    required RealtimeStreamHandle stream,
    required RealtimeNativeBuffer nativeBuf,
    required int targetBufferedFrames,
    required int chunkFrames,
    required bool isInt16,
    required RealtimeFloat32FeedCallback<TState>? onFeedFloat32,
    required RealtimeInt16FeedCallback<TState>? onFeedInt16,
    required TState state,
    required void Function(Object? event) emitEvent,
  }) {
    if (nativeBuf.isDisposed) return;
    while (stream.availableToRead < targetBufferedFrames &&
        stream.availableToWrite >= chunkFrames) {
      final int written;
      if (isInt16) {
        written = onFeedInt16!(
          nativeBuf.int16View,
          chunkFrames,
          state,
          emitEvent,
        );
      } else {
        written = onFeedFloat32!(
          nativeBuf.float32View,
          chunkFrames,
          state,
          emitEvent,
        );
      }
      if (written <= 0) {
        break;
      }
      final clamped = written > chunkFrames ? chunkFrames : written;
      if (isInt16) {
        stream.pushNativeInt16(nativeBuf, clamped);
      } else {
        stream.pushNativeFloat32(nativeBuf, clamped);
      }
      if (clamped < chunkFrames) {
        break;
      }
    }
  }
}

final class _FeederHandshakeMsg {
  const _FeederHandshakeMsg(this.workerSendPort);
  final SendPort workerSendPort;
}

final class _FeederDisposeMsg {
  const _FeederDisposeMsg();
}

final class _FeederDisposedAckMsg {
  const _FeederDisposedAckMsg();
}

final class _FeederBindVoiceMsg {
  const _FeederBindVoiceMsg(this.handle);
  final SoundHandle? handle;
}

final class _FeederStateUpdateMsg<TState> {
  const _FeederStateUpdateMsg(this.newState);
  final TState newState;
}

final class _FeederCustomMsg {
  const _FeederCustomMsg(this.message);
  final Object? message;
}

final class _FeederIsolateInit<TState> {
  const _FeederIsolateInit({
    required this.uiSendPort,
    required this.soundHash,
    required this.sampleRate,
    required this.channels,
    required this.format,
    required this.bufferFrames,
    required this.historyFrames,
    required this.activeHandle,
    required this.initialState,
    required this.chunkFrames,
    required this.targetBufferedFrames,
    required this.tickInterval,
    required this.isInt16,
    this.onFeedFloat32,
    this.onFeedInt16,
    this.onStateUpdate,
    this.onMessage,
  });

  final SendPort uiSendPort;
  final SoundHash soundHash;
  final int sampleRate;
  final Channels channels;
  final BufferType format;
  final int bufferFrames;
  final int historyFrames;
  final SoundHandle? activeHandle;
  final TState initialState;
  final int chunkFrames;
  final int targetBufferedFrames;
  final Duration tickInterval;
  final bool isInt16;
  final RealtimeFloat32FeedCallback<TState>? onFeedFloat32;
  final RealtimeInt16FeedCallback<TState>? onFeedInt16;
  final RealtimeStateUpdateCallback<TState>? onStateUpdate;
  final RealtimeMessageCallback<TState>? onMessage;

  void run() {
    final stream = RealtimeStreamHandle.fromHash(
      soundHash: soundHash,
      sampleRate: sampleRate,
      channels: channels,
      format: format,
      bufferFrames: bufferFrames,
      historyFrames: historyFrames,
      activeHandle: activeHandle,
    );
    final nativeBuf = RealtimeNativeBuffer(
      maxFrames: chunkFrames,
      channels: channels,
    );
    var state = initialState;

    final rx = ReceivePort();
    uiSendPort.send(_FeederHandshakeMsg(rx.sendPort));

    Timer? timer;
    rx.listen((Object? msg) {
      if (msg is _FeederDisposeMsg) {
        timer?.cancel();
        nativeBuf.dispose();
        rx.close();
        uiSendPort.send(const _FeederDisposedAckMsg());
        return;
      }
      if (msg is _FeederBindVoiceMsg) {
        stream.bindActiveVoice(msg.handle);
        return;
      }
      if (msg is _FeederStateUpdateMsg<TState>) {
        if (onStateUpdate != null) {
          onStateUpdate!(state, msg.newState);
        } else {
          state = msg.newState;
        }
        RealtimeStreamFeeder._refillBuffer<TState>(
          stream: stream,
          nativeBuf: nativeBuf,
          targetBufferedFrames: targetBufferedFrames,
          chunkFrames: chunkFrames,
          isInt16: isInt16,
          onFeedFloat32: onFeedFloat32,
          onFeedInt16: onFeedInt16,
          state: state,
          emitEvent: uiSendPort.send,
        );
        return;
      }
      if (msg is _FeederCustomMsg) {
        onMessage?.call(state, msg.message);
        RealtimeStreamFeeder._refillBuffer<TState>(
          stream: stream,
          nativeBuf: nativeBuf,
          targetBufferedFrames: targetBufferedFrames,
          chunkFrames: chunkFrames,
          isInt16: isInt16,
          onFeedFloat32: onFeedFloat32,
          onFeedInt16: onFeedInt16,
          state: state,
          emitEvent: uiSendPort.send,
        );
      }
    });

    timer = Timer.periodic(
      tickInterval,
      (_) => RealtimeStreamFeeder._refillBuffer<TState>(
        stream: stream,
        nativeBuf: nativeBuf,
        targetBufferedFrames: targetBufferedFrames,
        chunkFrames: chunkFrames,
        isInt16: isInt16,
        onFeedFloat32: onFeedFloat32,
        onFeedInt16: onFeedInt16,
        state: state,
        emitEvent: uiSendPort.send,
      ),
    );
  }
}

void _realtimeFeederIsolateMain(_FeederIsolateInit<dynamic> init) {
  init.run();
}

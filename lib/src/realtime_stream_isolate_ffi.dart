import 'dart:async';
import 'dart:isolate';

import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/realtime_stream.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:meta/meta.dart';

/// Spawns a background [Isolate] to drive the real-time PCM refill loop for
/// [stream] and returns control callbacks for the UI isolate.
@internal
Future<
  ({
    void Function(TState newState) onUpdateState,
    void Function(Object? message) onSendMessage,
    void Function(SoundHandle? handle) onBindVoice,
    Future<void> Function() onDispose,
  })
>
startRealtimeFeederIsolate<TState>({
  required RealtimeStreamHandle stream,
  required TState initialState,
  required int chunkFrames,
  required int targetBufferedFrames,
  required Duration tickInterval,
  required bool isInt16,
  required void Function(Object? event) dispatchUiEvent,
  RealtimeFloat32FeedCallback<TState>? onFeedFloat32,
  RealtimeInt16FeedCallback<TState>? onFeedInt16,
  RealtimeStateUpdateCallback<TState>? onStateUpdate,
  RealtimeMessageCallback<TState>? onMessage,
}) async {
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

  return (
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
    },
  );
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
        refillRealtimeStreamBuffer<TState>(
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
        refillRealtimeStreamBuffer<TState>(
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
      (_) => refillRealtimeStreamBuffer<TState>(
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

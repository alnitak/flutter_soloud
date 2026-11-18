import 'dart:async';

import 'package:flutter_soloud/src/realtime_stream.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:meta/meta.dart';

/// Web stub for spawning a background feeder isolate.
///
/// Isolates are not supported on Web; callers fall back to running the feeder
/// loop on the main event loop.
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
}) {
  throw UnsupportedError(
    'Isolate feeder is not supported on the Web platform.',
  );
}

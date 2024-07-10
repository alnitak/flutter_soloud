// ignore_for_file: public_member_api_docs

import 'dart:async';
import 'dart:convert' show jsonEncode;
import 'dart:js_interop';

import 'package:meta/meta.dart';
import 'package:web/web.dart' as web;

@internal
class WorkerController {
  web.Worker? _worker;
  StreamController<dynamic>? _outputController;

  /// Spawn a new web Worker with the given JS source (not used now).
  static Future<WorkerController> spawn(String path) async {
    final controller = WorkerController()
      .._outputController = StreamController()
      .._worker = web.Worker(path.endsWith('.dart') ? '$path.js' : path);

    controller._worker?.onmessage = ((web.MessageEvent event) {
      controller._outputController?.add(event.data.dartify());
    }).toJS;

    return controller;
  }

  /// Set the worker created in WASM.
  /// This is used to get events sent from the native audio thread.
  void setWasmWorker(web.Worker wasmWorker) {
    _outputController = StreamController();
    _worker = wasmWorker;
    _worker?.onmessage = ((web.MessageEvent event) {
      _outputController?.add(event.data.dartify());
    }).toJS;
  }

  /// Not used with `Module.wasmWorker`.
  void sendMessage(dynamic message) {
    switch (message) {
      case Map():
        final mapEncoded = jsonEncode(message);
        _worker?.postMessage(mapEncoded.jsify());
      case num():
        _worker?.postMessage(message.toJS);
      case String():
        _worker?.postMessage(message.toJS);
      default:
        try {
          final messageJsifyed = (message as Object).jsify();
          _worker?.postMessage(messageJsifyed);
        } catch (e) {
          throw UnsupportedError(
            'sendMessage(): Type ${message.runtimeType} unsupported',
          );
        }
    }
  }

  /// The receiver Stream.
  Stream<dynamic> onReceive() {
    return _outputController!.stream;
  }

  /// Kill the Worker.
  void kill() {
    _worker?.terminate();
  }
}

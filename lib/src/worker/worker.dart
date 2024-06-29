// ignore_for_file: public_member_api_docs

import 'dart:async';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:js_util';
import 'dart:convert' show jsonEncode;

import 'package:meta/meta.dart';
import 'package:web/web.dart' as web;

// Masked type: ServiceWorkerGlobalScope
@JS('self')
external JSAny get globalScopeSelf;

@JS('self.importScript')
external JSAny _importScript(String path);

void jsSendMessage(JSAny m) {
  globalContext.callMethod('postMessage'.toJS, m);
}

Stream<T> callbackToStream<J, T>(
  dynamic object,
  String name,
  T Function(J jsValue) unwrapValue,
) {
  final controller = StreamController<T>.broadcast(sync: true);
  setProperty(object as Object, name, allowInterop((J event) {
    controller.add(unwrapValue(event));
  }));
  return controller.stream;
}

@internal
class Worker {
  late StreamController<dynamic> _outputController;

  Worker() {
    _outputController = StreamController();
    callbackToStream(globalScopeSelf, 'onmessage', (web.MessageEvent e) {
      _outputController.add(getProperty<dynamic>(e, 'data'));
    });
  }

  Stream<dynamic> onReceive() => _outputController.stream;

  void sendMessage(JSAny message) {
    jsSendMessage(message);
  }
}

@internal
class WorkerController {
  web.Worker? _worker;
  StreamController<dynamic>? _outputController;

  /// Spawn a new web Worker with the given JS source (not used now).
  static Future<WorkerController> spawn(String path) async {
    final controller = WorkerController();
    controller._outputController = StreamController();
    path = (path.endsWith('.dart') ? '$path.js' : path);
    controller._worker = web.Worker(path);

    controller._worker?.onmessage = (((web.MessageEvent event) {
      controller._outputController?.add(event.data.dartify());
    })).toJS;

    return controller;
  }

  /// Set the worker created in WASM.
  /// This is used to get events sent from the native audio thread.
  void setWasmWorker(web.Worker wasmWorker) {
    _outputController = StreamController();
    _worker = wasmWorker;
    _worker?.onmessage = (((web.MessageEvent event) {
      _outputController?.add(event.data.dartify());
    })).toJS;
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
          throw UnsupportedError(
              'sendMessage(): Type ${message.runtimeType} unsupported');
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


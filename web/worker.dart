import 'dart:async';
import 'dart:convert' show jsonDecode;
import 'dart:js_interop';
import 'dart:js_interop_unsafe' as js_unsafe;
// ignore: avoid_web_libraries_in_flutter
/// This lib is only used on the web and the linter warn about this.
/// Usually we do a condition import to resolve this problem, but this
/// source is only used on web. Therefore it's safe to ignore.
import 'dart:js_util' as js_util;

import 'package:web/web.dart' as web;

// Masked type: ServiceWorkerGlobalScope
@JS('self')
external JSAny get globalScopeSelf;

@JS('self.importScript')
// ignore: unused_element
external JSAny _importScript(String path);

void jsSendMessage(dynamic m) {
  globalContext.callMethod('postMessage'.toJS, (m as Object).jsify());
}

Stream<T> callbackToStream<J, T>(
  dynamic object,
  String name,
  T Function(J jsValue) unwrapValue,
) {
  final controller = StreamController<T>.broadcast(sync: true);
  js_util.setProperty(
    object as Object,
    name,
    js_util.allowInterop((J event) {
      controller.add(unwrapValue(event));
    }),
  );
  return controller.stream;
}

class Worker {
  Worker() {
    _outputController = StreamController();
    callbackToStream(globalScopeSelf, 'onmessage', (web.MessageEvent e) {
      _outputController.add(js_util.getProperty<dynamic>(e as Object, 'data'));
    });
  }
  late StreamController<dynamic> _outputController;

  Stream<dynamic> onReceive() => _outputController.stream;

  void sendMessage(dynamic message) {
    jsSendMessage(message);
  }
}

/// The main Web Worker
void main() async {
  // print('Worker created.\n');
  final worker = Worker();
  worker.onReceive().listen((data) {
    // print('Dart worker: '
    //     'onMessage received $data with type of ${data.runtimeType}\n');

    if (data is String) {
      try {
        final parseMap = jsonDecode(data) as Map;
        // ignore: avoid_print
        print('Received $data  PARSED TO $parseMap\n');
        if (parseMap['message'] == 'voiceEndedCallback') {
          worker.sendMessage(data);
        }
      } catch (e) {
        // ignore: avoid_print
        print("Received data from WASM worker but it's not a String!\n");
      }
    }
  });
}

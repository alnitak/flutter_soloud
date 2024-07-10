import 'dart:async';
import 'dart:convert' show jsonDecode;
import 'dart:js_interop';
import 'dart:js_interop_unsafe';

import 'package:web/web.dart' as web;

// Masked type: ServiceWorkerGlobalScope
@JS('self')
external JSObject get globalScopeSelf;

@JS('self.importScript')
// ignore: unused_element
external JSAny _importScript(String path);

void jsSendMessage(dynamic m) {
  globalContext.callMethod('postMessage'.toJS, (m as Object).jsify());
}

Stream<T> callbackToStream<J, T>(
  JSObject object,
  String name,
  T Function(J jsValue) unwrapValue,
) {
  final controller = StreamController<T>.broadcast(sync: true);

  void eventFunction(JSAny event) {
    controller.add(unwrapValue(event as J));
  }

  object.setProperty(
    name.toJS,
    eventFunction.toJS,
  );
  return controller.stream;
}

class Worker {
  Worker() {
    _outputController = StreamController();
    callbackToStream(globalScopeSelf, 'onmessage', (web.MessageEvent e) {
      final data = (e as JSObject).getProperty('data'.toJS);
      _outputController.add(data);
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

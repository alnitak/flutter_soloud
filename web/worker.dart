// ignore_for_file: avoid_print

import 'dart:convert' show jsonDecode;
import 'dart:js_interop';

import 'package:flutter_soloud/src/worker/worker.dart';

void main() {
  // print('Worker created.\n');
  final worker = Worker();
  worker.onReceive().listen((data) {
    print('Dart worker: '
        'onMessage received $data with type of ${data.runtimeType}\n');

    if (data is String) {
      try {
        final parseMap = jsonDecode(data);
        if ((parseMap as Map)['message'] == 'voiceEndedCallback') {
          worker.sendMessage(data.jsify()!);
        }
      } catch (e) {
        print("Received String from WASM worker but it's not a Map! $e\n");
      }
    }
  });
}

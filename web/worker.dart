import 'dart:convert' show jsonDecode;

import 'package:flutter_soloud/src/worker/worker.dart';

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

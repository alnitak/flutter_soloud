import 'dart:convert' show jsonDecode;

import 'package:flutter_soloud/src/worker/worker.dart';

void doJob() async {
  // print('Worker created.\n');
  var worker = Worker(args: null);
  worker.onReceive().listen((data) {
    // print('Dart worker: '
    //     'onMessage received $data with type of ${data.runtimeType}\n');

    if (data is String) {
      try {
        final parseMap = jsonDecode(data);
        print("Received $data  PARSED TO $parseMap\n");
        if (parseMap['message'] == 'voiceEndedCallback') {
          worker.sendMessage(data);
        }
      } catch (e) {
        print("Received data from WASM worker but it's not a String!\n");
      }
    }
  });
}

/// Called only when creating a web worker.
void main() {
  doJob();
}

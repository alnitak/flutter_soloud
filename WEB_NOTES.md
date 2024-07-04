# Web Notes


## Description

The web platform is now supported, but some testing is welcome.

## How to use

In the `web` directory, there is a `compile_wasm.sh` script that generates the `.js` and `.wasm` files for the native C code. Run it after installing *emscripten*. There is also a `compile_web.sh` to compile the web worker needed by native code to communicate with Dart.
These scripts should be run when changing C/C++ code or the `web/worker.dart` code.
The `compile_wasm.sh` script uses the `-O1` code optimization. A better optimization doesn't work for me,  this need furter investigation.

To see a better errors logs, use `-O0 -g -s ASSERTIONS=1` in `compile_wasm.sh`.

To add the plugin to a web app, add the following line to the `<body>` section of `web/index.html`:
`<script src="assets/packages/flutter_soloud/web/libflutter_soloud_plugin.js" defer></script>`

## The problems

The AudioIsolate was causing many problems related to web workers. It was used to monitor all the sound states and to send some operations to the native code. These operations were not working inside a JS Worker because web audio is not supported.

The `AudioIsolate` [has been removed](https://github.com/alnitak/flutter_soloud/pull/89) and all the logic has been implemented natively. Events like audio finished are sent from C back to Dart. However, since it is not possible to call Dart from a native thread (the audio thread), a new web worker is created using the WASM `EM_ASM` directive. This allows sending the `audio finished` event back to Dart via the worker. Here an example just to let me explain and receive suggestions/critiques:
```CPP
// On C side define an inline function to create the Web Worker and another
// to send messages to the listening worker in Dart.

void createWorkerInWasm()
{
	EM_ASM({
		...
		// Create a new Worker
		var workerUri = "assets/packages/flutter_soloud/web/worker.dart.js";
		Module.wasmWorker = new Worker(workerUri);
	});
}

void sendToWorker(const char *message, int value)
{
	EM_ASM({
			// Send the message
			Module.wasmWorker.postMessage(JSON.stringify({
				"message" : UTF8ToString($0),
				"value" : $1
			}));
		}, message, value);
}

```

```Dart
// On Dart side listen to the messages.
@JS('Module.wasmWorker')
external web.Worker wasmWorker;

void setDartEventCallbacks() {
	web.Worker _worker = wasmWorker;
	_worker?.onmessage = ((web.MessageEvent event) {
      _outputController?.add(event.data.dartify());
    }).toJS;
}
```

The same problem happens using `dart:ffi`, it is also not possible to call a function directly from a native thread to Dart without using send and receive ports. Here, `NativeCallable` helps. With `NativeCallable`, it is not necessary to import sendPort and receivePort into the native code, which are part of ffi and thus not compatible with the web.


## Notes

Acquiring audio data is (was?) experimental, the following methods are now deprecated and this functionality is now in the `AudioData` class.
- `@experimental SoLoud.getAudioTexture2D()`
- `@experimental SoLoudCapture.getCaptureAudioTexture2D()`

It is not possible to read a local audio file directly on the web. For this reason, `loadMem()` has been added, which requires the `Uint8List` byte buffer of the audio file.

In addition to the `getAudioTexture2D`, with `AudioData` class is now possible to acquire audio as `linear`, which represents the FFT+wave vector, or just the `wave` data vector for better performance. With this class, it is also possible to choose to acquire data from the player or from the microphone.


**`loadUrl()`** may produre the following error when the app is run on a remote server:
>> Cross-Origin Request Blocked: The Same Origin Policy disallows reading the remote resource at https://www.learningcontainer.com/wp-content/uploads/2020/02/Kalimba.mp3. (Reason: CORS header ‘Access-Control-Allow-Origin’ missing). Status code: 200.

This is due for the default beavior of http servers which don't allow to make requests outside their domain. Refer [here](https://enable-cors.org/server.html) to learn how to enable your server to handle this situation.
Instead, if you run the app locally, you could run the app with something like the following command:
`flutter run -d chrome --web-renderer canvaskit --web-browser-flag '--disable-web-security' -t lib/main.dart --release`

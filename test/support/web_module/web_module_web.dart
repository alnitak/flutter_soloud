import 'dart:async';
import 'dart:js_interop';

import 'package:web/web.dart' as web;

@JS('self.flutter_soloud_ready')
external JSPromise? get _flutterSoloudReady;

@JS('self.flutter_soloud_force_single_threaded')
external set _forceSingleThreaded(bool value);

/// `--dart-define=SOLOUD_FORCE_ST=true` makes init_soloud.js load the
/// single-threaded build even when the page is cross-origin isolated.
const _forceSt = bool.fromEnvironment('SOLOUD_FORCE_ST');

/// Makes `new Worker(url)` work on the test page.
///
/// With `--wasm` the test page is served with COOP/COEP headers, but the test
/// server sends no COEP header with the scripts, so Chrome refuses to start
/// any worker from a URL: the plugin's event worker (worker.dart.js) and the
/// threads of the multi-threaded build. A worker created from a blob URL
/// takes the page's policy instead, and same-origin `importScripts` is
/// allowed, so same-origin script URLs are wrapped that way. An app served
/// with the headers on every response does not need this.
const _workerShim = '''
(function () {
  if (self.__soloudWorkerShim) return;
  self.__soloudWorkerShim = true;
  const NativeWorker = self.Worker;
  self.Worker = function (url, options) {
    const abs = new URL(String(url), document.baseURI);
    if (abs.origin === location.origin && abs.protocol !== 'blob:' &&
        !(options && options.type === 'module')) {
      const src = 'importScripts(' + JSON.stringify(abs.href) + ');';
      url = URL.createObjectURL(new Blob([src], {type: 'text/javascript'}));
    }
    return new NativeWorker(url, options);
  };
  self.Worker.prototype = NativeWorker.prototype;
})();
''';

/// Adds `init_soloud.js` to the test page, as an app's index.html does, and
/// waits for it to instantiate the module.
///
/// The script and the WASM files are looked up at the paths an app uses
/// (`assets/packages/flutter_soloud/web/...`, relative to the page). The
/// test server only serves files under test/, so
/// test/support/web_tests.sh links that path to the repository's web/
/// folder before running the tests.
Future<void> loadSoloudWebModule() async {
  if (_flutterSoloudReady == null) {
    web.document.head!.appendChild(web.HTMLScriptElement()..text = _workerShim);
    if (_forceSt) _forceSingleThreaded = true;
    const src = 'assets/packages/flutter_soloud/web/init_soloud.js';
    final loaded = Completer<void>();
    final script = web.HTMLScriptElement()
      ..src = src
      ..onload = ((web.Event _) => loaded.complete()).toJS
      ..onerror = ((web.Event _) => loaded.completeError(
        StateError(
          'Could not load $src. Run the browser tests with '
          'test/support/web_tests.sh, which makes web/ reachable.',
        ),
      )).toJS;
    web.document.head!.appendChild(script);
    await loaded.future;
  }
  await _flutterSoloudReady!.toDart;
}

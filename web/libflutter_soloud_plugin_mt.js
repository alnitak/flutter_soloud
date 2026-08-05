// This code implements the `-sMODULARIZE` settings by taking the generated
// JS program code (INNER_JS_CODE) and wrapping it in a factory function.

// Single threaded MINIMAL_RUNTIME programs do not need access to
// document.currentScript, so a simple export declaration is enough.
var Module_soloud = (() => {
  // When MODULARIZE this JS may be executed later,
  // after document.currentScript is gone, so we save it.
  // In EXPORT_ES6 mode we can just use 'import.meta.url'.
  var _scriptName = globalThis.document?.currentScript?.src;
  return async function(moduleArg = {}) {
    var moduleRtn;

// include: shell.js
// include: minimum_runtime_check.js
(function() {
  // "30.0.0" -> 300000
  function humanReadableVersionToPacked(str) {
    str = str.split("-")[0];
    // Remove any trailing part from e.g. "12.53.3-alpha"
    var vers = str.split(".").slice(0, 3);
    while (vers.length < 3) vers.push("00");
    vers = vers.map((n, i, arr) => n.padStart(2, "0"));
    return vers.join("");
  }
  // 300000 -> "30.0.0"
  var packedVersionToHumanReadable = n => [ n / 1e4 | 0, (n / 100 | 0) % 100, n % 100 ].join(".");
  var TARGET_NOT_SUPPORTED = 2147483647;
  // Note: We use a typeof check here instead of optional chaining using
  // globalThis because older browsers might not have globalThis defined.
  var currentNodeVersion = typeof process !== "undefined" && process.versions?.node ? humanReadableVersionToPacked(process.versions.node) : TARGET_NOT_SUPPORTED;
  if (currentNodeVersion < 160400) {
    throw new Error(`This emscripten-generated code requires node v${packedVersionToHumanReadable(160400)} (detected v${packedVersionToHumanReadable(currentNodeVersion)})`);
  }
  var userAgent = typeof navigator !== "undefined" && navigator.userAgent;
  if (!userAgent) {
    return;
  }
  var currentSafariVersion = userAgent.includes("Safari/") && !userAgent.includes("Chrome/") && userAgent.match(/Version\/(\d+\.?\d*\.?\d*)/) ? humanReadableVersionToPacked(userAgent.match(/Version\/(\d+\.?\d*\.?\d*)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentSafariVersion < 15e4) {
    throw new Error(`This emscripten-generated code requires Safari v${packedVersionToHumanReadable(15e4)} (detected v${currentSafariVersion})`);
  }
  var currentFirefoxVersion = userAgent.match(/Firefox\/(\d+(?:\.\d+)?)/) ? parseFloat(userAgent.match(/Firefox\/(\d+(?:\.\d+)?)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentFirefoxVersion < 79) {
    throw new Error(`This emscripten-generated code requires Firefox v79 (detected v${currentFirefoxVersion})`);
  }
  var currentChromeVersion = userAgent.match(/Chrome\/(\d+(?:\.\d+)?)/) ? parseFloat(userAgent.match(/Chrome\/(\d+(?:\.\d+)?)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentChromeVersion < 85) {
    throw new Error(`This emscripten-generated code requires Chrome v85 (detected v${currentChromeVersion})`);
  }
})();

// end include: minimum_runtime_check.js
// The Module object: Our interface to the outside world. We import
// and export values on it. There are various ways Module can be used:
// 1. Not defined. We create it here
// 2. A function parameter, function(moduleArg) => Promise<Module>
// 3. pre-run appended it, var Module = {}; ..generated code..
// 4. External script tag defines var Module.
// We need to check if Module already exists (e.g. case 3 above).
// Substitution will be replaced with actual code on later stage of the build,
// this way Closure Compiler will not mangle it (e.g. case 4. above).
// Note that if you want to run closure, and also to use Module
// after the generated code, you will need to define   var Module = {};
// before the code. Then that object will be used in the code, and you
// can continue to use Module afterwards as well.
var Module = moduleArg;

// The way we signal to a worker that it is hosting a pthread is to construct
// it with a specific name.
var ENVIRONMENT_IS_WASM_WORKER = globalThis.name == "em-ww";

var ENVIRONMENT_IS_AUDIO_WORKLET = !!globalThis.AudioWorkletGlobalScope;

// Audio worklets behave as wasm workers.
if (ENVIRONMENT_IS_AUDIO_WORKLET) ENVIRONMENT_IS_WASM_WORKER = true;

// Determine the runtime environment we are in. You can customize this by
// setting the ENVIRONMENT setting at compile time (see settings.js).
// Attempt to auto-detect the environment
var ENVIRONMENT_IS_WEB = !!globalThis.window;

var ENVIRONMENT_IS_WORKER = !!globalThis.WorkerGlobalScope;

// N.b. Electron.js environment is simultaneously a NODE-environment, but
// also a web environment.
var ENVIRONMENT_IS_NODE = globalThis.process?.versions?.node && globalThis.process?.type != "renderer";

var ENVIRONMENT_IS_SHELL = !ENVIRONMENT_IS_WEB && !ENVIRONMENT_IS_NODE && !ENVIRONMENT_IS_WORKER && !ENVIRONMENT_IS_AUDIO_WORKLET;

// Three configurations we can be running in:
// 1) We could be the application main() thread running in the main JS UI thread. (ENVIRONMENT_IS_WORKER == false and ENVIRONMENT_IS_PTHREAD == false)
// 2) We could be the application main() running directly in a worker. (ENVIRONMENT_IS_WORKER == true, ENVIRONMENT_IS_PTHREAD == false)
// 3) We could be an application pthread running in a worker. (ENVIRONMENT_IS_WORKER == true and ENVIRONMENT_IS_PTHREAD == true)
// The way we signal to a worker that it is hosting a pthread is to construct
// it with a specific name.
var ENVIRONMENT_IS_PTHREAD = ENVIRONMENT_IS_WORKER && self.name?.startsWith("em-pthread");

if (ENVIRONMENT_IS_PTHREAD) {
  assert(!globalThis.moduleLoaded, "module should only be loaded once on each pthread worker");
  globalThis.moduleLoaded = true;
}

if (ENVIRONMENT_IS_NODE) {
  var worker_threads = require("worker_threads");
  global.Worker = worker_threads.Worker;
  ENVIRONMENT_IS_WORKER = !worker_threads.isMainThread;
  // Under node we set `workerData` to `em-pthread` to signal that the worker
  // is hosting a pthread.
  ENVIRONMENT_IS_PTHREAD = ENVIRONMENT_IS_WORKER && worker_threads["workerData"] == "em-pthread";
  ENVIRONMENT_IS_WASM_WORKER = ENVIRONMENT_IS_WORKER && worker_threads["workerData"] == "em-ww";
}

// --pre-jses are emitted after the Module integration code, so that they can
// refer to Module (if they choose; they can also define Module)
var arguments_ = [];

var thisProgram = "./this.program";

var quit_ = (status, toThrow) => {
  throw toThrow;
};

if (typeof __filename != "undefined") {
  // Node
  _scriptName = __filename;
} else if (ENVIRONMENT_IS_WORKER) {
  _scriptName = self.location.href;
}

// `/` should be present at the end if `scriptDirectory` is not empty
var scriptDirectory = "";

function locateFile(path) {
  if (Module["locateFile"]) {
    return Module["locateFile"](path, scriptDirectory);
  }
  return scriptDirectory + path;
}

// Hooks that are implemented differently in different runtime environments.
var readAsync, readBinary;

if (ENVIRONMENT_IS_NODE) {
  const isNode = globalThis.process?.versions?.node && globalThis.process?.type != "renderer";
  if (!isNode) throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
  // These modules will usually be used on Node.js. Load them eagerly to avoid
  // the complexity of lazy-loading.
  var fs = require("fs");
  scriptDirectory = __dirname + "/";
  // include: node_shell_read.js
  readBinary = filename => {
    // We need to re-wrap `file://` strings to URLs.
    filename = isFileURI(filename) ? new URL(filename) : filename;
    var ret = fs.readFileSync(filename);
    assert(Buffer.isBuffer(ret));
    return ret;
  };
  readAsync = async (filename, binary = true) => {
    // See the comment in the `readBinary` function.
    filename = isFileURI(filename) ? new URL(filename) : filename;
    var ret = fs.readFileSync(filename, binary ? undefined : "utf8");
    assert(binary ? Buffer.isBuffer(ret) : typeof ret == "string");
    return ret;
  };
  // end include: node_shell_read.js
  if (process.argv.length > 1) {
    thisProgram = process.argv[1].replace(/\\/g, "/");
  }
  arguments_ = process.argv.slice(2);
  quit_ = (status, toThrow) => {
    process.exitCode = status;
    throw toThrow;
  };
} else if (ENVIRONMENT_IS_SHELL) {} else // Note that this includes Node.js workers when relevant (pthreads is enabled).
// Node.js workers are detected as a combination of ENVIRONMENT_IS_WORKER and
// ENVIRONMENT_IS_NODE.
if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
  try {
    scriptDirectory = new URL(".", _scriptName).href;
  } catch {}
  if (!(globalThis.window || globalThis.WorkerGlobalScope)) throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
  // Differentiate the Web Worker from the Node Worker case, as reading must
  // be done differently.
  if (!ENVIRONMENT_IS_NODE) {
    // include: web_or_worker_shell_read.js
    if (ENVIRONMENT_IS_WORKER) {
      readBinary = url => {
        var xhr = new XMLHttpRequest;
        xhr.open("GET", url, false);
        xhr.responseType = "arraybuffer";
        xhr.send(null);
        return new Uint8Array(/** @type{!ArrayBuffer} */ (xhr.response));
      };
    }
    readAsync = async url => {
      // Fetch has some additional restrictions over XHR, like it can't be used on a file:// url.
      // See https://github.com/github/fetch/pull/92#issuecomment-140665932
      // Cordova or Electron apps are typically loaded from a file:// url.
      // So use XHR on webview if URL is a file URL.
      if (isFileURI(url)) {
        return new Promise((resolve, reject) => {
          var xhr = new XMLHttpRequest;
          xhr.open("GET", url, true);
          xhr.responseType = "arraybuffer";
          xhr.onload = () => {
            if (xhr.status == 200 || (xhr.status == 0 && xhr.response)) {
              // file URLs can return 0
              resolve(xhr.response);
              return;
            }
            reject(xhr.status);
          };
          xhr.onerror = reject;
          xhr.send(null);
        });
      }
      var response = await fetch(url, {
        credentials: "same-origin"
      });
      if (response.ok) {
        return response.arrayBuffer();
      }
      throw new Error(response.status + " : " + response.url);
    };
  }
} else if (!ENVIRONMENT_IS_AUDIO_WORKLET) {
  throw new Error("environment detection error");
}

// Set up the out() and err() hooks, which are how we can print to stdout or
// stderr, respectively.
// Normally just binding console.log/console.error here works fine, but
// under node (with workers) we see missing/out-of-order messages so route
// directly to stdout and stderr.
// See https://github.com/emscripten-core/emscripten/issues/14804
var defaultPrint = console.log.bind(console);

var defaultPrintErr = console.error.bind(console);

if (ENVIRONMENT_IS_NODE) {
  var utils = require("util");
  var stringify = a => typeof a == "object" ? utils.inspect(a) : a;
  defaultPrint = (...args) => fs.writeSync(1, args.map(stringify).join(" ") + "\n");
  defaultPrintErr = (...args) => fs.writeSync(2, args.map(stringify).join(" ") + "\n");
}

var out = defaultPrint;

var err = defaultPrintErr;

var IDBFS = "IDBFS is no longer included by default; build with -lidbfs.js";

var PROXYFS = "PROXYFS is no longer included by default; build with -lproxyfs.js";

var WORKERFS = "WORKERFS is no longer included by default; build with -lworkerfs.js";

var FETCHFS = "FETCHFS is no longer included by default; build with -lfetchfs.js";

var ICASEFS = "ICASEFS is no longer included by default; build with -licasefs.js";

var JSFILEFS = "JSFILEFS is no longer included by default; build with -ljsfilefs.js";

var OPFS = "OPFS is no longer included by default; build with -lopfs.js";

var NODEFS = "NODEFS is no longer included by default; build with -lnodefs.js";

// perform assertions in shell.js after we set up out() and err(), as otherwise
// if an assertion fails it cannot print the message
assert(ENVIRONMENT_IS_AUDIO_WORKLET || ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER || ENVIRONMENT_IS_NODE, "Pthreads do not work in this environment yet (need Web Workers, or an alternative to them)");

assert(!ENVIRONMENT_IS_SHELL, "shell environment detected but not enabled at build time.  Add `shell` to `-sENVIRONMENT` to enable.");

// end include: shell.js
// include: preamble.js
// === Preamble library stuff ===
// Documentation for the public APIs defined in this file must be updated in:
//    site/source/docs/api_reference/preamble.js.rst
// A prebuilt local version of the documentation is available at:
//    site/build/text/docs/api_reference/preamble.js.txt
// You can also build docs locally as HTML or other formats in site/
// An online HTML version (which may be of a different version of Emscripten)
//    is up at http://kripken.github.io/emscripten-site/docs/api_reference/preamble.js.html
var wasmBinary;

if (!globalThis.WebAssembly) {
  err("no native wasm support detected");
}

// Wasm globals
// For sending to workers.
var wasmModule;

//========================================
// Runtime essentials
//========================================
// whether we are quitting the application. no code should run after this.
// set in exit() and abort()
var ABORT = false;

// set by exit() and abort().  Passed to 'onExit' handler.
// NOTE: This is also used as the process return code code in shell environments
// but only when noExitRuntime is false.
var EXITSTATUS;

// In STRICT mode, we only define assert() when ASSERTIONS is set.  i.e. we
// don't define it at all in release modes.  This matches the behaviour of
// MINIMAL_RUNTIME.
// TODO(sbc): Make this the default even without STRICT enabled.
/** @type {function(*, string=)} */ function assert(condition, text) {
  if (!condition) {
    abort("Assertion failed" + (text ? ": " + text : ""));
  }
}

// We used to include malloc/free by default in the past. Show a helpful error in
// builds with assertions.
/**
 * Indicates whether filename is delivered via file protocol (as opposed to http/https)
 * @noinline
 */ var isFileURI = filename => filename.startsWith("file://");

// include: runtime_common.js
// include: runtime_stack_check.js
// Initializes the stack cookie. Called at the startup of main and at the startup of each thread in pthreads mode.
function writeStackCookie() {
  var max = _emscripten_stack_get_end();
  assert((max & 3) == 0);
  // If the stack ends at address zero we write our cookies 4 bytes into the
  // stack.  This prevents interference with SAFE_HEAP and ASAN which also
  // monitor writes to address zero.
  if (max == 0) {
    max += 4;
  }
  // The stack grow downwards towards _emscripten_stack_get_end.
  // We write cookies to the final two words in the stack and detect if they are
  // ever overwritten.
  (growMemViews(), HEAPU32)[((max) >> 2)] = 34821223;
  (growMemViews(), HEAPU32)[(((max) + (4)) >> 2)] = 2310721022;
  // Also test the global address 0 for integrity.
  (growMemViews(), HEAPU32)[((0) >> 2)] = 1668509029;
}

function checkStackCookie() {
  if (ABORT) return;
  var max = _emscripten_stack_get_end();
  // See writeStackCookie().
  if (max == 0) {
    max += 4;
  }
  var cookie1 = (growMemViews(), HEAPU32)[((max) >> 2)];
  var cookie2 = (growMemViews(), HEAPU32)[(((max) + (4)) >> 2)];
  if (cookie1 != 34821223 || cookie2 != 2310721022) {
    abort(`Stack overflow! Stack cookie has been overwritten at ${ptrToString(max)}, expected hex dwords 0x89BACDFE and 0x2135467, but received ${ptrToString(cookie2)} ${ptrToString(cookie1)}`);
  }
  // Also test the global address 0 for integrity.
  if ((growMemViews(), HEAPU32)[((0) >> 2)] != 1668509029) {
    abort("Runtime error: The application has corrupted its heap memory area (address zero)!");
  }
}

// end include: runtime_stack_check.js
// include: runtime_exceptions.js
// end include: runtime_exceptions.js
// include: runtime_debug.js
var runtimeDebug = true;

// Switch to false at runtime to disable logging at the right times
// Used by XXXXX_DEBUG settings to output debug messages.
function dbg(...args) {
  if (!runtimeDebug && typeof runtimeDebug != "undefined") return;
  // Avoid using the console for debugging in multi-threaded node applications
  // See https://github.com/emscripten-core/emscripten/issues/14804
  if (ENVIRONMENT_IS_NODE) {
    // TODO(sbc): Unify with err/out implementation in shell.sh.
    var fs = require("fs");
    var utils = require("util");
    function stringify(a) {
      switch (typeof a) {
       case "object":
        return utils.inspect(a);

       case "undefined":
        return "undefined";
      }
      return a;
    }
    fs.writeSync(2, args.map(stringify).join(" ") + "\n");
  } else // TODO(sbc): Make this configurable somehow.  Its not always convenient for
  // logging to show up as warnings.
  console.warn(...args);
}

// Endianness check
(() => {
  var h16 = new Int16Array(1);
  var h8 = new Int8Array(h16.buffer);
  h16[0] = 25459;
  if (h8[0] !== 115 || h8[1] !== 99) abort("Runtime error: expected the system to be little-endian! (Run with -sSUPPORT_BIG_ENDIAN to bypass)");
})();

function consumedModuleProp(prop) {
  if (!Object.getOwnPropertyDescriptor(Module, prop)) {
    Object.defineProperty(Module, prop, {
      configurable: true,
      set() {
        abort(`Attempt to set \`Module.${prop}\` after it has already been processed.  This can happen, for example, when code is injected via '--post-js' rather than '--pre-js'`);
      }
    });
  }
}

function makeInvalidEarlyAccess(name) {
  return () => assert(false, `call to '${name}' via reference taken before Wasm module initialization`);
}

function ignoredModuleProp(prop) {
  if (Object.getOwnPropertyDescriptor(Module, prop)) {
    abort(`\`Module.${prop}\` was supplied but \`${prop}\` not included in INCOMING_MODULE_JS_API`);
  }
}

// forcing the filesystem exports a few things by default
function isExportedByForceFilesystem(name) {
  return name === "FS_createPath" || name === "FS_createDataFile" || name === "FS_createPreloadedFile" || name === "FS_preloadFile" || name === "FS_unlink" || name === "addRunDependency" || // The old FS has some functionality that WasmFS lacks.
  name === "FS_createLazyFile" || name === "FS_createDevice" || name === "removeRunDependency";
}

function missingLibrarySymbol(sym) {
  // Any symbol that is not included from the JS library is also (by definition)
  // not exported on the Module object.
  unexportedRuntimeSymbol(sym);
}

function unexportedRuntimeSymbol(sym) {
  if (ENVIRONMENT_IS_PTHREAD) {
    return;
  }
  if (!Object.getOwnPropertyDescriptor(Module, sym)) {
    Object.defineProperty(Module, sym, {
      configurable: true,
      get() {
        var msg = `'${sym}' was not exported. add it to EXPORTED_RUNTIME_METHODS (see the Emscripten FAQ)`;
        if (isExportedByForceFilesystem(sym)) {
          msg += ". Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you";
        }
        abort(msg);
      }
    });
  }
}

/**
 * Override `err`/`out`/`dbg` to report thread / worker information
 */ function initWorkerLogging() {
  function getLogPrefix() {
    if (wwParams?.wwID) {
      return `ww:${wwParams?.wwID}:`;
    }
    var t = 0;
    if (runtimeInitialized && typeof _pthread_self != "undefined") {
      t = _pthread_self();
    }
    return `w:${workerID},t:${ptrToString(t)}:`;
  }
  // Prefix all dbg() messages with the calling thread info.
  var origDbg = dbg;
  dbg = (...args) => origDbg(getLogPrefix(), ...args);
}

initWorkerLogging();

// end include: runtime_debug.js
// Support for growable heap + pthreads, where the buffer may change, so JS views
// must be updated.
function growMemViews() {
  // `updateMemoryViews` updates all the views simultaneously, so it's enough to check any of them.
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
}

var readyPromiseResolve, readyPromiseReject;

if (ENVIRONMENT_IS_NODE && (ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER)) {
  // Create as web-worker-like an environment as we can.
  var parentPort = worker_threads["parentPort"];
  parentPort.on("message", msg => global.onmessage?.({
    data: msg
  }));
  Object.assign(globalThis, {
    self: global,
    postMessage: msg => parentPort["postMessage"](msg)
  });
  // Node.js Workers do not pass postMessage()s and uncaught exception events to the parent
  // thread necessarily in the same order where they were generated in sequential program order.
  // See https://github.com/nodejs/node/issues/59617
  // To remedy this, capture all uncaughtExceptions in the Worker, and sequentialize those over
  // to the same postMessage pipe that other messages use.
  process.on("uncaughtException", err => {
    postMessage({
      cmd: "uncaughtException",
      error: err
    });
    // Also shut down the Worker to match the same semantics as if this uncaughtException
    // handler was not registered.
    // (n.b. this will not shut down the whole Node.js app process, but just the Worker)
    process.exit(1);
  });
}

// include: runtime_pthread.js
// Pthread Web Worker handling code.
// This code runs only on pthread web workers and handles pthread setup
// and communication with the main thread via postMessage.
// Unique ID of the current pthread worker (zero on non-pthread-workers
// including the main thread).
var workerID = 0;

var startWorker;

if (ENVIRONMENT_IS_PTHREAD) {
  // Thread-local guard variable for one-time init of the JS state
  var initializedJS = false;
  // Turn unhandled rejected promises into errors so that the main thread will be
  // notified about them.
  self.onunhandledrejection = e => {
    throw e.reason || e;
  };
  function handleMessage(e) {
    try {
      var msgData = e["data"];
      //dbg('msgData: ' + Object.keys(msgData));
      var cmd = msgData.cmd;
      if (cmd === "load") {
        // Preload command that is called once per worker to parse and load the Emscripten code.
        workerID = msgData.workerID;
        // Until we initialize the runtime, queue up any further incoming messages.
        let messageQueue = [];
        self.onmessage = e => messageQueue.push(e);
        // And add a callback for when the runtime is initialized.
        startWorker = () => {
          // Notify the main thread that this thread has loaded.
          postMessage({
            cmd: "loaded"
          });
          // Process any messages that were queued before the thread was ready.
          for (let msg of messageQueue) {
            handleMessage(msg);
          }
          // Restore the real message handler.
          self.onmessage = handleMessage;
        };
        // Use `const` here to ensure that the variable is scoped only to
        // that iteration, allowing safe reference from a closure.
        for (const handler of msgData.handlers) {
          // The the main module has a handler for a certain even, but no
          // handler exists on the pthread worker, then proxy that handler
          // back to the main thread.
          if (!Module[handler] || Module[handler].proxy) {
            Module[handler] = (...args) => {
              postMessage({
                cmd: "callHandler",
                handler,
                args
              });
            };
            // Rebind the out / err handlers if needed
            if (handler == "print") out = Module[handler];
            if (handler == "printErr") err = Module[handler];
          }
        }
        wasmMemory = msgData.wasmMemory;
        updateMemoryViews();
        wasmModule = msgData.wasmModule;
        createWasm();
        run();
      } else if (cmd === "run") {
        assert(msgData.pthread_ptr);
        // Call inside JS module to set up the stack frame for this pthread in JS module scope.
        // This needs to be the first thing that we do, as we cannot call to any C/C++ functions
        // until the thread stack is initialized.
        establishStackSpace(msgData.pthread_ptr);
        // Pass the thread address to wasm to store it for fast access.
        __emscripten_thread_init(msgData.pthread_ptr, /*is_main=*/ 0, /*is_runtime=*/ 0, /*can_block=*/ 1, 0, 0);
        PThread.threadInitTLS();
        // Await mailbox notifications with `Atomics.waitAsync` so we can start
        // using the fast `Atomics.notify` notification path.
        __emscripten_thread_mailbox_await(msgData.pthread_ptr);
        if (!initializedJS) {
          initializedJS = true;
        }
        try {
          invokeEntryPoint(msgData.start_routine, msgData.arg);
        } catch (ex) {
          if (ex != "unwind") {
            // The pthread "crashed".  Do not call `_emscripten_thread_exit` (which
            // would make this thread joinable).  Instead, re-throw the exception
            // and let the top level handler propagate it back to the main thread.
            throw ex;
          }
        }
      } else if (msgData.target === "setimmediate") {} else if (cmd === "checkMailbox") {
        if (initializedJS) {
          checkMailbox();
        }
      } else if (cmd) {
        // The received message looks like something that should be handled by this message
        // handler, (since there is a cmd field present), but is not one of the
        // recognized commands:
        err(`worker: received unknown command ${cmd}`);
        err(msgData);
      }
    } catch (ex) {
      err(`worker: onmessage() captured an uncaught exception: ${ex}`);
      if (ex?.stack) err(ex.stack);
      __emscripten_thread_crashed();
      throw ex;
    }
  }
  self.onmessage = handleMessage;
}

// ENVIRONMENT_IS_PTHREAD
// end include: runtime_pthread.js
// include: wasm_worker.js
var wwParams;

/**
 * Called once the intiial message has been recieved from the creating thread.
 * The `props` object is property bag sent via postMessage to create the worker.
 *
 * This function is called both in normal wasm workers and in audio worklets.
 */ function startWasmWorker(props) {
  wwParams = props;
  wasmMemory = props.wasmMemory;
  updateMemoryViews();
  wasmModule = props.wasm;
  createWasm();
  run();
  // Drop now unneeded references to from the Module object in this Worker,
  // these are not needed anymore.
  props.wasm = props.memMemory = 0;
}

if (ENVIRONMENT_IS_WASM_WORKER && !ENVIRONMENT_IS_AUDIO_WORKLET) {
  // Node.js support
  if (ENVIRONMENT_IS_NODE) {
    // Weak map of handle functions to their wrapper. Used to implement
    // addEventListener/removeEventListener.
    var wrappedHandlers = new WeakMap;
    /** @suppress {checkTypes} */ globalThis.onmessage = null;
    function wrapMsgHandler(h) {
      var f = wrappedHandlers.get(h);
      if (!f) {
        f = msg => h({
          data: msg
        });
        wrappedHandlers.set(h, f);
      }
      return f;
    }
    Object.assign(globalThis, {
      addEventListener: (name, handler) => parentPort["on"](name, wrapMsgHandler(handler)),
      removeEventListener: (name, handler) => parentPort["off"](name, wrapMsgHandler(handler))
    });
  }
  onmessage = d => {
    // The first message sent to the Worker is always the bootstrap message.
    // Drop this message listener, it served its purpose of bootstrapping
    // the Wasm Module load, and is no longer needed. Let user code register
    // any desired message handlers from now on.
    /** @suppress {checkTypes} */ onmessage = null;
    startWasmWorker(d.data);
  };
}

// end include: wasm_worker.js
// include: audio_worklet.js
// This file is the main bootstrap script for Wasm Audio Worklets loaded in an
// Emscripten application.  Build with -sAUDIO_WORKLET linker flag to enable
// targeting Audio Worklets.
// AudioWorkletGlobalScope does not have a onmessage/postMessage() functionality
// at the global scope, which means that after creating an
// AudioWorkletGlobalScope and loading this script into it, we cannot
// postMessage() information into it like one would do with Web Workers.
// Instead, we must create an AudioWorkletProcessor class, then instantiate a
// Web Audio graph node from it on the main thread. Using its message port and
// the node constructor's "processorOptions" field, we can share the necessary
// bootstrap information from the main thread to the AudioWorkletGlobalScope.
if (ENVIRONMENT_IS_AUDIO_WORKLET) {
  function createWasmAudioWorkletProcessor(audioParams) {
    class WasmAudioWorkletProcessor extends AudioWorkletProcessor {
      constructor(args) {
        super();
        // Capture the Wasm function callback to invoke.
        let opts = args.processorOptions;
        assert(opts.callback);
        assert(opts.samplesPerChannel);
        this.callback = ((a1, a2, a3, a4, a5, a6, a7) => dynCall_iiiiiiii(opts.callback, a1, a2, a3, a4, a5, a6, a7));
        this.userData = opts.userData;
        // Then the samples per channel to process, fixed for the lifetime of the
        // context that created this processor. Even though this 'render quantum
        // size' is fixed at 128 samples in the 1.0 spec, it will be variable in
        // the 1.1 spec. It's passed in now, just to prove it's settable, but will
        // eventually be a property of the  AudioWorkletGlobalScope (globalThis).
        this.samplesPerChannel = opts.samplesPerChannel;
        this.bytesPerChannel = this.samplesPerChannel * 4;
        // Prepare the output views; see createOutputViews(). The 'STACK_ALIGN'
        // deduction stops the STACK_OVERFLOW_CHECK failing (since the stack will
        // be full if we allocate all the available space) leaving room for a
        // single AudioSampleFrame as a minumum. There's an arbitrary maximum of
        // 64 frames, for the case where a multi-MB stack is passed.
        this.outputViews = new Array(Math.min(((wwParams.stackSize - 16) / this.bytesPerChannel) | 0, /*sensible limit*/ 64));
        assert(this.outputViews.length > 0, `AudioWorklet needs more stack allocating (at least ${this.bytesPerChannel})`);
        this.createOutputViews();
        // Explicitly verify this later in process(). Note to self, stackSave is a
        // bit of a misnomer as it simply gets the stack address.
        this.ctorOldStackPtr = stackSave();
      }
      /**
     * Create up-front as many typed views for marshalling the output data as
     * may be required, allocated at the *top* of the worklet's stack (and whose
     * addresses are fixed). 
     */ createOutputViews() {
        // These are still alloc'd to take advantage of the overflow checks, etc.
        var oldStackPtr = stackSave();
        var viewDataIdx = ((stackAlloc(this.outputViews.length * this.bytesPerChannel)) >> 2);
        // Inserted in reverse so the lowest indices are closest to the stack top
        for (var n = this.outputViews.length - 1; n >= 0; n--) {
          this.outputViews[n] = (growMemViews(), HEAPF32).subarray(viewDataIdx, viewDataIdx += this.samplesPerChannel);
        }
        stackRestore(oldStackPtr);
      }
      static get parameterDescriptors() {
        return audioParams;
      }
      /**
     * Marshals all inputs and parameters to the Wasm memory on the thread's
     * stack, then performs the wasm audio worklet call, and finally marshals
     * audio output data back.
     *
     * @param {Object} parameters
     */ process(inputList, outputList, parameters) {
        // Recreate the output views if the heap has changed
        // TODO: add support for GROWABLE_ARRAYBUFFERS
        if ((growMemViews(), HEAPF32).buffer != this.outputViews[0].buffer) {
          this.createOutputViews();
        }
        var numInputs = inputList.length;
        var numOutputs = outputList.length;
        var entry;
        // reused list entry or index
        var subentry;
        // reused channel or other array in each list entry or index
        // Calculate the required stack and output buffer views (stack is further
        // split into aligned structs and the raw float data).
        var stackMemoryStruct = (numInputs + numOutputs) * 12;
        var stackMemoryData = 0;
        for (entry of inputList) {
          stackMemoryData += entry.length;
        }
        stackMemoryData *= this.bytesPerChannel;
        // Collect the total number of output channels (mapped to array views)
        var outputViewsNeeded = 0;
        for (entry of outputList) {
          outputViewsNeeded += entry.length;
        }
        stackMemoryData += outputViewsNeeded * this.bytesPerChannel;
        var numParams = 0;
        for (entry in parameters) {
          ++numParams;
          stackMemoryStruct += 8;
          stackMemoryData += parameters[entry].byteLength;
        }
        var oldStackPtr = stackSave();
        assert(oldStackPtr == this.ctorOldStackPtr, "AudioWorklet stack address has unexpectedly moved");
        assert(outputViewsNeeded <= this.outputViews.length, `Too many AudioWorklet outputs (need ${outputViewsNeeded} but have stack space for ${this.outputViews.length})`);
        // Allocate the necessary stack space. All pointer variables are in bytes;
        // 'structPtr' starts at the first struct entry (all run sequentially)
        // and is the working start to each record; 'dataPtr' is the same for the
        // audio/params data, starting after *all* the structs.
        // 'structPtr' begins 16-byte aligned, allocated from the internal
        // _emscripten_stack_alloc(), as are the output views, and so to ensure
        // the views fall on the correct addresses (and we finish at stacktop) we
        // request additional bytes, taking this alignment into account, then
        // offset `dataPtr` by the difference.
        var stackMemoryAligned = (stackMemoryStruct + stackMemoryData + 15) & ~15;
        var structPtr = stackAlloc(stackMemoryAligned);
        var dataPtr = structPtr + (stackMemoryAligned - stackMemoryData);
        // TODO: look at why stackAlloc isn't tripping the assertions
        assert(stackMemoryAligned <= wwParams.stackSize, `Not enough stack allocated to the AudioWorklet (need ${stackMemoryAligned}, got ${wwParams.stackSize})`);
        // Copy input audio descriptor structs and data to Wasm (recall, structs
        // first, audio data after). 'inputsPtr' is the start of the C callback's
        // input AudioSampleFrame.
        var /*const*/ inputsPtr = structPtr;
        for (entry of inputList) {
          // Write the AudioSampleFrame struct instance
          (growMemViews(), HEAPU32)[((structPtr) >> 2)] = entry.length;
          (growMemViews(), HEAPU32)[(((structPtr) + (4)) >> 2)] = this.samplesPerChannel;
          (growMemViews(), HEAPU32)[(((structPtr) + (8)) >> 2)] = dataPtr;
          structPtr += 12;
          // Marshal the input audio sample data for each audio channel of this input
          for (subentry of entry) {
            (growMemViews(), HEAPF32).set(subentry, ((dataPtr) >> 2));
            dataPtr += this.bytesPerChannel;
          }
        }
        // Copy parameters descriptor structs and data to Wasm. 'paramsPtr' is the
        // start of the C callback's input AudioParamFrame.
        var /*const*/ paramsPtr = structPtr;
        for (entry = 0; subentry = parameters[entry++]; ) {
          // Write the AudioParamFrame struct instance
          (growMemViews(), HEAPU32)[((structPtr) >> 2)] = subentry.length;
          (growMemViews(), HEAPU32)[(((structPtr) + (4)) >> 2)] = dataPtr;
          structPtr += 8;
          // Marshal the audio parameters array
          (growMemViews(), HEAPF32).set(subentry, ((dataPtr) >> 2));
          dataPtr += subentry.length * 4;
        }
        // Copy output audio descriptor structs to Wasm. 'outputsPtr' is the start
        // of the C callback's output AudioSampleFrame. 'dataPtr' will now be
        // aligned with the output views, ending at stacktop (which is why this
        // needs to be last).
        var /*const*/ outputsPtr = structPtr;
        for (entry of outputList) {
          // Write the AudioSampleFrame struct instance
          (growMemViews(), HEAPU32)[((structPtr) >> 2)] = entry.length;
          (growMemViews(), HEAPU32)[(((structPtr) + (4)) >> 2)] = this.samplesPerChannel;
          (growMemViews(), HEAPU32)[(((structPtr) + (8)) >> 2)] = dataPtr;
          structPtr += 12;
          // Advance the output pointer to the next output (matching the pre-allocated views)
          dataPtr += this.bytesPerChannel * entry.length;
        }
        // If all the maths worked out, we arrived at the original stack address
        console.assert(dataPtr == oldStackPtr, `AudioWorklet stack missmatch (audio data finishes at ${dataPtr} instead of ${oldStackPtr})`);
        // Sanity checks. If these trip the most likely cause, beyond unforeseen
        // stack shenanigans, is that the 'render quantum size' changed after
        // construction (which shouldn't be possible).
        if (numOutputs) {
          // First that the output view addresses match the stack positions
          dataPtr -= this.bytesPerChannel;
          for (entry = 0; entry < outputViewsNeeded; entry++) {
            console.assert(dataPtr == this.outputViews[entry].byteOffset, "AudioWorklet internal error in addresses of the output array views");
            dataPtr -= this.bytesPerChannel;
          }
          // And that the views' size match the passed in output buffers
          for (entry of outputList) {
            for (subentry of entry) {
              assert(subentry.byteLength == this.bytesPerChannel, `AudioWorklet unexpected output buffer size (expected ${this.bytesPerChannel} got ${subentry.byteLength})`);
            }
          }
        }
        // Call out to Wasm callback to perform audio processing
        var didProduceAudio = this.callback(numInputs, inputsPtr, numOutputs, outputsPtr, numParams, paramsPtr, this.userData);
        if (didProduceAudio) {
          // Read back the produced audio data to all outputs and their channels.
          // The preallocated 'outputViews' already have the correct offsets and
          // sizes into the stack (recall from createOutputViews() that they run
          // backwards).
          for (entry of outputList) {
            for (subentry of entry) {
              subentry.set(this.outputViews[--outputViewsNeeded]);
            }
          }
        }
        stackRestore(oldStackPtr);
        // Return 'true' to tell the browser to continue running this processor.
        // (Returning 1 or any other truthy value won't work in Chrome)
        return !!didProduceAudio;
      }
    }
    return WasmAudioWorkletProcessor;
  }
  // If this browser does not support the up-to-date AudioWorklet standard
  // that has a MessagePort over to the AudioWorklet, then polyfill that by
  // a hacky AudioWorkletProcessor that provides the MessagePort.
  // Firefox added support in https://hg-edge.mozilla.org/integration/autoland/rev/ab38a1796126f2b3fc06475ffc5a625059af59c1
  // Chrome ticket: https://crbug.com/446920095
  // Safari ticket: https://webkit.org/b/299386
  /**
 * @suppress {duplicate, checkTypes}
 */ var port = globalThis.port || {};
  // Specify a worklet processor that will be used to receive messages to this
  // AudioWorkletGlobalScope.  We never connect this initial AudioWorkletProcessor
  // to the audio graph to do any audio processing.
  class BootstrapMessages extends AudioWorkletProcessor {
    constructor(arg) {
      super();
      startWasmWorker(arg.processorOptions);
      // Listen to messages from the main thread. These messages will ask this
      // scope to create the real AudioWorkletProcessors that call out to Wasm to
      // do audio processing.
      if (!(port instanceof MessagePort)) {
        this.port.onmessage = port.onmessage;
        /** @suppress {checkTypes} */ port = this.port;
      }
    }
    // No-op, not doing audio processing in this processor. It is just for
    // receiving bootstrap messages.  However browsers require it to still be
    // present. It should never be called because we never add a node to the graph
    // with this processor, although it does look like Chrome does still call this
    // function.
    process() {}
  }
  // Register the dummy processor that will just receive messages.
  registerProcessor("em-bootstrap", BootstrapMessages);
  port.onmessage = async msg => {
    let d = msg.data;
    if (d["_boot"]) {
      startWasmWorker(d);
    } else if (d["_wpn"]) {
      // '_wpn' is short for 'Worklet Processor Node', using an identifier
      // that will never conflict with user messages
      // Register a real AudioWorkletProcessor that will actually do audio processing.
      registerProcessor(d["_wpn"], createWasmAudioWorkletProcessor(d.audioParams));
      // Post a Wasm Call message back telling that we have now registered the
      // AudioWorkletProcessor, and should trigger the user onSuccess callback
      // of the emscripten_create_wasm_audio_worklet_processor_async() call.
      // '_wsc' is short for 'wasm call', using an identifier that will never
      // conflict with user messages.
      // Note: we convert the pointer arg manually here since the call site
      // ($_EmAudioDispatchProcessorCallback) is used with various signatures
      // and we do not know the types in advance.
      port.postMessage({
        "_wsc": d.callback,
        args: [ d.contextHandle, 1, d.userData ]
      });
    } else if (d["_wsc"]) {
      getWasmTableEntry(d["_wsc"])(...d.args);
    }
  };
}

// ENVIRONMENT_IS_AUDIO_WORKLET
// end include: audio_worklet.js
// Memory management
var /** @type {!Int8Array} */ HEAP8, /** @type {!Uint8Array} */ HEAPU8, /** @type {!Int16Array} */ HEAP16, /** @type {!Uint16Array} */ HEAPU16, /** @type {!Int32Array} */ HEAP32, /** @type {!Uint32Array} */ HEAPU32, /** @type {!Float32Array} */ HEAPF32, /** @type {!Float64Array} */ HEAPF64;

// BigInt64Array type is not correctly defined in closure
var /** not-@type {!BigInt64Array} */ HEAP64, /* BigUint64Array type is not correctly defined in closure
/** not-@type {!BigUint64Array} */ HEAPU64;

var runtimeInitialized = false;

function updateMemoryViews() {
  var b = wasmMemory.buffer;
  HEAP8 = new Int8Array(b);
  HEAP16 = new Int16Array(b);
  Module["HEAPU8"] = HEAPU8 = new Uint8Array(b);
  HEAPU16 = new Uint16Array(b);
  HEAP32 = new Int32Array(b);
  HEAPU32 = new Uint32Array(b);
  Module["HEAPF32"] = HEAPF32 = new Float32Array(b);
  HEAPF64 = new Float64Array(b);
  HEAP64 = new BigInt64Array(b);
  HEAPU64 = new BigUint64Array(b);
}

// In non-standalone/normal mode, we create the memory here.
// include: runtime_init_memory.js
// Create the wasm memory. (Note: this only applies if IMPORTED_MEMORY is defined)
// check for full engine support (use string 'subarray' to avoid closure compiler confusion)
function initMemory() {
  if ((ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER)) {
    return;
  }
  if (Module["wasmMemory"]) {
    wasmMemory = Module["wasmMemory"];
  } else {
    var INITIAL_MEMORY = Module["INITIAL_MEMORY"] || 67108864;
    assert(INITIAL_MEMORY >= 4194304, "INITIAL_MEMORY should be larger than STACK_SIZE, was " + INITIAL_MEMORY + "! (STACK_SIZE=" + 4194304 + ")");
    /** @suppress {checkTypes} */ wasmMemory = new WebAssembly.Memory({
      "initial": INITIAL_MEMORY / 65536,
      // In theory we should not need to emit the maximum if we want "unlimited"
      // or 4GB of memory, but VMs error on that atm, see
      // https://github.com/emscripten-core/emscripten/issues/14130
      // And in the pthreads case we definitely need to emit a maximum. So
      // always emit one.
      "maximum": 32768,
      "shared": true
    });
  }
  updateMemoryViews();
}

// end include: runtime_init_memory.js
// include: memoryprofiler.js
// end include: memoryprofiler.js
// end include: runtime_common.js
assert(globalThis.Int32Array && globalThis.Float64Array && Int32Array.prototype.subarray && Int32Array.prototype.set, "JS engine does not provide full typed array support");

function preRun() {
  assert(!ENVIRONMENT_IS_PTHREAD);
  // PThreads reuse the runtime from the main thread.
  if (Module["preRun"]) {
    if (typeof Module["preRun"] == "function") Module["preRun"] = [ Module["preRun"] ];
    while (Module["preRun"].length) {
      addOnPreRun(Module["preRun"].shift());
    }
  }
  consumedModuleProp("preRun");
  // Begin ATPRERUNS hooks
  callRuntimeCallbacks(onPreRuns);
}

function initRuntime() {
  assert(!runtimeInitialized);
  runtimeInitialized = true;
  if (ENVIRONMENT_IS_WASM_WORKER) return _wasmWorkerInitializeRuntime();
  if (ENVIRONMENT_IS_PTHREAD) return startWorker();
  checkStackCookie();
  // Begin ATINITS hooks
  if (!Module["noFSInit"] && !FS.initialized) FS.init();
  TTY.init();
  // End ATINITS hooks
  wasmExports["__wasm_call_ctors"]();
  // Begin ATPOSTCTORS hooks
  FS.ignorePermissions = false;
}

function postRun() {
  checkStackCookie();
  if ((ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER)) {
    return;
  }
  // PThreads reuse the runtime from the main thread.
  if (Module["postRun"]) {
    if (typeof Module["postRun"] == "function") Module["postRun"] = [ Module["postRun"] ];
    while (Module["postRun"].length) {
      addOnPostRun(Module["postRun"].shift());
    }
  }
  consumedModuleProp("postRun");
  // Begin ATPOSTRUNS hooks
  callRuntimeCallbacks(onPostRuns);
}

/** @param {string|number=} what */ function abort(what) {
  Module["onAbort"]?.(what);
  what = "Aborted(" + what + ")";
  // TODO(sbc): Should we remove printing and leave it up to whoever
  // catches the exception?
  err(what);
  ABORT = true;
  if (what.indexOf("RuntimeError: unreachable") >= 0) {
    what += '. "unreachable" may be due to ASYNCIFY_STACK_SIZE not being large enough (try increasing it)';
  }
  // Use a wasm runtime error, because a JS error might be seen as a foreign
  // exception, which means we'd run destructors on it. We need the error to
  // simply make the program stop.
  // FIXME This approach does not work in Wasm EH because it currently does not assume
  // all RuntimeErrors are from traps; it decides whether a RuntimeError is from
  // a trap or not based on a hidden field within the object. So at the moment
  // we don't have a way of throwing a wasm trap from JS. TODO Make a JS API that
  // allows this in the wasm spec.
  // Suppress closure compiler warning here. Closure compiler's builtin extern
  // definition for WebAssembly.RuntimeError claims it takes no arguments even
  // though it can.
  // TODO(https://github.com/google/closure-compiler/pull/3913): Remove if/when upstream closure gets fixed.
  /** @suppress {checkTypes} */ var e = new WebAssembly.RuntimeError(what);
  readyPromiseReject?.(e);
  // Throw the error whether or not MODULARIZE is set because abort is used
  // in code paths apart from instantiation where an exception is expected
  // to be thrown when abort is called.
  throw e;
}

function createExportWrapper(name, nargs) {
  return (...args) => {
    assert(runtimeInitialized, `native function \`${name}\` called before runtime initialization`);
    var f = wasmExports[name];
    assert(f, `exported native function \`${name}\` not found`);
    // Only assert for too many arguments. Too few can be valid since the missing arguments will be zero filled.
    assert(args.length <= nargs, `native function \`${name}\` called with ${args.length} args but expects ${nargs}`);
    return f(...args);
  };
}

var wasmBinaryFile;

function findWasmBinary() {
  return locateFile("libflutter_soloud_plugin_mt.wasm");
}

function getBinarySync(file) {
  if (file == wasmBinaryFile && wasmBinary) {
    return new Uint8Array(wasmBinary);
  }
  if (readBinary) {
    return readBinary(file);
  }
  // Throwing a plain string here, even though it not normally adviables since
  // this gets turning into an `abort` in instantiateArrayBuffer.
  throw "both async and sync fetching of the wasm failed";
}

async function getWasmBinary(binaryFile) {
  // If we don't have the binary yet, load it asynchronously using readAsync.
  if (!wasmBinary) {
    // Fetch the binary using readAsync
    try {
      var response = await readAsync(binaryFile);
      return new Uint8Array(response);
    } catch {}
  }
  // Otherwise, getBinarySync should be able to get it synchronously
  return getBinarySync(binaryFile);
}

async function instantiateArrayBuffer(binaryFile, imports) {
  try {
    var binary = await getWasmBinary(binaryFile);
    var instance = await WebAssembly.instantiate(binary, imports);
    return instance;
  } catch (reason) {
    err(`failed to asynchronously prepare wasm: ${reason}`);
    // Warn on some common problems.
    if (isFileURI(binaryFile)) {
      err(`warning: Loading from a file URI (${binaryFile}) is not supported in most browsers. See https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-run-a-local-webserver-for-testing-why-does-my-program-stall-in-downloading-or-preparing`);
    }
    abort(reason);
  }
}

async function instantiateAsync(binary, binaryFile, imports) {
  if (!binary && !isFileURI(binaryFile) && !ENVIRONMENT_IS_NODE) {
    try {
      var response = fetch(binaryFile, {
        credentials: "same-origin"
      });
      var instantiationResult = await WebAssembly.instantiateStreaming(response, imports);
      return instantiationResult;
    } catch (reason) {
      // We expect the most common failure cause to be a bad MIME type for the binary,
      // in which case falling back to ArrayBuffer instantiation should work.
      err(`wasm streaming compile failed: ${reason}`);
      err("falling back to ArrayBuffer instantiation");
    }
  }
  return instantiateArrayBuffer(binaryFile, imports);
}

function getWasmImports() {
  assignWasmImports();
  // instrumenting imports is used in asyncify in two ways: to add assertions
  // that check for proper import use, and for ASYNCIFY=2 we use them to set up
  // the Promise API on the import side.
  // In pthreads builds getWasmImports is called more than once but we only
  // and the instrument the imports once.
  if (!wasmImports.__instrumented) {
    wasmImports.__instrumented = true;
    Asyncify.instrumentWasmImports(wasmImports);
  }
  // prepare imports
  var imports = {
    "env": wasmImports,
    "wasi_snapshot_preview1": wasmImports
  };
  return imports;
}

// Create the wasm instance.
// Receives the wasm imports, returns the exports.
async function createWasm() {
  // Load the wasm module and create an instance of using native support in the JS engine.
  // handle a generated wasm instance, receiving its exports and
  // performing other necessary setup
  /** @param {WebAssembly.Module=} module*/ function receiveInstance(instance, module) {
    wasmExports = instance.exports;
    wasmExports = Asyncify.instrumentWasmExports(wasmExports);
    registerTLSInit(wasmExports["_emscripten_tls_init"]);
    assignWasmExports(wasmExports);
    // We now have the Wasm module loaded up, keep a reference to the compiled module so we can post it to the workers.
    wasmModule = module;
    return wasmExports;
  }
  // Prefer streaming instantiation if available.
  // Async compilation can be confusing when an error on the page overwrites Module
  // (for example, if the order of elements is wrong, and the one defining Module is
  // later), so we save Module and check it later.
  var trueModule = Module;
  function receiveInstantiationResult(result) {
    // 'result' is a ResultObject object which has both the module and instance.
    // receiveInstance() will swap in the exports (to Module.asm) so they can be called
    assert(Module === trueModule, "the Module object should not be replaced during async compilation - perhaps the order of HTML elements is wrong?");
    trueModule = null;
    return receiveInstance(result["instance"], result["module"]);
  }
  var info = getWasmImports();
  // User shell pages can write their own Module.instantiateWasm = function(imports, successCallback) callback
  // to manually instantiate the Wasm module themselves. This allows pages to
  // run the instantiation parallel to any other async startup actions they are
  // performing.
  // Also pthreads and wasm workers initialize the wasm instance through this
  // path.
  if (Module["instantiateWasm"]) {
    return new Promise((resolve, reject) => {
      try {
        Module["instantiateWasm"](info, (inst, mod) => {
          resolve(receiveInstance(inst, mod));
        });
      } catch (e) {
        err(`Module.instantiateWasm callback failed with error: ${e}`);
        reject(e);
      }
    });
  }
  if ((ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER)) {
    // Instantiate from the module that was recieved via postMessage from
    // the main thread. We can just use sync instantiation in the worker.
    assert(wasmModule, "wasmModule should have been received via postMessage");
    var instance = new WebAssembly.Instance(wasmModule, getWasmImports());
    return receiveInstance(instance, wasmModule);
  }
  wasmBinaryFile ??= findWasmBinary();
  var result = await instantiateAsync(wasmBinary, wasmBinaryFile, info);
  var exports = receiveInstantiationResult(result);
  return exports;
}

// end include: preamble.js
// Begin JS library code
class ExitStatus {
  name="ExitStatus";
  constructor(status) {
    this.message = `Program terminated with exit(${status})`;
    this.status = status;
  }
}

var terminateWorker = worker => {
  worker.terminate();
  // terminate() can be asynchronous, so in theory the worker can continue
  // to run for some amount of time after termination.  However from our POV
  // the worker now dead and we don't want to hear from it again, so we stub
  // out its message handler here.  This avoids having to check in each of
  // the onmessage handlers if the message was coming from valid worker.
  worker.onmessage = e => {
    var cmd = e["data"].cmd;
    err(`received "${cmd}" command from terminated worker: ${worker.workerID}`);
  };
};

var cleanupThread = pthread_ptr => {
  assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! cleanupThread() can only ever be called from main application thread!");
  assert(pthread_ptr, "Internal Error! Null pthread_ptr in cleanupThread!");
  var worker = PThread.pthreads[pthread_ptr];
  assert(worker);
  PThread.returnWorkerToPool(worker);
};

var callRuntimeCallbacks = callbacks => {
  while (callbacks.length > 0) {
    // Pass the module as the first argument.
    callbacks.shift()(Module);
  }
};

var onPreRuns = [];

var addOnPreRun = cb => onPreRuns.push(cb);

var runDependencies = 0;

var dependenciesFulfilled = null;

var runDependencyTracking = {};

var runDependencyWatcher = null;

var removeRunDependency = id => {
  runDependencies--;
  Module["monitorRunDependencies"]?.(runDependencies);
  assert(id, "removeRunDependency requires an ID");
  assert(runDependencyTracking[id]);
  delete runDependencyTracking[id];
  if (runDependencies == 0) {
    if (runDependencyWatcher !== null) {
      clearInterval(runDependencyWatcher);
      runDependencyWatcher = null;
    }
    if (dependenciesFulfilled) {
      var callback = dependenciesFulfilled;
      dependenciesFulfilled = null;
      callback();
    }
  }
};

var addRunDependency = id => {
  runDependencies++;
  Module["monitorRunDependencies"]?.(runDependencies);
  assert(id, "addRunDependency requires an ID");
  assert(!runDependencyTracking[id]);
  runDependencyTracking[id] = 1;
  if (runDependencyWatcher === null && globalThis.setInterval) {
    // Check for missing dependencies every few seconds
    runDependencyWatcher = setInterval(() => {
      if (ABORT) {
        clearInterval(runDependencyWatcher);
        runDependencyWatcher = null;
        return;
      }
      var shown = false;
      for (var dep in runDependencyTracking) {
        if (!shown) {
          shown = true;
          err("still waiting on run dependencies:");
        }
        err(`dependency: ${dep}`);
      }
      if (shown) {
        err("(end of list)");
      }
    }, 1e4);
    // Prevent this timer from keeping the runtime alive if nothing
    // else is.
    runDependencyWatcher.unref?.();
  }
};

var spawnThread = threadParams => {
  assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! spawnThread() can only ever be called from main application thread!");
  assert(threadParams.pthread_ptr, "Internal error, no pthread ptr!");
  var worker = PThread.getNewWorker();
  if (!worker) {
    // No available workers in the PThread pool.
    return 6;
  }
  assert(!worker.pthread_ptr, "Internal error!");
  PThread.runningWorkers.push(worker);
  // Add to pthreads map
  PThread.pthreads[threadParams.pthread_ptr] = worker;
  worker.pthread_ptr = threadParams.pthread_ptr;
  var msg = {
    cmd: "run",
    start_routine: threadParams.startRoutine,
    arg: threadParams.arg,
    pthread_ptr: threadParams.pthread_ptr
  };
  if (ENVIRONMENT_IS_NODE) {
    // Mark worker as weakly referenced once we start executing a pthread,
    // so that its existence does not prevent Node.js from exiting.  This
    // has no effect if the worker is already weakly referenced (e.g. if
    // this worker was previously idle/unused).
    worker.unref();
  }
  // Ask the worker to start executing its pthread entry point function.
  worker.postMessage(msg, threadParams.transferList);
  return 0;
};

var runtimeKeepaliveCounter = 0;

var keepRuntimeAlive = () => noExitRuntime || runtimeKeepaliveCounter > 0;

var stackSave = () => _emscripten_stack_get_current();

var stackRestore = val => __emscripten_stack_restore(val);

var stackAlloc = sz => __emscripten_stack_alloc(sz);

/** @type{function(number, (number|boolean), ...number)} */ var proxyToMainThread = (funcIndex, emAsmAddr, sync, ...callArgs) => {
  // EM_ASM proxying is done by passing a pointer to the address of the EM_ASM
  // content as `emAsmAddr`.  JS library proxying is done by passing an index
  // into `proxiedJSCallArgs` as `funcIndex`. If `emAsmAddr` is non-zero then
  // `funcIndex` will be ignored.
  // Additional arguments are passed after the first three are the actual
  // function arguments.
  // The serialization buffer contains the number of call params, and then
  // all the args here.
  // We also pass 'sync' to C separately, since C needs to look at it.
  // Allocate a buffer, which will be copied by the C code.
  // First passed parameter specifies the number of arguments to the function.
  // When BigInt support is enabled, we must handle types in a more complex
  // way, detecting at runtime if a value is a BigInt or not (as we have no
  // type info here). To do that, add a "prefix" before each value that
  // indicates if it is a BigInt, which effectively doubles the number of
  // values we serialize for proxying. TODO: pack this?
  var serializedNumCallArgs = callArgs.length * 2;
  var sp = stackSave();
  var args = stackAlloc(serializedNumCallArgs * 8);
  var b = ((args) >> 3);
  for (var i = 0; i < callArgs.length; i++) {
    var arg = callArgs[i];
    if (typeof arg == "bigint") {
      // The prefix is non-zero to indicate a bigint.
      (growMemViews(), HEAP64)[b + 2 * i] = 1n;
      (growMemViews(), HEAP64)[b + 2 * i + 1] = arg;
    } else {
      // The prefix is zero to indicate a JS Number.
      (growMemViews(), HEAP64)[b + 2 * i] = 0n;
      (growMemViews(), HEAPF64)[b + 2 * i + 1] = arg;
    }
  }
  var rtn = __emscripten_run_js_on_main_thread(funcIndex, emAsmAddr, serializedNumCallArgs, args, sync);
  stackRestore(sp);
  return rtn;
};

function _proc_exit(code) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(0, 0, 1, code);
  EXITSTATUS = code;
  if (!keepRuntimeAlive()) {
    PThread.terminateAllThreads();
    Module["onExit"]?.(code);
    ABORT = true;
  }
  quit_(code, new ExitStatus(code));
}

function exitOnMainThread(returnCode) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(1, 0, 0, returnCode);
  _exit(returnCode);
}

/** @param {boolean|number=} implicit */ var exitJS = (status, implicit) => {
  EXITSTATUS = status;
  checkUnflushedContent();
  if (ENVIRONMENT_IS_PTHREAD) {
    // implicit exit can never happen on a pthread
    assert(!implicit);
    // When running in a pthread we propagate the exit back to the main thread
    // where it can decide if the whole process should be shut down or not.
    // The pthread may have decided not to exit its own runtime, for example
    // because it runs a main loop, but that doesn't affect the main thread.
    exitOnMainThread(status);
    throw "unwind";
  }
  // if exit() was called explicitly, warn the user if the runtime isn't actually being shut down
  if (keepRuntimeAlive() && !implicit) {
    var msg = `program exited (with status: ${status}), but keepRuntimeAlive() is set (counter=${runtimeKeepaliveCounter}) due to an async operation, so halting execution but not exiting the runtime or preventing further async execution (you can use emscripten_force_exit, if you want to force a true shutdown)`;
    readyPromiseReject?.(msg);
    err(msg);
  }
  _proc_exit(status);
};

var _exit = exitJS;

var ptrToString = ptr => {
  assert(typeof ptr === "number", `ptrToString expects a number, got ${typeof ptr}`);
  // Convert to 32-bit unsigned value
  ptr >>>= 0;
  return "0x" + ptr.toString(16).padStart(8, "0");
};

var PThread = {
  unusedWorkers: [],
  runningWorkers: [],
  tlsInitFunctions: [],
  pthreads: {},
  nextWorkerID: 1,
  init() {
    if ((!(ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER))) {
      PThread.initMainThread();
    }
  },
  initMainThread() {
    var pthreadPoolSize = 8;
    // Start loading up the Worker pool, if requested.
    while (pthreadPoolSize--) {
      PThread.allocateUnusedWorker();
    }
    // MINIMAL_RUNTIME takes care of calling loadWasmModuleToAllWorkers
    // in postamble_minimal.js
    addOnPreRun(async () => {
      var pthreadPoolReady = PThread.loadWasmModuleToAllWorkers();
      addRunDependency("loading-workers");
      await pthreadPoolReady;
      removeRunDependency("loading-workers");
    });
  },
  terminateAllThreads: () => {
    assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! terminateAllThreads() can only ever be called from main application thread!");
    // Attempt to kill all workers.  Sadly (at least on the web) there is no
    // way to terminate a worker synchronously, or to be notified when a
    // worker in actually terminated.  This means there is some risk that
    // pthreads will continue to be executing after `worker.terminate` has
    // returned.  For this reason, we don't call `returnWorkerToPool` here or
    // free the underlying pthread data structures.
    for (var worker of PThread.runningWorkers) {
      terminateWorker(worker);
    }
    for (var worker of PThread.unusedWorkers) {
      terminateWorker(worker);
    }
    PThread.unusedWorkers = [];
    PThread.runningWorkers = [];
    PThread.pthreads = {};
  },
  returnWorkerToPool: worker => {
    // We don't want to run main thread queued calls here, since we are doing
    // some operations that leave the worker queue in an invalid state until
    // we are completely done (it would be bad if free() ends up calling a
    // queued pthread_create which looks at the global data structures we are
    // modifying). To achieve that, defer the free() til the very end, when
    // we are all done.
    var pthread_ptr = worker.pthread_ptr;
    delete PThread.pthreads[pthread_ptr];
    // Note: worker is intentionally not terminated so the pool can
    // dynamically grow.
    PThread.unusedWorkers.push(worker);
    PThread.runningWorkers.splice(PThread.runningWorkers.indexOf(worker), 1);
    // Not a running Worker anymore
    // Detach the worker from the pthread object, and return it to the
    // worker pool as an unused worker.
    worker.pthread_ptr = 0;
    // Finally, free the underlying (and now-unused) pthread structure in
    // linear memory.
    __emscripten_thread_free_data(pthread_ptr);
  },
  threadInitTLS() {
    // Call thread init functions (these are the _emscripten_tls_init for each
    // module loaded.
    PThread.tlsInitFunctions.forEach(f => f());
  },
  loadWasmModuleToWorker: worker => new Promise(onFinishedLoading => {
    worker.onmessage = e => {
      var d = e["data"];
      var cmd = d.cmd;
      // If this message is intended to a recipient that is not the main
      // thread, forward it to the target thread.
      if (d.targetThread && d.targetThread != _pthread_self()) {
        var targetWorker = PThread.pthreads[d.targetThread];
        if (targetWorker) {
          targetWorker.postMessage(d, d.transferList);
        } else {
          err(`Internal error! Worker sent a message "${cmd}" to target pthread ${d.targetThread}, but that thread no longer exists!`);
        }
        return;
      }
      if (cmd === "checkMailbox") {
        checkMailbox();
      } else if (cmd === "spawnThread") {
        spawnThread(d);
      } else if (cmd === "cleanupThread") {
        // cleanupThread needs to be run via callUserCallback since it calls
        // back into user code to free thread data. Without this it's possible
        // the unwind or ExitStatus exception could escape here.
        callUserCallback(() => cleanupThread(d.thread));
      } else if (cmd === "loaded") {
        worker.loaded = true;
        // Check that this worker doesn't have an associated pthread.
        if (ENVIRONMENT_IS_NODE && !worker.pthread_ptr) {
          // Once worker is loaded & idle, mark it as weakly referenced,
          // so that mere existence of a Worker in the pool does not prevent
          // Node.js from exiting the app.
          worker.unref();
        }
        onFinishedLoading(worker);
      } else if (d.target === "setimmediate") {
        // Worker wants to postMessage() to itself to implement setImmediate()
        // emulation.
        worker.postMessage(d);
      } else if (cmd === "uncaughtException") {
        // Message handler for Node.js specific out-of-order behavior:
        // https://github.com/nodejs/node/issues/59617
        // A pthread sent an uncaught exception event. Re-raise it on the main thread.
        worker.onerror(d.error);
      } else if (cmd === "callHandler") {
        Module[d.handler](...d.args);
      } else if (cmd) {
        // The received message looks like something that should be handled by this message
        // handler, (since there is a e.data.cmd field present), but is not one of the
        // recognized commands:
        err(`worker sent an unknown command ${cmd}`);
      }
    };
    worker.onerror = e => {
      var message = "worker sent an error!";
      if (worker.pthread_ptr) {
        message = `Pthread ${ptrToString(worker.pthread_ptr)} sent an error!`;
      }
      err(`${message} ${e.filename}:${e.lineno}: ${e.message}`);
      throw e;
    };
    if (ENVIRONMENT_IS_NODE) {
      worker.on("message", data => worker.onmessage({
        data
      }));
      worker.on("error", e => worker.onerror(e));
    }
    assert(wasmMemory instanceof WebAssembly.Memory, "WebAssembly memory should have been loaded by now!");
    assert(wasmModule instanceof WebAssembly.Module, "WebAssembly Module should have been loaded by now!");
    // When running on a pthread, none of the incoming parameters on the module
    // object are present. Proxy known handlers back to the main thread if specified.
    var handlers = [];
    var knownHandlers = [ "onExit", "onAbort", "print", "printErr" ];
    for (var handler of knownHandlers) {
      if (Module.propertyIsEnumerable(handler)) {
        handlers.push(handler);
      }
    }
    // Ask the new worker to load up the Emscripten-compiled page. This is a heavy operation.
    worker.postMessage({
      cmd: "load",
      handlers,
      wasmMemory,
      wasmModule,
      "workerID": worker.workerID
    });
  }),
  async loadWasmModuleToAllWorkers() {
    // Instantiation is synchronous in pthreads.
    if (ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER) {
      return;
    }
    let pthreadPoolReady = Promise.all(PThread.unusedWorkers.map(PThread.loadWasmModuleToWorker));
    return pthreadPoolReady;
  },
  allocateUnusedWorker() {
    var worker;
    var pthreadMainJs = _scriptName;
    // We can't use makeModuleReceiveWithVar here since we want to also
    // call URL.createObjectURL on the mainScriptUrlOrBlob.
    if (Module["mainScriptUrlOrBlob"]) {
      pthreadMainJs = Module["mainScriptUrlOrBlob"];
      if (typeof pthreadMainJs != "string") {
        pthreadMainJs = URL.createObjectURL(pthreadMainJs);
      }
    }
    worker = new Worker(pthreadMainJs, {
      // This is the way that we signal to the node worker that it is hosting
      // a pthread.
      "workerData": "em-pthread",
      // This is the way that we signal to the Web Worker that it is hosting
      // a pthread.
      "name": "em-pthread-" + PThread.nextWorkerID
    });
    worker.workerID = PThread.nextWorkerID++;
    PThread.unusedWorkers.push(worker);
  },
  getNewWorker() {
    if (PThread.unusedWorkers.length == 0) {
      // PTHREAD_POOL_SIZE_STRICT should show a warning and, if set to level `2`, return from the function.
      // However, if we're in Node.js, then we can create new workers on the fly and PTHREAD_POOL_SIZE_STRICT
      // should be ignored altogether.
      if (!ENVIRONMENT_IS_NODE) {
        err("Tried to spawn a new thread, but the thread pool is exhausted.\n" + "This might result in a deadlock unless some threads eventually exit or the code explicitly breaks out to the event loop.\n" + "If you want to increase the pool size, use setting `-sPTHREAD_POOL_SIZE=...`." + "\nIf you want to throw an explicit error instead of the risk of deadlocking in those cases, use setting `-sPTHREAD_POOL_SIZE_STRICT=2`.");
      }
      PThread.allocateUnusedWorker();
      PThread.loadWasmModuleToWorker(PThread.unusedWorkers[0]);
    }
    return PThread.unusedWorkers.pop();
  }
};

var _wasmWorkerDelayedMessageQueue = [];

var handleException = e => {
  // Certain exception types we do not treat as errors since they are used for
  // internal control flow.
  // 1. ExitStatus, which is thrown by exit()
  // 2. "unwind", which is thrown by emscripten_unwind_to_js_event_loop() and others
  //    that wish to return to JS event loop.
  if (e instanceof ExitStatus || e == "unwind") {
    return EXITSTATUS;
  }
  checkStackCookie();
  if (e instanceof WebAssembly.RuntimeError) {
    if (_emscripten_stack_get_current() <= 0) {
      err("Stack overflow detected.  You can try increasing -sSTACK_SIZE (currently set to 4194304)");
    }
  }
  quit_(1, e);
};

var maybeExit = () => {
  if (!keepRuntimeAlive()) {
    try {
      if (ENVIRONMENT_IS_PTHREAD) {
        // exit the current thread, but only if there is one active.
        // TODO(https://github.com/emscripten-core/emscripten/issues/25076):
        // Unify this check with the runtimeExited check above
        if (_pthread_self()) __emscripten_thread_exit(EXITSTATUS);
        return;
      }
      _exit(EXITSTATUS);
    } catch (e) {
      handleException(e);
    }
  }
};

var callUserCallback = func => {
  if (ABORT) {
    err("user callback triggered after runtime exited or application aborted.  Ignoring.");
    return;
  }
  try {
    func();
    maybeExit();
  } catch (e) {
    handleException(e);
  }
};

var wasmTableMirror = [];

var getWasmTableEntry = funcPtr => {
  var func = wasmTableMirror[funcPtr];
  if (!func) {
    /** @suppress {checkTypes} */ wasmTableMirror[funcPtr] = func = wasmTable.get(funcPtr);
  }
  /** @suppress {checkTypes} */ assert(wasmTable.get(funcPtr) == func, "JavaScript-side Wasm function table mirror is out of date!");
  return func;
};

var _wasmWorkerRunPostMessage = e => {
  // '_wsc' is short for 'wasm call', trying to use an identifier name that
  // will never conflict with user code
  let data = e.data;
  let wasmCall = data["_wsc"];
  wasmCall && callUserCallback(() => getWasmTableEntry(wasmCall)(...data["x"]));
};

var _wasmWorkerAppendToQueue = e => {
  _wasmWorkerDelayedMessageQueue.push(e);
};

var _wasmWorkerInitializeRuntime = () => {
  assert(wwParams);
  assert(wwParams.wwID);
  assert(wwParams.stackLowestAddress % 16 == 0);
  assert(wwParams.stackSize % 16 == 0);
  // Wasm workers basically never exit their runtime
  noExitRuntime = 1;
  // Run the C side Worker initialization for stack and TLS.
  __emscripten_wasm_worker_initialize(wwParams.stackLowestAddress, wwParams.stackSize);
  // Record the pthread configuration, and whether this Wasm Worker supports synchronous blocking in emscripten_futex_wait().
  // (regular Wasm Workers do, AudioWorklets don't)
  ___set_thread_state(/*thread_ptr=*/ 0, /*is_main_thread=*/ 0, /*is_runtime_thread=*/ 0, /*supports_wait=*/ !ENVIRONMENT_IS_AUDIO_WORKLET);
  // Write the stack cookie last, after we have set up the proper bounds and
  // current position of the stack.
  writeStackCookie();
  // Audio Worklets do not have postMessage()ing capabilities.
  if (!ENVIRONMENT_IS_AUDIO_WORKLET) {
    // The Wasm Worker runtime is now up, so we can start processing
    // any postMessage function calls that have been received. Drop the temp
    // message handler that queued any pending incoming postMessage function calls ...
    removeEventListener("message", _wasmWorkerAppendToQueue);
    // ... then flush whatever messages we may have already gotten in the queue,
    //     and clear _wasmWorkerDelayedMessageQueue to undefined ...
    _wasmWorkerDelayedMessageQueue = _wasmWorkerDelayedMessageQueue.forEach(_wasmWorkerRunPostMessage);
    // ... and finally register the proper postMessage handler that immediately
    // dispatches incoming function calls without queueing them.
    addEventListener("message", _wasmWorkerRunPostMessage);
  }
};

var onPostRuns = [];

var addOnPostRun = cb => onPostRuns.push(cb);

var dynCalls = {};

var dynCallLegacy = (sig, ptr, args) => {
  sig = sig.replace(/p/g, "i");
  assert(sig in dynCalls, `bad function pointer type - sig is not in dynCalls: '${sig}'`);
  if (args?.length) {
    // j (64-bit integer) is fine, and is implemented as a BigInt. Without
    // legalization, the number of parameters should match (j is not expanded
    // into two i's).
    assert(args.length === sig.length - 1);
  } else {
    assert(sig.length == 1);
  }
  var f = dynCalls[sig];
  return f(ptr, ...args);
};

var dynCall = (sig, ptr, args = [], promising = false) => {
  assert(ptr, `null function pointer in dynCall`);
  assert(!promising, "async dynCall is not supported in this mode");
  var rtn = dynCallLegacy(sig, ptr, args);
  function convert(rtn) {
    return rtn;
  }
  return convert(rtn);
};

function establishStackSpace(pthread_ptr) {
  var stackHigh = (growMemViews(), HEAPU32)[(((pthread_ptr) + (52)) >> 2)];
  var stackSize = (growMemViews(), HEAPU32)[(((pthread_ptr) + (56)) >> 2)];
  var stackLow = stackHigh - stackSize;
  assert(stackHigh != 0);
  assert(stackLow != 0);
  assert(stackHigh > stackLow, "stackHigh must be higher then stackLow");
  // Set stack limits used by `emscripten/stack.h` function.  These limits are
  // cached in wasm-side globals to make checks as fast as possible.
  _emscripten_stack_set_limits(stackHigh, stackLow);
  // Call inside wasm module to set up the stack frame for this pthread in wasm module scope
  stackRestore(stackHigh);
  // Write the stack cookie last, after we have set up the proper bounds and
  // current position of the stack.
  writeStackCookie();
}

/**
     * @param {number} ptr
     * @param {string} type
     */ function getValue(ptr, type = "i8") {
  if (type.endsWith("*")) type = "*";
  switch (type) {
   case "i1":
    return (growMemViews(), HEAP8)[ptr];

   case "i8":
    return (growMemViews(), HEAP8)[ptr];

   case "i16":
    return (growMemViews(), HEAP16)[((ptr) >> 1)];

   case "i32":
    return (growMemViews(), HEAP32)[((ptr) >> 2)];

   case "i64":
    return (growMemViews(), HEAP64)[((ptr) >> 3)];

   case "float":
    return (growMemViews(), HEAPF32)[((ptr) >> 2)];

   case "double":
    return (growMemViews(), HEAPF64)[((ptr) >> 3)];

   case "*":
    return (growMemViews(), HEAPU32)[((ptr) >> 2)];

   default:
    abort(`invalid type for getValue: ${type}`);
  }
}

var invokeEntryPoint = (ptr, arg) => {
  // An old thread on this worker may have been canceled without returning the
  // `runtimeKeepaliveCounter` to zero. Reset it now so the new thread won't
  // be affected.
  runtimeKeepaliveCounter = 0;
  // Same for noExitRuntime.  The default for pthreads should always be false
  // otherwise pthreads would never complete and attempts to pthread_join to
  // them would block forever.
  // pthreads can still choose to set `noExitRuntime` explicitly, or
  // call emscripten_unwind_to_js_event_loop to extend their lifetime beyond
  // their main function.  See comment in src/runtime_pthread.js for more.
  noExitRuntime = 0;
  // pthread entry points are always of signature 'void *ThreadMain(void *arg)'
  // Native codebases sometimes spawn threads with other thread entry point
  // signatures, such as void ThreadMain(void *arg), void *ThreadMain(), or
  // void ThreadMain().  That is not acceptable per C/C++ specification, but
  // x86 compiler ABI extensions enable that to work. If you find the
  // following line to crash, either change the signature to "proper" void
  // *ThreadMain(void *arg) form, or try linking with the Emscripten linker
  // flag -sEMULATE_FUNCTION_POINTER_CASTS to add in emulation for this x86
  // ABI extension.
  var result = (a1 => dynCall_ii(ptr, a1))(arg);
  checkStackCookie();
  function finish(result) {
    // In MINIMAL_RUNTIME the noExitRuntime concept does not apply to
    // pthreads. To exit a pthread with live runtime, use the function
    // emscripten_unwind_to_js_event_loop() in the pthread body.
    if (keepRuntimeAlive()) {
      EXITSTATUS = result;
      return;
    }
    __emscripten_thread_exit(result);
  }
  finish(result);
};

invokeEntryPoint.isAsync = true;

var noExitRuntime = true;

var registerTLSInit = tlsInitFunc => PThread.tlsInitFunctions.push(tlsInitFunc);

/**
     * @param {number} ptr
     * @param {number} value
     * @param {string} type
     */ function setValue(ptr, value, type = "i8") {
  if (type.endsWith("*")) type = "*";
  switch (type) {
   case "i1":
    (growMemViews(), HEAP8)[ptr] = value;
    break;

   case "i8":
    (growMemViews(), HEAP8)[ptr] = value;
    break;

   case "i16":
    (growMemViews(), HEAP16)[((ptr) >> 1)] = value;
    break;

   case "i32":
    (growMemViews(), HEAP32)[((ptr) >> 2)] = value;
    break;

   case "i64":
    (growMemViews(), HEAP64)[((ptr) >> 3)] = BigInt(value);
    break;

   case "float":
    (growMemViews(), HEAPF32)[((ptr) >> 2)] = value;
    break;

   case "double":
    (growMemViews(), HEAPF64)[((ptr) >> 3)] = value;
    break;

   case "*":
    (growMemViews(), HEAPU32)[((ptr) >> 2)] = value;
    break;

   default:
    abort(`invalid type for setValue: ${type}`);
  }
}

var warnOnce = text => {
  warnOnce.shown ||= {};
  if (!warnOnce.shown[text]) {
    warnOnce.shown[text] = 1;
    if (ENVIRONMENT_IS_NODE) text = "warning: " + text;
    err(text);
  }
};

var wasmMemory;

var UTF8Decoder = globalThis.TextDecoder && new TextDecoder;

var findStringEnd = (heapOrArray, idx, maxBytesToRead, ignoreNul) => {
  var maxIdx = idx + maxBytesToRead;
  if (ignoreNul) return maxIdx;
  // TextDecoder needs to know the byte length in advance, it doesn't stop on
  // null terminator by itself.
  // As a tiny code save trick, compare idx against maxIdx using a negation,
  // so that maxBytesToRead=undefined/NaN means Infinity.
  while (heapOrArray[idx] && !(idx >= maxIdx)) ++idx;
  return idx;
};

/**
     * Given a pointer 'idx' to a null-terminated UTF8-encoded string in the given
     * array that contains uint8 values, returns a copy of that string as a
     * Javascript String object.
     * heapOrArray is either a regular array, or a JavaScript typed array view.
     * @param {number=} idx
     * @param {number=} maxBytesToRead
     * @param {boolean=} ignoreNul - If true, the function will not stop on a NUL character.
     * @return {string}
     */ var UTF8ArrayToString = (heapOrArray, idx = 0, maxBytesToRead, ignoreNul) => {
  var endPtr = findStringEnd(heapOrArray, idx, maxBytesToRead, ignoreNul);
  // When using conditional TextDecoder, skip it for short strings as the overhead of the native call is not worth it.
  if (endPtr - idx > 16 && heapOrArray.buffer && UTF8Decoder) {
    return UTF8Decoder.decode(heapOrArray.buffer instanceof ArrayBuffer ? heapOrArray.subarray(idx, endPtr) : heapOrArray.slice(idx, endPtr));
  }
  var str = "";
  while (idx < endPtr) {
    // For UTF8 byte structure, see:
    // http://en.wikipedia.org/wiki/UTF-8#Description
    // https://www.ietf.org/rfc/rfc2279.txt
    // https://tools.ietf.org/html/rfc3629
    var u0 = heapOrArray[idx++];
    if (!(u0 & 128)) {
      str += String.fromCharCode(u0);
      continue;
    }
    var u1 = heapOrArray[idx++] & 63;
    if ((u0 & 224) == 192) {
      str += String.fromCharCode(((u0 & 31) << 6) | u1);
      continue;
    }
    var u2 = heapOrArray[idx++] & 63;
    if ((u0 & 240) == 224) {
      u0 = ((u0 & 15) << 12) | (u1 << 6) | u2;
    } else {
      if ((u0 & 248) != 240) warnOnce("Invalid UTF-8 leading byte " + ptrToString(u0) + " encountered when deserializing a UTF-8 string in wasm memory to a JS string!");
      u0 = ((u0 & 7) << 18) | (u1 << 12) | (u2 << 6) | (heapOrArray[idx++] & 63);
    }
    if (u0 < 65536) {
      str += String.fromCharCode(u0);
    } else {
      var ch = u0 - 65536;
      str += String.fromCharCode(55296 | (ch >> 10), 56320 | (ch & 1023));
    }
  }
  return str;
};

/**
     * Given a pointer 'ptr' to a null-terminated UTF8-encoded string in the
     * emscripten HEAP, returns a copy of that string as a Javascript String object.
     *
     * @param {number} ptr
     * @param {number=} maxBytesToRead - An optional length that specifies the
     *   maximum number of bytes to read. You can omit this parameter to scan the
     *   string until the first 0 byte. If maxBytesToRead is passed, and the string
     *   at [ptr, ptr+maxBytesToReadr[ contains a null byte in the middle, then the
     *   string will cut short at that byte index.
     * @param {boolean=} ignoreNul - If true, the function will not stop on a NUL character.
     * @return {string}
     */ var UTF8ToString = (ptr, maxBytesToRead, ignoreNul) => {
  assert(typeof ptr == "number", `UTF8ToString expects a number (got ${typeof ptr})`);
  return ptr ? UTF8ArrayToString((growMemViews(), HEAPU8), ptr, maxBytesToRead, ignoreNul) : "";
};

var ___assert_fail = (condition, filename, line, func) => abort(`Assertion failed: ${UTF8ToString(condition)}, at: ` + [ filename ? UTF8ToString(filename) : "unknown filename", line, func ? UTF8ToString(func) : "unknown function" ]);

class ExceptionInfo {
  // excPtr - Thrown object pointer to wrap. Metadata pointer is calculated from it.
  constructor(excPtr) {
    this.excPtr = excPtr;
    this.ptr = excPtr - 24;
  }
  set_type(type) {
    (growMemViews(), HEAPU32)[(((this.ptr) + (4)) >> 2)] = type;
  }
  get_type() {
    return (growMemViews(), HEAPU32)[(((this.ptr) + (4)) >> 2)];
  }
  set_destructor(destructor) {
    (growMemViews(), HEAPU32)[(((this.ptr) + (8)) >> 2)] = destructor;
  }
  get_destructor() {
    return (growMemViews(), HEAPU32)[(((this.ptr) + (8)) >> 2)];
  }
  set_caught(caught) {
    caught = caught ? 1 : 0;
    (growMemViews(), HEAP8)[(this.ptr) + (12)] = caught;
  }
  get_caught() {
    return (growMemViews(), HEAP8)[(this.ptr) + (12)] != 0;
  }
  set_rethrown(rethrown) {
    rethrown = rethrown ? 1 : 0;
    (growMemViews(), HEAP8)[(this.ptr) + (13)] = rethrown;
  }
  get_rethrown() {
    return (growMemViews(), HEAP8)[(this.ptr) + (13)] != 0;
  }
  // Initialize native structure fields. Should be called once after allocated.
  init(type, destructor) {
    this.set_adjusted_ptr(0);
    this.set_type(type);
    this.set_destructor(destructor);
  }
  set_adjusted_ptr(adjustedPtr) {
    (growMemViews(), HEAPU32)[(((this.ptr) + (16)) >> 2)] = adjustedPtr;
  }
  get_adjusted_ptr() {
    return (growMemViews(), HEAPU32)[(((this.ptr) + (16)) >> 2)];
  }
}

var exceptionLast = 0;

var uncaughtExceptionCount = 0;

var ___cxa_throw = (ptr, type, destructor) => {
  var info = new ExceptionInfo(ptr);
  // Initialize ExceptionInfo content after it was allocated in __cxa_allocate_exception.
  info.init(type, destructor);
  exceptionLast = ptr;
  uncaughtExceptionCount++;
  assert(false, "Exception thrown, but exception catching is not enabled. Compile with -sNO_DISABLE_EXCEPTION_CATCHING or -sEXCEPTION_CATCHING_ALLOWED=[..] to catch.");
};

function pthreadCreateProxied(pthread_ptr, attr, startRoutine, arg) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(2, 0, 1, pthread_ptr, attr, startRoutine, arg);
  return ___pthread_create_js(pthread_ptr, attr, startRoutine, arg);
}

var _emscripten_has_threading_support = () => !!globalThis.SharedArrayBuffer;

var ___pthread_create_js = (pthread_ptr, attr, startRoutine, arg) => {
  if (!_emscripten_has_threading_support()) {
    dbg("pthread_create: environment does not support SharedArrayBuffer, pthreads are not available");
    return 6;
  }
  // List of JS objects that will transfer ownership to the Worker hosting the thread
  var transferList = [];
  var error = 0;
  // Synchronously proxy the thread creation to main thread if possible. If we
  // need to transfer ownership of objects, then proxy asynchronously via
  // postMessage.
  if (ENVIRONMENT_IS_PTHREAD && (transferList.length === 0 || error)) {
    return pthreadCreateProxied(pthread_ptr, attr, startRoutine, arg);
  }
  // If on the main thread, and accessing Canvas/OffscreenCanvas failed, abort
  // with the detected error.
  if (error) return error;
  var threadParams = {
    startRoutine,
    pthread_ptr,
    arg,
    transferList
  };
  if (ENVIRONMENT_IS_PTHREAD) {
    // The prepopulated pool of web workers that can host pthreads is stored
    // in the main JS thread. Therefore if a pthread is attempting to spawn a
    // new thread, the thread creation must be deferred to the main JS thread.
    threadParams.cmd = "spawnThread";
    postMessage(threadParams, transferList);
    // When we defer thread creation this way, we have no way to detect thread
    // creation synchronously today, so we have to assume success and return 0.
    return 0;
  }
  // We are the main thread, so we have the pthread warmup pool in this
  // thread and can fire off JS thread creation directly ourselves.
  return spawnThread(threadParams);
};

var syscallGetVarargI = () => {
  assert(SYSCALLS.varargs != undefined);
  // the `+` prepended here is necessary to convince the JSCompiler that varargs is indeed a number.
  var ret = (growMemViews(), HEAP32)[((+SYSCALLS.varargs) >> 2)];
  SYSCALLS.varargs += 4;
  return ret;
};

var syscallGetVarargP = syscallGetVarargI;

var PATH = {
  isAbs: path => path.charAt(0) === "/",
  splitPath: filename => {
    var splitPathRe = /^(\/?|)([\s\S]*?)((?:\.{1,2}|[^\/]+?|)(\.[^.\/]*|))(?:[\/]*)$/;
    return splitPathRe.exec(filename).slice(1);
  },
  normalizeArray: (parts, allowAboveRoot) => {
    // if the path tries to go above the root, `up` ends up > 0
    var up = 0;
    for (var i = parts.length - 1; i >= 0; i--) {
      var last = parts[i];
      if (last === ".") {
        parts.splice(i, 1);
      } else if (last === "..") {
        parts.splice(i, 1);
        up++;
      } else if (up) {
        parts.splice(i, 1);
        up--;
      }
    }
    // if the path is allowed to go above the root, restore leading ..s
    if (allowAboveRoot) {
      for (;up; up--) {
        parts.unshift("..");
      }
    }
    return parts;
  },
  normalize: path => {
    var isAbsolute = PATH.isAbs(path), trailingSlash = path.slice(-1) === "/";
    // Normalize the path
    path = PATH.normalizeArray(path.split("/").filter(p => !!p), !isAbsolute).join("/");
    if (!path && !isAbsolute) {
      path = ".";
    }
    if (path && trailingSlash) {
      path += "/";
    }
    return (isAbsolute ? "/" : "") + path;
  },
  dirname: path => {
    var result = PATH.splitPath(path), root = result[0], dir = result[1];
    if (!root && !dir) {
      // No dirname whatsoever
      return ".";
    }
    if (dir) {
      // It has a dirname, strip trailing slash
      dir = dir.slice(0, -1);
    }
    return root + dir;
  },
  basename: path => path && path.match(/([^\/]+|\/)\/*$/)[1],
  join: (...paths) => PATH.normalize(paths.join("/")),
  join2: (l, r) => PATH.normalize(l + "/" + r)
};

var initRandomFill = () => {
  // This block is not needed on v19+ since crypto.getRandomValues is builtin
  if (ENVIRONMENT_IS_NODE) {
    var nodeCrypto = require("crypto");
    return view => nodeCrypto.randomFillSync(view);
  }
  // like with most Web APIs, we can't use Web Crypto API directly on shared memory,
  // so we need to create an intermediate buffer and copy it to the destination
  return view => view.set(crypto.getRandomValues(new Uint8Array(view.byteLength)));
};

var randomFill = view => {
  // Lazily init on the first invocation.
  (randomFill = initRandomFill())(view);
};

var PATH_FS = {
  resolve: (...args) => {
    var resolvedPath = "", resolvedAbsolute = false;
    for (var i = args.length - 1; i >= -1 && !resolvedAbsolute; i--) {
      var path = (i >= 0) ? args[i] : FS.cwd();
      // Skip empty and invalid entries
      if (typeof path != "string") {
        throw new TypeError("Arguments to path.resolve must be strings");
      } else if (!path) {
        return "";
      }
      resolvedPath = path + "/" + resolvedPath;
      resolvedAbsolute = PATH.isAbs(path);
    }
    // At this point the path should be resolved to a full absolute path, but
    // handle relative paths to be safe (might happen when process.cwd() fails)
    resolvedPath = PATH.normalizeArray(resolvedPath.split("/").filter(p => !!p), !resolvedAbsolute).join("/");
    return ((resolvedAbsolute ? "/" : "") + resolvedPath) || ".";
  },
  relative: (from, to) => {
    from = PATH_FS.resolve(from).slice(1);
    to = PATH_FS.resolve(to).slice(1);
    function trim(arr) {
      var start = 0;
      for (;start < arr.length; start++) {
        if (arr[start] !== "") break;
      }
      var end = arr.length - 1;
      for (;end >= 0; end--) {
        if (arr[end] !== "") break;
      }
      if (start > end) return [];
      return arr.slice(start, end - start + 1);
    }
    var fromParts = trim(from.split("/"));
    var toParts = trim(to.split("/"));
    var length = Math.min(fromParts.length, toParts.length);
    var samePartsLength = length;
    for (var i = 0; i < length; i++) {
      if (fromParts[i] !== toParts[i]) {
        samePartsLength = i;
        break;
      }
    }
    var outputParts = [];
    for (var i = samePartsLength; i < fromParts.length; i++) {
      outputParts.push("..");
    }
    outputParts = outputParts.concat(toParts.slice(samePartsLength));
    return outputParts.join("/");
  }
};

var FS_stdin_getChar_buffer = [];

var lengthBytesUTF8 = str => {
  var len = 0;
  for (var i = 0; i < str.length; ++i) {
    // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code
    // unit, not a Unicode code point of the character! So decode
    // UTF16->UTF32->UTF8.
    // See http://unicode.org/faq/utf_bom.html#utf16-3
    var c = str.charCodeAt(i);
    // possibly a lead surrogate
    if (c <= 127) {
      len++;
    } else if (c <= 2047) {
      len += 2;
    } else if (c >= 55296 && c <= 57343) {
      len += 4;
      ++i;
    } else {
      len += 3;
    }
  }
  return len;
};

var stringToUTF8Array = (str, heap, outIdx, maxBytesToWrite) => {
  assert(typeof str === "string", `stringToUTF8Array expects a string (got ${typeof str})`);
  // Parameter maxBytesToWrite is not optional. Negative values, 0, null,
  // undefined and false each don't write out any bytes.
  if (!(maxBytesToWrite > 0)) return 0;
  var startIdx = outIdx;
  var endIdx = outIdx + maxBytesToWrite - 1;
  // -1 for string null terminator.
  for (var i = 0; i < str.length; ++i) {
    // For UTF8 byte structure, see http://en.wikipedia.org/wiki/UTF-8#Description
    // and https://www.ietf.org/rfc/rfc2279.txt
    // and https://tools.ietf.org/html/rfc3629
    var u = str.codePointAt(i);
    if (u <= 127) {
      if (outIdx >= endIdx) break;
      heap[outIdx++] = u;
    } else if (u <= 2047) {
      if (outIdx + 1 >= endIdx) break;
      heap[outIdx++] = 192 | (u >> 6);
      heap[outIdx++] = 128 | (u & 63);
    } else if (u <= 65535) {
      if (outIdx + 2 >= endIdx) break;
      heap[outIdx++] = 224 | (u >> 12);
      heap[outIdx++] = 128 | ((u >> 6) & 63);
      heap[outIdx++] = 128 | (u & 63);
    } else {
      if (outIdx + 3 >= endIdx) break;
      if (u > 1114111) warnOnce("Invalid Unicode code point " + ptrToString(u) + " encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF).");
      heap[outIdx++] = 240 | (u >> 18);
      heap[outIdx++] = 128 | ((u >> 12) & 63);
      heap[outIdx++] = 128 | ((u >> 6) & 63);
      heap[outIdx++] = 128 | (u & 63);
      // Gotcha: if codePoint is over 0xFFFF, it is represented as a surrogate pair in UTF-16.
      // We need to manually skip over the second code unit for correct iteration.
      i++;
    }
  }
  // Null-terminate the pointer to the buffer.
  heap[outIdx] = 0;
  return outIdx - startIdx;
};

/** @type {function(string, boolean=, number=)} */ var intArrayFromString = (stringy, dontAddNull, length) => {
  var len = length > 0 ? length : lengthBytesUTF8(stringy) + 1;
  var u8array = new Array(len);
  var numBytesWritten = stringToUTF8Array(stringy, u8array, 0, u8array.length);
  if (dontAddNull) u8array.length = numBytesWritten;
  return u8array;
};

var FS_stdin_getChar = () => {
  if (!FS_stdin_getChar_buffer.length) {
    var result = null;
    if (ENVIRONMENT_IS_NODE) {
      // we will read data by chunks of BUFSIZE
      var BUFSIZE = 256;
      var buf = Buffer.alloc(BUFSIZE);
      var bytesRead = 0;
      // For some reason we must suppress a closure warning here, even though
      // fd definitely exists on process.stdin, and is even the proper way to
      // get the fd of stdin,
      // https://github.com/nodejs/help/issues/2136#issuecomment-523649904
      // This started to happen after moving this logic out of library_tty.js,
      // so it is related to the surrounding code in some unclear manner.
      /** @suppress {missingProperties} */ var fd = process.stdin.fd;
      try {
        bytesRead = fs.readSync(fd, buf, 0, BUFSIZE);
      } catch (e) {
        // Cross-platform differences: on Windows, reading EOF throws an
        // exception, but on other OSes, reading EOF returns 0. Uniformize
        // behavior by treating the EOF exception to return 0.
        if (e.toString().includes("EOF")) bytesRead = 0; else throw e;
      }
      if (bytesRead > 0) {
        result = buf.slice(0, bytesRead).toString("utf-8");
      }
    } else if (globalThis.window?.prompt) {
      // Browser.
      result = window.prompt("Input: ");
      // returns null on cancel
      if (result !== null) {
        result += "\n";
      }
    } else {}
    if (!result) {
      return null;
    }
    FS_stdin_getChar_buffer = intArrayFromString(result, true);
  }
  return FS_stdin_getChar_buffer.shift();
};

var TTY = {
  ttys: [],
  init() {},
  shutdown() {},
  register(dev, ops) {
    TTY.ttys[dev] = {
      input: [],
      output: [],
      ops
    };
    FS.registerDevice(dev, TTY.stream_ops);
  },
  stream_ops: {
    open(stream) {
      var tty = TTY.ttys[stream.node.rdev];
      if (!tty) {
        throw new FS.ErrnoError(43);
      }
      stream.tty = tty;
      stream.seekable = false;
    },
    close(stream) {
      // flush any pending line data
      stream.tty.ops.fsync(stream.tty);
    },
    fsync(stream) {
      stream.tty.ops.fsync(stream.tty);
    },
    read(stream, buffer, offset, length, pos) {
      if (!stream.tty || !stream.tty.ops.get_char) {
        throw new FS.ErrnoError(60);
      }
      var bytesRead = 0;
      for (var i = 0; i < length; i++) {
        var result;
        try {
          result = stream.tty.ops.get_char(stream.tty);
        } catch (e) {
          throw new FS.ErrnoError(29);
        }
        if (result === undefined && bytesRead === 0) {
          throw new FS.ErrnoError(6);
        }
        if (result === null || result === undefined) break;
        bytesRead++;
        buffer[offset + i] = result;
      }
      if (bytesRead) {
        stream.node.atime = Date.now();
      }
      return bytesRead;
    },
    write(stream, buffer, offset, length, pos) {
      if (!stream.tty || !stream.tty.ops.put_char) {
        throw new FS.ErrnoError(60);
      }
      try {
        for (var i = 0; i < length; i++) {
          stream.tty.ops.put_char(stream.tty, buffer[offset + i]);
        }
      } catch (e) {
        throw new FS.ErrnoError(29);
      }
      if (length) {
        stream.node.mtime = stream.node.ctime = Date.now();
      }
      return i;
    }
  },
  default_tty_ops: {
    get_char(tty) {
      return FS_stdin_getChar();
    },
    put_char(tty, val) {
      if (val === null || val === 10) {
        out(UTF8ArrayToString(tty.output));
        tty.output = [];
      } else {
        if (val != 0) tty.output.push(val);
      }
    },
    fsync(tty) {
      if (tty.output?.length > 0) {
        out(UTF8ArrayToString(tty.output));
        tty.output = [];
      }
    },
    ioctl_tcgets(tty) {
      // typical setting
      return {
        c_iflag: 25856,
        c_oflag: 5,
        c_cflag: 191,
        c_lflag: 35387,
        c_cc: [ 3, 28, 127, 21, 4, 0, 1, 0, 17, 19, 26, 0, 18, 15, 23, 22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ]
      };
    },
    ioctl_tcsets(tty, optional_actions, data) {
      // currently just ignore
      return 0;
    },
    ioctl_tiocgwinsz(tty) {
      return [ 24, 80 ];
    }
  },
  default_tty1_ops: {
    put_char(tty, val) {
      if (val === null || val === 10) {
        err(UTF8ArrayToString(tty.output));
        tty.output = [];
      } else {
        if (val != 0) tty.output.push(val);
      }
    },
    fsync(tty) {
      if (tty.output?.length > 0) {
        err(UTF8ArrayToString(tty.output));
        tty.output = [];
      }
    }
  }
};

var mmapAlloc = size => {
  abort("internal error: mmapAlloc called but `emscripten_builtin_memalign` native symbol not exported");
};

var MEMFS = {
  ops_table: null,
  mount(mount) {
    return MEMFS.createNode(null, "/", 16895, 0);
  },
  createNode(parent, name, mode, dev) {
    if (FS.isBlkdev(mode) || FS.isFIFO(mode)) {
      // no supported
      throw new FS.ErrnoError(63);
    }
    MEMFS.ops_table ||= {
      dir: {
        node: {
          getattr: MEMFS.node_ops.getattr,
          setattr: MEMFS.node_ops.setattr,
          lookup: MEMFS.node_ops.lookup,
          mknod: MEMFS.node_ops.mknod,
          rename: MEMFS.node_ops.rename,
          unlink: MEMFS.node_ops.unlink,
          rmdir: MEMFS.node_ops.rmdir,
          readdir: MEMFS.node_ops.readdir,
          symlink: MEMFS.node_ops.symlink
        },
        stream: {
          llseek: MEMFS.stream_ops.llseek
        }
      },
      file: {
        node: {
          getattr: MEMFS.node_ops.getattr,
          setattr: MEMFS.node_ops.setattr
        },
        stream: {
          llseek: MEMFS.stream_ops.llseek,
          read: MEMFS.stream_ops.read,
          write: MEMFS.stream_ops.write,
          mmap: MEMFS.stream_ops.mmap,
          msync: MEMFS.stream_ops.msync
        }
      },
      link: {
        node: {
          getattr: MEMFS.node_ops.getattr,
          setattr: MEMFS.node_ops.setattr,
          readlink: MEMFS.node_ops.readlink
        },
        stream: {}
      },
      chrdev: {
        node: {
          getattr: MEMFS.node_ops.getattr,
          setattr: MEMFS.node_ops.setattr
        },
        stream: FS.chrdev_stream_ops
      }
    };
    var node = FS.createNode(parent, name, mode, dev);
    if (FS.isDir(node.mode)) {
      node.node_ops = MEMFS.ops_table.dir.node;
      node.stream_ops = MEMFS.ops_table.dir.stream;
      node.contents = {};
    } else if (FS.isFile(node.mode)) {
      node.node_ops = MEMFS.ops_table.file.node;
      node.stream_ops = MEMFS.ops_table.file.stream;
      node.usedBytes = 0;
      // The actual number of bytes used in the typed array, as opposed to contents.length which gives the whole capacity.
      // When the byte data of the file is populated, this will point to either a typed array, or a normal JS array. Typed arrays are preferred
      // for performance, and used by default. However, typed arrays are not resizable like normal JS arrays are, so there is a small disk size
      // penalty involved for appending file writes that continuously grow a file similar to std::vector capacity vs used -scheme.
      node.contents = null;
    } else if (FS.isLink(node.mode)) {
      node.node_ops = MEMFS.ops_table.link.node;
      node.stream_ops = MEMFS.ops_table.link.stream;
    } else if (FS.isChrdev(node.mode)) {
      node.node_ops = MEMFS.ops_table.chrdev.node;
      node.stream_ops = MEMFS.ops_table.chrdev.stream;
    }
    node.atime = node.mtime = node.ctime = Date.now();
    // add the new node to the parent
    if (parent) {
      parent.contents[name] = node;
      parent.atime = parent.mtime = parent.ctime = node.atime;
    }
    return node;
  },
  getFileDataAsTypedArray(node) {
    if (!node.contents) return new Uint8Array(0);
    if (node.contents.subarray) return node.contents.subarray(0, node.usedBytes);
    // Make sure to not return excess unused bytes.
    return new Uint8Array(node.contents);
  },
  expandFileStorage(node, newCapacity) {
    var prevCapacity = node.contents ? node.contents.length : 0;
    if (prevCapacity >= newCapacity) return;
    // No need to expand, the storage was already large enough.
    // Don't expand strictly to the given requested limit if it's only a very small increase, but instead geometrically grow capacity.
    // For small filesizes (<1MB), perform size*2 geometric increase, but for large sizes, do a much more conservative size*1.125 increase to
    // avoid overshooting the allocation cap by a very large margin.
    var CAPACITY_DOUBLING_MAX = 1024 * 1024;
    newCapacity = Math.max(newCapacity, (prevCapacity * (prevCapacity < CAPACITY_DOUBLING_MAX ? 2 : 1.125)) >>> 0);
    if (prevCapacity != 0) newCapacity = Math.max(newCapacity, 256);
    // At minimum allocate 256b for each file when expanding.
    var oldContents = node.contents;
    node.contents = new Uint8Array(newCapacity);
    // Allocate new storage.
    if (node.usedBytes > 0) node.contents.set(oldContents.subarray(0, node.usedBytes), 0);
  },
  resizeFileStorage(node, newSize) {
    if (node.usedBytes == newSize) return;
    if (newSize == 0) {
      node.contents = null;
      // Fully decommit when requesting a resize to zero.
      node.usedBytes = 0;
    } else {
      var oldContents = node.contents;
      node.contents = new Uint8Array(newSize);
      // Allocate new storage.
      if (oldContents) {
        node.contents.set(oldContents.subarray(0, Math.min(newSize, node.usedBytes)));
      }
      node.usedBytes = newSize;
    }
  },
  node_ops: {
    getattr(node) {
      var attr = {};
      // device numbers reuse inode numbers.
      attr.dev = FS.isChrdev(node.mode) ? node.id : 1;
      attr.ino = node.id;
      attr.mode = node.mode;
      attr.nlink = 1;
      attr.uid = 0;
      attr.gid = 0;
      attr.rdev = node.rdev;
      if (FS.isDir(node.mode)) {
        attr.size = 4096;
      } else if (FS.isFile(node.mode)) {
        attr.size = node.usedBytes;
      } else if (FS.isLink(node.mode)) {
        attr.size = node.link.length;
      } else {
        attr.size = 0;
      }
      attr.atime = new Date(node.atime);
      attr.mtime = new Date(node.mtime);
      attr.ctime = new Date(node.ctime);
      // NOTE: In our implementation, st_blocks = Math.ceil(st_size/st_blksize),
      //       but this is not required by the standard.
      attr.blksize = 4096;
      attr.blocks = Math.ceil(attr.size / attr.blksize);
      return attr;
    },
    setattr(node, attr) {
      for (const key of [ "mode", "atime", "mtime", "ctime" ]) {
        if (attr[key] != null) {
          node[key] = attr[key];
        }
      }
      if (attr.size !== undefined) {
        MEMFS.resizeFileStorage(node, attr.size);
      }
    },
    lookup(parent, name) {
      throw new FS.ErrnoError(44);
    },
    mknod(parent, name, mode, dev) {
      return MEMFS.createNode(parent, name, mode, dev);
    },
    rename(old_node, new_dir, new_name) {
      var new_node;
      try {
        new_node = FS.lookupNode(new_dir, new_name);
      } catch (e) {}
      if (new_node) {
        if (FS.isDir(old_node.mode)) {
          // if we're overwriting a directory at new_name, make sure it's empty.
          for (var i in new_node.contents) {
            throw new FS.ErrnoError(55);
          }
        }
        FS.hashRemoveNode(new_node);
      }
      // do the internal rewiring
      delete old_node.parent.contents[old_node.name];
      new_dir.contents[new_name] = old_node;
      old_node.name = new_name;
      new_dir.ctime = new_dir.mtime = old_node.parent.ctime = old_node.parent.mtime = Date.now();
    },
    unlink(parent, name) {
      delete parent.contents[name];
      parent.ctime = parent.mtime = Date.now();
    },
    rmdir(parent, name) {
      var node = FS.lookupNode(parent, name);
      for (var i in node.contents) {
        throw new FS.ErrnoError(55);
      }
      delete parent.contents[name];
      parent.ctime = parent.mtime = Date.now();
    },
    readdir(node) {
      return [ ".", "..", ...Object.keys(node.contents) ];
    },
    symlink(parent, newname, oldpath) {
      var node = MEMFS.createNode(parent, newname, 511 | 40960, 0);
      node.link = oldpath;
      return node;
    },
    readlink(node) {
      if (!FS.isLink(node.mode)) {
        throw new FS.ErrnoError(28);
      }
      return node.link;
    }
  },
  stream_ops: {
    read(stream, buffer, offset, length, position) {
      var contents = stream.node.contents;
      if (position >= stream.node.usedBytes) return 0;
      var size = Math.min(stream.node.usedBytes - position, length);
      assert(size >= 0);
      if (size > 8 && contents.subarray) {
        // non-trivial, and typed array
        buffer.set(contents.subarray(position, position + size), offset);
      } else {
        for (var i = 0; i < size; i++) buffer[offset + i] = contents[position + i];
      }
      return size;
    },
    write(stream, buffer, offset, length, position, canOwn) {
      // The data buffer should be a typed array view
      assert(!(buffer instanceof ArrayBuffer));
      // If the buffer is located in main memory (HEAP), and if
      // memory can grow, we can't hold on to references of the
      // memory buffer, as they may get invalidated. That means we
      // need to do copy its contents.
      if (buffer.buffer === (growMemViews(), HEAP8).buffer) {
        canOwn = false;
      }
      if (!length) return 0;
      var node = stream.node;
      node.mtime = node.ctime = Date.now();
      if (buffer.subarray && (!node.contents || node.contents.subarray)) {
        // This write is from a typed array to a typed array?
        if (canOwn) {
          assert(position === 0, "canOwn must imply no weird position inside the file");
          node.contents = buffer.subarray(offset, offset + length);
          node.usedBytes = length;
          return length;
        } else if (node.usedBytes === 0 && position === 0) {
          // If this is a simple first write to an empty file, do a fast set since we don't need to care about old data.
          node.contents = buffer.slice(offset, offset + length);
          node.usedBytes = length;
          return length;
        } else if (position + length <= node.usedBytes) {
          // Writing to an already allocated and used subrange of the file?
          node.contents.set(buffer.subarray(offset, offset + length), position);
          return length;
        }
      }
      // Appending to an existing file and we need to reallocate, or source data did not come as a typed array.
      MEMFS.expandFileStorage(node, position + length);
      if (node.contents.subarray && buffer.subarray) {
        // Use typed array write which is available.
        node.contents.set(buffer.subarray(offset, offset + length), position);
      } else {
        for (var i = 0; i < length; i++) {
          node.contents[position + i] = buffer[offset + i];
        }
      }
      node.usedBytes = Math.max(node.usedBytes, position + length);
      return length;
    },
    llseek(stream, offset, whence) {
      var position = offset;
      if (whence === 1) {
        position += stream.position;
      } else if (whence === 2) {
        if (FS.isFile(stream.node.mode)) {
          position += stream.node.usedBytes;
        }
      }
      if (position < 0) {
        throw new FS.ErrnoError(28);
      }
      return position;
    },
    mmap(stream, length, position, prot, flags) {
      if (!FS.isFile(stream.node.mode)) {
        throw new FS.ErrnoError(43);
      }
      var ptr;
      var allocated;
      var contents = stream.node.contents;
      // Only make a new copy when MAP_PRIVATE is specified.
      if (!(flags & 2) && contents && contents.buffer === (growMemViews(), HEAP8).buffer) {
        // We can't emulate MAP_SHARED when the file is not backed by the
        // buffer we're mapping to (e.g. the HEAP buffer).
        allocated = false;
        ptr = contents.byteOffset;
      } else {
        allocated = true;
        ptr = mmapAlloc(length);
        if (!ptr) {
          throw new FS.ErrnoError(48);
        }
        if (contents) {
          // Try to avoid unnecessary slices.
          if (position > 0 || position + length < contents.length) {
            if (contents.subarray) {
              contents = contents.subarray(position, position + length);
            } else {
              contents = Array.prototype.slice.call(contents, position, position + length);
            }
          }
          (growMemViews(), HEAP8).set(contents, ptr);
        }
      }
      return {
        ptr,
        allocated
      };
    },
    msync(stream, buffer, offset, length, mmapFlags) {
      MEMFS.stream_ops.write(stream, buffer, 0, length, offset, false);
      // should we check if bytesWritten and length are the same?
      return 0;
    }
  }
};

var FS_modeStringToFlags = str => {
  var flagModes = {
    "r": 0,
    "r+": 2,
    "w": 512 | 64 | 1,
    "w+": 512 | 64 | 2,
    "a": 1024 | 64 | 1,
    "a+": 1024 | 64 | 2
  };
  var flags = flagModes[str];
  if (typeof flags == "undefined") {
    throw new Error(`Unknown file open mode: ${str}`);
  }
  return flags;
};

var FS_getMode = (canRead, canWrite) => {
  var mode = 0;
  if (canRead) mode |= 292 | 73;
  if (canWrite) mode |= 146;
  return mode;
};

var strError = errno => UTF8ToString(_strerror(errno));

var ERRNO_CODES = {
  "EPERM": 63,
  "ENOENT": 44,
  "ESRCH": 71,
  "EINTR": 27,
  "EIO": 29,
  "ENXIO": 60,
  "E2BIG": 1,
  "ENOEXEC": 45,
  "EBADF": 8,
  "ECHILD": 12,
  "EAGAIN": 6,
  "EWOULDBLOCK": 6,
  "ENOMEM": 48,
  "EACCES": 2,
  "EFAULT": 21,
  "ENOTBLK": 105,
  "EBUSY": 10,
  "EEXIST": 20,
  "EXDEV": 75,
  "ENODEV": 43,
  "ENOTDIR": 54,
  "EISDIR": 31,
  "EINVAL": 28,
  "ENFILE": 41,
  "EMFILE": 33,
  "ENOTTY": 59,
  "ETXTBSY": 74,
  "EFBIG": 22,
  "ENOSPC": 51,
  "ESPIPE": 70,
  "EROFS": 69,
  "EMLINK": 34,
  "EPIPE": 64,
  "EDOM": 18,
  "ERANGE": 68,
  "ENOMSG": 49,
  "EIDRM": 24,
  "ECHRNG": 106,
  "EL2NSYNC": 156,
  "EL3HLT": 107,
  "EL3RST": 108,
  "ELNRNG": 109,
  "EUNATCH": 110,
  "ENOCSI": 111,
  "EL2HLT": 112,
  "EDEADLK": 16,
  "ENOLCK": 46,
  "EBADE": 113,
  "EBADR": 114,
  "EXFULL": 115,
  "ENOANO": 104,
  "EBADRQC": 103,
  "EBADSLT": 102,
  "EDEADLOCK": 16,
  "EBFONT": 101,
  "ENOSTR": 100,
  "ENODATA": 116,
  "ETIME": 117,
  "ENOSR": 118,
  "ENONET": 119,
  "ENOPKG": 120,
  "EREMOTE": 121,
  "ENOLINK": 47,
  "EADV": 122,
  "ESRMNT": 123,
  "ECOMM": 124,
  "EPROTO": 65,
  "EMULTIHOP": 36,
  "EDOTDOT": 125,
  "EBADMSG": 9,
  "ENOTUNIQ": 126,
  "EBADFD": 127,
  "EREMCHG": 128,
  "ELIBACC": 129,
  "ELIBBAD": 130,
  "ELIBSCN": 131,
  "ELIBMAX": 132,
  "ELIBEXEC": 133,
  "ENOSYS": 52,
  "ENOTEMPTY": 55,
  "ENAMETOOLONG": 37,
  "ELOOP": 32,
  "EOPNOTSUPP": 138,
  "EPFNOSUPPORT": 139,
  "ECONNRESET": 15,
  "ENOBUFS": 42,
  "EAFNOSUPPORT": 5,
  "EPROTOTYPE": 67,
  "ENOTSOCK": 57,
  "ENOPROTOOPT": 50,
  "ESHUTDOWN": 140,
  "ECONNREFUSED": 14,
  "EADDRINUSE": 3,
  "ECONNABORTED": 13,
  "ENETUNREACH": 40,
  "ENETDOWN": 38,
  "ETIMEDOUT": 73,
  "EHOSTDOWN": 142,
  "EHOSTUNREACH": 23,
  "EINPROGRESS": 26,
  "EALREADY": 7,
  "EDESTADDRREQ": 17,
  "EMSGSIZE": 35,
  "EPROTONOSUPPORT": 66,
  "ESOCKTNOSUPPORT": 137,
  "EADDRNOTAVAIL": 4,
  "ENETRESET": 39,
  "EISCONN": 30,
  "ENOTCONN": 53,
  "ETOOMANYREFS": 141,
  "EUSERS": 136,
  "EDQUOT": 19,
  "ESTALE": 72,
  "ENOTSUP": 138,
  "ENOMEDIUM": 148,
  "EILSEQ": 25,
  "EOVERFLOW": 61,
  "ECANCELED": 11,
  "ENOTRECOVERABLE": 56,
  "EOWNERDEAD": 62,
  "ESTRPIPE": 135
};

var asyncLoad = async url => {
  var arrayBuffer = await readAsync(url);
  assert(arrayBuffer, `Loading data file "${url}" failed (no arrayBuffer).`);
  return new Uint8Array(arrayBuffer);
};

var FS_createDataFile = (...args) => FS.createDataFile(...args);

var getUniqueRunDependency = id => {
  var orig = id;
  while (1) {
    if (!runDependencyTracking[id]) return id;
    id = orig + Math.random();
  }
};

var preloadPlugins = [];

var FS_handledByPreloadPlugin = async (byteArray, fullname) => {
  // Ensure plugins are ready.
  if (typeof Browser != "undefined") Browser.init();
  for (var plugin of preloadPlugins) {
    if (plugin["canHandle"](fullname)) {
      assert(plugin["handle"].constructor.name === "AsyncFunction", "Filesystem plugin handlers must be async functions (See #24914)");
      return plugin["handle"](byteArray, fullname);
    }
  }
  // In no plugin handled this file then return the original/unmodified
  // byteArray.
  return byteArray;
};

var FS_preloadFile = async (parent, name, url, canRead, canWrite, dontCreateFile, canOwn, preFinish) => {
  // TODO we should allow people to just pass in a complete filename instead
  // of parent and name being that we just join them anyways
  var fullname = name ? PATH_FS.resolve(PATH.join2(parent, name)) : parent;
  var dep = getUniqueRunDependency(`cp ${fullname}`);
  // might have several active requests for the same fullname
  addRunDependency(dep);
  try {
    var byteArray = url;
    if (typeof url == "string") {
      byteArray = await asyncLoad(url);
    }
    byteArray = await FS_handledByPreloadPlugin(byteArray, fullname);
    preFinish?.();
    if (!dontCreateFile) {
      FS_createDataFile(parent, name, byteArray, canRead, canWrite, canOwn);
    }
  } finally {
    removeRunDependency(dep);
  }
};

var FS_createPreloadedFile = (parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish) => {
  FS_preloadFile(parent, name, url, canRead, canWrite, dontCreateFile, canOwn, preFinish).then(onload).catch(onerror);
};

var FS = {
  root: null,
  mounts: [],
  devices: {},
  streams: [],
  nextInode: 1,
  nameTable: null,
  currentPath: "/",
  initialized: false,
  ignorePermissions: true,
  filesystems: null,
  syncFSRequests: 0,
  readFiles: {},
  ErrnoError: class extends Error {
    name="ErrnoError";
    // We set the `name` property to be able to identify `FS.ErrnoError`
    // - the `name` is a standard ECMA-262 property of error objects. Kind of good to have it anyway.
    // - when using PROXYFS, an error can come from an underlying FS
    // as different FS objects have their own FS.ErrnoError each,
    // the test `err instanceof FS.ErrnoError` won't detect an error coming from another filesystem, causing bugs.
    // we'll use the reliable test `err.name == "ErrnoError"` instead
    constructor(errno) {
      super(runtimeInitialized ? strError(errno) : "");
      this.errno = errno;
      for (var key in ERRNO_CODES) {
        if (ERRNO_CODES[key] === errno) {
          this.code = key;
          break;
        }
      }
    }
  },
  FSStream: class {
    shared={};
    get object() {
      return this.node;
    }
    set object(val) {
      this.node = val;
    }
    get isRead() {
      return (this.flags & 2097155) !== 1;
    }
    get isWrite() {
      return (this.flags & 2097155) !== 0;
    }
    get isAppend() {
      return (this.flags & 1024);
    }
    get flags() {
      return this.shared.flags;
    }
    set flags(val) {
      this.shared.flags = val;
    }
    get position() {
      return this.shared.position;
    }
    set position(val) {
      this.shared.position = val;
    }
  },
  FSNode: class {
    node_ops={};
    stream_ops={};
    readMode=292 | 73;
    writeMode=146;
    mounted=null;
    constructor(parent, name, mode, rdev) {
      if (!parent) {
        parent = this;
      }
      this.parent = parent;
      this.mount = parent.mount;
      this.id = FS.nextInode++;
      this.name = name;
      this.mode = mode;
      this.rdev = rdev;
      this.atime = this.mtime = this.ctime = Date.now();
    }
    get read() {
      return (this.mode & this.readMode) === this.readMode;
    }
    set read(val) {
      val ? this.mode |= this.readMode : this.mode &= ~this.readMode;
    }
    get write() {
      return (this.mode & this.writeMode) === this.writeMode;
    }
    set write(val) {
      val ? this.mode |= this.writeMode : this.mode &= ~this.writeMode;
    }
    get isFolder() {
      return FS.isDir(this.mode);
    }
    get isDevice() {
      return FS.isChrdev(this.mode);
    }
  },
  lookupPath(path, opts = {}) {
    if (!path) {
      throw new FS.ErrnoError(44);
    }
    opts.follow_mount ??= true;
    if (!PATH.isAbs(path)) {
      path = FS.cwd() + "/" + path;
    }
    // limit max consecutive symlinks to 40 (SYMLOOP_MAX).
    linkloop: for (var nlinks = 0; nlinks < 40; nlinks++) {
      // split the absolute path
      var parts = path.split("/").filter(p => !!p);
      // start at the root
      var current = FS.root;
      var current_path = "/";
      for (var i = 0; i < parts.length; i++) {
        var islast = (i === parts.length - 1);
        if (islast && opts.parent) {
          // stop resolving
          break;
        }
        if (parts[i] === ".") {
          continue;
        }
        if (parts[i] === "..") {
          current_path = PATH.dirname(current_path);
          if (FS.isRoot(current)) {
            path = current_path + "/" + parts.slice(i + 1).join("/");
            // We're making progress here, don't let many consecutive ..'s
            // lead to ELOOP
            nlinks--;
            continue linkloop;
          } else {
            current = current.parent;
          }
          continue;
        }
        current_path = PATH.join2(current_path, parts[i]);
        try {
          current = FS.lookupNode(current, parts[i]);
        } catch (e) {
          // if noent_okay is true, suppress a ENOENT in the last component
          // and return an object with an undefined node. This is needed for
          // resolving symlinks in the path when creating a file.
          if ((e?.errno === 44) && islast && opts.noent_okay) {
            return {
              path: current_path
            };
          }
          throw e;
        }
        // jump to the mount's root node if this is a mountpoint
        if (FS.isMountpoint(current) && (!islast || opts.follow_mount)) {
          current = current.mounted.root;
        }
        // by default, lookupPath will not follow a symlink if it is the final path component.
        // setting opts.follow = true will override this behavior.
        if (FS.isLink(current.mode) && (!islast || opts.follow)) {
          if (!current.node_ops.readlink) {
            throw new FS.ErrnoError(52);
          }
          var link = current.node_ops.readlink(current);
          if (!PATH.isAbs(link)) {
            link = PATH.dirname(current_path) + "/" + link;
          }
          path = link + "/" + parts.slice(i + 1).join("/");
          continue linkloop;
        }
      }
      return {
        path: current_path,
        node: current
      };
    }
    throw new FS.ErrnoError(32);
  },
  getPath(node) {
    var path;
    while (true) {
      if (FS.isRoot(node)) {
        var mount = node.mount.mountpoint;
        if (!path) return mount;
        return mount[mount.length - 1] !== "/" ? `${mount}/${path}` : mount + path;
      }
      path = path ? `${node.name}/${path}` : node.name;
      node = node.parent;
    }
  },
  hashName(parentid, name) {
    var hash = 0;
    for (var i = 0; i < name.length; i++) {
      hash = ((hash << 5) - hash + name.charCodeAt(i)) | 0;
    }
    return ((parentid + hash) >>> 0) % FS.nameTable.length;
  },
  hashAddNode(node) {
    var hash = FS.hashName(node.parent.id, node.name);
    node.name_next = FS.nameTable[hash];
    FS.nameTable[hash] = node;
  },
  hashRemoveNode(node) {
    var hash = FS.hashName(node.parent.id, node.name);
    if (FS.nameTable[hash] === node) {
      FS.nameTable[hash] = node.name_next;
    } else {
      var current = FS.nameTable[hash];
      while (current) {
        if (current.name_next === node) {
          current.name_next = node.name_next;
          break;
        }
        current = current.name_next;
      }
    }
  },
  lookupNode(parent, name) {
    var errCode = FS.mayLookup(parent);
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    var hash = FS.hashName(parent.id, name);
    for (var node = FS.nameTable[hash]; node; node = node.name_next) {
      var nodeName = node.name;
      if (node.parent.id === parent.id && nodeName === name) {
        return node;
      }
    }
    // if we failed to find it in the cache, call into the VFS
    return FS.lookup(parent, name);
  },
  createNode(parent, name, mode, rdev) {
    assert(typeof parent == "object");
    var node = new FS.FSNode(parent, name, mode, rdev);
    FS.hashAddNode(node);
    return node;
  },
  destroyNode(node) {
    FS.hashRemoveNode(node);
  },
  isRoot(node) {
    return node === node.parent;
  },
  isMountpoint(node) {
    return !!node.mounted;
  },
  isFile(mode) {
    return (mode & 61440) === 32768;
  },
  isDir(mode) {
    return (mode & 61440) === 16384;
  },
  isLink(mode) {
    return (mode & 61440) === 40960;
  },
  isChrdev(mode) {
    return (mode & 61440) === 8192;
  },
  isBlkdev(mode) {
    return (mode & 61440) === 24576;
  },
  isFIFO(mode) {
    return (mode & 61440) === 4096;
  },
  isSocket(mode) {
    return (mode & 49152) === 49152;
  },
  flagsToPermissionString(flag) {
    var perms = [ "r", "w", "rw" ][flag & 3];
    if ((flag & 512)) {
      perms += "w";
    }
    return perms;
  },
  nodePermissions(node, perms) {
    if (FS.ignorePermissions) {
      return 0;
    }
    // return 0 if any user, group or owner bits are set.
    if (perms.includes("r") && !(node.mode & 292)) {
      return 2;
    } else if (perms.includes("w") && !(node.mode & 146)) {
      return 2;
    } else if (perms.includes("x") && !(node.mode & 73)) {
      return 2;
    }
    return 0;
  },
  mayLookup(dir) {
    if (!FS.isDir(dir.mode)) return 54;
    var errCode = FS.nodePermissions(dir, "x");
    if (errCode) return errCode;
    if (!dir.node_ops.lookup) return 2;
    return 0;
  },
  mayCreate(dir, name) {
    if (!FS.isDir(dir.mode)) {
      return 54;
    }
    try {
      var node = FS.lookupNode(dir, name);
      return 20;
    } catch (e) {}
    return FS.nodePermissions(dir, "wx");
  },
  mayDelete(dir, name, isdir) {
    var node;
    try {
      node = FS.lookupNode(dir, name);
    } catch (e) {
      return e.errno;
    }
    var errCode = FS.nodePermissions(dir, "wx");
    if (errCode) {
      return errCode;
    }
    if (isdir) {
      if (!FS.isDir(node.mode)) {
        return 54;
      }
      if (FS.isRoot(node) || FS.getPath(node) === FS.cwd()) {
        return 10;
      }
    } else {
      if (FS.isDir(node.mode)) {
        return 31;
      }
    }
    return 0;
  },
  mayOpen(node, flags) {
    if (!node) {
      return 44;
    }
    if (FS.isLink(node.mode)) {
      return 32;
    } else if (FS.isDir(node.mode)) {
      if (FS.flagsToPermissionString(flags) !== "r" || (flags & (512 | 64))) {
        // TODO: check for O_SEARCH? (== search for dir only)
        return 31;
      }
    }
    return FS.nodePermissions(node, FS.flagsToPermissionString(flags));
  },
  checkOpExists(op, err) {
    if (!op) {
      throw new FS.ErrnoError(err);
    }
    return op;
  },
  MAX_OPEN_FDS: 4096,
  nextfd() {
    for (var fd = 0; fd <= FS.MAX_OPEN_FDS; fd++) {
      if (!FS.streams[fd]) {
        return fd;
      }
    }
    throw new FS.ErrnoError(33);
  },
  getStreamChecked(fd) {
    var stream = FS.getStream(fd);
    if (!stream) {
      throw new FS.ErrnoError(8);
    }
    return stream;
  },
  getStream: fd => FS.streams[fd],
  createStream(stream, fd = -1) {
    assert(fd >= -1);
    // clone it, so we can return an instance of FSStream
    stream = Object.assign(new FS.FSStream, stream);
    if (fd == -1) {
      fd = FS.nextfd();
    }
    stream.fd = fd;
    FS.streams[fd] = stream;
    return stream;
  },
  closeStream(fd) {
    FS.streams[fd] = null;
  },
  dupStream(origStream, fd = -1) {
    var stream = FS.createStream(origStream, fd);
    stream.stream_ops?.dup?.(stream);
    return stream;
  },
  doSetAttr(stream, node, attr) {
    var setattr = stream?.stream_ops.setattr;
    var arg = setattr ? stream : node;
    setattr ??= node.node_ops.setattr;
    FS.checkOpExists(setattr, 63);
    setattr(arg, attr);
  },
  chrdev_stream_ops: {
    open(stream) {
      var device = FS.getDevice(stream.node.rdev);
      // override node's stream ops with the device's
      stream.stream_ops = device.stream_ops;
      // forward the open call
      stream.stream_ops.open?.(stream);
    },
    llseek() {
      throw new FS.ErrnoError(70);
    }
  },
  major: dev => ((dev) >> 8),
  minor: dev => ((dev) & 255),
  makedev: (ma, mi) => ((ma) << 8 | (mi)),
  registerDevice(dev, ops) {
    FS.devices[dev] = {
      stream_ops: ops
    };
  },
  getDevice: dev => FS.devices[dev],
  getMounts(mount) {
    var mounts = [];
    var check = [ mount ];
    while (check.length) {
      var m = check.pop();
      mounts.push(m);
      check.push(...m.mounts);
    }
    return mounts;
  },
  syncfs(populate, callback) {
    if (typeof populate == "function") {
      callback = populate;
      populate = false;
    }
    FS.syncFSRequests++;
    if (FS.syncFSRequests > 1) {
      err(`warning: ${FS.syncFSRequests} FS.syncfs operations in flight at once, probably just doing extra work`);
    }
    var mounts = FS.getMounts(FS.root.mount);
    var completed = 0;
    function doCallback(errCode) {
      assert(FS.syncFSRequests > 0);
      FS.syncFSRequests--;
      return callback(errCode);
    }
    function done(errCode) {
      if (errCode) {
        if (!done.errored) {
          done.errored = true;
          return doCallback(errCode);
        }
        return;
      }
      if (++completed >= mounts.length) {
        doCallback(null);
      }
    }
    // sync all mounts
    for (var mount of mounts) {
      if (mount.type.syncfs) {
        mount.type.syncfs(mount, populate, done);
      } else {
        done(null);
      }
    }
  },
  mount(type, opts, mountpoint) {
    if (typeof type == "string") {
      // The filesystem was not included, and instead we have an error
      // message stored in the variable.
      throw type;
    }
    var root = mountpoint === "/";
    var pseudo = !mountpoint;
    var node;
    if (root && FS.root) {
      throw new FS.ErrnoError(10);
    } else if (!root && !pseudo) {
      var lookup = FS.lookupPath(mountpoint, {
        follow_mount: false
      });
      mountpoint = lookup.path;
      // use the absolute path
      node = lookup.node;
      if (FS.isMountpoint(node)) {
        throw new FS.ErrnoError(10);
      }
      if (!FS.isDir(node.mode)) {
        throw new FS.ErrnoError(54);
      }
    }
    var mount = {
      type,
      opts,
      mountpoint,
      mounts: []
    };
    // create a root node for the fs
    var mountRoot = type.mount(mount);
    mountRoot.mount = mount;
    mount.root = mountRoot;
    if (root) {
      FS.root = mountRoot;
    } else if (node) {
      // set as a mountpoint
      node.mounted = mount;
      // add the new mount to the current mount's children
      if (node.mount) {
        node.mount.mounts.push(mount);
      }
    }
    return mountRoot;
  },
  unmount(mountpoint) {
    var lookup = FS.lookupPath(mountpoint, {
      follow_mount: false
    });
    if (!FS.isMountpoint(lookup.node)) {
      throw new FS.ErrnoError(28);
    }
    // destroy the nodes for this mount, and all its child mounts
    var node = lookup.node;
    var mount = node.mounted;
    var mounts = FS.getMounts(mount);
    for (var [hash, current] of Object.entries(FS.nameTable)) {
      while (current) {
        var next = current.name_next;
        if (mounts.includes(current.mount)) {
          FS.destroyNode(current);
        }
        current = next;
      }
    }
    // no longer a mountpoint
    node.mounted = null;
    // remove this mount from the child mounts
    var idx = node.mount.mounts.indexOf(mount);
    assert(idx !== -1);
    node.mount.mounts.splice(idx, 1);
  },
  lookup(parent, name) {
    return parent.node_ops.lookup(parent, name);
  },
  mknod(path, mode, dev) {
    var lookup = FS.lookupPath(path, {
      parent: true
    });
    var parent = lookup.node;
    var name = PATH.basename(path);
    if (!name) {
      throw new FS.ErrnoError(28);
    }
    if (name === "." || name === "..") {
      throw new FS.ErrnoError(20);
    }
    var errCode = FS.mayCreate(parent, name);
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    if (!parent.node_ops.mknod) {
      throw new FS.ErrnoError(63);
    }
    return parent.node_ops.mknod(parent, name, mode, dev);
  },
  statfs(path) {
    return FS.statfsNode(FS.lookupPath(path, {
      follow: true
    }).node);
  },
  statfsStream(stream) {
    // We keep a separate statfsStream function because noderawfs overrides
    // it. In noderawfs, stream.node is sometimes null. Instead, we need to
    // look at stream.path.
    return FS.statfsNode(stream.node);
  },
  statfsNode(node) {
    // NOTE: None of the defaults here are true. We're just returning safe and
    //       sane values. Currently nodefs and rawfs replace these defaults,
    //       other file systems leave them alone.
    var rtn = {
      bsize: 4096,
      frsize: 4096,
      blocks: 1e6,
      bfree: 5e5,
      bavail: 5e5,
      files: FS.nextInode,
      ffree: FS.nextInode - 1,
      fsid: 42,
      flags: 2,
      namelen: 255
    };
    if (node.node_ops.statfs) {
      Object.assign(rtn, node.node_ops.statfs(node.mount.opts.root));
    }
    return rtn;
  },
  create(path, mode = 438) {
    mode &= 4095;
    mode |= 32768;
    return FS.mknod(path, mode, 0);
  },
  mkdir(path, mode = 511) {
    mode &= 511 | 512;
    mode |= 16384;
    return FS.mknod(path, mode, 0);
  },
  mkdirTree(path, mode) {
    var dirs = path.split("/");
    var d = "";
    for (var dir of dirs) {
      if (!dir) continue;
      if (d || PATH.isAbs(path)) d += "/";
      d += dir;
      try {
        FS.mkdir(d, mode);
      } catch (e) {
        if (e.errno != 20) throw e;
      }
    }
  },
  mkdev(path, mode, dev) {
    if (typeof dev == "undefined") {
      dev = mode;
      mode = 438;
    }
    mode |= 8192;
    return FS.mknod(path, mode, dev);
  },
  symlink(oldpath, newpath) {
    if (!PATH_FS.resolve(oldpath)) {
      throw new FS.ErrnoError(44);
    }
    var lookup = FS.lookupPath(newpath, {
      parent: true
    });
    var parent = lookup.node;
    if (!parent) {
      throw new FS.ErrnoError(44);
    }
    var newname = PATH.basename(newpath);
    var errCode = FS.mayCreate(parent, newname);
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    if (!parent.node_ops.symlink) {
      throw new FS.ErrnoError(63);
    }
    return parent.node_ops.symlink(parent, newname, oldpath);
  },
  rename(old_path, new_path) {
    var old_dirname = PATH.dirname(old_path);
    var new_dirname = PATH.dirname(new_path);
    var old_name = PATH.basename(old_path);
    var new_name = PATH.basename(new_path);
    // parents must exist
    var lookup, old_dir, new_dir;
    // let the errors from non existent directories percolate up
    lookup = FS.lookupPath(old_path, {
      parent: true
    });
    old_dir = lookup.node;
    lookup = FS.lookupPath(new_path, {
      parent: true
    });
    new_dir = lookup.node;
    if (!old_dir || !new_dir) throw new FS.ErrnoError(44);
    // need to be part of the same mount
    if (old_dir.mount !== new_dir.mount) {
      throw new FS.ErrnoError(75);
    }
    // source must exist
    var old_node = FS.lookupNode(old_dir, old_name);
    // old path should not be an ancestor of the new path
    var relative = PATH_FS.relative(old_path, new_dirname);
    if (relative.charAt(0) !== ".") {
      throw new FS.ErrnoError(28);
    }
    // new path should not be an ancestor of the old path
    relative = PATH_FS.relative(new_path, old_dirname);
    if (relative.charAt(0) !== ".") {
      throw new FS.ErrnoError(55);
    }
    // see if the new path already exists
    var new_node;
    try {
      new_node = FS.lookupNode(new_dir, new_name);
    } catch (e) {}
    // early out if nothing needs to change
    if (old_node === new_node) {
      return;
    }
    // we'll need to delete the old entry
    var isdir = FS.isDir(old_node.mode);
    var errCode = FS.mayDelete(old_dir, old_name, isdir);
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    // need delete permissions if we'll be overwriting.
    // need create permissions if new doesn't already exist.
    errCode = new_node ? FS.mayDelete(new_dir, new_name, isdir) : FS.mayCreate(new_dir, new_name);
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    if (!old_dir.node_ops.rename) {
      throw new FS.ErrnoError(63);
    }
    if (FS.isMountpoint(old_node) || (new_node && FS.isMountpoint(new_node))) {
      throw new FS.ErrnoError(10);
    }
    // if we are going to change the parent, check write permissions
    if (new_dir !== old_dir) {
      errCode = FS.nodePermissions(old_dir, "w");
      if (errCode) {
        throw new FS.ErrnoError(errCode);
      }
    }
    // remove the node from the lookup hash
    FS.hashRemoveNode(old_node);
    // do the underlying fs rename
    try {
      old_dir.node_ops.rename(old_node, new_dir, new_name);
      // update old node (we do this here to avoid each backend
      // needing to)
      old_node.parent = new_dir;
    } catch (e) {
      throw e;
    } finally {
      // add the node back to the hash (in case node_ops.rename
      // changed its name)
      FS.hashAddNode(old_node);
    }
  },
  rmdir(path) {
    var lookup = FS.lookupPath(path, {
      parent: true
    });
    var parent = lookup.node;
    var name = PATH.basename(path);
    var node = FS.lookupNode(parent, name);
    var errCode = FS.mayDelete(parent, name, true);
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    if (!parent.node_ops.rmdir) {
      throw new FS.ErrnoError(63);
    }
    if (FS.isMountpoint(node)) {
      throw new FS.ErrnoError(10);
    }
    parent.node_ops.rmdir(parent, name);
    FS.destroyNode(node);
  },
  readdir(path) {
    var lookup = FS.lookupPath(path, {
      follow: true
    });
    var node = lookup.node;
    var readdir = FS.checkOpExists(node.node_ops.readdir, 54);
    return readdir(node);
  },
  unlink(path) {
    var lookup = FS.lookupPath(path, {
      parent: true
    });
    var parent = lookup.node;
    if (!parent) {
      throw new FS.ErrnoError(44);
    }
    var name = PATH.basename(path);
    var node = FS.lookupNode(parent, name);
    var errCode = FS.mayDelete(parent, name, false);
    if (errCode) {
      // According to POSIX, we should map EISDIR to EPERM, but
      // we instead do what Linux does (and we must, as we use
      // the musl linux libc).
      throw new FS.ErrnoError(errCode);
    }
    if (!parent.node_ops.unlink) {
      throw new FS.ErrnoError(63);
    }
    if (FS.isMountpoint(node)) {
      throw new FS.ErrnoError(10);
    }
    parent.node_ops.unlink(parent, name);
    FS.destroyNode(node);
  },
  readlink(path) {
    var lookup = FS.lookupPath(path);
    var link = lookup.node;
    if (!link) {
      throw new FS.ErrnoError(44);
    }
    if (!link.node_ops.readlink) {
      throw new FS.ErrnoError(28);
    }
    return link.node_ops.readlink(link);
  },
  stat(path, dontFollow) {
    var lookup = FS.lookupPath(path, {
      follow: !dontFollow
    });
    var node = lookup.node;
    var getattr = FS.checkOpExists(node.node_ops.getattr, 63);
    return getattr(node);
  },
  fstat(fd) {
    var stream = FS.getStreamChecked(fd);
    var node = stream.node;
    var getattr = stream.stream_ops.getattr;
    var arg = getattr ? stream : node;
    getattr ??= node.node_ops.getattr;
    FS.checkOpExists(getattr, 63);
    return getattr(arg);
  },
  lstat(path) {
    return FS.stat(path, true);
  },
  doChmod(stream, node, mode, dontFollow) {
    FS.doSetAttr(stream, node, {
      mode: (mode & 4095) | (node.mode & ~4095),
      ctime: Date.now(),
      dontFollow
    });
  },
  chmod(path, mode, dontFollow) {
    var node;
    if (typeof path == "string") {
      var lookup = FS.lookupPath(path, {
        follow: !dontFollow
      });
      node = lookup.node;
    } else {
      node = path;
    }
    FS.doChmod(null, node, mode, dontFollow);
  },
  lchmod(path, mode) {
    FS.chmod(path, mode, true);
  },
  fchmod(fd, mode) {
    var stream = FS.getStreamChecked(fd);
    FS.doChmod(stream, stream.node, mode, false);
  },
  doChown(stream, node, dontFollow) {
    FS.doSetAttr(stream, node, {
      timestamp: Date.now(),
      dontFollow
    });
  },
  chown(path, uid, gid, dontFollow) {
    var node;
    if (typeof path == "string") {
      var lookup = FS.lookupPath(path, {
        follow: !dontFollow
      });
      node = lookup.node;
    } else {
      node = path;
    }
    FS.doChown(null, node, dontFollow);
  },
  lchown(path, uid, gid) {
    FS.chown(path, uid, gid, true);
  },
  fchown(fd, uid, gid) {
    var stream = FS.getStreamChecked(fd);
    FS.doChown(stream, stream.node, false);
  },
  doTruncate(stream, node, len) {
    if (FS.isDir(node.mode)) {
      throw new FS.ErrnoError(31);
    }
    if (!FS.isFile(node.mode)) {
      throw new FS.ErrnoError(28);
    }
    var errCode = FS.nodePermissions(node, "w");
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    FS.doSetAttr(stream, node, {
      size: len,
      timestamp: Date.now()
    });
  },
  truncate(path, len) {
    if (len < 0) {
      throw new FS.ErrnoError(28);
    }
    var node;
    if (typeof path == "string") {
      var lookup = FS.lookupPath(path, {
        follow: true
      });
      node = lookup.node;
    } else {
      node = path;
    }
    FS.doTruncate(null, node, len);
  },
  ftruncate(fd, len) {
    var stream = FS.getStreamChecked(fd);
    if (len < 0 || (stream.flags & 2097155) === 0) {
      throw new FS.ErrnoError(28);
    }
    FS.doTruncate(stream, stream.node, len);
  },
  utime(path, atime, mtime) {
    var lookup = FS.lookupPath(path, {
      follow: true
    });
    var node = lookup.node;
    var setattr = FS.checkOpExists(node.node_ops.setattr, 63);
    setattr(node, {
      atime,
      mtime
    });
  },
  open(path, flags, mode = 438) {
    if (path === "") {
      throw new FS.ErrnoError(44);
    }
    flags = typeof flags == "string" ? FS_modeStringToFlags(flags) : flags;
    if ((flags & 64)) {
      mode = (mode & 4095) | 32768;
    } else {
      mode = 0;
    }
    var node;
    var isDirPath;
    if (typeof path == "object") {
      node = path;
    } else {
      isDirPath = path.endsWith("/");
      // noent_okay makes it so that if the final component of the path
      // doesn't exist, lookupPath returns `node: undefined`. `path` will be
      // updated to point to the target of all symlinks.
      var lookup = FS.lookupPath(path, {
        follow: !(flags & 131072),
        noent_okay: true
      });
      node = lookup.node;
      path = lookup.path;
    }
    // perhaps we need to create the node
    var created = false;
    if ((flags & 64)) {
      if (node) {
        // if O_CREAT and O_EXCL are set, error out if the node already exists
        if ((flags & 128)) {
          throw new FS.ErrnoError(20);
        }
      } else if (isDirPath) {
        throw new FS.ErrnoError(31);
      } else {
        // node doesn't exist, try to create it
        // Ignore the permission bits here to ensure we can `open` this new
        // file below. We use chmod below the apply the permissions once the
        // file is open.
        node = FS.mknod(path, mode | 511, 0);
        created = true;
      }
    }
    if (!node) {
      throw new FS.ErrnoError(44);
    }
    // can't truncate a device
    if (FS.isChrdev(node.mode)) {
      flags &= ~512;
    }
    // if asked only for a directory, then this must be one
    if ((flags & 65536) && !FS.isDir(node.mode)) {
      throw new FS.ErrnoError(54);
    }
    // check permissions, if this is not a file we just created now (it is ok to
    // create and write to a file with read-only permissions; it is read-only
    // for later use)
    if (!created) {
      var errCode = FS.mayOpen(node, flags);
      if (errCode) {
        throw new FS.ErrnoError(errCode);
      }
    }
    // do truncation if necessary
    if ((flags & 512) && !created) {
      FS.truncate(node, 0);
    }
    // we've already handled these, don't pass down to the underlying vfs
    flags &= ~(128 | 512 | 131072);
    // register the stream with the filesystem
    var stream = FS.createStream({
      node,
      path: FS.getPath(node),
      // we want the absolute path to the node
      flags,
      seekable: true,
      position: 0,
      stream_ops: node.stream_ops,
      // used by the file family libc calls (fopen, fwrite, ferror, etc.)
      ungotten: [],
      error: false
    });
    // call the new stream's open function
    if (stream.stream_ops.open) {
      stream.stream_ops.open(stream);
    }
    if (created) {
      FS.chmod(node, mode & 511);
    }
    if (Module["logReadFiles"] && !(flags & 1)) {
      if (!(path in FS.readFiles)) {
        FS.readFiles[path] = 1;
      }
    }
    return stream;
  },
  close(stream) {
    if (FS.isClosed(stream)) {
      throw new FS.ErrnoError(8);
    }
    if (stream.getdents) stream.getdents = null;
    // free readdir state
    try {
      if (stream.stream_ops.close) {
        stream.stream_ops.close(stream);
      }
    } catch (e) {
      throw e;
    } finally {
      FS.closeStream(stream.fd);
    }
    stream.fd = null;
  },
  isClosed(stream) {
    return stream.fd === null;
  },
  llseek(stream, offset, whence) {
    if (FS.isClosed(stream)) {
      throw new FS.ErrnoError(8);
    }
    if (!stream.seekable || !stream.stream_ops.llseek) {
      throw new FS.ErrnoError(70);
    }
    if (whence != 0 && whence != 1 && whence != 2) {
      throw new FS.ErrnoError(28);
    }
    stream.position = stream.stream_ops.llseek(stream, offset, whence);
    stream.ungotten = [];
    return stream.position;
  },
  read(stream, buffer, offset, length, position) {
    assert(offset >= 0);
    if (length < 0 || position < 0) {
      throw new FS.ErrnoError(28);
    }
    if (FS.isClosed(stream)) {
      throw new FS.ErrnoError(8);
    }
    if ((stream.flags & 2097155) === 1) {
      throw new FS.ErrnoError(8);
    }
    if (FS.isDir(stream.node.mode)) {
      throw new FS.ErrnoError(31);
    }
    if (!stream.stream_ops.read) {
      throw new FS.ErrnoError(28);
    }
    var seeking = typeof position != "undefined";
    if (!seeking) {
      position = stream.position;
    } else if (!stream.seekable) {
      throw new FS.ErrnoError(70);
    }
    var bytesRead = stream.stream_ops.read(stream, buffer, offset, length, position);
    if (!seeking) stream.position += bytesRead;
    return bytesRead;
  },
  write(stream, buffer, offset, length, position, canOwn) {
    assert(offset >= 0);
    if (length < 0 || position < 0) {
      throw new FS.ErrnoError(28);
    }
    if (FS.isClosed(stream)) {
      throw new FS.ErrnoError(8);
    }
    if ((stream.flags & 2097155) === 0) {
      throw new FS.ErrnoError(8);
    }
    if (FS.isDir(stream.node.mode)) {
      throw new FS.ErrnoError(31);
    }
    if (!stream.stream_ops.write) {
      throw new FS.ErrnoError(28);
    }
    if (stream.seekable && stream.flags & 1024) {
      // seek to the end before writing in append mode
      FS.llseek(stream, 0, 2);
    }
    var seeking = typeof position != "undefined";
    if (!seeking) {
      position = stream.position;
    } else if (!stream.seekable) {
      throw new FS.ErrnoError(70);
    }
    var bytesWritten = stream.stream_ops.write(stream, buffer, offset, length, position, canOwn);
    if (!seeking) stream.position += bytesWritten;
    return bytesWritten;
  },
  mmap(stream, length, position, prot, flags) {
    // User requests writing to file (prot & PROT_WRITE != 0).
    // Checking if we have permissions to write to the file unless
    // MAP_PRIVATE flag is set. According to POSIX spec it is possible
    // to write to file opened in read-only mode with MAP_PRIVATE flag,
    // as all modifications will be visible only in the memory of
    // the current process.
    if ((prot & 2) !== 0 && (flags & 2) === 0 && (stream.flags & 2097155) !== 2) {
      throw new FS.ErrnoError(2);
    }
    if ((stream.flags & 2097155) === 1) {
      throw new FS.ErrnoError(2);
    }
    if (!stream.stream_ops.mmap) {
      throw new FS.ErrnoError(43);
    }
    if (!length) {
      throw new FS.ErrnoError(28);
    }
    return stream.stream_ops.mmap(stream, length, position, prot, flags);
  },
  msync(stream, buffer, offset, length, mmapFlags) {
    assert(offset >= 0);
    if (!stream.stream_ops.msync) {
      return 0;
    }
    return stream.stream_ops.msync(stream, buffer, offset, length, mmapFlags);
  },
  ioctl(stream, cmd, arg) {
    if (!stream.stream_ops.ioctl) {
      throw new FS.ErrnoError(59);
    }
    return stream.stream_ops.ioctl(stream, cmd, arg);
  },
  readFile(path, opts = {}) {
    opts.flags = opts.flags || 0;
    opts.encoding = opts.encoding || "binary";
    if (opts.encoding !== "utf8" && opts.encoding !== "binary") {
      abort(`Invalid encoding type "${opts.encoding}"`);
    }
    var stream = FS.open(path, opts.flags);
    var stat = FS.stat(path);
    var length = stat.size;
    var buf = new Uint8Array(length);
    FS.read(stream, buf, 0, length, 0);
    if (opts.encoding === "utf8") {
      buf = UTF8ArrayToString(buf);
    }
    FS.close(stream);
    return buf;
  },
  writeFile(path, data, opts = {}) {
    opts.flags = opts.flags || 577;
    var stream = FS.open(path, opts.flags, opts.mode);
    if (typeof data == "string") {
      data = new Uint8Array(intArrayFromString(data, true));
    }
    if (ArrayBuffer.isView(data)) {
      FS.write(stream, data, 0, data.byteLength, undefined, opts.canOwn);
    } else {
      abort("Unsupported data type");
    }
    FS.close(stream);
  },
  cwd: () => FS.currentPath,
  chdir(path) {
    var lookup = FS.lookupPath(path, {
      follow: true
    });
    if (lookup.node === null) {
      throw new FS.ErrnoError(44);
    }
    if (!FS.isDir(lookup.node.mode)) {
      throw new FS.ErrnoError(54);
    }
    var errCode = FS.nodePermissions(lookup.node, "x");
    if (errCode) {
      throw new FS.ErrnoError(errCode);
    }
    FS.currentPath = lookup.path;
  },
  createDefaultDirectories() {
    FS.mkdir("/tmp");
    FS.mkdir("/home");
    FS.mkdir("/home/web_user");
  },
  createDefaultDevices() {
    // create /dev
    FS.mkdir("/dev");
    // setup /dev/null
    FS.registerDevice(FS.makedev(1, 3), {
      read: () => 0,
      write: (stream, buffer, offset, length, pos) => length,
      llseek: () => 0
    });
    FS.mkdev("/dev/null", FS.makedev(1, 3));
    // setup /dev/tty and /dev/tty1
    // stderr needs to print output using err() rather than out()
    // so we register a second tty just for it.
    TTY.register(FS.makedev(5, 0), TTY.default_tty_ops);
    TTY.register(FS.makedev(6, 0), TTY.default_tty1_ops);
    FS.mkdev("/dev/tty", FS.makedev(5, 0));
    FS.mkdev("/dev/tty1", FS.makedev(6, 0));
    // setup /dev/[u]random
    // use a buffer to avoid overhead of individual crypto calls per byte
    var randomBuffer = new Uint8Array(1024), randomLeft = 0;
    var randomByte = () => {
      if (randomLeft === 0) {
        randomFill(randomBuffer);
        randomLeft = randomBuffer.byteLength;
      }
      return randomBuffer[--randomLeft];
    };
    FS.createDevice("/dev", "random", randomByte);
    FS.createDevice("/dev", "urandom", randomByte);
    // we're not going to emulate the actual shm device,
    // just create the tmp dirs that reside in it commonly
    FS.mkdir("/dev/shm");
    FS.mkdir("/dev/shm/tmp");
  },
  createSpecialDirectories() {
    // create /proc/self/fd which allows /proc/self/fd/6 => readlink gives the
    // name of the stream for fd 6 (see test_unistd_ttyname)
    FS.mkdir("/proc");
    var proc_self = FS.mkdir("/proc/self");
    FS.mkdir("/proc/self/fd");
    FS.mount({
      mount() {
        var node = FS.createNode(proc_self, "fd", 16895, 73);
        node.stream_ops = {
          llseek: MEMFS.stream_ops.llseek
        };
        node.node_ops = {
          lookup(parent, name) {
            var fd = +name;
            var stream = FS.getStreamChecked(fd);
            var ret = {
              parent: null,
              mount: {
                mountpoint: "fake"
              },
              node_ops: {
                readlink: () => stream.path
              },
              id: fd + 1
            };
            ret.parent = ret;
            // make it look like a simple root node
            return ret;
          },
          readdir() {
            return Array.from(FS.streams.entries()).filter(([k, v]) => v).map(([k, v]) => k.toString());
          }
        };
        return node;
      }
    }, {}, "/proc/self/fd");
  },
  createStandardStreams(input, output, error) {
    // TODO deprecate the old functionality of a single
    // input / output callback and that utilizes FS.createDevice
    // and instead require a unique set of stream ops
    // by default, we symlink the standard streams to the
    // default tty devices. however, if the standard streams
    // have been overwritten we create a unique device for
    // them instead.
    if (input) {
      FS.createDevice("/dev", "stdin", input);
    } else {
      FS.symlink("/dev/tty", "/dev/stdin");
    }
    if (output) {
      FS.createDevice("/dev", "stdout", null, output);
    } else {
      FS.symlink("/dev/tty", "/dev/stdout");
    }
    if (error) {
      FS.createDevice("/dev", "stderr", null, error);
    } else {
      FS.symlink("/dev/tty1", "/dev/stderr");
    }
    // open default streams for the stdin, stdout and stderr devices
    var stdin = FS.open("/dev/stdin", 0);
    var stdout = FS.open("/dev/stdout", 1);
    var stderr = FS.open("/dev/stderr", 1);
    assert(stdin.fd === 0, `invalid handle for stdin (${stdin.fd})`);
    assert(stdout.fd === 1, `invalid handle for stdout (${stdout.fd})`);
    assert(stderr.fd === 2, `invalid handle for stderr (${stderr.fd})`);
  },
  staticInit() {
    FS.nameTable = new Array(4096);
    FS.mount(MEMFS, {}, "/");
    FS.createDefaultDirectories();
    FS.createDefaultDevices();
    FS.createSpecialDirectories();
    FS.filesystems = {
      "MEMFS": MEMFS
    };
  },
  init(input, output, error) {
    assert(!FS.initialized, "FS.init was previously called. If you want to initialize later with custom parameters, remove any earlier calls (note that one is automatically added to the generated code)");
    FS.initialized = true;
    // Allow Module.stdin etc. to provide defaults, if none explicitly passed to us here
    input ??= Module["stdin"];
    output ??= Module["stdout"];
    error ??= Module["stderr"];
    FS.createStandardStreams(input, output, error);
  },
  quit() {
    FS.initialized = false;
    // force-flush all streams, so we get musl std streams printed out
    _fflush(0);
    // close all of our streams
    for (var stream of FS.streams) {
      if (stream) {
        FS.close(stream);
      }
    }
  },
  findObject(path, dontResolveLastLink) {
    var ret = FS.analyzePath(path, dontResolveLastLink);
    if (!ret.exists) {
      return null;
    }
    return ret.object;
  },
  analyzePath(path, dontResolveLastLink) {
    // operate from within the context of the symlink's target
    try {
      var lookup = FS.lookupPath(path, {
        follow: !dontResolveLastLink
      });
      path = lookup.path;
    } catch (e) {}
    var ret = {
      isRoot: false,
      exists: false,
      error: 0,
      name: null,
      path: null,
      object: null,
      parentExists: false,
      parentPath: null,
      parentObject: null
    };
    try {
      var lookup = FS.lookupPath(path, {
        parent: true
      });
      ret.parentExists = true;
      ret.parentPath = lookup.path;
      ret.parentObject = lookup.node;
      ret.name = PATH.basename(path);
      lookup = FS.lookupPath(path, {
        follow: !dontResolveLastLink
      });
      ret.exists = true;
      ret.path = lookup.path;
      ret.object = lookup.node;
      ret.name = lookup.node.name;
      ret.isRoot = lookup.path === "/";
    } catch (e) {
      ret.error = e.errno;
    }
    return ret;
  },
  createPath(parent, path, canRead, canWrite) {
    parent = typeof parent == "string" ? parent : FS.getPath(parent);
    var parts = path.split("/").reverse();
    while (parts.length) {
      var part = parts.pop();
      if (!part) continue;
      var current = PATH.join2(parent, part);
      try {
        FS.mkdir(current);
      } catch (e) {
        if (e.errno != 20) throw e;
      }
      parent = current;
    }
    return current;
  },
  createFile(parent, name, properties, canRead, canWrite) {
    var path = PATH.join2(typeof parent == "string" ? parent : FS.getPath(parent), name);
    var mode = FS_getMode(canRead, canWrite);
    return FS.create(path, mode);
  },
  createDataFile(parent, name, data, canRead, canWrite, canOwn) {
    var path = name;
    if (parent) {
      parent = typeof parent == "string" ? parent : FS.getPath(parent);
      path = name ? PATH.join2(parent, name) : parent;
    }
    var mode = FS_getMode(canRead, canWrite);
    var node = FS.create(path, mode);
    if (data) {
      if (typeof data == "string") {
        var arr = new Array(data.length);
        for (var i = 0, len = data.length; i < len; ++i) arr[i] = data.charCodeAt(i);
        data = arr;
      }
      // make sure we can write to the file
      FS.chmod(node, mode | 146);
      var stream = FS.open(node, 577);
      FS.write(stream, data, 0, data.length, 0, canOwn);
      FS.close(stream);
      FS.chmod(node, mode);
    }
  },
  createDevice(parent, name, input, output) {
    var path = PATH.join2(typeof parent == "string" ? parent : FS.getPath(parent), name);
    var mode = FS_getMode(!!input, !!output);
    FS.createDevice.major ??= 64;
    var dev = FS.makedev(FS.createDevice.major++, 0);
    // Create a fake device that a set of stream ops to emulate
    // the old behavior.
    FS.registerDevice(dev, {
      open(stream) {
        stream.seekable = false;
      },
      close(stream) {
        // flush any pending line data
        if (output?.buffer?.length) {
          output(10);
        }
      },
      read(stream, buffer, offset, length, pos) {
        var bytesRead = 0;
        for (var i = 0; i < length; i++) {
          var result;
          try {
            result = input();
          } catch (e) {
            throw new FS.ErrnoError(29);
          }
          if (result === undefined && bytesRead === 0) {
            throw new FS.ErrnoError(6);
          }
          if (result === null || result === undefined) break;
          bytesRead++;
          buffer[offset + i] = result;
        }
        if (bytesRead) {
          stream.node.atime = Date.now();
        }
        return bytesRead;
      },
      write(stream, buffer, offset, length, pos) {
        for (var i = 0; i < length; i++) {
          try {
            output(buffer[offset + i]);
          } catch (e) {
            throw new FS.ErrnoError(29);
          }
        }
        if (length) {
          stream.node.mtime = stream.node.ctime = Date.now();
        }
        return i;
      }
    });
    return FS.mkdev(path, mode, dev);
  },
  forceLoadFile(obj) {
    if (obj.isDevice || obj.isFolder || obj.link || obj.contents) return true;
    if (globalThis.XMLHttpRequest) {
      abort("Lazy loading should have been performed (contents set) in createLazyFile, but it was not. Lazy loading only works in web workers. Use --embed-file or --preload-file in emcc on the main thread.");
    } else {
      // Command-line.
      try {
        obj.contents = readBinary(obj.url);
      } catch (e) {
        throw new FS.ErrnoError(29);
      }
    }
  },
  createLazyFile(parent, name, url, canRead, canWrite) {
    // Lazy chunked Uint8Array (implements get and length from Uint8Array).
    // Actual getting is abstracted away for eventual reuse.
    class LazyUint8Array {
      lengthKnown=false;
      chunks=[];
      // Loaded chunks. Index is the chunk number
      get(idx) {
        if (idx > this.length - 1 || idx < 0) {
          return undefined;
        }
        var chunkOffset = idx % this.chunkSize;
        var chunkNum = (idx / this.chunkSize) | 0;
        return this.getter(chunkNum)[chunkOffset];
      }
      setDataGetter(getter) {
        this.getter = getter;
      }
      cacheLength() {
        // Find length
        var xhr = new XMLHttpRequest;
        xhr.open("HEAD", url, false);
        xhr.send(null);
        if (!(xhr.status >= 200 && xhr.status < 300 || xhr.status === 304)) abort("Couldn't load " + url + ". Status: " + xhr.status);
        var datalength = Number(xhr.getResponseHeader("Content-length"));
        var header;
        var hasByteServing = (header = xhr.getResponseHeader("Accept-Ranges")) && header === "bytes";
        var usesGzip = (header = xhr.getResponseHeader("Content-Encoding")) && header === "gzip";
        var chunkSize = 1024 * 1024;
        // Chunk size in bytes
        if (!hasByteServing) chunkSize = datalength;
        // Function to get a range from the remote URL.
        var doXHR = (from, to) => {
          if (from > to) abort("invalid range (" + from + ", " + to + ") or no bytes requested!");
          if (to > datalength - 1) abort("only " + datalength + " bytes available! programmer error!");
          // TODO: Use mozResponseArrayBuffer, responseStream, etc. if available.
          var xhr = new XMLHttpRequest;
          xhr.open("GET", url, false);
          if (datalength !== chunkSize) xhr.setRequestHeader("Range", "bytes=" + from + "-" + to);
          // Some hints to the browser that we want binary data.
          xhr.responseType = "arraybuffer";
          if (xhr.overrideMimeType) {
            xhr.overrideMimeType("text/plain; charset=x-user-defined");
          }
          xhr.send(null);
          if (!(xhr.status >= 200 && xhr.status < 300 || xhr.status === 304)) abort("Couldn't load " + url + ". Status: " + xhr.status);
          if (xhr.response !== undefined) {
            return new Uint8Array(/** @type{Array<number>} */ (xhr.response || []));
          }
          return intArrayFromString(xhr.responseText || "", true);
        };
        var lazyArray = this;
        lazyArray.setDataGetter(chunkNum => {
          var start = chunkNum * chunkSize;
          var end = (chunkNum + 1) * chunkSize - 1;
          // including this byte
          end = Math.min(end, datalength - 1);
          // if datalength-1 is selected, this is the last block
          if (typeof lazyArray.chunks[chunkNum] == "undefined") {
            lazyArray.chunks[chunkNum] = doXHR(start, end);
          }
          if (typeof lazyArray.chunks[chunkNum] == "undefined") abort("doXHR failed!");
          return lazyArray.chunks[chunkNum];
        });
        if (usesGzip || !datalength) {
          // if the server uses gzip or doesn't supply the length, we have to download the whole file to get the (uncompressed) length
          chunkSize = datalength = 1;
          // this will force getter(0)/doXHR do download the whole file
          datalength = this.getter(0).length;
          chunkSize = datalength;
          out("LazyFiles on gzip forces download of the whole file when length is accessed");
        }
        this._length = datalength;
        this._chunkSize = chunkSize;
        this.lengthKnown = true;
      }
      get length() {
        if (!this.lengthKnown) {
          this.cacheLength();
        }
        return this._length;
      }
      get chunkSize() {
        if (!this.lengthKnown) {
          this.cacheLength();
        }
        return this._chunkSize;
      }
    }
    if (globalThis.XMLHttpRequest) {
      if (!ENVIRONMENT_IS_WORKER) abort("Cannot do synchronous binary XHRs outside webworkers in modern browsers. Use --embed-file or --preload-file in emcc");
      var lazyArray = new LazyUint8Array;
      var properties = {
        isDevice: false,
        contents: lazyArray
      };
    } else {
      var properties = {
        isDevice: false,
        url
      };
    }
    var node = FS.createFile(parent, name, properties, canRead, canWrite);
    // This is a total hack, but I want to get this lazy file code out of the
    // core of MEMFS. If we want to keep this lazy file concept I feel it should
    // be its own thin LAZYFS proxying calls to MEMFS.
    if (properties.contents) {
      node.contents = properties.contents;
    } else if (properties.url) {
      node.contents = null;
      node.url = properties.url;
    }
    // Add a function that defers querying the file size until it is asked the first time.
    Object.defineProperties(node, {
      usedBytes: {
        get: function() {
          return this.contents.length;
        }
      }
    });
    // override each stream op with one that tries to force load the lazy file first
    var stream_ops = {};
    for (const [key, fn] of Object.entries(node.stream_ops)) {
      stream_ops[key] = (...args) => {
        FS.forceLoadFile(node);
        return fn(...args);
      };
    }
    function writeChunks(stream, buffer, offset, length, position) {
      var contents = stream.node.contents;
      if (position >= contents.length) return 0;
      var size = Math.min(contents.length - position, length);
      assert(size >= 0);
      if (contents.slice) {
        // normal array
        for (var i = 0; i < size; i++) {
          buffer[offset + i] = contents[position + i];
        }
      } else {
        for (var i = 0; i < size; i++) {
          // LazyUint8Array from sync binary XHR
          buffer[offset + i] = contents.get(position + i);
        }
      }
      return size;
    }
    // use a custom read function
    stream_ops.read = (stream, buffer, offset, length, position) => {
      FS.forceLoadFile(node);
      return writeChunks(stream, buffer, offset, length, position);
    };
    // use a custom mmap function
    stream_ops.mmap = (stream, length, position, prot, flags) => {
      FS.forceLoadFile(node);
      var ptr = mmapAlloc(length);
      if (!ptr) {
        throw new FS.ErrnoError(48);
      }
      writeChunks(stream, (growMemViews(), HEAP8), ptr, length, position);
      return {
        ptr,
        allocated: true
      };
    };
    node.stream_ops = stream_ops;
    return node;
  },
  absolutePath() {
    abort("FS.absolutePath has been removed; use PATH_FS.resolve instead");
  },
  createFolder() {
    abort("FS.createFolder has been removed; use FS.mkdir instead");
  },
  createLink() {
    abort("FS.createLink has been removed; use FS.symlink instead");
  },
  joinPath() {
    abort("FS.joinPath has been removed; use PATH.join instead");
  },
  mmapAlloc() {
    abort("FS.mmapAlloc has been replaced by the top level function mmapAlloc");
  },
  standardizePath() {
    abort("FS.standardizePath has been removed; use PATH.normalize instead");
  }
};

var SYSCALLS = {
  DEFAULT_POLLMASK: 5,
  calculateAt(dirfd, path, allowEmpty) {
    if (PATH.isAbs(path)) {
      return path;
    }
    // relative path
    var dir;
    if (dirfd === -100) {
      dir = FS.cwd();
    } else {
      var dirstream = SYSCALLS.getStreamFromFD(dirfd);
      dir = dirstream.path;
    }
    if (path.length == 0) {
      if (!allowEmpty) {
        throw new FS.ErrnoError(44);
      }
      return dir;
    }
    return dir + "/" + path;
  },
  writeStat(buf, stat) {
    (growMemViews(), HEAPU32)[((buf) >> 2)] = stat.dev;
    (growMemViews(), HEAPU32)[(((buf) + (4)) >> 2)] = stat.mode;
    (growMemViews(), HEAPU32)[(((buf) + (8)) >> 2)] = stat.nlink;
    (growMemViews(), HEAPU32)[(((buf) + (12)) >> 2)] = stat.uid;
    (growMemViews(), HEAPU32)[(((buf) + (16)) >> 2)] = stat.gid;
    (growMemViews(), HEAPU32)[(((buf) + (20)) >> 2)] = stat.rdev;
    (growMemViews(), HEAP64)[(((buf) + (24)) >> 3)] = BigInt(stat.size);
    (growMemViews(), HEAP32)[(((buf) + (32)) >> 2)] = 4096;
    (growMemViews(), HEAP32)[(((buf) + (36)) >> 2)] = stat.blocks;
    var atime = stat.atime.getTime();
    var mtime = stat.mtime.getTime();
    var ctime = stat.ctime.getTime();
    (growMemViews(), HEAP64)[(((buf) + (40)) >> 3)] = BigInt(Math.floor(atime / 1e3));
    (growMemViews(), HEAPU32)[(((buf) + (48)) >> 2)] = (atime % 1e3) * 1e3 * 1e3;
    (growMemViews(), HEAP64)[(((buf) + (56)) >> 3)] = BigInt(Math.floor(mtime / 1e3));
    (growMemViews(), HEAPU32)[(((buf) + (64)) >> 2)] = (mtime % 1e3) * 1e3 * 1e3;
    (growMemViews(), HEAP64)[(((buf) + (72)) >> 3)] = BigInt(Math.floor(ctime / 1e3));
    (growMemViews(), HEAPU32)[(((buf) + (80)) >> 2)] = (ctime % 1e3) * 1e3 * 1e3;
    (growMemViews(), HEAP64)[(((buf) + (88)) >> 3)] = BigInt(stat.ino);
    return 0;
  },
  writeStatFs(buf, stats) {
    (growMemViews(), HEAPU32)[(((buf) + (4)) >> 2)] = stats.bsize;
    (growMemViews(), HEAPU32)[(((buf) + (60)) >> 2)] = stats.bsize;
    (growMemViews(), HEAP64)[(((buf) + (8)) >> 3)] = BigInt(stats.blocks);
    (growMemViews(), HEAP64)[(((buf) + (16)) >> 3)] = BigInt(stats.bfree);
    (growMemViews(), HEAP64)[(((buf) + (24)) >> 3)] = BigInt(stats.bavail);
    (growMemViews(), HEAP64)[(((buf) + (32)) >> 3)] = BigInt(stats.files);
    (growMemViews(), HEAP64)[(((buf) + (40)) >> 3)] = BigInt(stats.ffree);
    (growMemViews(), HEAPU32)[(((buf) + (48)) >> 2)] = stats.fsid;
    (growMemViews(), HEAPU32)[(((buf) + (64)) >> 2)] = stats.flags;
    // ST_NOSUID
    (growMemViews(), HEAPU32)[(((buf) + (56)) >> 2)] = stats.namelen;
  },
  doMsync(addr, stream, len, flags, offset) {
    if (!FS.isFile(stream.node.mode)) {
      throw new FS.ErrnoError(43);
    }
    if (flags & 2) {
      // MAP_PRIVATE calls need not to be synced back to underlying fs
      return 0;
    }
    var buffer = (growMemViews(), HEAPU8).slice(addr, addr + len);
    FS.msync(stream, buffer, offset, len, flags);
  },
  getStreamFromFD(fd) {
    var stream = FS.getStreamChecked(fd);
    return stream;
  },
  varargs: undefined,
  getStr(ptr) {
    var ret = UTF8ToString(ptr);
    return ret;
  }
};

function ___syscall_fcntl64(fd, cmd, varargs) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(3, 0, 1, fd, cmd, varargs);
  SYSCALLS.varargs = varargs;
  try {
    var stream = SYSCALLS.getStreamFromFD(fd);
    switch (cmd) {
     case 0:
      {
        var arg = syscallGetVarargI();
        if (arg < 0) {
          return -28;
        }
        while (FS.streams[arg]) {
          arg++;
        }
        var newStream;
        newStream = FS.dupStream(stream, arg);
        return newStream.fd;
      }

     case 1:
     case 2:
      return 0;

     // FD_CLOEXEC makes no sense for a single process.
      case 3:
      return stream.flags;

     case 4:
      {
        var arg = syscallGetVarargI();
        stream.flags |= arg;
        return 0;
      }

     case 12:
      {
        var arg = syscallGetVarargP();
        var offset = 0;
        // We're always unlocked.
        (growMemViews(), HEAP16)[(((arg) + (offset)) >> 1)] = 2;
        return 0;
      }

     case 13:
     case 14:
      // Pretend that the locking is successful. These are process-level locks,
      // and Emscripten programs are a single process. If we supported linking a
      // filesystem between programs, we'd need to do more here.
      // See https://github.com/emscripten-core/emscripten/issues/23697
      return 0;
    }
    return -28;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return -e.errno;
  }
}

function ___syscall_ioctl(fd, op, varargs) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(4, 0, 1, fd, op, varargs);
  SYSCALLS.varargs = varargs;
  try {
    var stream = SYSCALLS.getStreamFromFD(fd);
    switch (op) {
     case 21509:
      {
        if (!stream.tty) return -59;
        return 0;
      }

     case 21505:
      {
        if (!stream.tty) return -59;
        if (stream.tty.ops.ioctl_tcgets) {
          var termios = stream.tty.ops.ioctl_tcgets(stream);
          var argp = syscallGetVarargP();
          (growMemViews(), HEAP32)[((argp) >> 2)] = termios.c_iflag || 0;
          (growMemViews(), HEAP32)[(((argp) + (4)) >> 2)] = termios.c_oflag || 0;
          (growMemViews(), HEAP32)[(((argp) + (8)) >> 2)] = termios.c_cflag || 0;
          (growMemViews(), HEAP32)[(((argp) + (12)) >> 2)] = termios.c_lflag || 0;
          for (var i = 0; i < 32; i++) {
            (growMemViews(), HEAP8)[(argp + i) + (17)] = termios.c_cc[i] || 0;
          }
          return 0;
        }
        return 0;
      }

     case 21510:
     case 21511:
     case 21512:
      {
        if (!stream.tty) return -59;
        return 0;
      }

     case 21506:
     case 21507:
     case 21508:
      {
        if (!stream.tty) return -59;
        if (stream.tty.ops.ioctl_tcsets) {
          var argp = syscallGetVarargP();
          var c_iflag = (growMemViews(), HEAP32)[((argp) >> 2)];
          var c_oflag = (growMemViews(), HEAP32)[(((argp) + (4)) >> 2)];
          var c_cflag = (growMemViews(), HEAP32)[(((argp) + (8)) >> 2)];
          var c_lflag = (growMemViews(), HEAP32)[(((argp) + (12)) >> 2)];
          var c_cc = [];
          for (var i = 0; i < 32; i++) {
            c_cc.push((growMemViews(), HEAP8)[(argp + i) + (17)]);
          }
          return stream.tty.ops.ioctl_tcsets(stream.tty, op, {
            c_iflag,
            c_oflag,
            c_cflag,
            c_lflag,
            c_cc
          });
        }
        return 0;
      }

     case 21519:
      {
        if (!stream.tty) return -59;
        var argp = syscallGetVarargP();
        (growMemViews(), HEAP32)[((argp) >> 2)] = 0;
        return 0;
      }

     case 21520:
      {
        if (!stream.tty) return -59;
        return -28;
      }

     case 21537:
     case 21531:
      {
        var argp = syscallGetVarargP();
        return FS.ioctl(stream, op, argp);
      }

     case 21523:
      {
        // TODO: in theory we should write to the winsize struct that gets
        // passed in, but for now musl doesn't read anything on it
        if (!stream.tty) return -59;
        if (stream.tty.ops.ioctl_tiocgwinsz) {
          var winsize = stream.tty.ops.ioctl_tiocgwinsz(stream.tty);
          var argp = syscallGetVarargP();
          (growMemViews(), HEAP16)[((argp) >> 1)] = winsize[0];
          (growMemViews(), HEAP16)[(((argp) + (2)) >> 1)] = winsize[1];
        }
        return 0;
      }

     case 21524:
      {
        // TODO: technically, this ioctl call should change the window size.
        // but, since emscripten doesn't have any concept of a terminal window
        // yet, we'll just silently throw it away as we do TIOCGWINSZ
        if (!stream.tty) return -59;
        return 0;
      }

     case 21515:
      {
        if (!stream.tty) return -59;
        return 0;
      }

     default:
      return -28;
    }
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return -e.errno;
  }
}

function ___syscall_openat(dirfd, path, flags, varargs) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(5, 0, 1, dirfd, path, flags, varargs);
  SYSCALLS.varargs = varargs;
  try {
    path = SYSCALLS.getStr(path);
    path = SYSCALLS.calculateAt(dirfd, path);
    var mode = varargs ? syscallGetVarargI() : 0;
    return FS.open(path, flags, mode).fd;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return -e.errno;
  }
}

var __abort_js = () => abort("native code called abort()");

var __emscripten_init_main_thread_js = tb => {
  // Pass the thread address to the native code where they stored in wasm
  // globals which act as a form of TLS. Global constructors trying
  // to access this value will read the wrong value, but that is UB anyway.
  __emscripten_thread_init(tb, /*is_main=*/ !ENVIRONMENT_IS_WORKER, /*is_runtime=*/ 1, /*can_block=*/ !ENVIRONMENT_IS_WEB, /*default_stacksize=*/ 4194304, /*start_profiling=*/ false);
  PThread.threadInitTLS();
};

var jsStackTrace = () => (new Error).stack.toString();

/** @param {number=} flags */ var getCallstack = flags => {
  var callstack = jsStackTrace();
  if (flags & 8) {
    warnOnce("emscripten_log with EM_LOG_C_STACK no longer has any effect");
  }
  // Process all lines:
  var lines = callstack.split("\n");
  callstack = "";
  // Extract components of form:
  // '       Object._main@http://server.com:4324:12'
  var firefoxRe = new RegExp("\\s*(.*?)@(.*?):([0-9]+):([0-9]+)");
  // Extract components of form:
  // '    at Object._main (http://server.com/file.html:4324:12)'
  var chromeRe = new RegExp("\\s*at (.*?) \\((.*):(.*):(.*)\\)");
  for (var line of lines) {
    var symbolName = "";
    var file = "";
    var lineno = 0;
    var column = 0;
    var parts = chromeRe.exec(line);
    if (parts?.length == 5) {
      symbolName = parts[1];
      file = parts[2];
      lineno = parts[3];
      column = parts[4];
    } else {
      parts = firefoxRe.exec(line);
      if (parts?.length >= 4) {
        symbolName = parts[1];
        file = parts[2];
        lineno = parts[3];
        // Old Firefox doesn't carry column information, but in new FF30, it
        // is present. See https://bugzil.la/762556
        column = parts[4] | 0;
      } else {
        // Was not able to extract this line for demangling/sourcemapping
        // purposes. Output it as-is.
        callstack += line + "\n";
        continue;
      }
    }
    // Find the symbols in the callstack that corresponds to the functions that
    // report callstack information, and remove everything up to these from the
    // output.
    if (symbolName == "_emscripten_log" || symbolName == "_emscripten_get_callstack") {
      callstack = "";
      continue;
    }
    if ((flags & 24)) {
      if (flags & 64) {
        file = file.substring(file.replace(/\\/g, "/").lastIndexOf("/") + 1);
      }
      callstack += `    at ${symbolName} (${file}:${lineno}:${column})\n`;
    }
  }
  // Trim extra whitespace at the end of the output.
  callstack = callstack.replace(/\s+$/, "");
  return callstack;
};

var __emscripten_log_formatted = (flags, str) => {
  str = UTF8ToString(str);
  if (flags & 24) {
    str = str.replace(/\s+$/, "");
    // Ensure the message and the callstack are joined cleanly with exactly one newline.
    str += (str.length > 0 ? "\n" : "") + getCallstack(flags);
  }
  if (flags & 1) {
    if (flags & 4) {
      console.error(str);
    } else if (flags & 2) {
      console.warn(str);
    } else if (flags & 512) {
      console.info(str);
    } else if (flags & 256) {
      console.debug(str);
    } else {
      console.log(str);
    }
  } else if (flags & 6) {
    err(str);
  } else {
    out(str);
  }
};

var waitAsyncPolyfilled = (!Atomics.waitAsync || (globalThis.navigator?.userAgent && Number((navigator.userAgent.match(/Chrom(e|ium)\/([0-9]+)\./) || [])[2]) < 91));

var __emscripten_thread_mailbox_await = pthread_ptr => {
  if (!waitAsyncPolyfilled) {
    // Wait on the pthread's initial self-pointer field because it is easy and
    // safe to access from sending threads that need to notify the waiting
    // thread.
    // TODO: How to make this work with wasm64?
    var wait = Atomics.waitAsync((growMemViews(), HEAP32), ((pthread_ptr) >> 2), pthread_ptr);
    assert(wait.async);
    wait.value.then(checkMailbox);
    var waitingAsync = pthread_ptr + 128;
    Atomics.store((growMemViews(), HEAP32), ((waitingAsync) >> 2), 1);
  }
};

var checkMailbox = () => callUserCallback(() => {
  // Only check the mailbox if we have a live pthread runtime. We implement
  // pthread_self to return 0 if there is no live runtime.
  // TODO(https://github.com/emscripten-core/emscripten/issues/25076):
  // Is this check still needed?  `callUserCallback` is supposed to
  // ensure the runtime is alive, and if `_pthread_self` is NULL then the
  // runtime certainly is *not* alive, so this should be a redundant check.
  var pthread_ptr = _pthread_self();
  if (pthread_ptr) {
    // If we are using Atomics.waitAsync as our notification mechanism, wait
    // for a notification before processing the mailbox to avoid missing any
    // work that could otherwise arrive after we've finished processing the
    // mailbox and before we're ready for the next notification.
    __emscripten_thread_mailbox_await(pthread_ptr);
    __emscripten_check_mailbox();
  }
});

var __emscripten_notify_mailbox_postmessage = (targetThread, currThreadId) => {
  if (targetThread == currThreadId) {
    setTimeout(checkMailbox);
  } else if (ENVIRONMENT_IS_PTHREAD) {
    postMessage({
      targetThread,
      cmd: "checkMailbox"
    });
  } else {
    var worker = PThread.pthreads[targetThread];
    if (!worker) {
      err(`Cannot send message to thread with ID ${targetThread}, unknown thread ID!`);
      return;
    }
    worker.postMessage({
      cmd: "checkMailbox"
    });
  }
};

var proxiedJSCallArgs = [];

var __emscripten_receive_on_main_thread_js = (funcIndex, emAsmAddr, callingThread, numCallArgs, args) => {
  // Sometimes we need to backproxy events to the calling thread (e.g.
  // HTML5 DOM events handlers such as
  // emscripten_set_mousemove_callback()), so keep track in a globally
  // accessible variable about the thread that initiated the proxying.
  numCallArgs /= 2;
  proxiedJSCallArgs.length = numCallArgs;
  var b = ((args) >> 3);
  for (var i = 0; i < numCallArgs; i++) {
    if ((growMemViews(), HEAP64)[b + 2 * i]) {
      // It's a BigInt.
      proxiedJSCallArgs[i] = (growMemViews(), HEAP64)[b + 2 * i + 1];
    } else {
      // It's a Number.
      proxiedJSCallArgs[i] = (growMemViews(), HEAPF64)[b + 2 * i + 1];
    }
  }
  // Proxied JS library funcs use funcIndex and EM_ASM functions use emAsmAddr
  var func = emAsmAddr ? ASM_CONSTS[emAsmAddr] : proxiedFunctionTable[funcIndex];
  assert(!(funcIndex && emAsmAddr));
  assert(func.length == numCallArgs, "Call args mismatch in _emscripten_receive_on_main_thread_js");
  PThread.currentProxiedOperationCallerThread = callingThread;
  var rtn = func(...proxiedJSCallArgs);
  PThread.currentProxiedOperationCallerThread = 0;
  // Proxied functions can return any type except bigint.  All other types
  // cooerce to f64/double (the return type of this function in C) but not
  // bigint.
  assert(typeof rtn != "bigint");
  return rtn;
};

var __emscripten_thread_cleanup = thread => {
  // Called when a thread needs to be cleaned up so it can be reused.
  // A thread is considered reusable when it either returns from its
  // entry point, calls pthread_exit, or acts upon a cancellation.
  // Detached threads are responsible for calling this themselves,
  // otherwise pthread_join is responsible for calling this.
  if (!ENVIRONMENT_IS_PTHREAD) cleanupThread(thread); else postMessage({
    cmd: "cleanupThread",
    thread
  });
};

var __emscripten_thread_set_strongref = thread => {
  // Called when a thread needs to be strongly referenced.
  // Currently only used for:
  // - keeping the "main" thread alive in PROXY_TO_PTHREAD mode;
  // - crashed threads that needs to propagate the uncaught exception
  //   back to the main thread.
  if (ENVIRONMENT_IS_NODE) {
    PThread.pthreads[thread].ref();
  }
};

var stringToUTF8 = (str, outPtr, maxBytesToWrite) => {
  assert(typeof maxBytesToWrite == "number", "stringToUTF8(str, outPtr, maxBytesToWrite) is missing the third parameter that specifies the length of the output buffer!");
  return stringToUTF8Array(str, (growMemViews(), HEAPU8), outPtr, maxBytesToWrite);
};

var __tzset_js = (timezone, daylight, std_name, dst_name) => {
  // TODO: Use (malleable) environment variables instead of system settings.
  var currentYear = (new Date).getFullYear();
  var winter = new Date(currentYear, 0, 1);
  var summer = new Date(currentYear, 6, 1);
  var winterOffset = winter.getTimezoneOffset();
  var summerOffset = summer.getTimezoneOffset();
  // Local standard timezone offset. Local standard time is not adjusted for
  // daylight savings.  This code uses the fact that getTimezoneOffset returns
  // a greater value during Standard Time versus Daylight Saving Time (DST).
  // Thus it determines the expected output during Standard Time, and it
  // compares whether the output of the given date the same (Standard) or less
  // (DST).
  var stdTimezoneOffset = Math.max(winterOffset, summerOffset);
  // timezone is specified as seconds west of UTC ("The external variable
  // `timezone` shall be set to the difference, in seconds, between
  // Coordinated Universal Time (UTC) and local standard time."), the same
  // as returned by stdTimezoneOffset.
  // See http://pubs.opengroup.org/onlinepubs/009695399/functions/tzset.html
  (growMemViews(), HEAPU32)[((timezone) >> 2)] = stdTimezoneOffset * 60;
  (growMemViews(), HEAP32)[((daylight) >> 2)] = Number(winterOffset != summerOffset);
  var extractZone = timezoneOffset => {
    // Why inverse sign?
    // Read here https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Date/getTimezoneOffset
    var sign = timezoneOffset >= 0 ? "-" : "+";
    var absOffset = Math.abs(timezoneOffset);
    var hours = String(Math.floor(absOffset / 60)).padStart(2, "0");
    var minutes = String(absOffset % 60).padStart(2, "0");
    return `UTC${sign}${hours}${minutes}`;
  };
  var winterName = extractZone(winterOffset);
  var summerName = extractZone(summerOffset);
  assert(winterName);
  assert(summerName);
  assert(lengthBytesUTF8(winterName) <= 16, `timezone name truncated to fit in TZNAME_MAX (${winterName})`);
  assert(lengthBytesUTF8(summerName) <= 16, `timezone name truncated to fit in TZNAME_MAX (${summerName})`);
  if (summerOffset < winterOffset) {
    // Northern hemisphere
    stringToUTF8(winterName, std_name, 17);
    stringToUTF8(summerName, dst_name, 17);
  } else {
    stringToUTF8(winterName, dst_name, 17);
    stringToUTF8(summerName, std_name, 17);
  }
};

var _emscripten_get_now;

// AudioWorkletGlobalScope does not have performance.now()
// (https://github.com/WebAudio/web-audio-api/issues/2527), so if building
// with
// Audio Worklets enabled, do a dynamic check for its presence.
if (globalThis.performance && performance.now) {
  _emscripten_get_now = () => performance.timeOrigin + performance.now();
} else {
  _emscripten_get_now = Date.now;
}

var _emscripten_date_now = () => Date.now();

var nowIsMonotonic = !!globalThis.performance?.now;

var checkWasiClock = clock_id => clock_id >= 0 && clock_id <= 3;

var INT53_MAX = 9007199254740992;

var INT53_MIN = -9007199254740992;

var bigintToI53Checked = num => (num < INT53_MIN || num > INT53_MAX) ? NaN : Number(num);

function _clock_time_get(clk_id, ignored_precision, ptime) {
  ignored_precision = bigintToI53Checked(ignored_precision);
  if (!checkWasiClock(clk_id)) {
    return 28;
  }
  var now;
  // all wasi clocks but realtime are monotonic
  if (clk_id === 0) {
    now = _emscripten_date_now();
  } else if (nowIsMonotonic) {
    now = _emscripten_get_now();
  } else {
    return 52;
  }
  // "now" is in ms, and wasi times are in ns.
  var nsec = Math.round(now * 1e3 * 1e3);
  (growMemViews(), HEAP64)[((ptime) >> 3)] = BigInt(nsec);
  return 0;
}

var readEmAsmArgsArray = [];

var readEmAsmArgs = (sigPtr, buf) => {
  // Nobody should have mutated _readEmAsmArgsArray underneath us to be something else than an array.
  assert(Array.isArray(readEmAsmArgsArray));
  // The input buffer is allocated on the stack, so it must be stack-aligned.
  assert(buf % 16 == 0);
  readEmAsmArgsArray.length = 0;
  var ch;
  // Most arguments are i32s, so shift the buffer pointer so it is a plain
  // index into HEAP32.
  while (ch = (growMemViews(), HEAPU8)[sigPtr++]) {
    var chr = String.fromCharCode(ch);
    var validChars = [ "d", "f", "i", "p" ];
    // In WASM_BIGINT mode we support passing i64 values as bigint.
    validChars.push("j");
    assert(validChars.includes(chr), `Invalid character ${ch}("${chr}") in readEmAsmArgs! Use only [${validChars}], and do not specify "v" for void return argument.`);
    // Floats are always passed as doubles, so all types except for 'i'
    // are 8 bytes and require alignment.
    var wide = (ch != 105);
    wide &= (ch != 112);
    buf += wide && (buf % 8) ? 4 : 0;
    readEmAsmArgsArray.push(// Special case for pointers under wasm64 or CAN_ADDRESS_2GB mode.
    ch == 112 ? (growMemViews(), HEAPU32)[((buf) >> 2)] : ch == 106 ? (growMemViews(), 
    HEAP64)[((buf) >> 3)] : ch == 105 ? (growMemViews(), HEAP32)[((buf) >> 2)] : (growMemViews(), 
    HEAPF64)[((buf) >> 3)]);
    buf += wide ? 8 : 4;
  }
  return readEmAsmArgsArray;
};

var runEmAsmFunction = (code, sigPtr, argbuf) => {
  var args = readEmAsmArgs(sigPtr, argbuf);
  assert(ASM_CONSTS.hasOwnProperty(code), `No EM_ASM constant found at address ${code}.  The loaded WebAssembly file is likely out of sync with the generated JavaScript.`);
  return ASM_CONSTS[code](...args);
};

var _emscripten_asm_const_int = (code, sigPtr, argbuf) => runEmAsmFunction(code, sigPtr, argbuf);

var emAudioExpectNodeOrContext = (handle, methodName) => {
  var obj = _emAudioExpectHandle(handle, methodName);
  assert(obj instanceof window.AudioNode || obj instanceof (window.AudioContext || window.webkitAudioContext), `${methodName}() called with a handle ${handle} that is not an AudioContext or AudioNode, but of type ${typeof obj}`);
};

var emAudioExpectContext = (handle, methodName) => {
  var obj = _emAudioExpectHandle(handle, methodName);
  assert(obj instanceof (window.AudioContext || window.webkitAudioContext), `${methodName}() called with ${handle} that is not an AudioContext, but of type ${typeof obj}`);
};

var emAudioExpectNode = (handle, methodName) => {
  var obj = _emAudioExpectHandle(handle, methodName);
  assert(obj instanceof window.AudioNode, `${methodName}() called with a handle ${handle} that is not an AudioNode, but of type ${typeof obj}`);
};

var _emAudioExpectHandle = (handle, methodName) => {
  var obj = emAudio[handle];
  assert(obj, `Called ${methodName}() on a nonexisting handle ${handle}`);
  return obj;
};

var emscriptenGetContextQuantumSize = contextHandle => emAudio[contextHandle]["renderQuantumSize"] || 128;

var _emscripten_audio_context_quantum_size = contextHandle => {
  emAudioExpectContext(contextHandle, "emscripten_audio_context_quantum_size");
  return emscriptenGetContextQuantumSize(contextHandle);
};

function emAudioWorkletPostFunction(audioContext, funcPtr, args) {
  assert(funcPtr);
  if (audioContext) emAudioExpectContext(audioContext, "emAudioWorkletPostFunction");
  // _wsc = "WaSm Call"
  (audioContext ? emAudio[audioContext].audioWorklet["port"] : port).postMessage({
    "_wsc": funcPtr,
    args
  });
}

var _emscripten_audio_worklet_post_function_sig = (audioContext, funcPtr, sigPtr, varargs) => {
  assert(sigPtr);
  assert(UTF8ToString(sigPtr)[0] != "v", "Do NOT specify the return argument in the signature string for a call to emscripten_audio_worklet_post_function_sig(), just pass the function arguments.");
  assert(varargs);
  emAudioWorkletPostFunction(audioContext, funcPtr, readEmAsmArgs(sigPtr, varargs));
};

var emscripten_audio_worklet_post_function_2 = (audioContext, funcPtr, arg0, arg1) => {
  emAudioWorkletPostFunction(audioContext, funcPtr, [ arg0, arg1 ]);
};

var _emscripten_audio_worklet_post_function_vii = (audioContext, funcPtr, arg0, arg1) => {
  emscripten_audio_worklet_post_function_2(audioContext, funcPtr, arg0, arg1);
};

var emscripten_audio_worklet_post_function_3 = (audioContext, funcPtr, arg0, arg1, arg2) => {
  emAudioWorkletPostFunction(audioContext, funcPtr, [ arg0, arg1, arg2 ]);
};

var _emscripten_audio_worklet_post_function_viii = (audioContext, funcPtr, arg0, arg1, arg2) => {
  emscripten_audio_worklet_post_function_3(audioContext, funcPtr, arg0, arg1, arg2);
};

var _emscripten_check_blocking_allowed = () => {
  if (ENVIRONMENT_IS_NODE) return;
  if (ENVIRONMENT_IS_WORKER) return;
  // Blocking in a worker/pthread is fine.
  warnOnce("Blocking on the main thread is very dangerous, see https://emscripten.org/docs/porting/pthreads.html#blocking-on-the-main-browser-thread");
};

var emAudio = {};

var emAudioCounter = 0;

var emscriptenRegisterAudioObject = object => {
  assert(object, "Called emscriptenRegisterAudioObject() with a null object handle!");
  emAudio[++emAudioCounter] = object;
  return emAudioCounter;
};

var emscriptenGetAudioObject = objectHandle => emAudio[objectHandle];

var _emscripten_create_audio_context = options => {
  // Safari added unprefixed AudioContext support in Safari 14.5 on iOS: https://caniuse.com/audio-api
  var ctx = window.AudioContext || window.webkitAudioContext;
  if (!ctx) console.error("emscripten_create_audio_context failed! Web Audio is not supported.");
  // Converts AUDIO_CONTEXT_RENDER_SIZE_* into AudioContextRenderSizeCategory
  // enums, otherwise returns a positive int value.
  function readRenderSizeHint(val) {
    return (val < 0) ? "hardware" : (val || "default");
  }
  var opts = options ? {
    latencyHint: UTF8ToString((growMemViews(), HEAPU32)[((options) >> 2)]) || undefined,
    sampleRate: (growMemViews(), HEAPU32)[(((options) + (4)) >> 2)] || undefined,
    renderSizeHint: readRenderSizeHint((growMemViews(), HEAP32)[(((options) + (8)) >> 2)])
  } : undefined;
  return ctx && emscriptenRegisterAudioObject(new ctx(opts));
};

var _emscripten_create_wasm_audio_worklet_node = (contextHandle, name, options, callback, userData) => {
  emAudioExpectContext(contextHandle, "emscripten_create_wasm_audio_worklet_node");
  function readChannelCountArray(heapIndex, numOutputs) {
    if (!heapIndex) return undefined;
    heapIndex = ((heapIndex) >> 2);
    var channelCounts = [];
    while (numOutputs--) channelCounts.push((growMemViews(), HEAPU32)[heapIndex++]);
    return channelCounts;
  }
  var optionsOutputs = options ? (growMemViews(), HEAP32)[(((options) + (4)) >> 2)] : 0;
  var opts = options ? {
    numberOfInputs: (growMemViews(), HEAP32)[((options) >> 2)],
    numberOfOutputs: optionsOutputs,
    outputChannelCount: readChannelCountArray((growMemViews(), HEAPU32)[(((options) + (8)) >> 2)], optionsOutputs),
    channelCount: (growMemViews(), HEAPU32)[(((options) + (12)) >> 2)] || undefined,
    channelCountMode: [ , "clamped-max", "explicit" ][(growMemViews(), HEAP32)[(((options) + (16)) >> 2)]],
    channelInterpretation: [ , "discrete" ][(growMemViews(), HEAP32)[(((options) + (20)) >> 2)]],
    processorOptions: {
      callback,
      userData,
      samplesPerChannel: emscriptenGetContextQuantumSize(contextHandle)
    }
  } : undefined;
  return emscriptenRegisterAudioObject(new AudioWorkletNode(emAudio[contextHandle], UTF8ToString(name), opts));
};

var _emscripten_create_wasm_audio_worklet_processor_async = (contextHandle, options, callback, userData) => {
  emAudioExpectContext(contextHandle, "emscripten_create_wasm_audio_worklet_processor_async");
  var processorName = UTF8ToString((growMemViews(), HEAPU32)[((options) >> 2)]);
  var numAudioParams = (growMemViews(), HEAP32)[(((options) + (4)) >> 2)];
  var audioParamDescriptors = (growMemViews(), HEAPU32)[(((options) + (8)) >> 2)];
  var audioParams = [];
  var paramIndex = 0;
  while (numAudioParams--) {
    audioParams.push({
      name: paramIndex++,
      defaultValue: (growMemViews(), HEAPF32)[((audioParamDescriptors) >> 2)],
      minValue: (growMemViews(), HEAPF32)[(((audioParamDescriptors) + (4)) >> 2)],
      maxValue: (growMemViews(), HEAPF32)[(((audioParamDescriptors) + (8)) >> 2)],
      automationRate: ((growMemViews(), HEAP32)[(((audioParamDescriptors) + (12)) >> 2)] ? "k" : "a") + "-rate"
    });
    audioParamDescriptors += 16;
  }
  emAudio[contextHandle].audioWorklet["port"].postMessage({
    // Deliberately mangled and short names used here ('_wpn', the 'Worklet
    // Processor Name' used as a 'key' to verify the message type so as to
    // not get accidentally mixed with user submitted messages, the remainder
    // for space saving reasons, abbreviated from their variable names).
    "_wpn": processorName,
    audioParams,
    contextHandle,
    callback,
    userData
  });
};

var _emscripten_destroy_audio_context = contextHandle => {
  emAudioExpectContext(contextHandle, "emscripten_destroy_audio_context");
  emAudio[contextHandle].suspend();
  delete emAudio[contextHandle];
};

var _emscripten_destroy_web_audio_node = objectHandle => {
  emAudioExpectNode(objectHandle, "emscripten_destroy_web_audio_node");
  // Explicitly disconnect the node from Web Audio graph before letting it GC,
  // to work around browser bugs such as https://webkit.org/b/222098#c23
  emAudio[objectHandle].disconnect();
  delete emAudio[objectHandle];
};

var runtimeKeepalivePush = () => {
  runtimeKeepaliveCounter += 1;
};

var _emscripten_exit_with_live_runtime = () => {
  runtimeKeepalivePush();
  throw "unwind";
};

var getHeapMax = () => // Stay one Wasm page short of 4GB: while e.g. Chrome is able to allocate
// full 4GB Wasm memories, the size will wrap back to 0 bytes in Wasm side
// for any code that deals with heap sizes, which would require special
// casing all heap size related code to treat 0 specially.
2147483648;

var alignMemory = (size, alignment) => {
  assert(alignment, "alignment argument is required");
  return Math.ceil(size / alignment) * alignment;
};

var growMemory = size => {
  var oldHeapSize = wasmMemory.buffer.byteLength;
  var pages = ((size - oldHeapSize + 65535) / 65536) | 0;
  try {
    // round size grow request up to wasm page size (fixed 64KB per spec)
    wasmMemory.grow(pages);
    // .grow() takes a delta compared to the previous size
    updateMemoryViews();
    return 1;
  } catch (e) {
    err(`growMemory: Attempted to grow heap from ${oldHeapSize} bytes to ${size} bytes, but got error: ${e}`);
  }
};

var _emscripten_resize_heap = requestedSize => {
  var oldSize = (growMemViews(), HEAPU8).length;
  // With CAN_ADDRESS_2GB or MEMORY64, pointers are already unsigned.
  requestedSize >>>= 0;
  // With multithreaded builds, races can happen (another thread might increase the size
  // in between), so return a failure, and let the caller retry.
  if (requestedSize <= oldSize) {
    return false;
  }
  // Memory resize rules:
  // 1.  Always increase heap size to at least the requested size, rounded up
  //     to next page multiple.
  // 2a. If MEMORY_GROWTH_LINEAR_STEP == -1, excessively resize the heap
  //     geometrically: increase the heap size according to
  //     MEMORY_GROWTH_GEOMETRIC_STEP factor (default +20%), At most
  //     overreserve by MEMORY_GROWTH_GEOMETRIC_CAP bytes (default 96MB).
  // 2b. If MEMORY_GROWTH_LINEAR_STEP != -1, excessively resize the heap
  //     linearly: increase the heap size by at least
  //     MEMORY_GROWTH_LINEAR_STEP bytes.
  // 3.  Max size for the heap is capped at 2048MB-WASM_PAGE_SIZE, or by
  //     MAXIMUM_MEMORY, or by ASAN limit, depending on which is smallest
  // 4.  If we were unable to allocate as much memory, it may be due to
  //     over-eager decision to excessively reserve due to (3) above.
  //     Hence if an allocation fails, cut down on the amount of excess
  //     growth, in an attempt to succeed to perform a smaller allocation.
  // A limit is set for how much we can grow. We should not exceed that
  // (the wasm binary specifies it, so if we tried, we'd fail anyhow).
  var maxHeapSize = getHeapMax();
  if (requestedSize > maxHeapSize) {
    err(`Cannot enlarge memory, requested ${requestedSize} bytes, but the limit is ${maxHeapSize} bytes!`);
    return false;
  }
  // Loop through potential heap size increases. If we attempt a too eager
  // reservation that fails, cut down on the attempted size and reserve a
  // smaller bump instead. (max 3 times, chosen somewhat arbitrarily)
  for (var cutDown = 1; cutDown <= 4; cutDown *= 2) {
    var overGrownHeapSize = oldSize * (1 + .2 / cutDown);
    // ensure geometric growth
    // but limit overreserving (default to capping at +96MB overgrowth at most)
    overGrownHeapSize = Math.min(overGrownHeapSize, requestedSize + 100663296);
    var newSize = Math.min(maxHeapSize, alignMemory(Math.max(requestedSize, overGrownHeapSize), 65536));
    var replacement = growMemory(newSize);
    if (replacement) {
      return true;
    }
  }
  err(`Failed to grow the heap from ${oldSize} bytes to ${newSize} bytes, not enough memory!`);
  return false;
};

var runtimeKeepalivePop = () => {
  assert(runtimeKeepaliveCounter > 0);
  runtimeKeepaliveCounter -= 1;
};

/** @param {number=} timeout */ var safeSetTimeout = (func, timeout) => {
  runtimeKeepalivePush();
  return setTimeout(() => {
    runtimeKeepalivePop();
    callUserCallback(func);
  }, timeout);
};

var _emscripten_sleep = ms => Asyncify.handleSleep(wakeUp => safeSetTimeout(wakeUp, ms));

_emscripten_sleep.isAsync = true;

var _wasmWorkersID = 1;

var _emAudioDispatchProcessorCallback = e => {
  var data = e.data;
  // '_wsc' is short for 'wasm call', trying to use an identifier name that
  // will never conflict with user code. This is used to call both the 3-param
  // call (handle, true, userData) and the variable argument post functions.
  var wasmCall = data["_wsc"];
  wasmCall && getWasmTableEntry(wasmCall)(...data.args);
};

var _emscripten_start_wasm_audio_worklet_thread_async = (contextHandle, stackLowestAddress, stackSize, callback, userData) => {
  emAudioExpectContext(contextHandle, "emscripten_start_wasm_audio_worklet_thread_async");
  var audioContext = emAudio[contextHandle];
  var audioWorklet = audioContext.audioWorklet;
  assert(stackLowestAddress != 0, "AudioWorklets require a dedicated stack space for audio data marshalling between Wasm and JS!");
  assert(stackLowestAddress % 16 == 0, `AudioWorklet stack should be aligned to 16 bytes! (was ${stackLowestAddress} == ${stackLowestAddress % 16} mod 16) Use e.g. memalign(16, stackSize) to align the stack!`);
  assert(stackSize != 0, "AudioWorklets require a dedicated stack space for audio data marshalling between Wasm and JS!");
  assert(stackSize % 16 == 0, `AudioWorklet stack size should be a multiple of 16 bytes! (was ${stackSize} == ${stackSize % 16} mod 16)`);
  assert(!audioContext.audioWorkletInitialized, "emscripten_create_wasm_audio_worklet() was already called for AudioContext " + contextHandle + "! Only call this function once per AudioContext!");
  audioContext.audioWorkletInitialized = 1;
  var audioWorkletCreationFailed = () => {
    dbg(`emscripten_start_wasm_audio_worklet_thread_async() addModule() failed!`);
    ((a1, a2, a3) => dynCall_viii(callback, a1, a2, a3))(contextHandle, 0, userData);
  };
  // Does browser not support AudioWorklets?
  if (!audioWorklet) {
    if (location.protocol == "http:") {
      console.error(`AudioWorklets are not supported. This is possibly due to running the page over unsecure http:// protocol. Try running over https://, or debug via a localhost-based server, which should also allow AudioWorklets to function.`);
    } else {
      console.error(`AudioWorklets are not supported by current browser.`);
    }
    return audioWorkletCreationFailed();
  }
  audioWorklet.addModule(locateFile("libflutter_soloud_plugin_mt.js")).then(() => {
    // If this browser does not support the up-to-date AudioWorklet standard
    // that has a MessagePort over to the AudioWorklet, then polyfill that by
    // instantiating a dummy AudioWorkletNode to get a MessagePort over.
    // Firefox added support in https://hg-edge.mozilla.org/integration/autoland/rev/ab38a1796126f2b3fc06475ffc5a625059af59c1
    // Chrome ticket: https://crbug.com/446920095
    // Safari ticket: https://webkit.org/b/299386
    if (!audioWorklet["port"]) {
      audioWorklet["port"] = {
        postMessage: msg => {
          if (msg["_boot"]) {
            audioWorklet.bootstrapMessage = new AudioWorkletNode(audioContext, "em-bootstrap", {
              processorOptions: msg
            });
            audioWorklet.bootstrapMessage["port"].onmessage = msg => {
              audioWorklet["port"].onmessage(msg);
            };
          } else {
            audioWorklet.bootstrapMessage["port"].postMessage(msg);
          }
        }
      };
    }
    audioWorklet["port"].postMessage({
      // This is the bootstrap message to the Audio Worklet.
      "_boot": 1,
      // Assign the loaded AudioWorkletGlobalScope a Wasm Worker ID so that
      // it can utilized its own TLS slots, and it is recognized to not be
      // the main browser thread.
      wwID: _wasmWorkersID++,
      wasm: wasmModule,
      wasmMemory,
      stackLowestAddress,
      // sb = stack base
      stackSize
    });
    audioWorklet["port"].onmessage = _emAudioDispatchProcessorCallback;
    ((a1, a2, a3) => dynCall_viii(callback, a1, a2, a3))(contextHandle, 1, userData);
  }).catch(audioWorkletCreationFailed);
};

var ENV = {};

var getExecutableName = () => thisProgram || "./this.program";

var getEnvStrings = () => {
  if (!getEnvStrings.strings) {
    // Default values.
    // Browser language detection #8751
    var lang = (globalThis.navigator?.language ?? "C").replace("-", "_") + ".UTF-8";
    var env = {
      "USER": "web_user",
      "LOGNAME": "web_user",
      "PATH": "/",
      "PWD": "/",
      "HOME": "/home/web_user",
      "LANG": lang,
      "_": getExecutableName()
    };
    // Apply the user-provided values, if any.
    for (var x in ENV) {
      // x is a key in ENV; if ENV[x] is undefined, that means it was
      // explicitly set to be so. We allow user code to do that to
      // force variables with default values to remain unset.
      if (ENV[x] === undefined) delete env[x]; else env[x] = ENV[x];
    }
    var strings = [];
    for (var x in env) {
      strings.push(`${x}=${env[x]}`);
    }
    getEnvStrings.strings = strings;
  }
  return getEnvStrings.strings;
};

function _environ_get(__environ, environ_buf) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(6, 0, 1, __environ, environ_buf);
  var bufSize = 0;
  var envp = 0;
  for (var string of getEnvStrings()) {
    var ptr = environ_buf + bufSize;
    (growMemViews(), HEAPU32)[(((__environ) + (envp)) >> 2)] = ptr;
    bufSize += stringToUTF8(string, ptr, Infinity) + 1;
    envp += 4;
  }
  return 0;
}

function _environ_sizes_get(penviron_count, penviron_buf_size) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(7, 0, 1, penviron_count, penviron_buf_size);
  var strings = getEnvStrings();
  (growMemViews(), HEAPU32)[((penviron_count) >> 2)] = strings.length;
  var bufSize = 0;
  for (var string of strings) {
    bufSize += lengthBytesUTF8(string) + 1;
  }
  (growMemViews(), HEAPU32)[((penviron_buf_size) >> 2)] = bufSize;
  return 0;
}

function _fd_close(fd) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(8, 0, 1, fd);
  try {
    var stream = SYSCALLS.getStreamFromFD(fd);
    FS.close(stream);
    return 0;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return e.errno;
  }
}

/** @param {number=} offset */ var doReadv = (stream, iov, iovcnt, offset) => {
  var ret = 0;
  for (var i = 0; i < iovcnt; i++) {
    var ptr = (growMemViews(), HEAPU32)[((iov) >> 2)];
    var len = (growMemViews(), HEAPU32)[(((iov) + (4)) >> 2)];
    iov += 8;
    var curr = FS.read(stream, (growMemViews(), HEAP8), ptr, len, offset);
    if (curr < 0) return -1;
    ret += curr;
    if (curr < len) break;
    // nothing more to read
    if (typeof offset != "undefined") {
      offset += curr;
    }
  }
  return ret;
};

function _fd_read(fd, iov, iovcnt, pnum) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(9, 0, 1, fd, iov, iovcnt, pnum);
  try {
    var stream = SYSCALLS.getStreamFromFD(fd);
    var num = doReadv(stream, iov, iovcnt);
    (growMemViews(), HEAPU32)[((pnum) >> 2)] = num;
    return 0;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return e.errno;
  }
}

function _fd_seek(fd, offset, whence, newOffset) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(10, 0, 1, fd, offset, whence, newOffset);
  offset = bigintToI53Checked(offset);
  try {
    if (isNaN(offset)) return 61;
    var stream = SYSCALLS.getStreamFromFD(fd);
    FS.llseek(stream, offset, whence);
    (growMemViews(), HEAP64)[((newOffset) >> 3)] = BigInt(stream.position);
    if (stream.getdents && offset === 0 && whence === 0) stream.getdents = null;
    // reset readdir state
    return 0;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return e.errno;
  }
}

/** @param {number=} offset */ var doWritev = (stream, iov, iovcnt, offset) => {
  var ret = 0;
  for (var i = 0; i < iovcnt; i++) {
    var ptr = (growMemViews(), HEAPU32)[((iov) >> 2)];
    var len = (growMemViews(), HEAPU32)[(((iov) + (4)) >> 2)];
    iov += 8;
    var curr = FS.write(stream, (growMemViews(), HEAP8), ptr, len, offset);
    if (curr < 0) return -1;
    ret += curr;
    if (curr < len) {
      // No more space to write.
      break;
    }
    if (typeof offset != "undefined") {
      offset += curr;
    }
  }
  return ret;
};

function _fd_write(fd, iov, iovcnt, pnum) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(11, 0, 1, fd, iov, iovcnt, pnum);
  try {
    var stream = SYSCALLS.getStreamFromFD(fd);
    var num = doWritev(stream, iov, iovcnt);
    (growMemViews(), HEAPU32)[((pnum) >> 2)] = num;
    return 0;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return e.errno;
  }
}

function _random_get(buffer, size) {
  try {
    randomFill((growMemViews(), HEAPU8).subarray(buffer, buffer + size));
    return 0;
  } catch (e) {
    if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
    return e.errno;
  }
}

var runAndAbortIfError = func => {
  try {
    return func();
  } catch (e) {
    abort(e);
  }
};

var createNamedFunction = (name, func) => Object.defineProperty(func, "name", {
  value: name
});

var Asyncify = {
  instrumentWasmImports(imports) {
    var importPattern = /^(invoke_.*|__asyncjs__.*)$/;
    for (let [x, original] of Object.entries(imports)) {
      if (typeof original == "function") {
        let isAsyncifyImport = original.isAsync || importPattern.test(x);
        imports[x] = (...args) => {
          var originalAsyncifyState = Asyncify.state;
          try {
            return original(...args);
          } finally {
            // Only asyncify-declared imports are allowed to change the
            // state.
            // Changing the state from normal to disabled is allowed (in any
            // function) as that is what shutdown does (and we don't have an
            // explicit list of shutdown imports).
            var changedToDisabled = originalAsyncifyState === Asyncify.State.Normal && Asyncify.state === Asyncify.State.Disabled;
            // invoke_* functions are allowed to change the state if we do
            // not ignore indirect calls.
            var ignoredInvoke = x.startsWith("invoke_") && true;
            if (Asyncify.state !== originalAsyncifyState && !isAsyncifyImport && !changedToDisabled && !ignoredInvoke) {
              abort(`import ${x} was not in ASYNCIFY_IMPORTS, but changed the state`);
            }
          }
        };
      }
    }
  },
  instrumentFunction(original) {
    var wrapper = (...args) => {
      Asyncify.exportCallStack.push(original);
      try {
        return original(...args);
      } finally {
        if (!ABORT) {
          var top = Asyncify.exportCallStack.pop();
          assert(top === original);
          Asyncify.maybeStopUnwind();
        }
      }
    };
    Asyncify.funcWrappers.set(original, wrapper);
    wrapper = createNamedFunction(`__asyncify_wrapper_${original.name}`, wrapper);
    return wrapper;
  },
  instrumentWasmExports(exports) {
    var ret = {};
    for (let [x, original] of Object.entries(exports)) {
      if (typeof original == "function") {
        var wrapper = Asyncify.instrumentFunction(original);
        ret[x] = wrapper;
      } else {
        ret[x] = original;
      }
    }
    return ret;
  },
  State: {
    Normal: 0,
    Unwinding: 1,
    Rewinding: 2,
    Disabled: 3
  },
  state: 0,
  StackSize: 65536,
  currData: null,
  handleSleepReturnValue: 0,
  exportCallStack: [],
  callstackFuncToId: new Map,
  callStackIdToFunc: new Map,
  funcWrappers: new Map,
  callStackId: 0,
  asyncPromiseHandlers: null,
  sleepCallbacks: [],
  getCallStackId(func) {
    assert(func);
    if (!Asyncify.callstackFuncToId.has(func)) {
      var id = Asyncify.callStackId++;
      Asyncify.callstackFuncToId.set(func, id);
      Asyncify.callStackIdToFunc.set(id, func);
    }
    return Asyncify.callstackFuncToId.get(func);
  },
  maybeStopUnwind() {
    if (Asyncify.currData && Asyncify.state === Asyncify.State.Unwinding && Asyncify.exportCallStack.length === 0) {
      // We just finished unwinding.
      // Be sure to set the state before calling any other functions to avoid
      // possible infinite recursion here (For example in debug pthread builds
      // the dbg() function itself can call back into WebAssembly to get the
      // current pthread_self() pointer).
      Asyncify.state = Asyncify.State.Normal;
      runtimeKeepalivePush();
      // Keep the runtime alive so that a re-wind can be done later.
      runAndAbortIfError(_asyncify_stop_unwind);
      if (typeof Fibers != "undefined") {
        Fibers.trampoline();
      }
    }
  },
  whenDone() {
    assert(Asyncify.currData, "Tried to wait for an async operation when none is in progress.");
    assert(!Asyncify.asyncPromiseHandlers, "Cannot have multiple async operations in flight at once");
    return new Promise((resolve, reject) => {
      Asyncify.asyncPromiseHandlers = {
        resolve,
        reject
      };
    });
  },
  allocateData() {
    // An asyncify data structure has three fields:
    //  0  current stack pos
    //  4  max stack pos
    //  8  id of function at bottom of the call stack (callStackIdToFunc[id] == wasm func)
    // The Asyncify ABI only interprets the first two fields, the rest is for the runtime.
    // We also embed a stack in the same memory region here, right next to the structure.
    // This struct is also defined as asyncify_data_t in emscripten/fiber.h
    var ptr = _malloc(12 + Asyncify.StackSize);
    Asyncify.setDataHeader(ptr, ptr + 12, Asyncify.StackSize);
    Asyncify.setDataRewindFunc(ptr);
    return ptr;
  },
  setDataHeader(ptr, stack, stackSize) {
    (growMemViews(), HEAPU32)[((ptr) >> 2)] = stack;
    (growMemViews(), HEAPU32)[(((ptr) + (4)) >> 2)] = stack + stackSize;
  },
  setDataRewindFunc(ptr) {
    var bottomOfCallStack = Asyncify.exportCallStack[0];
    assert(bottomOfCallStack, "exportCallStack is empty");
    var rewindId = Asyncify.getCallStackId(bottomOfCallStack);
    (growMemViews(), HEAP32)[(((ptr) + (8)) >> 2)] = rewindId;
  },
  getDataRewindFunc(ptr) {
    var id = (growMemViews(), HEAP32)[(((ptr) + (8)) >> 2)];
    var func = Asyncify.callStackIdToFunc.get(id);
    assert(func, `id ${id} not found in callStackIdToFunc`);
    return func;
  },
  doRewind(ptr) {
    var original = Asyncify.getDataRewindFunc(ptr);
    var func = Asyncify.funcWrappers.get(original);
    assert(original);
    assert(func);
    // Once we have rewound and the stack we no longer need to artificially
    // keep the runtime alive.
    runtimeKeepalivePop();
    return func();
  },
  handleSleep(startAsync) {
    assert(Asyncify.state !== Asyncify.State.Disabled, "Asyncify cannot be done during or after the runtime exits");
    if (ABORT) return;
    if (Asyncify.state === Asyncify.State.Normal) {
      // Prepare to sleep. Call startAsync, and see what happens:
      // if the code decided to call our callback synchronously,
      // then no async operation was in fact begun, and we don't
      // need to do anything.
      var reachedCallback = false;
      var reachedAfterCallback = false;
      startAsync((handleSleepReturnValue = 0) => {
        assert(!handleSleepReturnValue || typeof handleSleepReturnValue == "number" || typeof handleSleepReturnValue == "boolean");
        // old emterpretify API supported other stuff
        if (ABORT) return;
        Asyncify.handleSleepReturnValue = handleSleepReturnValue;
        reachedCallback = true;
        if (!reachedAfterCallback) {
          // We are happening synchronously, so no need for async.
          return;
        }
        // This async operation did not happen synchronously, so we did
        // unwind. In that case there can be no compiled code on the stack,
        // as it might break later operations (we can rewind ok now, but if
        // we unwind again, we would unwind through the extra compiled code
        // too).
        assert(!Asyncify.exportCallStack.length, "Waking up (starting to rewind) must be done from JS, without compiled code on the stack.");
        Asyncify.state = Asyncify.State.Rewinding;
        runAndAbortIfError(() => _asyncify_start_rewind(Asyncify.currData));
        if (typeof MainLoop != "undefined" && MainLoop.func) {
          MainLoop.resume();
        }
        var asyncWasmReturnValue, isError = false;
        try {
          asyncWasmReturnValue = Asyncify.doRewind(Asyncify.currData);
        } catch (err) {
          asyncWasmReturnValue = err;
          isError = true;
        }
        // Track whether the return value was handled by any promise handlers.
        var handled = false;
        if (!Asyncify.currData) {
          // All asynchronous execution has finished.
          // `asyncWasmReturnValue` now contains the final
          // return value of the exported async WASM function.
          // Note: `asyncWasmReturnValue` is distinct from
          // `Asyncify.handleSleepReturnValue`.
          // `Asyncify.handleSleepReturnValue` contains the return
          // value of the last C function to have executed
          // `Asyncify.handleSleep()`, where as `asyncWasmReturnValue`
          // contains the return value of the exported WASM function
          // that may have called C functions that
          // call `Asyncify.handleSleep()`.
          var asyncPromiseHandlers = Asyncify.asyncPromiseHandlers;
          if (asyncPromiseHandlers) {
            Asyncify.asyncPromiseHandlers = null;
            (isError ? asyncPromiseHandlers.reject : asyncPromiseHandlers.resolve)(asyncWasmReturnValue);
            handled = true;
          }
        }
        if (isError && !handled) {
          // If there was an error and it was not handled by now, we have no choice but to
          // rethrow that error into the global scope where it can be caught only by
          // `onerror` or `onunhandledpromiserejection`.
          throw asyncWasmReturnValue;
        }
      });
      reachedAfterCallback = true;
      if (!reachedCallback) {
        // A true async operation was begun; start a sleep.
        Asyncify.state = Asyncify.State.Unwinding;
        // TODO: reuse, don't alloc/free every sleep
        Asyncify.currData = Asyncify.allocateData();
        if (typeof MainLoop != "undefined" && MainLoop.func) {
          MainLoop.pause();
        }
        runAndAbortIfError(() => _asyncify_start_unwind(Asyncify.currData));
      }
    } else if (Asyncify.state === Asyncify.State.Rewinding) {
      // Stop a resume.
      Asyncify.state = Asyncify.State.Normal;
      runAndAbortIfError(_asyncify_stop_rewind);
      _free(Asyncify.currData);
      Asyncify.currData = null;
      // Call all sleep callbacks now that the sleep-resume is all done.
      Asyncify.sleepCallbacks.forEach(callUserCallback);
    } else {
      abort(`invalid state: ${Asyncify.state}`);
    }
    return Asyncify.handleSleepReturnValue;
  },
  handleAsync: startAsync => Asyncify.handleSleep(wakeUp => {
    // TODO: add error handling as a second param when handleSleep implements it.
    startAsync().then(wakeUp);
  })
};

var getCFunc = ident => {
  var func = Module["_" + ident];
  // closure exported function
  assert(func, "Cannot call unknown function " + ident + ", make sure it is exported");
  return func;
};

var writeArrayToMemory = (array, buffer) => {
  assert(array.length >= 0, "writeArrayToMemory array must have a length (should be an array or typed array)");
  (growMemViews(), HEAP8).set(array, buffer);
};

var stringToUTF8OnStack = str => {
  var size = lengthBytesUTF8(str) + 1;
  var ret = stackAlloc(size);
  stringToUTF8(str, ret, size);
  return ret;
};

/**
     * @param {string|null=} returnType
     * @param {Array=} argTypes
     * @param {Array=} args
     * @param {Object=} opts
     */ var ccall = (ident, returnType, argTypes, args, opts) => {
  // For fast lookup of conversion functions
  var toC = {
    "string": str => {
      var ret = 0;
      if (str !== null && str !== undefined && str !== 0) {
        // null string
        ret = stringToUTF8OnStack(str);
      }
      return ret;
    },
    "array": arr => {
      var ret = stackAlloc(arr.length);
      writeArrayToMemory(arr, ret);
      return ret;
    }
  };
  function convertReturnValue(ret) {
    if (returnType === "string") {
      return UTF8ToString(ret);
    }
    if (returnType === "boolean") return Boolean(ret);
    return ret;
  }
  var func = getCFunc(ident);
  var cArgs = [];
  var stack = 0;
  assert(returnType !== "array", 'Return type should not be "array".');
  if (args) {
    for (var i = 0; i < args.length; i++) {
      var converter = toC[argTypes[i]];
      if (converter) {
        if (stack === 0) stack = stackSave();
        cArgs[i] = converter(args[i]);
      } else {
        cArgs[i] = args[i];
      }
    }
  }
  // Data for a previous async operation that was in flight before us.
  var previousAsync = Asyncify.currData;
  var ret = func(...cArgs);
  function onDone(ret) {
    runtimeKeepalivePop();
    if (stack !== 0) stackRestore(stack);
    return convertReturnValue(ret);
  }
  var asyncMode = opts?.async;
  // Keep the runtime alive through all calls. Note that this call might not be
  // async, but for simplicity we push and pop in all calls.
  runtimeKeepalivePush();
  if (Asyncify.currData != previousAsync) {
    // A change in async operation happened. If there was already an async
    // operation in flight before us, that is an error: we should not start
    // another async operation while one is active, and we should not stop one
    // either. The only valid combination is to have no change in the async
    // data (so we either had one in flight and left it alone, or we didn't have
    // one), or to have nothing in flight and to start one.
    assert(!(previousAsync && Asyncify.currData), "We cannot start an async operation when one is already flight");
    assert(!(previousAsync && !Asyncify.currData), "We cannot stop an async operation in flight");
    // This is a new async operation. The wasm is paused and has unwound its stack.
    // We need to return a Promise that resolves the return value
    // once the stack is rewound and execution finishes.
    assert(asyncMode, "The call to " + ident + " is running asynchronously. If this was intended, add the async option to the ccall/cwrap call.");
    return Asyncify.whenDone().then(onDone);
  }
  ret = onDone(ret);
  // If this is an async ccall, ensure we return a promise
  if (asyncMode) return Promise.resolve(ret);
  return ret;
};

/**
     * @param {string=} returnType
     * @param {Array=} argTypes
     * @param {Object=} opts
     */ var cwrap = (ident, returnType, argTypes, opts) => (...args) => ccall(ident, returnType, argTypes, args, opts);

PThread.init();

FS.createPreloadedFile = FS_createPreloadedFile;

FS.preloadFile = FS_preloadFile;

FS.staticInit();

// End JS library code
// include: postlibrary.js
// This file is included after the automatically-generated JS library code
// but before the wasm module is created.
{
  // With WASM_ESM_INTEGRATION this has to happen at the top level and not
  // delayed until processModuleArgs.
  initMemory();
  // Begin ATMODULES hooks
  if (Module["noExitRuntime"]) noExitRuntime = Module["noExitRuntime"];
  if (Module["preloadPlugins"]) preloadPlugins = Module["preloadPlugins"];
  if (Module["print"]) out = Module["print"];
  if (Module["printErr"]) err = Module["printErr"];
  if (Module["wasmBinary"]) wasmBinary = Module["wasmBinary"];
  // End ATMODULES hooks
  checkIncomingModuleAPI();
  if (Module["arguments"]) arguments_ = Module["arguments"];
  if (Module["thisProgram"]) thisProgram = Module["thisProgram"];
  // Assertions on removed incoming Module JS APIs.
  assert(typeof Module["memoryInitializerPrefixURL"] == "undefined", "Module.memoryInitializerPrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["pthreadMainPrefixURL"] == "undefined", "Module.pthreadMainPrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["cdInitializerPrefixURL"] == "undefined", "Module.cdInitializerPrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["filePackagePrefixURL"] == "undefined", "Module.filePackagePrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["read"] == "undefined", "Module.read option was removed");
  assert(typeof Module["readAsync"] == "undefined", "Module.readAsync option was removed (modify readAsync in JS)");
  assert(typeof Module["readBinary"] == "undefined", "Module.readBinary option was removed (modify readBinary in JS)");
  assert(typeof Module["setWindowTitle"] == "undefined", "Module.setWindowTitle option was removed (modify emscripten_set_window_title in JS)");
  assert(typeof Module["TOTAL_MEMORY"] == "undefined", "Module.TOTAL_MEMORY has been renamed Module.INITIAL_MEMORY");
  assert(typeof Module["ENVIRONMENT"] == "undefined", "Module.ENVIRONMENT has been deprecated. To force the environment, use the ENVIRONMENT compile-time option (for example, -sENVIRONMENT=web or -sENVIRONMENT=node)");
  assert(typeof Module["STACK_SIZE"] == "undefined", "STACK_SIZE can no longer be set at runtime.  Use -sSTACK_SIZE at link time");
  if (Module["preInit"]) {
    if (typeof Module["preInit"] == "function") Module["preInit"] = [ Module["preInit"] ];
    while (Module["preInit"].length > 0) {
      Module["preInit"].shift()();
    }
  }
  consumedModuleProp("preInit");
}

// Begin runtime exports
Module["ccall"] = ccall;

Module["cwrap"] = cwrap;

Module["setValue"] = setValue;

Module["getValue"] = getValue;

Module["UTF8ToString"] = UTF8ToString;

var missingLibrarySymbols = [ "writeI53ToI64", "writeI53ToI64Clamped", "writeI53ToI64Signaling", "writeI53ToU64Clamped", "writeI53ToU64Signaling", "readI53FromI64", "readI53FromU64", "convertI32PairToI53", "convertI32PairToI53Checked", "convertU32PairToI53", "getTempRet0", "setTempRet0", "zeroMemory", "withStackSave", "inetPton4", "inetNtop4", "inetPton6", "inetNtop6", "readSockaddr", "writeSockaddr", "runMainThreadEmAsm", "jstoi_q", "autoResumeAudioContext", "getDynCaller", "asmjsMangle", "HandleAllocator", "addOnInit", "addOnPostCtor", "addOnPreMain", "addOnExit", "STACK_SIZE", "STACK_ALIGN", "POINTER_SIZE", "ASSERTIONS", "convertJsFunctionToWasm", "getEmptyTableSlot", "updateTableMap", "getFunctionAddress", "addFunction", "removeFunction", "intArrayToString", "AsciiToString", "stringToAscii", "UTF16ToString", "stringToUTF16", "lengthBytesUTF16", "UTF32ToString", "stringToUTF32", "lengthBytesUTF32", "stringToNewUTF8", "registerKeyEventCallback", "maybeCStringToJsString", "findEventTarget", "getBoundingClientRect", "fillMouseEventData", "registerMouseEventCallback", "registerWheelEventCallback", "registerUiEventCallback", "registerFocusEventCallback", "fillDeviceOrientationEventData", "registerDeviceOrientationEventCallback", "fillDeviceMotionEventData", "registerDeviceMotionEventCallback", "screenOrientation", "fillOrientationChangeEventData", "registerOrientationChangeEventCallback", "fillFullscreenChangeEventData", "registerFullscreenChangeEventCallback", "JSEvents_requestFullscreen", "JSEvents_resizeCanvasForFullscreen", "registerRestoreOldStyle", "hideEverythingExceptGivenElement", "restoreHiddenElements", "setLetterbox", "softFullscreenResizeWebGLRenderTarget", "doRequestFullscreen", "fillPointerlockChangeEventData", "registerPointerlockChangeEventCallback", "registerPointerlockErrorEventCallback", "requestPointerLock", "fillVisibilityChangeEventData", "registerVisibilityChangeEventCallback", "registerTouchEventCallback", "fillGamepadEventData", "registerGamepadEventCallback", "registerBeforeUnloadEventCallback", "fillBatteryEventData", "registerBatteryEventCallback", "setCanvasElementSizeCallingThread", "setCanvasElementSizeMainThread", "setCanvasElementSize", "getCanvasSizeCallingThread", "getCanvasSizeMainThread", "getCanvasElementSize", "convertPCtoSourceLocation", "wasiRightsToMuslOFlags", "wasiOFlagsToMuslOFlags", "setImmediateWrapped", "safeRequestAnimationFrame", "clearImmediateWrapped", "registerPostMainLoop", "registerPreMainLoop", "getPromise", "makePromise", "idsToPromises", "makePromiseCallback", "findMatchingCatch", "Browser_asyncPrepareDataCounter", "isLeapYear", "ydayFromDate", "arraySum", "addDays", "getSocketFromFD", "getSocketAddress", "FS_mkdirTree", "_setNetworkCallback", "heapObjectForWebGLType", "toTypedArrayIndex", "webgl_enable_ANGLE_instanced_arrays", "webgl_enable_OES_vertex_array_object", "webgl_enable_WEBGL_draw_buffers", "webgl_enable_WEBGL_multi_draw", "webgl_enable_EXT_polygon_offset_clamp", "webgl_enable_EXT_clip_control", "webgl_enable_WEBGL_polygon_mode", "emscriptenWebGLGet", "computeUnpackAlignedImageSize", "colorChannelsInGlTextureFormat", "emscriptenWebGLGetTexPixelData", "emscriptenWebGLGetUniform", "webglGetUniformLocation", "webglPrepareUniformLocationsBeforeFirstUse", "webglGetLeftBracePos", "emscriptenWebGLGetVertexAttrib", "__glGetActiveAttribOrUniform", "writeGLArray", "emscripten_webgl_destroy_context_before_on_calling_thread", "registerWebGlEventCallback", "ALLOC_NORMAL", "ALLOC_STACK", "allocate", "writeStringToMemory", "writeAsciiToMemory", "allocateUTF8", "allocateUTF8OnStack", "demangle", "stackTrace", "getNativeTypeSize", "_wasmWorkerPostFunction1", "_wasmWorkerPostFunction2", "_wasmWorkerPostFunction3", "emscripten_audio_worklet_post_function_1" ];

missingLibrarySymbols.forEach(missingLibrarySymbol);

var unexportedSymbols = [ "run", "out", "err", "callMain", "abort", "wasmExports", "HEAPF64", "HEAP8", "HEAP16", "HEAPU16", "HEAP32", "HEAPU32", "HEAP64", "HEAPU64", "writeStackCookie", "checkStackCookie", "INT53_MAX", "INT53_MIN", "bigintToI53Checked", "stackSave", "stackRestore", "stackAlloc", "createNamedFunction", "ptrToString", "exitJS", "getHeapMax", "growMemory", "ENV", "ERRNO_CODES", "strError", "DNS", "Protocols", "Sockets", "timers", "warnOnce", "readEmAsmArgsArray", "readEmAsmArgs", "runEmAsmFunction", "getExecutableName", "dynCallLegacy", "dynCall", "handleException", "keepRuntimeAlive", "runtimeKeepalivePush", "runtimeKeepalivePop", "callUserCallback", "maybeExit", "asyncLoad", "alignMemory", "mmapAlloc", "wasmTable", "wasmMemory", "getUniqueRunDependency", "noExitRuntime", "addRunDependency", "removeRunDependency", "addOnPreRun", "addOnPostRun", "freeTableIndexes", "functionsInTableMap", "PATH", "PATH_FS", "UTF8Decoder", "UTF8ArrayToString", "stringToUTF8Array", "stringToUTF8", "lengthBytesUTF8", "intArrayFromString", "UTF16Decoder", "stringToUTF8OnStack", "writeArrayToMemory", "JSEvents", "specialHTMLTargets", "findCanvasEventTarget", "currentFullscreenStrategy", "restoreOldWindowedStyle", "jsStackTrace", "getCallstack", "UNWIND_CACHE", "ExitStatus", "getEnvStrings", "checkWasiClock", "doReadv", "doWritev", "initRandomFill", "randomFill", "safeSetTimeout", "emSetImmediate", "emClearImmediate_deps", "emClearImmediate", "promiseMap", "uncaughtExceptionCount", "exceptionLast", "exceptionCaught", "ExceptionInfo", "Browser", "requestFullscreen", "requestFullScreen", "setCanvasSize", "getUserMedia", "createContext", "getPreloadedImageData__data", "wget", "MONTH_DAYS_REGULAR", "MONTH_DAYS_LEAP", "MONTH_DAYS_REGULAR_CUMULATIVE", "MONTH_DAYS_LEAP_CUMULATIVE", "SYSCALLS", "preloadPlugins", "FS_createPreloadedFile", "FS_preloadFile", "FS_modeStringToFlags", "FS_getMode", "FS_stdin_getChar_buffer", "FS_stdin_getChar", "FS_unlink", "FS_createPath", "FS_createDevice", "FS_readFile", "FS", "FS_root", "FS_mounts", "FS_devices", "FS_streams", "FS_nextInode", "FS_nameTable", "FS_currentPath", "FS_initialized", "FS_ignorePermissions", "FS_filesystems", "FS_syncFSRequests", "FS_readFiles", "FS_lookupPath", "FS_getPath", "FS_hashName", "FS_hashAddNode", "FS_hashRemoveNode", "FS_lookupNode", "FS_createNode", "FS_destroyNode", "FS_isRoot", "FS_isMountpoint", "FS_isFile", "FS_isDir", "FS_isLink", "FS_isChrdev", "FS_isBlkdev", "FS_isFIFO", "FS_isSocket", "FS_flagsToPermissionString", "FS_nodePermissions", "FS_mayLookup", "FS_mayCreate", "FS_mayDelete", "FS_mayOpen", "FS_checkOpExists", "FS_nextfd", "FS_getStreamChecked", "FS_getStream", "FS_createStream", "FS_closeStream", "FS_dupStream", "FS_doSetAttr", "FS_chrdev_stream_ops", "FS_major", "FS_minor", "FS_makedev", "FS_registerDevice", "FS_getDevice", "FS_getMounts", "FS_syncfs", "FS_mount", "FS_unmount", "FS_lookup", "FS_mknod", "FS_statfs", "FS_statfsStream", "FS_statfsNode", "FS_create", "FS_mkdir", "FS_mkdev", "FS_symlink", "FS_rename", "FS_rmdir", "FS_readdir", "FS_readlink", "FS_stat", "FS_fstat", "FS_lstat", "FS_doChmod", "FS_chmod", "FS_lchmod", "FS_fchmod", "FS_doChown", "FS_chown", "FS_lchown", "FS_fchown", "FS_doTruncate", "FS_truncate", "FS_ftruncate", "FS_utime", "FS_open", "FS_close", "FS_isClosed", "FS_llseek", "FS_read", "FS_write", "FS_mmap", "FS_msync", "FS_ioctl", "FS_writeFile", "FS_cwd", "FS_chdir", "FS_createDefaultDirectories", "FS_createDefaultDevices", "FS_createSpecialDirectories", "FS_createStandardStreams", "FS_staticInit", "FS_init", "FS_quit", "FS_findObject", "FS_analyzePath", "FS_createFile", "FS_createDataFile", "FS_forceLoadFile", "FS_createLazyFile", "FS_absolutePath", "FS_createFolder", "FS_createLink", "FS_joinPath", "FS_mmapAlloc", "FS_standardizePath", "MEMFS", "TTY", "PIPEFS", "SOCKFS", "tempFixedLengthArray", "miniTempWebGLFloatBuffers", "miniTempWebGLIntBuffers", "GL", "AL", "GLUT", "EGL", "GLEW", "IDBStore", "runAndAbortIfError", "Asyncify", "Fibers", "SDL", "SDL_gfx", "waitAsyncPolyfilled", "print", "printErr", "jstoi_s", "PThread", "terminateWorker", "cleanupThread", "registerTLSInit", "spawnThread", "exitOnMainThread", "proxyToMainThread", "proxiedJSCallArgs", "invokeEntryPoint", "checkMailbox", "_wasmWorkers", "_wasmWorkersID", "_wasmWorkerDelayedMessageQueue", "_wasmWorkerAppendToQueue", "_wasmWorkerRunPostMessage", "_wasmWorkerInitializeRuntime", "emAudio", "emAudioCounter", "emscriptenRegisterAudioObject", "emAudioExpectNodeOrContext_internal", "emAudioExpectNodeOrContext", "emscriptenDestroyAudioContext", "emscriptenGetAudioObject", "emscriptenGetContextQuantumSize", "_emAudioDispatchProcessorCallback", "emscripten_audio_worklet_post_function_2", "emscripten_audio_worklet_post_function_3" ];

unexportedSymbols.forEach(unexportedRuntimeSymbol);

// End runtime exports
// Begin JS library exports
// End JS library exports
// end include: postlibrary.js
// proxiedFunctionTable specifies the list of functions that can be called
// either synchronously or asynchronously from other threads in postMessage()d
// or internally queued events. This way a pthread in a Worker can synchronously
// access e.g. the DOM on the main thread.
var proxiedFunctionTable = [ _proc_exit, exitOnMainThread, pthreadCreateProxied, ___syscall_fcntl64, ___syscall_ioctl, ___syscall_openat, _environ_get, _environ_sizes_get, _fd_close, _fd_read, _fd_seek, _fd_write ];

function checkIncomingModuleAPI() {
  ignoredModuleProp("fetchSettings");
}

var ASM_CONSTS = {
  725180: ($0, $1, $2, $3, $4) => {
    if (typeof window === "undefined" || (window.AudioContext || window.webkitAudioContext) === undefined) {
      return 0;
    }
    if (typeof (window.miniaudio) === "undefined") {
      window.miniaudio = {
        referenceCount: 0
      };
      window.miniaudio.device_type = {};
      window.miniaudio.device_type.playback = $0;
      window.miniaudio.device_type.capture = $1;
      window.miniaudio.device_type.duplex = $2;
      window.miniaudio.device_state = {};
      window.miniaudio.device_state.stopped = $3;
      window.miniaudio.device_state.started = $4;
      let miniaudio = window.miniaudio;
      miniaudio.devices = [];
      miniaudio.track_device = function(device) {
        for (var iDevice = 0; iDevice < miniaudio.devices.length; ++iDevice) {
          if (miniaudio.devices[iDevice] == null) {
            miniaudio.devices[iDevice] = device;
            return iDevice;
          }
        }
        miniaudio.devices.push(device);
        return miniaudio.devices.length - 1;
      };
      miniaudio.untrack_device_by_index = function(deviceIndex) {
        miniaudio.devices[deviceIndex] = null;
        while (miniaudio.devices.length > 0) {
          if (miniaudio.devices[miniaudio.devices.length - 1] == null) {
            miniaudio.devices.pop();
          } else {
            break;
          }
        }
      };
      miniaudio.untrack_device = function(device) {
        for (var iDevice = 0; iDevice < miniaudio.devices.length; ++iDevice) {
          if (miniaudio.devices[iDevice] == device) {
            return miniaudio.untrack_device_by_index(iDevice);
          }
        }
      };
      miniaudio.get_device_by_index = function(deviceIndex) {
        return miniaudio.devices[deviceIndex];
      };
      miniaudio.unlock_event_types = (function() {
        return [ "touchend", "click" ];
      })();
      miniaudio.unlock = function() {
        for (var i = 0; i < miniaudio.devices.length; ++i) {
          var device = miniaudio.devices[i];
          if (device != null && device.webaudio != null && device.state === miniaudio.device_state.started) {
            device.webaudio.resume().then(() => {
              _ma_device__on_notification_unlocked(device.pDevice);
            }, error => {
              console.error("Failed to resume audiocontext", error);
            });
          }
        }
        miniaudio.unlock_event_types.map(function(event_type) {
          document.removeEventListener(event_type, miniaudio.unlock, true);
        });
      };
      miniaudio.unlock_event_types.map(function(event_type) {
        document.addEventListener(event_type, miniaudio.unlock, true);
      });
    }
    window.miniaudio.referenceCount += 1;
    return 1;
  },
  727358: () => {
    if (typeof (window.miniaudio) !== "undefined") {
      window.miniaudio.unlock_event_types.map(function(event_type) {
        document.removeEventListener(event_type, window.miniaudio.unlock, true);
      });
      window.miniaudio.referenceCount -= 1;
      if (window.miniaudio.referenceCount === 0) {
        delete window.miniaudio;
      }
    }
  },
  727662: () => (navigator.mediaDevices !== undefined && navigator.mediaDevices.getUserMedia !== undefined),
  727766: () => {
    try {
      var temp = new (window.AudioContext || window.webkitAudioContext);
      var sampleRate = temp.sampleRate;
      temp.close();
      return sampleRate;
    } catch (e) {
      return 0;
    }
  },
  727937: ($0, $1) => window.miniaudio.track_device({
    webaudio: emscriptenGetAudioObject($0),
    state: 1,
    pDevice: $1
  }),
  728046: ($0, $1) => {
    var getUserMediaResult = 0;
    var audioWorklet = emscriptenGetAudioObject($0);
    var audioContext = emscriptenGetAudioObject($1);
    navigator.mediaDevices.getUserMedia({
      audio: true,
      video: false
    }).then(function(stream) {
      audioContext.streamNode = audioContext.createMediaStreamSource(stream);
      audioContext.streamNode.connect(audioWorklet);
      audioWorklet.connect(audioContext.destination);
      getUserMediaResult = 0;
    }).catch(function(error) {
      console.log("navigator.mediaDevices.getUserMedia Failed: " + error);
      getUserMediaResult = -1;
    });
    return getUserMediaResult;
  },
  728608: ($0, $1) => {
    var audioWorklet = emscriptenGetAudioObject($0);
    var audioContext = emscriptenGetAudioObject($1);
    audioWorklet.connect(audioContext.destination);
    return 0;
  },
  728768: $0 => emscriptenGetAudioObject($0).sampleRate,
  728820: $0 => {
    var device = window.miniaudio.get_device_by_index($0);
    if (device.streamNode !== undefined) {
      device.streamNode.disconnect();
      device.streamNode = undefined;
    }
    device.pDevice = undefined;
  },
  729011: $0 => {
    window.miniaudio.untrack_device_by_index($0);
  },
  729061: $0 => {
    var device = window.miniaudio.get_device_by_index($0);
    device.webaudio.resume();
    device.state = window.miniaudio.device_state.started;
  },
  729200: $0 => {
    var device = window.miniaudio.get_device_by_index($0);
    device.webaudio.suspend();
    device.state = window.miniaudio.device_state.stopped;
  },
  729340: () => {
    var workerUri = "assets/packages/flutter_soloud/web/worker.dart.js";
    console.log("EM_ASM creating Web Worker!");
    try {
      var newWorker = new Worker(workerUri);
      if (Module_soloud.wasmWorker) {
        try {
          Module_soloud.wasmWorker.terminate();
          console.log("EM_ASM terminated existing Web Worker.");
        } catch (e) {
          console.error("Failed to terminate existing worker:", e);
        }
      }
      Module_soloud.wasmWorker = newWorker;
      return 1;
    } catch (e) {
      console.error("Failed to create worker:", e);
      return 0;
    }
  },
  729829: ($0, $1, $2) => {
    if (Module_soloud.wasmWorker) {
      Module_soloud.wasmWorker.postMessage({
        message: UTF8ToString($0),
        value: $1,
        generation: $2
      });
    } else {
      console.error('flutter_soloud: the event worker is not created; dropping message "' + UTF8ToString($0) + '"');
    }
  },
  730087: ($0, $1, $2) => {
    if (Module_soloud.wasmWorker) {
      Module_soloud.wasmWorker.postMessage({
        message: "mixerOutputData",
        offset: $0,
        length: $1,
        captureId: $2
      });
    }
  },
  730239: ($0, $1) => {
    var functionName = "dartOnMetadataCallback_" + $1;
    if (typeof window[functionName] === "function") {
      window[functionName]($0);
    } else {}
  },
  730381: ($0, $1, $2, $3) => {
    var functionName = "dartOnBufferingCallback_" + $3;
    if (typeof window[functionName] === "function") {
      var buffering = $0 == 1 ? true : false;
      window[functionName](buffering, $1, $2);
    } else {
      console.log("EM_ASM 'dartOnBufferingCallback_$hash' not found.");
    }
  },
  730645: ($0, $1, $2, $3) => {
    var functionName = "dartOnBufferingCallback_" + $3;
    if (typeof window[functionName] === "function") {
      window[functionName]($0 == 1 ? true : false, $1, $2);
    }
  },
  730807: ($0, $1) => {
    var functionName = "dartOnMetadataCallback_" + $1;
    if (typeof window[functionName] === "function") {
      window[functionName]($0);
    }
  },
  730940: ($0, $1) => {
    var functionName = "dartOnMoreDataIsNeededCallback_" + $1;
    if (typeof window[functionName] === "function") {
      window[functionName]($0);
    }
  },
  731081: ($0, $1) => {
    var functionName = "dartOnAudioDurationCallback_" + $1;
    if (typeof window[functionName] === "function") {
      window[functionName]($0);
    }
  }
};

// Imports from the Wasm binary.
var _malloc = Module["_malloc"] = makeInvalidEarlyAccess("_malloc");

var _free = Module["_free"] = makeInvalidEarlyAccess("_free");

var _ma_device__on_notification_unlocked = Module["_ma_device__on_notification_unlocked"] = makeInvalidEarlyAccess("_ma_device__on_notification_unlocked");

var _ma_malloc_emscripten = Module["_ma_malloc_emscripten"] = makeInvalidEarlyAccess("_ma_malloc_emscripten");

var _ma_free_emscripten = Module["_ma_free_emscripten"] = makeInvalidEarlyAccess("_ma_free_emscripten");

var _ma_device_process_pcm_frames_capture__webaudio = Module["_ma_device_process_pcm_frames_capture__webaudio"] = makeInvalidEarlyAccess("_ma_device_process_pcm_frames_capture__webaudio");

var _ma_device_process_pcm_frames_playback__webaudio = Module["_ma_device_process_pcm_frames_playback__webaudio"] = makeInvalidEarlyAccess("_ma_device_process_pcm_frames_playback__webaudio");

var _createWorkerInWasm = Module["_createWorkerInWasm"] = makeInvalidEarlyAccess("_createWorkerInWasm");

var _sendToWorker = Module["_sendToWorker"] = makeInvalidEarlyAccess("_sendToWorker");

var _getEngineGeneration = Module["_getEngineGeneration"] = makeInvalidEarlyAccess("_getEngineGeneration");

var _nativeFree = Module["_nativeFree"] = makeInvalidEarlyAccess("_nativeFree");

var _voiceEndedCallback = Module["_voiceEndedCallback"] = makeInvalidEarlyAccess("_voiceEndedCallback");

var _setDartEventCallback = Module["_setDartEventCallback"] = makeInvalidEarlyAccess("_setDartEventCallback");

var _clearDartCallbackRegistrations = Module["_clearDartCallbackRegistrations"] = makeInvalidEarlyAccess("_clearDartCallbackRegistrations");

var _startMixerCapture = Module["_startMixerCapture"] = makeInvalidEarlyAccess("_startMixerCapture");

var _stopMixerCapture = Module["_stopMixerCapture"] = makeInvalidEarlyAccess("_stopMixerCapture");

var _isMixerCaptureRunning = Module["_isMixerCaptureRunning"] = makeInvalidEarlyAccess("_isMixerCaptureRunning");

var _getMixerCaptureBufferPointer = Module["_getMixerCaptureBufferPointer"] = makeInvalidEarlyAccess("_getMixerCaptureBufferPointer");

var _getMixerCaptureBufferSize = Module["_getMixerCaptureBufferSize"] = makeInvalidEarlyAccess("_getMixerCaptureBufferSize");

var _getMixerCaptureAvailableBytes = Module["_getMixerCaptureAvailableBytes"] = makeInvalidEarlyAccess("_getMixerCaptureAvailableBytes");

var _getMixerCaptureReadOffset = Module["_getMixerCaptureReadOffset"] = makeInvalidEarlyAccess("_getMixerCaptureReadOffset");

var _advanceMixerCaptureReadPosition = Module["_advanceMixerCaptureReadPosition"] = makeInvalidEarlyAccess("_advanceMixerCaptureReadPosition");

var _getMixerOutputWavHeader = Module["_getMixerOutputWavHeader"] = makeInvalidEarlyAccess("_getMixerOutputWavHeader");

var _setMixerOutputCallback = Module["_setMixerOutputCallback"] = makeInvalidEarlyAccess("_setMixerOutputCallback");

var _areXiphLibsAvailable = Module["_areXiphLibsAvailable"] = makeInvalidEarlyAccess("_areXiphLibsAvailable");

var _initEngine = Module["_initEngine"] = makeInvalidEarlyAccess("_initEngine");

var _setAndroidAAudioAttributes = Module["_setAndroidAAudioAttributes"] = makeInvalidEarlyAccess("_setAndroidAAudioAttributes");

var _listPlaybackDevices = Module["_listPlaybackDevices"] = makeInvalidEarlyAccess("_listPlaybackDevices");

var _changeDevice = Module["_changeDevice"] = makeInvalidEarlyAccess("_changeDevice");

var _freeListPlaybackDevices = Module["_freeListPlaybackDevices"] = makeInvalidEarlyAccess("_freeListPlaybackDevices");

var _dispose = Module["_dispose"] = makeInvalidEarlyAccess("_dispose");

var _isInited = Module["_isInited"] = makeInvalidEarlyAccess("_isInited");

var _loadFile = Module["_loadFile"] = makeInvalidEarlyAccess("_loadFile");

var _loadMem = Module["_loadMem"] = makeInvalidEarlyAccess("_loadMem");

var _setBufferStream = Module["_setBufferStream"] = makeInvalidEarlyAccess("_setBufferStream");

var _resetBufferStream = Module["_resetBufferStream"] = makeInvalidEarlyAccess("_resetBufferStream");

var _getStreamTimeConsumed = Module["_getStreamTimeConsumed"] = makeInvalidEarlyAccess("_getStreamTimeConsumed");

var _setBufferIcyMetaInt = Module["_setBufferIcyMetaInt"] = makeInvalidEarlyAccess("_setBufferIcyMetaInt");

var _setPullBufferStream = Module["_setPullBufferStream"] = makeInvalidEarlyAccess("_setPullBufferStream");

var _resetPullBufferStream = Module["_resetPullBufferStream"] = makeInvalidEarlyAccess("_resetPullBufferStream");

var _addPullBufferDataStream = Module["_addPullBufferDataStream"] = makeInvalidEarlyAccess("_addPullBufferDataStream");

var _getPullBufferTimeRange = Module["_getPullBufferTimeRange"] = makeInvalidEarlyAccess("_getPullBufferTimeRange");

var _addAudioDataStream = Module["_addAudioDataStream"] = makeInvalidEarlyAccess("_addAudioDataStream");

var _setDataIsEnded = Module["_setDataIsEnded"] = makeInvalidEarlyAccess("_setDataIsEnded");

var _getBufferSize = Module["_getBufferSize"] = makeInvalidEarlyAccess("_getBufferSize");

var _loadWaveform = Module["_loadWaveform"] = makeInvalidEarlyAccess("_loadWaveform");

var _setWaveformScale = Module["_setWaveformScale"] = makeInvalidEarlyAccess("_setWaveformScale");

var _setWaveformDetune = Module["_setWaveformDetune"] = makeInvalidEarlyAccess("_setWaveformDetune");

var _setWaveformFreq = Module["_setWaveformFreq"] = makeInvalidEarlyAccess("_setWaveformFreq");

var _setSuperWave = Module["_setSuperWave"] = makeInvalidEarlyAccess("_setSuperWave");

var _setWaveform = Module["_setWaveform"] = makeInvalidEarlyAccess("_setWaveform");

var _speechText = Module["_speechText"] = makeInvalidEarlyAccess("_speechText");

var _pauseSwitch = Module["_pauseSwitch"] = makeInvalidEarlyAccess("_pauseSwitch");

var _setPause = Module["_setPause"] = makeInvalidEarlyAccess("_setPause");

var _getPause = Module["_getPause"] = makeInvalidEarlyAccess("_getPause");

var _setRelativePlaySpeed = Module["_setRelativePlaySpeed"] = makeInvalidEarlyAccess("_setRelativePlaySpeed");

var _getRelativePlaySpeed = Module["_getRelativePlaySpeed"] = makeInvalidEarlyAccess("_getRelativePlaySpeed");

var _getApproximateVolume = Module["_getApproximateVolume"] = makeInvalidEarlyAccess("_getApproximateVolume");

var _play = Module["_play"] = makeInvalidEarlyAccess("_play");

var _playWithLoopPoints = Module["_playWithLoopPoints"] = makeInvalidEarlyAccess("_playWithLoopPoints");

var _playClocked = Module["_playClocked"] = makeInvalidEarlyAccess("_playClocked");

var _setDelaySamples = Module["_setDelaySamples"] = makeInvalidEarlyAccess("_setDelaySamples");

var _getStreamTime = Module["_getStreamTime"] = makeInvalidEarlyAccess("_getStreamTime");

var _resetStreamTime = Module["_resetStreamTime"] = makeInvalidEarlyAccess("_resetStreamTime");

var _getEngineTime = Module["_getEngineTime"] = makeInvalidEarlyAccess("_getEngineTime");

var _playScheduled = Module["_playScheduled"] = makeInvalidEarlyAccess("_playScheduled");

var _stopScheduled = Module["_stopScheduled"] = makeInvalidEarlyAccess("_stopScheduled");

var _fadeScheduled = Module["_fadeScheduled"] = makeInvalidEarlyAccess("_fadeScheduled");

var _stop = Module["_stop"] = makeInvalidEarlyAccess("_stop");

var _disposeSound = Module["_disposeSound"] = makeInvalidEarlyAccess("_disposeSound");

var _disposeAllSound = Module["_disposeAllSound"] = makeInvalidEarlyAccess("_disposeAllSound");

var _getLooping = Module["_getLooping"] = makeInvalidEarlyAccess("_getLooping");

var _setLooping = Module["_setLooping"] = makeInvalidEarlyAccess("_setLooping");

var _getLoopPoint = Module["_getLoopPoint"] = makeInvalidEarlyAccess("_getLoopPoint");

var _setLoopPoint = Module["_setLoopPoint"] = makeInvalidEarlyAccess("_setLoopPoint");

var _getLoopEndPoint = Module["_getLoopEndPoint"] = makeInvalidEarlyAccess("_getLoopEndPoint");

var _setLoopEndPoint = Module["_setLoopEndPoint"] = makeInvalidEarlyAccess("_setLoopEndPoint");

var _setVisualizationEnabled = Module["_setVisualizationEnabled"] = makeInvalidEarlyAccess("_setVisualizationEnabled");

var _getVisualizationEnabled = Module["_getVisualizationEnabled"] = makeInvalidEarlyAccess("_getVisualizationEnabled");

var _getFft = Module["_getFft"] = makeInvalidEarlyAccess("_getFft");

var _getWave = Module["_getWave"] = makeInvalidEarlyAccess("_getWave");

var _setFftSmoothing = Module["_setFftSmoothing"] = makeInvalidEarlyAccess("_setFftSmoothing");

var _getAudioTexture = Module["_getAudioTexture"] = makeInvalidEarlyAccess("_getAudioTexture");

var _getAudioTexture2D = Module["_getAudioTexture2D"] = makeInvalidEarlyAccess("_getAudioTexture2D");

var _getTextureValue = Module["_getTextureValue"] = makeInvalidEarlyAccess("_getTextureValue");

var _getLength = Module["_getLength"] = makeInvalidEarlyAccess("_getLength");

var _seek = Module["_seek"] = makeInvalidEarlyAccess("_seek");

var _getPosition = Module["_getPosition"] = makeInvalidEarlyAccess("_getPosition");

var _getGlobalVolume = Module["_getGlobalVolume"] = makeInvalidEarlyAccess("_getGlobalVolume");

var _setGlobalVolume = Module["_setGlobalVolume"] = makeInvalidEarlyAccess("_setGlobalVolume");

var _getVolume = Module["_getVolume"] = makeInvalidEarlyAccess("_getVolume");

var _setVolume = Module["_setVolume"] = makeInvalidEarlyAccess("_setVolume");

var _getPan = Module["_getPan"] = makeInvalidEarlyAccess("_getPan");

var _setPan = Module["_setPan"] = makeInvalidEarlyAccess("_setPan");

var _setPanAbsolute = Module["_setPanAbsolute"] = makeInvalidEarlyAccess("_setPanAbsolute");

var _getIsValidVoiceHandle = Module["_getIsValidVoiceHandle"] = makeInvalidEarlyAccess("_getIsValidVoiceHandle");

var _getActiveVoiceCount = Module["_getActiveVoiceCount"] = makeInvalidEarlyAccess("_getActiveVoiceCount");

var _countAudioSource = Module["_countAudioSource"] = makeInvalidEarlyAccess("_countAudioSource");

var _getVoiceCount = Module["_getVoiceCount"] = makeInvalidEarlyAccess("_getVoiceCount");

var _getProtectVoice = Module["_getProtectVoice"] = makeInvalidEarlyAccess("_getProtectVoice");

var _setProtectVoice = Module["_setProtectVoice"] = makeInvalidEarlyAccess("_setProtectVoice");

var _setInaudibleBehavior = Module["_setInaudibleBehavior"] = makeInvalidEarlyAccess("_setInaudibleBehavior");

var _getMaxActiveVoiceCount = Module["_getMaxActiveVoiceCount"] = makeInvalidEarlyAccess("_getMaxActiveVoiceCount");

var _setMaxActiveVoiceCount = Module["_setMaxActiveVoiceCount"] = makeInvalidEarlyAccess("_setMaxActiveVoiceCount");

var _createVoiceGroup = Module["_createVoiceGroup"] = makeInvalidEarlyAccess("_createVoiceGroup");

var _destroyVoiceGroup = Module["_destroyVoiceGroup"] = makeInvalidEarlyAccess("_destroyVoiceGroup");

var _addVoiceToGroup = Module["_addVoiceToGroup"] = makeInvalidEarlyAccess("_addVoiceToGroup");

var _isVoiceGroup = Module["_isVoiceGroup"] = makeInvalidEarlyAccess("_isVoiceGroup");

var _isVoiceGroupEmpty = Module["_isVoiceGroupEmpty"] = makeInvalidEarlyAccess("_isVoiceGroupEmpty");

var _fadeGlobalVolume = Module["_fadeGlobalVolume"] = makeInvalidEarlyAccess("_fadeGlobalVolume");

var _fadeVolume = Module["_fadeVolume"] = makeInvalidEarlyAccess("_fadeVolume");

var _fadePan = Module["_fadePan"] = makeInvalidEarlyAccess("_fadePan");

var _fadeRelativePlaySpeed = Module["_fadeRelativePlaySpeed"] = makeInvalidEarlyAccess("_fadeRelativePlaySpeed");

var _schedulePause = Module["_schedulePause"] = makeInvalidEarlyAccess("_schedulePause");

var _scheduleStop = Module["_scheduleStop"] = makeInvalidEarlyAccess("_scheduleStop");

var _oscillateVolume = Module["_oscillateVolume"] = makeInvalidEarlyAccess("_oscillateVolume");

var _oscillatePan = Module["_oscillatePan"] = makeInvalidEarlyAccess("_oscillatePan");

var _oscillateRelativePlaySpeed = Module["_oscillateRelativePlaySpeed"] = makeInvalidEarlyAccess("_oscillateRelativePlaySpeed");

var _oscillateGlobalVolume = Module["_oscillateGlobalVolume"] = makeInvalidEarlyAccess("_oscillateGlobalVolume");

var _isFilterActive = Module["_isFilterActive"] = makeInvalidEarlyAccess("_isFilterActive");

var _getFilterParamNames = Module["_getFilterParamNames"] = makeInvalidEarlyAccess("_getFilterParamNames");

var _addFilter = Module["_addFilter"] = makeInvalidEarlyAccess("_addFilter");

var _removeFilter = Module["_removeFilter"] = makeInvalidEarlyAccess("_removeFilter");

var _setFilterParams = Module["_setFilterParams"] = makeInvalidEarlyAccess("_setFilterParams");

var _getFilterParams = Module["_getFilterParams"] = makeInvalidEarlyAccess("_getFilterParams");

var _fadeFilterParameter = Module["_fadeFilterParameter"] = makeInvalidEarlyAccess("_fadeFilterParameter");

var _oscillateFilterParameter = Module["_oscillateFilterParameter"] = makeInvalidEarlyAccess("_oscillateFilterParameter");

var _play3d = Module["_play3d"] = makeInvalidEarlyAccess("_play3d");

var _play3dWithLoopPoints = Module["_play3dWithLoopPoints"] = makeInvalidEarlyAccess("_play3dWithLoopPoints");

var _play3dClocked = Module["_play3dClocked"] = makeInvalidEarlyAccess("_play3dClocked");

var _set3dSoundSpeed = Module["_set3dSoundSpeed"] = makeInvalidEarlyAccess("_set3dSoundSpeed");

var _get3dSoundSpeed = Module["_get3dSoundSpeed"] = makeInvalidEarlyAccess("_get3dSoundSpeed");

var _set3dListenerParameters = Module["_set3dListenerParameters"] = makeInvalidEarlyAccess("_set3dListenerParameters");

var _set3dListenerPosition = Module["_set3dListenerPosition"] = makeInvalidEarlyAccess("_set3dListenerPosition");

var _set3dListenerAt = Module["_set3dListenerAt"] = makeInvalidEarlyAccess("_set3dListenerAt");

var _set3dListenerUp = Module["_set3dListenerUp"] = makeInvalidEarlyAccess("_set3dListenerUp");

var _set3dListenerVelocity = Module["_set3dListenerVelocity"] = makeInvalidEarlyAccess("_set3dListenerVelocity");

var _set3dSourceParameters = Module["_set3dSourceParameters"] = makeInvalidEarlyAccess("_set3dSourceParameters");

var _set3dSourcePosition = Module["_set3dSourcePosition"] = makeInvalidEarlyAccess("_set3dSourcePosition");

var _set3dSourceVelocity = Module["_set3dSourceVelocity"] = makeInvalidEarlyAccess("_set3dSourceVelocity");

var _set3dSourceMinMaxDistance = Module["_set3dSourceMinMaxDistance"] = makeInvalidEarlyAccess("_set3dSourceMinMaxDistance");

var _set3dSourceAttenuation = Module["_set3dSourceAttenuation"] = makeInvalidEarlyAccess("_set3dSourceAttenuation");

var _set3dSourceDopplerFactor = Module["_set3dSourceDopplerFactor"] = makeInvalidEarlyAccess("_set3dSourceDopplerFactor");

var _readSamplesFromFile = Module["_readSamplesFromFile"] = makeInvalidEarlyAccess("_readSamplesFromFile");

var _readSamplesFromMem = Module["_readSamplesFromMem"] = makeInvalidEarlyAccess("_readSamplesFromMem");

var _createBus = Module["_createBus"] = makeInvalidEarlyAccess("_createBus");

var _destroyBus = Module["_destroyBus"] = makeInvalidEarlyAccess("_destroyBus");

var _busPlayOnEngine = Module["_busPlayOnEngine"] = makeInvalidEarlyAccess("_busPlayOnEngine");

var _busSetChannels = Module["_busSetChannels"] = makeInvalidEarlyAccess("_busSetChannels");

var _busSetVisualizationEnable = Module["_busSetVisualizationEnable"] = makeInvalidEarlyAccess("_busSetVisualizationEnable");

var _busCalcFFT = Module["_busCalcFFT"] = makeInvalidEarlyAccess("_busCalcFFT");

var _busGetWave = Module["_busGetWave"] = makeInvalidEarlyAccess("_busGetWave");

var _busGetApproximateVolume = Module["_busGetApproximateVolume"] = makeInvalidEarlyAccess("_busGetApproximateVolume");

var _busAnnexSound = Module["_busAnnexSound"] = makeInvalidEarlyAccess("_busAnnexSound");

var _busGetActiveVoiceCount = Module["_busGetActiveVoiceCount"] = makeInvalidEarlyAccess("_busGetActiveVoiceCount");

var __emscripten_tls_init = makeInvalidEarlyAccess("__emscripten_tls_init");

var _pthread_self = makeInvalidEarlyAccess("_pthread_self");

var _memcpy = Module["_memcpy"] = makeInvalidEarlyAccess("_memcpy");

var _memset = Module["_memset"] = makeInvalidEarlyAccess("_memset");

var __emscripten_thread_init = makeInvalidEarlyAccess("__emscripten_thread_init");

var ___set_thread_state = makeInvalidEarlyAccess("___set_thread_state");

var __emscripten_thread_crashed = makeInvalidEarlyAccess("__emscripten_thread_crashed");

var _fflush = makeInvalidEarlyAccess("_fflush");

var _emscripten_stack_get_end = makeInvalidEarlyAccess("_emscripten_stack_get_end");

var _emscripten_stack_get_base = makeInvalidEarlyAccess("_emscripten_stack_get_base");

var __emscripten_run_js_on_main_thread = makeInvalidEarlyAccess("__emscripten_run_js_on_main_thread");

var __emscripten_thread_free_data = makeInvalidEarlyAccess("__emscripten_thread_free_data");

var __emscripten_thread_exit = makeInvalidEarlyAccess("__emscripten_thread_exit");

var _strerror = makeInvalidEarlyAccess("_strerror");

var __emscripten_check_mailbox = makeInvalidEarlyAccess("__emscripten_check_mailbox");

var _emscripten_stack_init = makeInvalidEarlyAccess("_emscripten_stack_init");

var _emscripten_stack_set_limits = makeInvalidEarlyAccess("_emscripten_stack_set_limits");

var _emscripten_stack_get_free = makeInvalidEarlyAccess("_emscripten_stack_get_free");

var __emscripten_stack_restore = makeInvalidEarlyAccess("__emscripten_stack_restore");

var __emscripten_stack_alloc = makeInvalidEarlyAccess("__emscripten_stack_alloc");

var _emscripten_stack_get_current = makeInvalidEarlyAccess("_emscripten_stack_get_current");

var __emscripten_wasm_worker_initialize = makeInvalidEarlyAccess("__emscripten_wasm_worker_initialize");

var dynCall_ii = makeInvalidEarlyAccess("dynCall_ii");

var dynCall_vi = makeInvalidEarlyAccess("dynCall_vi");

var dynCall_v = makeInvalidEarlyAccess("dynCall_v");

var dynCall_iidii = makeInvalidEarlyAccess("dynCall_iidii");

var dynCall_fii = makeInvalidEarlyAccess("dynCall_fii");

var dynCall_viii = makeInvalidEarlyAccess("dynCall_viii");

var dynCall_iiiii = makeInvalidEarlyAccess("dynCall_iiiii");

var dynCall_iiii = makeInvalidEarlyAccess("dynCall_iiii");

var dynCall_vii = makeInvalidEarlyAccess("dynCall_vii");

var dynCall_iii = makeInvalidEarlyAccess("dynCall_iii");

var dynCall_vid = makeInvalidEarlyAccess("dynCall_vid");

var dynCall_viiiiifd = makeInvalidEarlyAccess("dynCall_viiiiifd");

var dynCall_viiifdii = makeInvalidEarlyAccess("dynCall_viiifdii");

var dynCall_viif = makeInvalidEarlyAccess("dynCall_viif");

var dynCall_viifdd = makeInvalidEarlyAccess("dynCall_viifdd");

var dynCall_viiffdd = makeInvalidEarlyAccess("dynCall_viiffdd");

var dynCall_iiji = makeInvalidEarlyAccess("dynCall_iiji");

var dynCall_viiii = makeInvalidEarlyAccess("dynCall_viiii");

var dynCall_iiiiiiii = makeInvalidEarlyAccess("dynCall_iiiiiiii");

var dynCall_iiiji = makeInvalidEarlyAccess("dynCall_iiiji");

var dynCall_iiiiiii = makeInvalidEarlyAccess("dynCall_iiiiiii");

var dynCall_jii = makeInvalidEarlyAccess("dynCall_jii");

var dynCall_iij = makeInvalidEarlyAccess("dynCall_iij");

var dynCall_iiiiiiiii = makeInvalidEarlyAccess("dynCall_iiiiiiiii");

var dynCall_iiiiii = makeInvalidEarlyAccess("dynCall_iiiiii");

var dynCall_viidi = makeInvalidEarlyAccess("dynCall_viidi");

var dynCall_viiiiii = makeInvalidEarlyAccess("dynCall_viiiiii");

var dynCall_iid = makeInvalidEarlyAccess("dynCall_iid");

var dynCall_jid = makeInvalidEarlyAccess("dynCall_jid");

var dynCall_di = makeInvalidEarlyAccess("dynCall_di");

var dynCall_vij = makeInvalidEarlyAccess("dynCall_vij");

var dynCall_vdi = makeInvalidEarlyAccess("dynCall_vdi");

var dynCall_viiiiiii = makeInvalidEarlyAccess("dynCall_viiiiiii");

var dynCall_jiji = makeInvalidEarlyAccess("dynCall_jiji");

var dynCall_iidiiii = makeInvalidEarlyAccess("dynCall_iidiiii");

var dynCall_viijii = makeInvalidEarlyAccess("dynCall_viijii");

var dynCall_iiiiij = makeInvalidEarlyAccess("dynCall_iiiiij");

var dynCall_iiiiid = makeInvalidEarlyAccess("dynCall_iiiiid");

var dynCall_iiiiijj = makeInvalidEarlyAccess("dynCall_iiiiijj");

var dynCall_iiiiiijj = makeInvalidEarlyAccess("dynCall_iiiiiijj");

var dynCall_viiiii = makeInvalidEarlyAccess("dynCall_viiiii");

var _asyncify_start_unwind = makeInvalidEarlyAccess("_asyncify_start_unwind");

var _asyncify_stop_unwind = makeInvalidEarlyAccess("_asyncify_stop_unwind");

var _asyncify_start_rewind = makeInvalidEarlyAccess("_asyncify_start_rewind");

var _asyncify_stop_rewind = makeInvalidEarlyAccess("_asyncify_stop_rewind");

var __indirect_function_table = makeInvalidEarlyAccess("__indirect_function_table");

var wasmTable = makeInvalidEarlyAccess("wasmTable");

function assignWasmExports(wasmExports) {
  assert(typeof wasmExports["malloc"] != "undefined", "missing Wasm export: malloc");
  assert(typeof wasmExports["free"] != "undefined", "missing Wasm export: free");
  assert(typeof wasmExports["ma_device__on_notification_unlocked"] != "undefined", "missing Wasm export: ma_device__on_notification_unlocked");
  assert(typeof wasmExports["ma_malloc_emscripten"] != "undefined", "missing Wasm export: ma_malloc_emscripten");
  assert(typeof wasmExports["ma_free_emscripten"] != "undefined", "missing Wasm export: ma_free_emscripten");
  assert(typeof wasmExports["ma_device_process_pcm_frames_capture__webaudio"] != "undefined", "missing Wasm export: ma_device_process_pcm_frames_capture__webaudio");
  assert(typeof wasmExports["ma_device_process_pcm_frames_playback__webaudio"] != "undefined", "missing Wasm export: ma_device_process_pcm_frames_playback__webaudio");
  assert(typeof wasmExports["createWorkerInWasm"] != "undefined", "missing Wasm export: createWorkerInWasm");
  assert(typeof wasmExports["sendToWorker"] != "undefined", "missing Wasm export: sendToWorker");
  assert(typeof wasmExports["getEngineGeneration"] != "undefined", "missing Wasm export: getEngineGeneration");
  assert(typeof wasmExports["nativeFree"] != "undefined", "missing Wasm export: nativeFree");
  assert(typeof wasmExports["voiceEndedCallback"] != "undefined", "missing Wasm export: voiceEndedCallback");
  assert(typeof wasmExports["setDartEventCallback"] != "undefined", "missing Wasm export: setDartEventCallback");
  assert(typeof wasmExports["clearDartCallbackRegistrations"] != "undefined", "missing Wasm export: clearDartCallbackRegistrations");
  assert(typeof wasmExports["startMixerCapture"] != "undefined", "missing Wasm export: startMixerCapture");
  assert(typeof wasmExports["stopMixerCapture"] != "undefined", "missing Wasm export: stopMixerCapture");
  assert(typeof wasmExports["isMixerCaptureRunning"] != "undefined", "missing Wasm export: isMixerCaptureRunning");
  assert(typeof wasmExports["getMixerCaptureBufferPointer"] != "undefined", "missing Wasm export: getMixerCaptureBufferPointer");
  assert(typeof wasmExports["getMixerCaptureBufferSize"] != "undefined", "missing Wasm export: getMixerCaptureBufferSize");
  assert(typeof wasmExports["getMixerCaptureAvailableBytes"] != "undefined", "missing Wasm export: getMixerCaptureAvailableBytes");
  assert(typeof wasmExports["getMixerCaptureReadOffset"] != "undefined", "missing Wasm export: getMixerCaptureReadOffset");
  assert(typeof wasmExports["advanceMixerCaptureReadPosition"] != "undefined", "missing Wasm export: advanceMixerCaptureReadPosition");
  assert(typeof wasmExports["getMixerOutputWavHeader"] != "undefined", "missing Wasm export: getMixerOutputWavHeader");
  assert(typeof wasmExports["setMixerOutputCallback"] != "undefined", "missing Wasm export: setMixerOutputCallback");
  assert(typeof wasmExports["areXiphLibsAvailable"] != "undefined", "missing Wasm export: areXiphLibsAvailable");
  assert(typeof wasmExports["initEngine"] != "undefined", "missing Wasm export: initEngine");
  assert(typeof wasmExports["setAndroidAAudioAttributes"] != "undefined", "missing Wasm export: setAndroidAAudioAttributes");
  assert(typeof wasmExports["listPlaybackDevices"] != "undefined", "missing Wasm export: listPlaybackDevices");
  assert(typeof wasmExports["changeDevice"] != "undefined", "missing Wasm export: changeDevice");
  assert(typeof wasmExports["freeListPlaybackDevices"] != "undefined", "missing Wasm export: freeListPlaybackDevices");
  assert(typeof wasmExports["dispose"] != "undefined", "missing Wasm export: dispose");
  assert(typeof wasmExports["isInited"] != "undefined", "missing Wasm export: isInited");
  assert(typeof wasmExports["loadFile"] != "undefined", "missing Wasm export: loadFile");
  assert(typeof wasmExports["loadMem"] != "undefined", "missing Wasm export: loadMem");
  assert(typeof wasmExports["setBufferStream"] != "undefined", "missing Wasm export: setBufferStream");
  assert(typeof wasmExports["resetBufferStream"] != "undefined", "missing Wasm export: resetBufferStream");
  assert(typeof wasmExports["getStreamTimeConsumed"] != "undefined", "missing Wasm export: getStreamTimeConsumed");
  assert(typeof wasmExports["setBufferIcyMetaInt"] != "undefined", "missing Wasm export: setBufferIcyMetaInt");
  assert(typeof wasmExports["setPullBufferStream"] != "undefined", "missing Wasm export: setPullBufferStream");
  assert(typeof wasmExports["resetPullBufferStream"] != "undefined", "missing Wasm export: resetPullBufferStream");
  assert(typeof wasmExports["addPullBufferDataStream"] != "undefined", "missing Wasm export: addPullBufferDataStream");
  assert(typeof wasmExports["getPullBufferTimeRange"] != "undefined", "missing Wasm export: getPullBufferTimeRange");
  assert(typeof wasmExports["addAudioDataStream"] != "undefined", "missing Wasm export: addAudioDataStream");
  assert(typeof wasmExports["setDataIsEnded"] != "undefined", "missing Wasm export: setDataIsEnded");
  assert(typeof wasmExports["getBufferSize"] != "undefined", "missing Wasm export: getBufferSize");
  assert(typeof wasmExports["loadWaveform"] != "undefined", "missing Wasm export: loadWaveform");
  assert(typeof wasmExports["setWaveformScale"] != "undefined", "missing Wasm export: setWaveformScale");
  assert(typeof wasmExports["setWaveformDetune"] != "undefined", "missing Wasm export: setWaveformDetune");
  assert(typeof wasmExports["setWaveformFreq"] != "undefined", "missing Wasm export: setWaveformFreq");
  assert(typeof wasmExports["setSuperWave"] != "undefined", "missing Wasm export: setSuperWave");
  assert(typeof wasmExports["setWaveform"] != "undefined", "missing Wasm export: setWaveform");
  assert(typeof wasmExports["speechText"] != "undefined", "missing Wasm export: speechText");
  assert(typeof wasmExports["pauseSwitch"] != "undefined", "missing Wasm export: pauseSwitch");
  assert(typeof wasmExports["setPause"] != "undefined", "missing Wasm export: setPause");
  assert(typeof wasmExports["getPause"] != "undefined", "missing Wasm export: getPause");
  assert(typeof wasmExports["setRelativePlaySpeed"] != "undefined", "missing Wasm export: setRelativePlaySpeed");
  assert(typeof wasmExports["getRelativePlaySpeed"] != "undefined", "missing Wasm export: getRelativePlaySpeed");
  assert(typeof wasmExports["getApproximateVolume"] != "undefined", "missing Wasm export: getApproximateVolume");
  assert(typeof wasmExports["play"] != "undefined", "missing Wasm export: play");
  assert(typeof wasmExports["playWithLoopPoints"] != "undefined", "missing Wasm export: playWithLoopPoints");
  assert(typeof wasmExports["playClocked"] != "undefined", "missing Wasm export: playClocked");
  assert(typeof wasmExports["setDelaySamples"] != "undefined", "missing Wasm export: setDelaySamples");
  assert(typeof wasmExports["getStreamTime"] != "undefined", "missing Wasm export: getStreamTime");
  assert(typeof wasmExports["resetStreamTime"] != "undefined", "missing Wasm export: resetStreamTime");
  assert(typeof wasmExports["getEngineTime"] != "undefined", "missing Wasm export: getEngineTime");
  assert(typeof wasmExports["playScheduled"] != "undefined", "missing Wasm export: playScheduled");
  assert(typeof wasmExports["stopScheduled"] != "undefined", "missing Wasm export: stopScheduled");
  assert(typeof wasmExports["fadeScheduled"] != "undefined", "missing Wasm export: fadeScheduled");
  assert(typeof wasmExports["stop"] != "undefined", "missing Wasm export: stop");
  assert(typeof wasmExports["disposeSound"] != "undefined", "missing Wasm export: disposeSound");
  assert(typeof wasmExports["disposeAllSound"] != "undefined", "missing Wasm export: disposeAllSound");
  assert(typeof wasmExports["getLooping"] != "undefined", "missing Wasm export: getLooping");
  assert(typeof wasmExports["setLooping"] != "undefined", "missing Wasm export: setLooping");
  assert(typeof wasmExports["getLoopPoint"] != "undefined", "missing Wasm export: getLoopPoint");
  assert(typeof wasmExports["setLoopPoint"] != "undefined", "missing Wasm export: setLoopPoint");
  assert(typeof wasmExports["getLoopEndPoint"] != "undefined", "missing Wasm export: getLoopEndPoint");
  assert(typeof wasmExports["setLoopEndPoint"] != "undefined", "missing Wasm export: setLoopEndPoint");
  assert(typeof wasmExports["setVisualizationEnabled"] != "undefined", "missing Wasm export: setVisualizationEnabled");
  assert(typeof wasmExports["getVisualizationEnabled"] != "undefined", "missing Wasm export: getVisualizationEnabled");
  assert(typeof wasmExports["getFft"] != "undefined", "missing Wasm export: getFft");
  assert(typeof wasmExports["getWave"] != "undefined", "missing Wasm export: getWave");
  assert(typeof wasmExports["setFftSmoothing"] != "undefined", "missing Wasm export: setFftSmoothing");
  assert(typeof wasmExports["getAudioTexture"] != "undefined", "missing Wasm export: getAudioTexture");
  assert(typeof wasmExports["getAudioTexture2D"] != "undefined", "missing Wasm export: getAudioTexture2D");
  assert(typeof wasmExports["getTextureValue"] != "undefined", "missing Wasm export: getTextureValue");
  assert(typeof wasmExports["getLength"] != "undefined", "missing Wasm export: getLength");
  assert(typeof wasmExports["seek"] != "undefined", "missing Wasm export: seek");
  assert(typeof wasmExports["getPosition"] != "undefined", "missing Wasm export: getPosition");
  assert(typeof wasmExports["getGlobalVolume"] != "undefined", "missing Wasm export: getGlobalVolume");
  assert(typeof wasmExports["setGlobalVolume"] != "undefined", "missing Wasm export: setGlobalVolume");
  assert(typeof wasmExports["getVolume"] != "undefined", "missing Wasm export: getVolume");
  assert(typeof wasmExports["setVolume"] != "undefined", "missing Wasm export: setVolume");
  assert(typeof wasmExports["getPan"] != "undefined", "missing Wasm export: getPan");
  assert(typeof wasmExports["setPan"] != "undefined", "missing Wasm export: setPan");
  assert(typeof wasmExports["setPanAbsolute"] != "undefined", "missing Wasm export: setPanAbsolute");
  assert(typeof wasmExports["getIsValidVoiceHandle"] != "undefined", "missing Wasm export: getIsValidVoiceHandle");
  assert(typeof wasmExports["getActiveVoiceCount"] != "undefined", "missing Wasm export: getActiveVoiceCount");
  assert(typeof wasmExports["countAudioSource"] != "undefined", "missing Wasm export: countAudioSource");
  assert(typeof wasmExports["getVoiceCount"] != "undefined", "missing Wasm export: getVoiceCount");
  assert(typeof wasmExports["getProtectVoice"] != "undefined", "missing Wasm export: getProtectVoice");
  assert(typeof wasmExports["setProtectVoice"] != "undefined", "missing Wasm export: setProtectVoice");
  assert(typeof wasmExports["setInaudibleBehavior"] != "undefined", "missing Wasm export: setInaudibleBehavior");
  assert(typeof wasmExports["getMaxActiveVoiceCount"] != "undefined", "missing Wasm export: getMaxActiveVoiceCount");
  assert(typeof wasmExports["setMaxActiveVoiceCount"] != "undefined", "missing Wasm export: setMaxActiveVoiceCount");
  assert(typeof wasmExports["createVoiceGroup"] != "undefined", "missing Wasm export: createVoiceGroup");
  assert(typeof wasmExports["destroyVoiceGroup"] != "undefined", "missing Wasm export: destroyVoiceGroup");
  assert(typeof wasmExports["addVoiceToGroup"] != "undefined", "missing Wasm export: addVoiceToGroup");
  assert(typeof wasmExports["isVoiceGroup"] != "undefined", "missing Wasm export: isVoiceGroup");
  assert(typeof wasmExports["isVoiceGroupEmpty"] != "undefined", "missing Wasm export: isVoiceGroupEmpty");
  assert(typeof wasmExports["fadeGlobalVolume"] != "undefined", "missing Wasm export: fadeGlobalVolume");
  assert(typeof wasmExports["fadeVolume"] != "undefined", "missing Wasm export: fadeVolume");
  assert(typeof wasmExports["fadePan"] != "undefined", "missing Wasm export: fadePan");
  assert(typeof wasmExports["fadeRelativePlaySpeed"] != "undefined", "missing Wasm export: fadeRelativePlaySpeed");
  assert(typeof wasmExports["schedulePause"] != "undefined", "missing Wasm export: schedulePause");
  assert(typeof wasmExports["scheduleStop"] != "undefined", "missing Wasm export: scheduleStop");
  assert(typeof wasmExports["oscillateVolume"] != "undefined", "missing Wasm export: oscillateVolume");
  assert(typeof wasmExports["oscillatePan"] != "undefined", "missing Wasm export: oscillatePan");
  assert(typeof wasmExports["oscillateRelativePlaySpeed"] != "undefined", "missing Wasm export: oscillateRelativePlaySpeed");
  assert(typeof wasmExports["oscillateGlobalVolume"] != "undefined", "missing Wasm export: oscillateGlobalVolume");
  assert(typeof wasmExports["isFilterActive"] != "undefined", "missing Wasm export: isFilterActive");
  assert(typeof wasmExports["getFilterParamNames"] != "undefined", "missing Wasm export: getFilterParamNames");
  assert(typeof wasmExports["addFilter"] != "undefined", "missing Wasm export: addFilter");
  assert(typeof wasmExports["removeFilter"] != "undefined", "missing Wasm export: removeFilter");
  assert(typeof wasmExports["setFilterParams"] != "undefined", "missing Wasm export: setFilterParams");
  assert(typeof wasmExports["getFilterParams"] != "undefined", "missing Wasm export: getFilterParams");
  assert(typeof wasmExports["fadeFilterParameter"] != "undefined", "missing Wasm export: fadeFilterParameter");
  assert(typeof wasmExports["oscillateFilterParameter"] != "undefined", "missing Wasm export: oscillateFilterParameter");
  assert(typeof wasmExports["play3d"] != "undefined", "missing Wasm export: play3d");
  assert(typeof wasmExports["play3dWithLoopPoints"] != "undefined", "missing Wasm export: play3dWithLoopPoints");
  assert(typeof wasmExports["play3dClocked"] != "undefined", "missing Wasm export: play3dClocked");
  assert(typeof wasmExports["set3dSoundSpeed"] != "undefined", "missing Wasm export: set3dSoundSpeed");
  assert(typeof wasmExports["get3dSoundSpeed"] != "undefined", "missing Wasm export: get3dSoundSpeed");
  assert(typeof wasmExports["set3dListenerParameters"] != "undefined", "missing Wasm export: set3dListenerParameters");
  assert(typeof wasmExports["set3dListenerPosition"] != "undefined", "missing Wasm export: set3dListenerPosition");
  assert(typeof wasmExports["set3dListenerAt"] != "undefined", "missing Wasm export: set3dListenerAt");
  assert(typeof wasmExports["set3dListenerUp"] != "undefined", "missing Wasm export: set3dListenerUp");
  assert(typeof wasmExports["set3dListenerVelocity"] != "undefined", "missing Wasm export: set3dListenerVelocity");
  assert(typeof wasmExports["set3dSourceParameters"] != "undefined", "missing Wasm export: set3dSourceParameters");
  assert(typeof wasmExports["set3dSourcePosition"] != "undefined", "missing Wasm export: set3dSourcePosition");
  assert(typeof wasmExports["set3dSourceVelocity"] != "undefined", "missing Wasm export: set3dSourceVelocity");
  assert(typeof wasmExports["set3dSourceMinMaxDistance"] != "undefined", "missing Wasm export: set3dSourceMinMaxDistance");
  assert(typeof wasmExports["set3dSourceAttenuation"] != "undefined", "missing Wasm export: set3dSourceAttenuation");
  assert(typeof wasmExports["set3dSourceDopplerFactor"] != "undefined", "missing Wasm export: set3dSourceDopplerFactor");
  assert(typeof wasmExports["readSamplesFromFile"] != "undefined", "missing Wasm export: readSamplesFromFile");
  assert(typeof wasmExports["readSamplesFromMem"] != "undefined", "missing Wasm export: readSamplesFromMem");
  assert(typeof wasmExports["createBus"] != "undefined", "missing Wasm export: createBus");
  assert(typeof wasmExports["destroyBus"] != "undefined", "missing Wasm export: destroyBus");
  assert(typeof wasmExports["busPlayOnEngine"] != "undefined", "missing Wasm export: busPlayOnEngine");
  assert(typeof wasmExports["busSetChannels"] != "undefined", "missing Wasm export: busSetChannels");
  assert(typeof wasmExports["busSetVisualizationEnable"] != "undefined", "missing Wasm export: busSetVisualizationEnable");
  assert(typeof wasmExports["busCalcFFT"] != "undefined", "missing Wasm export: busCalcFFT");
  assert(typeof wasmExports["busGetWave"] != "undefined", "missing Wasm export: busGetWave");
  assert(typeof wasmExports["busGetApproximateVolume"] != "undefined", "missing Wasm export: busGetApproximateVolume");
  assert(typeof wasmExports["busAnnexSound"] != "undefined", "missing Wasm export: busAnnexSound");
  assert(typeof wasmExports["busGetActiveVoiceCount"] != "undefined", "missing Wasm export: busGetActiveVoiceCount");
  assert(typeof wasmExports["_emscripten_tls_init"] != "undefined", "missing Wasm export: _emscripten_tls_init");
  assert(typeof wasmExports["pthread_self"] != "undefined", "missing Wasm export: pthread_self");
  assert(typeof wasmExports["memcpy"] != "undefined", "missing Wasm export: memcpy");
  assert(typeof wasmExports["memset"] != "undefined", "missing Wasm export: memset");
  assert(typeof wasmExports["_emscripten_thread_init"] != "undefined", "missing Wasm export: _emscripten_thread_init");
  assert(typeof wasmExports["__set_thread_state"] != "undefined", "missing Wasm export: __set_thread_state");
  assert(typeof wasmExports["_emscripten_thread_crashed"] != "undefined", "missing Wasm export: _emscripten_thread_crashed");
  assert(typeof wasmExports["fflush"] != "undefined", "missing Wasm export: fflush");
  assert(typeof wasmExports["emscripten_stack_get_end"] != "undefined", "missing Wasm export: emscripten_stack_get_end");
  assert(typeof wasmExports["emscripten_stack_get_base"] != "undefined", "missing Wasm export: emscripten_stack_get_base");
  assert(typeof wasmExports["_emscripten_run_js_on_main_thread"] != "undefined", "missing Wasm export: _emscripten_run_js_on_main_thread");
  assert(typeof wasmExports["_emscripten_thread_free_data"] != "undefined", "missing Wasm export: _emscripten_thread_free_data");
  assert(typeof wasmExports["_emscripten_thread_exit"] != "undefined", "missing Wasm export: _emscripten_thread_exit");
  assert(typeof wasmExports["strerror"] != "undefined", "missing Wasm export: strerror");
  assert(typeof wasmExports["_emscripten_check_mailbox"] != "undefined", "missing Wasm export: _emscripten_check_mailbox");
  assert(typeof wasmExports["emscripten_stack_init"] != "undefined", "missing Wasm export: emscripten_stack_init");
  assert(typeof wasmExports["emscripten_stack_set_limits"] != "undefined", "missing Wasm export: emscripten_stack_set_limits");
  assert(typeof wasmExports["emscripten_stack_get_free"] != "undefined", "missing Wasm export: emscripten_stack_get_free");
  assert(typeof wasmExports["_emscripten_stack_restore"] != "undefined", "missing Wasm export: _emscripten_stack_restore");
  assert(typeof wasmExports["_emscripten_stack_alloc"] != "undefined", "missing Wasm export: _emscripten_stack_alloc");
  assert(typeof wasmExports["emscripten_stack_get_current"] != "undefined", "missing Wasm export: emscripten_stack_get_current");
  assert(typeof wasmExports["_emscripten_wasm_worker_initialize"] != "undefined", "missing Wasm export: _emscripten_wasm_worker_initialize");
  assert(typeof wasmExports["dynCall_ii"] != "undefined", "missing Wasm export: dynCall_ii");
  assert(typeof wasmExports["dynCall_vi"] != "undefined", "missing Wasm export: dynCall_vi");
  assert(typeof wasmExports["dynCall_v"] != "undefined", "missing Wasm export: dynCall_v");
  assert(typeof wasmExports["dynCall_iidii"] != "undefined", "missing Wasm export: dynCall_iidii");
  assert(typeof wasmExports["dynCall_fii"] != "undefined", "missing Wasm export: dynCall_fii");
  assert(typeof wasmExports["dynCall_viii"] != "undefined", "missing Wasm export: dynCall_viii");
  assert(typeof wasmExports["dynCall_iiiii"] != "undefined", "missing Wasm export: dynCall_iiiii");
  assert(typeof wasmExports["dynCall_iiii"] != "undefined", "missing Wasm export: dynCall_iiii");
  assert(typeof wasmExports["dynCall_vii"] != "undefined", "missing Wasm export: dynCall_vii");
  assert(typeof wasmExports["dynCall_iii"] != "undefined", "missing Wasm export: dynCall_iii");
  assert(typeof wasmExports["dynCall_vid"] != "undefined", "missing Wasm export: dynCall_vid");
  assert(typeof wasmExports["dynCall_viiiiifd"] != "undefined", "missing Wasm export: dynCall_viiiiifd");
  assert(typeof wasmExports["dynCall_viiifdii"] != "undefined", "missing Wasm export: dynCall_viiifdii");
  assert(typeof wasmExports["dynCall_viif"] != "undefined", "missing Wasm export: dynCall_viif");
  assert(typeof wasmExports["dynCall_viifdd"] != "undefined", "missing Wasm export: dynCall_viifdd");
  assert(typeof wasmExports["dynCall_viiffdd"] != "undefined", "missing Wasm export: dynCall_viiffdd");
  assert(typeof wasmExports["dynCall_iiji"] != "undefined", "missing Wasm export: dynCall_iiji");
  assert(typeof wasmExports["dynCall_viiii"] != "undefined", "missing Wasm export: dynCall_viiii");
  assert(typeof wasmExports["dynCall_iiiiiiii"] != "undefined", "missing Wasm export: dynCall_iiiiiiii");
  assert(typeof wasmExports["dynCall_iiiji"] != "undefined", "missing Wasm export: dynCall_iiiji");
  assert(typeof wasmExports["dynCall_iiiiiii"] != "undefined", "missing Wasm export: dynCall_iiiiiii");
  assert(typeof wasmExports["dynCall_jii"] != "undefined", "missing Wasm export: dynCall_jii");
  assert(typeof wasmExports["dynCall_iij"] != "undefined", "missing Wasm export: dynCall_iij");
  assert(typeof wasmExports["dynCall_iiiiiiiii"] != "undefined", "missing Wasm export: dynCall_iiiiiiiii");
  assert(typeof wasmExports["dynCall_iiiiii"] != "undefined", "missing Wasm export: dynCall_iiiiii");
  assert(typeof wasmExports["dynCall_viidi"] != "undefined", "missing Wasm export: dynCall_viidi");
  assert(typeof wasmExports["dynCall_viiiiii"] != "undefined", "missing Wasm export: dynCall_viiiiii");
  assert(typeof wasmExports["dynCall_iid"] != "undefined", "missing Wasm export: dynCall_iid");
  assert(typeof wasmExports["dynCall_jid"] != "undefined", "missing Wasm export: dynCall_jid");
  assert(typeof wasmExports["dynCall_di"] != "undefined", "missing Wasm export: dynCall_di");
  assert(typeof wasmExports["dynCall_vij"] != "undefined", "missing Wasm export: dynCall_vij");
  assert(typeof wasmExports["dynCall_vdi"] != "undefined", "missing Wasm export: dynCall_vdi");
  assert(typeof wasmExports["dynCall_viiiiiii"] != "undefined", "missing Wasm export: dynCall_viiiiiii");
  assert(typeof wasmExports["dynCall_jiji"] != "undefined", "missing Wasm export: dynCall_jiji");
  assert(typeof wasmExports["dynCall_iidiiii"] != "undefined", "missing Wasm export: dynCall_iidiiii");
  assert(typeof wasmExports["dynCall_viijii"] != "undefined", "missing Wasm export: dynCall_viijii");
  assert(typeof wasmExports["dynCall_iiiiij"] != "undefined", "missing Wasm export: dynCall_iiiiij");
  assert(typeof wasmExports["dynCall_iiiiid"] != "undefined", "missing Wasm export: dynCall_iiiiid");
  assert(typeof wasmExports["dynCall_iiiiijj"] != "undefined", "missing Wasm export: dynCall_iiiiijj");
  assert(typeof wasmExports["dynCall_iiiiiijj"] != "undefined", "missing Wasm export: dynCall_iiiiiijj");
  assert(typeof wasmExports["dynCall_viiiii"] != "undefined", "missing Wasm export: dynCall_viiiii");
  assert(typeof wasmExports["asyncify_start_unwind"] != "undefined", "missing Wasm export: asyncify_start_unwind");
  assert(typeof wasmExports["asyncify_stop_unwind"] != "undefined", "missing Wasm export: asyncify_stop_unwind");
  assert(typeof wasmExports["asyncify_start_rewind"] != "undefined", "missing Wasm export: asyncify_start_rewind");
  assert(typeof wasmExports["asyncify_stop_rewind"] != "undefined", "missing Wasm export: asyncify_stop_rewind");
  assert(typeof wasmExports["__indirect_function_table"] != "undefined", "missing Wasm export: __indirect_function_table");
  _malloc = Module["_malloc"] = createExportWrapper("malloc", 1);
  _free = Module["_free"] = createExportWrapper("free", 1);
  _ma_device__on_notification_unlocked = Module["_ma_device__on_notification_unlocked"] = createExportWrapper("ma_device__on_notification_unlocked", 1);
  _ma_malloc_emscripten = Module["_ma_malloc_emscripten"] = createExportWrapper("ma_malloc_emscripten", 2);
  _ma_free_emscripten = Module["_ma_free_emscripten"] = createExportWrapper("ma_free_emscripten", 2);
  _ma_device_process_pcm_frames_capture__webaudio = Module["_ma_device_process_pcm_frames_capture__webaudio"] = createExportWrapper("ma_device_process_pcm_frames_capture__webaudio", 3);
  _ma_device_process_pcm_frames_playback__webaudio = Module["_ma_device_process_pcm_frames_playback__webaudio"] = createExportWrapper("ma_device_process_pcm_frames_playback__webaudio", 3);
  _createWorkerInWasm = Module["_createWorkerInWasm"] = createExportWrapper("createWorkerInWasm", 0);
  _sendToWorker = Module["_sendToWorker"] = createExportWrapper("sendToWorker", 2);
  _getEngineGeneration = Module["_getEngineGeneration"] = createExportWrapper("getEngineGeneration", 0);
  _nativeFree = Module["_nativeFree"] = createExportWrapper("nativeFree", 1);
  _voiceEndedCallback = Module["_voiceEndedCallback"] = createExportWrapper("voiceEndedCallback", 1);
  _setDartEventCallback = Module["_setDartEventCallback"] = createExportWrapper("setDartEventCallback", 3);
  _clearDartCallbackRegistrations = Module["_clearDartCallbackRegistrations"] = createExportWrapper("clearDartCallbackRegistrations", 0);
  _startMixerCapture = Module["_startMixerCapture"] = createExportWrapper("startMixerCapture", 6);
  _stopMixerCapture = Module["_stopMixerCapture"] = createExportWrapper("stopMixerCapture", 0);
  _isMixerCaptureRunning = Module["_isMixerCaptureRunning"] = createExportWrapper("isMixerCaptureRunning", 0);
  _getMixerCaptureBufferPointer = Module["_getMixerCaptureBufferPointer"] = createExportWrapper("getMixerCaptureBufferPointer", 0);
  _getMixerCaptureBufferSize = Module["_getMixerCaptureBufferSize"] = createExportWrapper("getMixerCaptureBufferSize", 0);
  _getMixerCaptureAvailableBytes = Module["_getMixerCaptureAvailableBytes"] = createExportWrapper("getMixerCaptureAvailableBytes", 0);
  _getMixerCaptureReadOffset = Module["_getMixerCaptureReadOffset"] = createExportWrapper("getMixerCaptureReadOffset", 0);
  _advanceMixerCaptureReadPosition = Module["_advanceMixerCaptureReadPosition"] = createExportWrapper("advanceMixerCaptureReadPosition", 1);
  _getMixerOutputWavHeader = Module["_getMixerOutputWavHeader"] = createExportWrapper("getMixerOutputWavHeader", 0);
  _setMixerOutputCallback = Module["_setMixerOutputCallback"] = createExportWrapper("setMixerOutputCallback", 1);
  _areXiphLibsAvailable = Module["_areXiphLibsAvailable"] = createExportWrapper("areXiphLibsAvailable", 0);
  _initEngine = Module["_initEngine"] = createExportWrapper("initEngine", 5);
  _setAndroidAAudioAttributes = Module["_setAndroidAAudioAttributes"] = createExportWrapper("setAndroidAAudioAttributes", 1);
  _listPlaybackDevices = Module["_listPlaybackDevices"] = createExportWrapper("listPlaybackDevices", 4);
  _changeDevice = Module["_changeDevice"] = createExportWrapper("changeDevice", 1);
  _freeListPlaybackDevices = Module["_freeListPlaybackDevices"] = createExportWrapper("freeListPlaybackDevices", 4);
  _dispose = Module["_dispose"] = createExportWrapper("dispose", 0);
  _isInited = Module["_isInited"] = createExportWrapper("isInited", 0);
  _loadFile = Module["_loadFile"] = createExportWrapper("loadFile", 3);
  _loadMem = Module["_loadMem"] = createExportWrapper("loadMem", 5);
  _setBufferStream = Module["_setBufferStream"] = createExportWrapper("setBufferStream", 9);
  _resetBufferStream = Module["_resetBufferStream"] = createExportWrapper("resetBufferStream", 1);
  _getStreamTimeConsumed = Module["_getStreamTimeConsumed"] = createExportWrapper("getStreamTimeConsumed", 2);
  _setBufferIcyMetaInt = Module["_setBufferIcyMetaInt"] = createExportWrapper("setBufferIcyMetaInt", 2);
  _setPullBufferStream = Module["_setPullBufferStream"] = createExportWrapper("setPullBufferStream", 11);
  _resetPullBufferStream = Module["_resetPullBufferStream"] = createExportWrapper("resetPullBufferStream", 1);
  _addPullBufferDataStream = Module["_addPullBufferDataStream"] = createExportWrapper("addPullBufferDataStream", 4);
  _getPullBufferTimeRange = Module["_getPullBufferTimeRange"] = createExportWrapper("getPullBufferTimeRange", 3);
  _addAudioDataStream = Module["_addAudioDataStream"] = createExportWrapper("addAudioDataStream", 3);
  _setDataIsEnded = Module["_setDataIsEnded"] = createExportWrapper("setDataIsEnded", 1);
  _getBufferSize = Module["_getBufferSize"] = createExportWrapper("getBufferSize", 2);
  _loadWaveform = Module["_loadWaveform"] = createExportWrapper("loadWaveform", 5);
  _setWaveformScale = Module["_setWaveformScale"] = createExportWrapper("setWaveformScale", 2);
  _setWaveformDetune = Module["_setWaveformDetune"] = createExportWrapper("setWaveformDetune", 2);
  _setWaveformFreq = Module["_setWaveformFreq"] = createExportWrapper("setWaveformFreq", 2);
  _setSuperWave = Module["_setSuperWave"] = createExportWrapper("setSuperWave", 2);
  _setWaveform = Module["_setWaveform"] = createExportWrapper("setWaveform", 2);
  _speechText = Module["_speechText"] = createExportWrapper("speechText", 2);
  _pauseSwitch = Module["_pauseSwitch"] = createExportWrapper("pauseSwitch", 1);
  _setPause = Module["_setPause"] = createExportWrapper("setPause", 2);
  _getPause = Module["_getPause"] = createExportWrapper("getPause", 1);
  _setRelativePlaySpeed = Module["_setRelativePlaySpeed"] = createExportWrapper("setRelativePlaySpeed", 2);
  _getRelativePlaySpeed = Module["_getRelativePlaySpeed"] = createExportWrapper("getRelativePlaySpeed", 1);
  _getApproximateVolume = Module["_getApproximateVolume"] = createExportWrapper("getApproximateVolume", 1);
  _play = Module["_play"] = createExportWrapper("play", 8);
  _playWithLoopPoints = Module["_playWithLoopPoints"] = createExportWrapper("playWithLoopPoints", 9);
  _playClocked = Module["_playClocked"] = createExportWrapper("playClocked", 6);
  _setDelaySamples = Module["_setDelaySamples"] = createExportWrapper("setDelaySamples", 2);
  _getStreamTime = Module["_getStreamTime"] = createExportWrapper("getStreamTime", 1);
  _resetStreamTime = Module["_resetStreamTime"] = createExportWrapper("resetStreamTime", 0);
  _getEngineTime = Module["_getEngineTime"] = createExportWrapper("getEngineTime", 0);
  _playScheduled = Module["_playScheduled"] = createExportWrapper("playScheduled", 7);
  _stopScheduled = Module["_stopScheduled"] = createExportWrapper("stopScheduled", 2);
  _fadeScheduled = Module["_fadeScheduled"] = createExportWrapper("fadeScheduled", 5);
  _stop = Module["_stop"] = createExportWrapper("stop", 1);
  _disposeSound = Module["_disposeSound"] = createExportWrapper("disposeSound", 1);
  _disposeAllSound = Module["_disposeAllSound"] = createExportWrapper("disposeAllSound", 0);
  _getLooping = Module["_getLooping"] = createExportWrapper("getLooping", 1);
  _setLooping = Module["_setLooping"] = createExportWrapper("setLooping", 2);
  _getLoopPoint = Module["_getLoopPoint"] = createExportWrapper("getLoopPoint", 1);
  _setLoopPoint = Module["_setLoopPoint"] = createExportWrapper("setLoopPoint", 2);
  _getLoopEndPoint = Module["_getLoopEndPoint"] = createExportWrapper("getLoopEndPoint", 1);
  _setLoopEndPoint = Module["_setLoopEndPoint"] = createExportWrapper("setLoopEndPoint", 2);
  _setVisualizationEnabled = Module["_setVisualizationEnabled"] = createExportWrapper("setVisualizationEnabled", 1);
  _getVisualizationEnabled = Module["_getVisualizationEnabled"] = createExportWrapper("getVisualizationEnabled", 0);
  _getFft = Module["_getFft"] = createExportWrapper("getFft", 2);
  _getWave = Module["_getWave"] = createExportWrapper("getWave", 2);
  _setFftSmoothing = Module["_setFftSmoothing"] = createExportWrapper("setFftSmoothing", 1);
  _getAudioTexture = Module["_getAudioTexture"] = createExportWrapper("getAudioTexture", 2);
  _getAudioTexture2D = Module["_getAudioTexture2D"] = createExportWrapper("getAudioTexture2D", 2);
  _getTextureValue = Module["_getTextureValue"] = createExportWrapper("getTextureValue", 2);
  _getLength = Module["_getLength"] = createExportWrapper("getLength", 1);
  _seek = Module["_seek"] = createExportWrapper("seek", 2);
  _getPosition = Module["_getPosition"] = createExportWrapper("getPosition", 1);
  _getGlobalVolume = Module["_getGlobalVolume"] = createExportWrapper("getGlobalVolume", 0);
  _setGlobalVolume = Module["_setGlobalVolume"] = createExportWrapper("setGlobalVolume", 1);
  _getVolume = Module["_getVolume"] = createExportWrapper("getVolume", 1);
  _setVolume = Module["_setVolume"] = createExportWrapper("setVolume", 2);
  _getPan = Module["_getPan"] = createExportWrapper("getPan", 1);
  _setPan = Module["_setPan"] = createExportWrapper("setPan", 2);
  _setPanAbsolute = Module["_setPanAbsolute"] = createExportWrapper("setPanAbsolute", 3);
  _getIsValidVoiceHandle = Module["_getIsValidVoiceHandle"] = createExportWrapper("getIsValidVoiceHandle", 1);
  _getActiveVoiceCount = Module["_getActiveVoiceCount"] = createExportWrapper("getActiveVoiceCount", 0);
  _countAudioSource = Module["_countAudioSource"] = createExportWrapper("countAudioSource", 1);
  _getVoiceCount = Module["_getVoiceCount"] = createExportWrapper("getVoiceCount", 0);
  _getProtectVoice = Module["_getProtectVoice"] = createExportWrapper("getProtectVoice", 1);
  _setProtectVoice = Module["_setProtectVoice"] = createExportWrapper("setProtectVoice", 2);
  _setInaudibleBehavior = Module["_setInaudibleBehavior"] = createExportWrapper("setInaudibleBehavior", 3);
  _getMaxActiveVoiceCount = Module["_getMaxActiveVoiceCount"] = createExportWrapper("getMaxActiveVoiceCount", 0);
  _setMaxActiveVoiceCount = Module["_setMaxActiveVoiceCount"] = createExportWrapper("setMaxActiveVoiceCount", 1);
  _createVoiceGroup = Module["_createVoiceGroup"] = createExportWrapper("createVoiceGroup", 0);
  _destroyVoiceGroup = Module["_destroyVoiceGroup"] = createExportWrapper("destroyVoiceGroup", 1);
  _addVoiceToGroup = Module["_addVoiceToGroup"] = createExportWrapper("addVoiceToGroup", 2);
  _isVoiceGroup = Module["_isVoiceGroup"] = createExportWrapper("isVoiceGroup", 1);
  _isVoiceGroupEmpty = Module["_isVoiceGroupEmpty"] = createExportWrapper("isVoiceGroupEmpty", 1);
  _fadeGlobalVolume = Module["_fadeGlobalVolume"] = createExportWrapper("fadeGlobalVolume", 2);
  _fadeVolume = Module["_fadeVolume"] = createExportWrapper("fadeVolume", 3);
  _fadePan = Module["_fadePan"] = createExportWrapper("fadePan", 3);
  _fadeRelativePlaySpeed = Module["_fadeRelativePlaySpeed"] = createExportWrapper("fadeRelativePlaySpeed", 3);
  _schedulePause = Module["_schedulePause"] = createExportWrapper("schedulePause", 2);
  _scheduleStop = Module["_scheduleStop"] = createExportWrapper("scheduleStop", 2);
  _oscillateVolume = Module["_oscillateVolume"] = createExportWrapper("oscillateVolume", 4);
  _oscillatePan = Module["_oscillatePan"] = createExportWrapper("oscillatePan", 4);
  _oscillateRelativePlaySpeed = Module["_oscillateRelativePlaySpeed"] = createExportWrapper("oscillateRelativePlaySpeed", 4);
  _oscillateGlobalVolume = Module["_oscillateGlobalVolume"] = createExportWrapper("oscillateGlobalVolume", 3);
  _isFilterActive = Module["_isFilterActive"] = createExportWrapper("isFilterActive", 4);
  _getFilterParamNames = Module["_getFilterParamNames"] = createExportWrapper("getFilterParamNames", 3);
  _addFilter = Module["_addFilter"] = createExportWrapper("addFilter", 3);
  _removeFilter = Module["_removeFilter"] = createExportWrapper("removeFilter", 3);
  _setFilterParams = Module["_setFilterParams"] = createExportWrapper("setFilterParams", 5);
  _getFilterParams = Module["_getFilterParams"] = createExportWrapper("getFilterParams", 5);
  _fadeFilterParameter = Module["_fadeFilterParameter"] = createExportWrapper("fadeFilterParameter", 6);
  _oscillateFilterParameter = Module["_oscillateFilterParameter"] = createExportWrapper("oscillateFilterParameter", 7);
  _play3d = Module["_play3d"] = createExportWrapper("play3d", 13);
  _play3dWithLoopPoints = Module["_play3dWithLoopPoints"] = createExportWrapper("play3dWithLoopPoints", 14);
  _play3dClocked = Module["_play3dClocked"] = createExportWrapper("play3dClocked", 11);
  _set3dSoundSpeed = Module["_set3dSoundSpeed"] = createExportWrapper("set3dSoundSpeed", 1);
  _get3dSoundSpeed = Module["_get3dSoundSpeed"] = createExportWrapper("get3dSoundSpeed", 0);
  _set3dListenerParameters = Module["_set3dListenerParameters"] = createExportWrapper("set3dListenerParameters", 12);
  _set3dListenerPosition = Module["_set3dListenerPosition"] = createExportWrapper("set3dListenerPosition", 3);
  _set3dListenerAt = Module["_set3dListenerAt"] = createExportWrapper("set3dListenerAt", 3);
  _set3dListenerUp = Module["_set3dListenerUp"] = createExportWrapper("set3dListenerUp", 3);
  _set3dListenerVelocity = Module["_set3dListenerVelocity"] = createExportWrapper("set3dListenerVelocity", 3);
  _set3dSourceParameters = Module["_set3dSourceParameters"] = createExportWrapper("set3dSourceParameters", 7);
  _set3dSourcePosition = Module["_set3dSourcePosition"] = createExportWrapper("set3dSourcePosition", 4);
  _set3dSourceVelocity = Module["_set3dSourceVelocity"] = createExportWrapper("set3dSourceVelocity", 4);
  _set3dSourceMinMaxDistance = Module["_set3dSourceMinMaxDistance"] = createExportWrapper("set3dSourceMinMaxDistance", 3);
  _set3dSourceAttenuation = Module["_set3dSourceAttenuation"] = createExportWrapper("set3dSourceAttenuation", 3);
  _set3dSourceDopplerFactor = Module["_set3dSourceDopplerFactor"] = createExportWrapper("set3dSourceDopplerFactor", 2);
  _readSamplesFromFile = Module["_readSamplesFromFile"] = createExportWrapper("readSamplesFromFile", 6);
  _readSamplesFromMem = Module["_readSamplesFromMem"] = createExportWrapper("readSamplesFromMem", 7);
  _createBus = Module["_createBus"] = createExportWrapper("createBus", 0);
  _destroyBus = Module["_destroyBus"] = createExportWrapper("destroyBus", 1);
  _busPlayOnEngine = Module["_busPlayOnEngine"] = createExportWrapper("busPlayOnEngine", 4);
  _busSetChannels = Module["_busSetChannels"] = createExportWrapper("busSetChannels", 2);
  _busSetVisualizationEnable = Module["_busSetVisualizationEnable"] = createExportWrapper("busSetVisualizationEnable", 2);
  _busCalcFFT = Module["_busCalcFFT"] = createExportWrapper("busCalcFFT", 1);
  _busGetWave = Module["_busGetWave"] = createExportWrapper("busGetWave", 1);
  _busGetApproximateVolume = Module["_busGetApproximateVolume"] = createExportWrapper("busGetApproximateVolume", 2);
  _busAnnexSound = Module["_busAnnexSound"] = createExportWrapper("busAnnexSound", 2);
  _busGetActiveVoiceCount = Module["_busGetActiveVoiceCount"] = createExportWrapper("busGetActiveVoiceCount", 1);
  __emscripten_tls_init = createExportWrapper("_emscripten_tls_init", 0);
  _pthread_self = createExportWrapper("pthread_self", 0);
  _memcpy = Module["_memcpy"] = createExportWrapper("memcpy", 3);
  _memset = Module["_memset"] = createExportWrapper("memset", 3);
  __emscripten_thread_init = createExportWrapper("_emscripten_thread_init", 6);
  ___set_thread_state = createExportWrapper("__set_thread_state", 4);
  __emscripten_thread_crashed = createExportWrapper("_emscripten_thread_crashed", 0);
  _fflush = createExportWrapper("fflush", 1);
  _emscripten_stack_get_end = wasmExports["emscripten_stack_get_end"];
  _emscripten_stack_get_base = wasmExports["emscripten_stack_get_base"];
  __emscripten_run_js_on_main_thread = createExportWrapper("_emscripten_run_js_on_main_thread", 5);
  __emscripten_thread_free_data = createExportWrapper("_emscripten_thread_free_data", 1);
  __emscripten_thread_exit = createExportWrapper("_emscripten_thread_exit", 1);
  _strerror = createExportWrapper("strerror", 1);
  __emscripten_check_mailbox = createExportWrapper("_emscripten_check_mailbox", 0);
  _emscripten_stack_init = wasmExports["emscripten_stack_init"];
  _emscripten_stack_set_limits = wasmExports["emscripten_stack_set_limits"];
  _emscripten_stack_get_free = wasmExports["emscripten_stack_get_free"];
  __emscripten_stack_restore = wasmExports["_emscripten_stack_restore"];
  __emscripten_stack_alloc = wasmExports["_emscripten_stack_alloc"];
  _emscripten_stack_get_current = wasmExports["emscripten_stack_get_current"];
  __emscripten_wasm_worker_initialize = createExportWrapper("_emscripten_wasm_worker_initialize", 2);
  dynCall_ii = dynCalls["ii"] = createExportWrapper("dynCall_ii", 2);
  dynCall_vi = dynCalls["vi"] = createExportWrapper("dynCall_vi", 2);
  dynCall_v = dynCalls["v"] = createExportWrapper("dynCall_v", 1);
  dynCall_iidii = dynCalls["iidii"] = createExportWrapper("dynCall_iidii", 5);
  dynCall_fii = dynCalls["fii"] = createExportWrapper("dynCall_fii", 3);
  dynCall_viii = dynCalls["viii"] = createExportWrapper("dynCall_viii", 4);
  dynCall_iiiii = dynCalls["iiiii"] = createExportWrapper("dynCall_iiiii", 5);
  dynCall_iiii = dynCalls["iiii"] = createExportWrapper("dynCall_iiii", 4);
  dynCall_vii = dynCalls["vii"] = createExportWrapper("dynCall_vii", 3);
  dynCall_iii = dynCalls["iii"] = createExportWrapper("dynCall_iii", 3);
  dynCall_vid = dynCalls["vid"] = createExportWrapper("dynCall_vid", 3);
  dynCall_viiiiifd = dynCalls["viiiiifd"] = createExportWrapper("dynCall_viiiiifd", 8);
  dynCall_viiifdii = dynCalls["viiifdii"] = createExportWrapper("dynCall_viiifdii", 8);
  dynCall_viif = dynCalls["viif"] = createExportWrapper("dynCall_viif", 4);
  dynCall_viifdd = dynCalls["viifdd"] = createExportWrapper("dynCall_viifdd", 6);
  dynCall_viiffdd = dynCalls["viiffdd"] = createExportWrapper("dynCall_viiffdd", 7);
  dynCall_iiji = dynCalls["iiji"] = createExportWrapper("dynCall_iiji", 4);
  dynCall_viiii = dynCalls["viiii"] = createExportWrapper("dynCall_viiii", 5);
  dynCall_iiiiiiii = dynCalls["iiiiiiii"] = createExportWrapper("dynCall_iiiiiiii", 8);
  dynCall_iiiji = dynCalls["iiiji"] = createExportWrapper("dynCall_iiiji", 5);
  dynCall_iiiiiii = dynCalls["iiiiiii"] = createExportWrapper("dynCall_iiiiiii", 7);
  dynCall_jii = dynCalls["jii"] = createExportWrapper("dynCall_jii", 3);
  dynCall_iij = dynCalls["iij"] = createExportWrapper("dynCall_iij", 3);
  dynCall_iiiiiiiii = dynCalls["iiiiiiiii"] = createExportWrapper("dynCall_iiiiiiiii", 9);
  dynCall_iiiiii = dynCalls["iiiiii"] = createExportWrapper("dynCall_iiiiii", 6);
  dynCall_viidi = dynCalls["viidi"] = createExportWrapper("dynCall_viidi", 5);
  dynCall_viiiiii = dynCalls["viiiiii"] = createExportWrapper("dynCall_viiiiii", 7);
  dynCall_iid = dynCalls["iid"] = createExportWrapper("dynCall_iid", 3);
  dynCall_jid = dynCalls["jid"] = createExportWrapper("dynCall_jid", 3);
  dynCall_di = dynCalls["di"] = createExportWrapper("dynCall_di", 2);
  dynCall_vij = dynCalls["vij"] = createExportWrapper("dynCall_vij", 3);
  dynCall_vdi = dynCalls["vdi"] = createExportWrapper("dynCall_vdi", 3);
  dynCall_viiiiiii = dynCalls["viiiiiii"] = createExportWrapper("dynCall_viiiiiii", 8);
  dynCall_jiji = dynCalls["jiji"] = createExportWrapper("dynCall_jiji", 4);
  dynCall_iidiiii = dynCalls["iidiiii"] = createExportWrapper("dynCall_iidiiii", 7);
  dynCall_viijii = dynCalls["viijii"] = createExportWrapper("dynCall_viijii", 6);
  dynCall_iiiiij = dynCalls["iiiiij"] = createExportWrapper("dynCall_iiiiij", 6);
  dynCall_iiiiid = dynCalls["iiiiid"] = createExportWrapper("dynCall_iiiiid", 6);
  dynCall_iiiiijj = dynCalls["iiiiijj"] = createExportWrapper("dynCall_iiiiijj", 7);
  dynCall_iiiiiijj = dynCalls["iiiiiijj"] = createExportWrapper("dynCall_iiiiiijj", 8);
  dynCall_viiiii = dynCalls["viiiii"] = createExportWrapper("dynCall_viiiii", 6);
  _asyncify_start_unwind = createExportWrapper("asyncify_start_unwind", 1);
  _asyncify_stop_unwind = createExportWrapper("asyncify_stop_unwind", 0);
  _asyncify_start_rewind = createExportWrapper("asyncify_start_rewind", 1);
  _asyncify_stop_rewind = createExportWrapper("asyncify_stop_rewind", 0);
  __indirect_function_table = wasmTable = wasmExports["__indirect_function_table"];
}

var wasmImports;

function assignWasmImports() {
  wasmImports = {
    /** @export */ __assert_fail: ___assert_fail,
    /** @export */ __cxa_throw: ___cxa_throw,
    /** @export */ __pthread_create_js: ___pthread_create_js,
    /** @export */ __syscall_fcntl64: ___syscall_fcntl64,
    /** @export */ __syscall_ioctl: ___syscall_ioctl,
    /** @export */ __syscall_openat: ___syscall_openat,
    /** @export */ _abort_js: __abort_js,
    /** @export */ _emscripten_init_main_thread_js: __emscripten_init_main_thread_js,
    /** @export */ _emscripten_log_formatted: __emscripten_log_formatted,
    /** @export */ _emscripten_notify_mailbox_postmessage: __emscripten_notify_mailbox_postmessage,
    /** @export */ _emscripten_receive_on_main_thread_js: __emscripten_receive_on_main_thread_js,
    /** @export */ _emscripten_thread_cleanup: __emscripten_thread_cleanup,
    /** @export */ _emscripten_thread_mailbox_await: __emscripten_thread_mailbox_await,
    /** @export */ _emscripten_thread_set_strongref: __emscripten_thread_set_strongref,
    /** @export */ _tzset_js: __tzset_js,
    /** @export */ clock_time_get: _clock_time_get,
    /** @export */ emscripten_asm_const_int: _emscripten_asm_const_int,
    /** @export */ emscripten_audio_context_quantum_size: _emscripten_audio_context_quantum_size,
    /** @export */ emscripten_audio_worklet_post_function_sig: _emscripten_audio_worklet_post_function_sig,
    /** @export */ emscripten_audio_worklet_post_function_vii: _emscripten_audio_worklet_post_function_vii,
    /** @export */ emscripten_audio_worklet_post_function_viii: _emscripten_audio_worklet_post_function_viii,
    /** @export */ emscripten_check_blocking_allowed: _emscripten_check_blocking_allowed,
    /** @export */ emscripten_create_audio_context: _emscripten_create_audio_context,
    /** @export */ emscripten_create_wasm_audio_worklet_node: _emscripten_create_wasm_audio_worklet_node,
    /** @export */ emscripten_create_wasm_audio_worklet_processor_async: _emscripten_create_wasm_audio_worklet_processor_async,
    /** @export */ emscripten_destroy_audio_context: _emscripten_destroy_audio_context,
    /** @export */ emscripten_destroy_web_audio_node: _emscripten_destroy_web_audio_node,
    /** @export */ emscripten_exit_with_live_runtime: _emscripten_exit_with_live_runtime,
    /** @export */ emscripten_get_now: _emscripten_get_now,
    /** @export */ emscripten_resize_heap: _emscripten_resize_heap,
    /** @export */ emscripten_sleep: _emscripten_sleep,
    /** @export */ emscripten_start_wasm_audio_worklet_thread_async: _emscripten_start_wasm_audio_worklet_thread_async,
    /** @export */ environ_get: _environ_get,
    /** @export */ environ_sizes_get: _environ_sizes_get,
    /** @export */ exit: _exit,
    /** @export */ fd_close: _fd_close,
    /** @export */ fd_read: _fd_read,
    /** @export */ fd_seek: _fd_seek,
    /** @export */ fd_write: _fd_write,
    /** @export */ memory: wasmMemory,
    /** @export */ random_get: _random_get
  };
}

// include: postamble.js
// === Auto-generated postamble setup entry stuff ===
var calledRun;

function stackCheckInit() {
  // This is normally called automatically during __wasm_call_ctors but need to
  // get these values before even running any of the ctors so we call it redundantly
  // here.
  // See $establishStackSpace for the equivalent code that runs on a thread
  assert(!ENVIRONMENT_IS_PTHREAD);
  _emscripten_stack_init();
  // TODO(sbc): Move writeStackCookie to native to to avoid this.
  writeStackCookie();
}

function run() {
  if (runDependencies > 0) {
    dependenciesFulfilled = run;
    return;
  }
  if ((ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER)) {
    readyPromiseResolve?.(Module);
    initRuntime();
    return;
  }
  stackCheckInit();
  preRun();
  // a preRun added a dependency, run will be called later
  if (runDependencies > 0) {
    dependenciesFulfilled = run;
    return;
  }
  function doRun() {
    // run may have just been called through dependencies being fulfilled just in this very frame,
    // or while the async setStatus time below was happening
    assert(!calledRun);
    calledRun = true;
    Module["calledRun"] = true;
    if (ABORT) return;
    initRuntime();
    readyPromiseResolve?.(Module);
    Module["onRuntimeInitialized"]?.();
    consumedModuleProp("onRuntimeInitialized");
    assert(!Module["_main"], 'compiled without a main, but one is present. if you added it from JS, use Module["onRuntimeInitialized"]');
    postRun();
  }
  if (Module["setStatus"]) {
    Module["setStatus"]("Running...");
    setTimeout(() => {
      setTimeout(() => Module["setStatus"](""), 1);
      doRun();
    }, 1);
  } else {
    doRun();
  }
  checkStackCookie();
}

function checkUnflushedContent() {
  // Compiler settings do not allow exiting the runtime, so flushing
  // the streams is not possible. but in ASSERTIONS mode we check
  // if there was something to flush, and if so tell the user they
  // should request that the runtime be exitable.
  // Normally we would not even include flush() at all, but in ASSERTIONS
  // builds we do so just for this check, and here we see if there is any
  // content to flush, that is, we check if there would have been
  // something a non-ASSERTIONS build would have not seen.
  // How we flush the streams depends on whether we are in SYSCALLS_REQUIRE_FILESYSTEM=0
  // mode (which has its own special function for this; otherwise, all
  // the code is inside libc)
  var oldOut = out;
  var oldErr = err;
  var has = false;
  out = err = x => {
    has = true;
  };
  try {
    // it doesn't matter if it fails
    _fflush(0);
    // also flush in the JS FS layer
    for (var name of [ "stdout", "stderr" ]) {
      var info = FS.analyzePath("/dev/" + name);
      if (!info) return;
      var stream = info.object;
      var rdev = stream.rdev;
      var tty = TTY.ttys[rdev];
      if (tty?.output?.length) {
        has = true;
      }
    }
  } catch (e) {}
  out = oldOut;
  err = oldErr;
  if (has) {
    warnOnce("stdio streams had content in them that was not flushed. you should set EXIT_RUNTIME to 1 (see the Emscripten FAQ), or make sure to emit a newline when you printf etc.");
  }
}

var wasmExports;

if ((!(ENVIRONMENT_IS_PTHREAD || ENVIRONMENT_IS_WASM_WORKER))) {
  // Call createWasm on startup if we are the main thread.
  // Worker threads call this once they receive the module via postMessage
  // In modularize mode the generated code is within a factory function so we
  // can use await here (since it's not top-level-await).
  wasmExports = await (createWasm());
  run();
}

// end include: postamble.js
// include: postamble_modularize.js
// In MODULARIZE mode we wrap the generated code in a factory function
// and return either the Module itself, or a promise of the module.
// We assign to the `moduleRtn` global here and configure closure to see
// this as and extern so it won't get minified.
if (runtimeInitialized) {
  moduleRtn = Module;
} else {
  // Set up the promise that indicates the Module is initialized
  moduleRtn = new Promise((resolve, reject) => {
    readyPromiseResolve = resolve;
    readyPromiseReject = reject;
  });
}

// Assertion for attempting to access module properties on the incoming
// moduleArg.  In the past we used this object as the prototype of the module
// and assigned properties to it, but now we return a distinct object.  This
// keeps the instance private until it is ready (i.e the promise has been
// resolved).
for (const prop of Object.keys(Module)) {
  if (!(prop in moduleArg)) {
    Object.defineProperty(moduleArg, prop, {
      configurable: true,
      get() {
        abort(`Access to module property ('${prop}') is no longer possible via the module constructor argument; Instead, use the result of the module constructor.`);
      }
    });
  }
}


    return moduleRtn;
  };
})();

// Export using a UMD style export, or ES6 exports if selected
if (typeof exports === 'object' && typeof module === 'object') {
  module.exports = Module_soloud;
  // This default export looks redundant, but it allows TS to import this
  // commonjs style module.
  module.exports.default = Module_soloud;
} else if (typeof define === 'function' && define['amd'])
  define([], () => Module_soloud);

// Create code for detecting if we are running in a pthread.
// Normally this detection is done when the module is itself run but
// when running in MODULARIZE mode we need use this to know if we should
// run the module constructor on startup (true only for pthreads).
var isPthread = globalThis.self?.name?.startsWith('em-pthread');
// In order to support both web and node we also need to detect node here.
var isNode = globalThis.process?.versions?.node && globalThis.process?.type != 'renderer';
if (isNode) isPthread = require('worker_threads').workerData === 'em-pthread'

isPthread && Module_soloud();

// Same as above for for WASM_WORKERS
// Normally this detection is done when the module is itself run but
// when running in MODULARIZE mode we need use this to know if we should
// run the module constructor on startup (true only for pthreads).
var isWW = globalThis.self?.name == 'em-ww';
// In order to support both web and node we also need to detect node here.
if (isNode) isWW = require('worker_threads').workerData === 'em-ww';

isWW ||= !!globalThis.AudioWorkletGlobalScope;
// When running as a wasm worker, construct a new instance on startup

isWW && Module_soloud();


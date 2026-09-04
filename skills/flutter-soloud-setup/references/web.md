# Web platform details for flutter_soloud

## Required script tag

Add to the `<body>` of `web/index.html`:

```html
<script src="assets/packages/flutter_soloud/web/init_soloud.js" defer></script>
```

This loader picks the right WASM build at runtime. The older two-tag form that also loads `libflutter_soloud_plugin.js` explicitly still works but is unnecessary.

## Two WASM builds, chosen automatically

- **Multi-threaded (AudioWorklet)** — used when the page is **cross-origin isolated**, i.e. served with:
  ```http
  Cross-Origin-Opener-Policy: same-origin
  Cross-Embedder-Policy: require-corp
  ```
  Mixing runs on a dedicated real-time thread; UI jank and GC pauses don't cause crackles. Strictly better whenever you control the headers.
- **Single-threaded (ScriptProcessorNode)** — fallback everywhere else. Works on any static hosting (CrazyGames, Poki, etc. that can't send COOP/COEP), but mixing shares the main thread with Flutter rendering, so heavy UI can glitch audio. ScriptProcessorNode is deprecated and may eventually be removed from browsers.

Missing headers are not an error: the plugin silently falls back to single-threaded.

## Dev-server combinations

```bash
# NOT cross-origin isolated -> single-threaded build
flutter run -d chrome -t lib/main.dart

# Dev server sends its own COOP/COEP for WasmGC -> AudioWorklet build.
# Do NOT add --web-header flags here (see traps below).
flutter run -d chrome --wasm -t lib/main.dart

# Manual isolation without --wasm -> AudioWorklet build
flutter run -d chrome \
  --web-header=Cross-Origin-Opener-Policy=same-origin \
  --web-header=Cross-Origin-Embedder-Policy=require-corp \
  -t lib/main.dart
```

Release: `flutter build web [--wasm]` — the build command is unrelated to headers; only what your server sends when serving the app matters.

## Traps

- **`--web-header` + `--wasm` = broken.** The dev server already sends `COEP: credentialless`; your flag is appended, producing `COEP: credentialless, require-corp`, which blocks the plugin's worker threads (`ERR_BLOCKED_BY_RESPONSE` in console) and the WASM module never loads.
- Cross-origin isolation requires a **secure context**: deploy over HTTPS.
- With `require-corp`, cross-origin resources (CDN assets, fonts, third-party APIs) must send `Cross-Origin-Resource-Policy: cross-origin` or be fetched via CORS, otherwise the browser blocks them. COOP/COEP also breaks some popup flows (certain Google Auth popups) and cross-origin ad iframes.
- **`loadUrl()` hits CORS** if the server doesn't send `Access-Control-Allow-Origin`. For local experiments only: `flutter run -d chrome --web-browser-flag '--disable-web-security' --release`.
- **No local file access on web**: `loadFile()` on a local path is impossible. Use `loadMem()` with the bytes instead. Note: on web `loadMem()` ignores its `mode` parameter (`LoadMode.disk` is not possible); the data is fed to the engine in 128 KB chunks yielding to the event loop, so large files don't freeze the UI.
- **Per-sound filters are not supported on web** (global filters are).
- Native-only `init()` options are silently ignored on web: `lowLatency`, `androidAAudioAttributes`, `devicePeriodFrames`, `renderAheadFrames`. So is `setAudioDeviceIdleTimeout` (the web device is always kept running).
- Only the default output device exists on web: `listPlaybackDevices()` returns it, and `changeDevice()` can only go back to default.
- `changeDevice()` on web is async because the AudioWorklet build must do an async ccall (miniaudio spin-waits while the worklet thread starts).

## Excluding Xiph libs on web

Build hooks don't apply to the web WASM binaries. Edit `web/compile_wasm.sh` **in the flutter_soloud package source** (set `NO_XIPH_LIBS="1"`) and run it. Requires Emscripten on Linux/macOS (Windows: WSL).

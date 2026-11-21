# flutter_soloud: instructions for coding agents

flutter_soloud is a Flutter audio plugin: a Dart API over the SoLoud C++
engine with the miniaudio backend. On Android, iOS, macOS, Windows and Linux
Dart calls the native code through `dart:ffi`; on the web the same C++ is
compiled to WASM and called through `dart:js_interop`.

These rules are for agents that change the plugin itself. Agents that only
*use* the plugin in an app should read the skills in `skills/`.

## Map

| Path | What it holds |
|---|---|
| `lib/flutter_soloud.dart` | The public exports. |
| `lib/src/soloud.dart` | `SoLoud`, the public API (a singleton). |
| `lib/src/bindings/` | `bindings_player.dart` (abstract), `bindings_player_ffi.dart` (native), `bindings_player_web.dart` + `js_extension.dart` (web), `flutter_soloud_ffigen.dart` (generated). |
| `lib/src/filters/`, `lib/src/vst3/` | Filter classes, VST3 hosting (desktop). |
| `src/` | Native code: `bindings.h/.cpp` (the C exports), `player.cpp`, `audiobuffer/` (buffer and pull streams, stream decoders), `native_decoder/` and `native_stream/` (OS decoders), `filters/`, `mixeroutput/`, `vst3/`. |
| `src/soloud/` | Vendored SoLoud. Local changes are marked `// ###### flutter_soloud local patch ... ######`. |
| `hook/build.dart` | Builds the native code (Dart build hooks). Options come from `hooks: user_defines:` in the app's pubspec (`no_xiph_libs`, `enable_vst3`, ...). |
| `web/` | WASM build scripts and the committed builds (single-threaded and `_mt` multi-threaded). |
| `test/` | Tests that need no app; start from `test/README.md`. |
| `example/` | Demo app, and `example/tests/` (tests that need the app's assets). |
| `skills/` | Agent skills shipped with the package, installed by `bin/skills.dart`. |

The user documentation is a separate repository, `flutter_soloud_docs`
(MDX for docs.page).

## Hard rules

- Never modify `miniaudio.h`, `dr_flac.h`, `dr_mp3.h` or `dr_wav.h` (under
  `src/soloud/src/`). They are external libraries: work around them in plugin
  code. The other SoLoud sources in `src/soloud/` may be changed; mark the
  change as a local patch.
- No C++ exception may leave an exported function, and exports validate their
  arguments (null pointers, NaN, ranges) and return a `PlayerErrors` code.
- The audio thread must not block: no lock that slow work also holds
  (decoding, file or device I/O), and no direct Dart calls. Dart callbacks go
  through `src/dart_callback_gate.h`.
- `SoLoud.instance` belongs to the isolate that called `init()`. Code meant
  for worker isolates goes in `SoLoudIsolate`, or checks only the native
  engine state.
- Files that import `dart:ffi` cannot compile for the web. Keep platform code
  behind the existing conditional imports (`*_ffi.dart` / `*_web.dart`).
- `flutter analyze` must report no issues (very_good_analysis) and the code
  must be formatted with `dart format`.

## Commands

```bash
flutter analyze && dart format lib test
flutter test                              # Dart tests on the VM (opens an audio device)
flutter test -x device                    # without an audio device
./test/support/web_tests.sh [--wasm]      # test/common in Chrome: ST build, or MT with --wasm
./test/common/<name>_test.sh [--asan|--tsan] [--strict]   # one native test
./test/macos/all.sh                       # or test/linux/all.sh: all native tests of the host
cmake -S test -B build/native_tests && cmake --build build/native_tests --parallel \
  && ctest --test-dir build/native_tests -j4 --output-on-failure
./test/docker/run.sh linux|windows|ffmpeg [what]   # Linux, Windows under Wine, FFmpeg releases
dart run ffigen --config ffigen.yaml      # regenerate the FFI bindings
(cd web && ./compile_wasm.sh)             # rebuild both WASM builds (needs Emscripten)
```

Run the tests that cover what you changed, and say what you ran and what you
did not. Android and iOS native tests need a device or simulator
(`test/android`, `test/ios`).

## When you change X, also update Y

| Change | Also do |
|---|---|
| An export in `src/bindings.h/.cpp` | Regenerate ffigen; update `bindings_player.dart`, `bindings_player_ffi.dart`, `bindings_player_web.dart` and `js_extension.dart`; rebuild the WASM. `ffigen_bindings_sync_test` and `web_bindings_sync_test` fail otherwise. |
| Any native source that the web build compiles | Rebuild with `web/compile_wasm.sh` and commit the four files in `web/` in their own commit (`update wasm libs`). |
| `PlayerErrors` or another enum in `src/enums.h` | The Dart enum in `lib/src/enums.dart` (same order and values) and the exception mapping in `lib/src/exceptions/`. |
| The public Dart API or its behaviour | The dartdoc, `CHANGELOG.md` (the entry of the next release, at the top; behaviour changes go under "Behaviour changes"), the matching skill in `skills/`, and the page in `flutter_soloud_docs`. |
| A skill (`SKILL.md` or its `references/`) | Increase its `version:` by 1, or installed copies never update. |
| A `test/common/*_test.sh`, added or changed | `python3 test/support/gen_cmake.py` (it regenerates `test/CMakeLists.txt`). |

## Bugs and tests

- A fix comes with a test that fails without it. Tests in `test/` do not use
  the example's assets as "any sound": they generate sounds with
  `test/support/sounds.dart`.
- A bug you find but do not fix goes in `test/KNOWN_BUGS.md` with a new ID,
  and the test that shows it carries that ID (`skip: 'bug <ID>: ...'` in
  Dart, `KNOWN BUG <ID>` in native tests). When you fix it, remove the marker
  and the row.
- Do not change a test to make it pass unless the test is wrong; say so when
  it is.
- If the right fix changes the public API or needs a design choice, stop and
  ask instead of guessing.

## Git

- Commit only when asked. Never push, and never open issues or pull requests
  unless explicitly requested.
- No AI attribution: no `Co-Authored-By` and no "Generated with" line. The
  author is the configured git user; do not write a name in the message.
- Messages are short, lowercase and specific to the fix, like the history
  (`git log --oneline -20`). One commit per bug or group of related bugs.
  When a commit fixes several things: a one-line subject, an empty line, then
  short `- ` lines.
- Test changes without a source change start with `tests:`; skill changes
  with `skills:`.

## More

- Tests, conventions and Docker images: `test/README.md`.
- Open bugs: `test/KNOWN_BUGS.md`.
- How the API is meant to be used, by area: `skills/*/SKILL.md`.

# flutter_soloud tests

Two kinds of tests live in this repo:

- **`test/`**: tests that need no audio assets. These are Dart tests run by
  `flutter test` on the host, plus standalone native (C/C++/Objective-C++)
  tests that compile parts of `src/` and run without the Flutter app.
- **`example/tests/`**: integration tests that run inside the example app,
  because they need the app's audio assets or run on web and mobile. See
  [Example app tests](#example-app-tests).

## Layout

| Folder | Contents |
|---|---|
| `common/` | Runs on any host (macOS, Linux, Windows). Dart tests, plus native tests that use the null backend and no OS framework. |
| `android/` | The Dart logic of the Android plugin; native tests built with the NDK and run over `adb`. |
| `ios/` | The iOS lifecycle handshake (Dart); native decoder tests run in the iOS simulator. |
| `macos/` | macOS-only native tests: CoreAudio, AudioToolbox, the VST3 host, Apple AAC. |
| `linux/` | Linux-only tests: audio backend selection, FFmpeg decoders, ALSA/PulseAudio. |
| `windows/` | Windows-only tests (Media Foundation, WASAPI, VST3), as PowerShell scripts. |
| `web/` | `@TestOn('browser')` Dart tests. |
| `support/` | Shared helpers: generated sounds, mixer capture, mocks, runners, the CMake generator, coverage. |

A Dart test in `android/`, `ios/` or `linux/` still runs on the host. Its
folder names the platform whose code path it covers.

## Conventions

- **Naming.** Every native test `<name>_test.cpp` (or `.mm`) has a script
  with the same base name: `<name>_test.sh`, or `<name>_test.ps1` on
  Windows. The script builds and runs the test, and lives in the folder of
  the platform it runs on. A source shared by several platforms lives in
  `common/`, and each platform's script points at it. For example,
  `android/native_stream_test.sh` builds `common/native_stream_test.cpp`.
- **Script header.** Each script starts with a comment that says:
  - what it verifies;
  - whether it needs an audio device or assets;
  - which flags it takes (`--asan`, `--tsan`, `--build-only`, …);
  - where it has been verified.
- **Exit codes.** A native test exits non-zero on failure. When a
  prerequisite is missing (no device, no library, no simulator), the test
  prints a line starting with `SKIPPED:` and exits 0.
- **Known bugs.** A test that finds a bug in `src/` or `lib/` does not hide
  it, and does not fail the suite either:
  - Dart tests mark the case `skip: 'bug: <what> (<file>:<line>)'`. Run
    `flutter test --run-skipped` to see these cases fail.
  - Native tests print `KNOWN BUG (...)`. Pass `--strict`, or set
    `OSDEC_STRICT=1` for the OS decoder tests, to make these count as
    failures.

  Every open bug has an ID (`E1`, `S4`, …), and the test that covers it
  uses the same ID. See `KNOWN_BUGS.md` for the list and for what to do
  when a bug gets fixed.
- **Dart tags** (configured in `dart_test.yaml`):
  - `device`: the test starts the real engine, which opens an audio output
    device.
  - `slow`: the test takes more than a few seconds.
- **Test sounds.** Tests in `test/` must not use the example's assets for
  "any sound". Use `support/sounds.dart` instead: `loadSine`,
  `loadTestSound` and `wavBytes` build a WAV in memory, and
  `loadTestWaveform` loads a waveform. `support/mixer_capture.dart` records
  the mixer output so a test can make assertions on what was actually heard.
  Native decoder tests may read `example/assets/audio/`, since decoding those
  formats is what they test.

## Running

### Dart

```bash
flutter test                                  # everything that runs on the VM
flutter test -x device                        # no audio device (CI)
flutter test test/common/voice_control_test.dart
flutter test --platform chrome test/web       # web, JS
flutter test --platform chrome --wasm test/web  # web, WASM
```

### Native: per-test scripts (macOS, Linux)

```bash
./test/common/limiter_test.sh [--asan|--tsan]
./test/macos/all.sh            # test/common + test/macos, with summary
./test/linux/all.sh            # test/common + test/linux
./test/android/all.sh          # needs adb + a device; scripts take --build-only
./test/ios/all.sh              # boots an iOS simulator
NATIVE_TEST_FILTER='decoder' ./test/macos/all.sh -- --asan
```

### Native: CMake / ctest (macOS, Linux, Windows)

`test/CMakeLists.txt` builds every `common/` test as a CMake target, so the
same suite also runs on Windows:

```bash
cmake -S test -B build/native_tests [-DSOLOUD_TEST_SANITIZE=address|thread]
cmake --build build/native_tests --parallel
ctest --test-dir build/native_tests -j4 --output-on-failure
```

On Windows, run `test\windows\all.ps1 [-Asan]`. It runs the ctest suite and
then the `test\windows\*_test.ps1` scripts.

The `common/` scripts are the reference for how each test is built, and
`test/CMakeLists.txt` is generated from them. After adding or changing a
`common/*_test.sh`, run:

```bash
python3 test/support/gen_cmake.py
```

The generator runs each script with fake compilers that only record their
arguments. It then writes one target per test executable, replacing the
Apple-only sources with the current platform's own sources.

### Coverage

```bash
./test/support/coverage.sh [-- -x device]
```

This writes `coverage/dart.info` (for `lib/`), `coverage/native.info` (for
`src/`, without the vendored SoLoud and pffft), `coverage/lcov.info` (both
merged) and, if `genhtml` is installed, an HTML report.

## Requirements

| What | Needed for |
|---|---|
| Flutter SDK | Dart tests |
| `brew install libogg libvorbis opus flac` (Linux: `libogg-dev libvorbis-dev libopus-dev libflac-dev`) | Xiph encoder/decoder tests. Without the libraries these tests are skipped. |
| An audio output device | Dart tests tagged `device`, `change_device_test` |
| Android SDK + NDK, `adb`, a device or emulator | `test/android/*` (`--build-only` needs only the NDK) |
| Xcode + an iOS simulator runtime | `test/ios/*` |
| Visual Studio / Build Tools (C++), CMake | `test/windows/*` |
| `afconvert` (macOS), `lame`, `ffmpeg` (optional) | Extra fixtures. Cases that need them are skipped when they are missing. |

## Design notes

- **Shared native sources.** A source used by several platforms is in
  `common/`, even when one platform runs it only through a script elsewhere:
  - `native_stream_test.cpp`: scripts in `common/` and `android/`;
  - `change_device_test.cpp`: scripts in `macos/`, `linux/` and `windows/`,
    because it needs an audio device;
  - `vst3_host_test.cpp`: scripts in `linux/` and `windows/`, plus the thin
    `macos/vst3_host_test.mm`.
- **Generated CMake.** The bash scripts in `common/` remain the reference for
  how a test is built, and `CMakeLists.txt` is generated from them, so the
  two cannot drift apart. Tests whose scripts chain several runs through
  temporary files (`simd_fallback`, `pull_mp3_seek`) run their script under
  ctest and are not run on Windows.
- **Device-free by default.** Native tests in `common/` use SoLoud's null
  backend. Tests that need an output device are the Dart tests tagged
  `device` and `change_device_test`.

## Example app tests

`example/tests/tests/*.dart` run inside the example app on every platform,
including web and mobile. They are listed in `example/tests/tests/all_tests.dart`.

```bash
cd example
flutter run -d macos -t tests/tests.dart                 # GUI runner
flutter run -d macos -t tests/run_tests.dart -a ALL      # headless, by name/index
flutter run -d macos -t tests/run_sequential.dart        # all tests in one engine session
./tests/run_all_tests.sh mt|st                           # web, via Chrome DevTools
```

Keep a test there only if it needs the app's assets or asset bundle, tests
the decoding of a specific file format, or covers web or mobile behaviour.
Anything else belongs in `test/common/` and uses `support/sounds.dart`.

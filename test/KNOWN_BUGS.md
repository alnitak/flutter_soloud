# Known bugs

Bugs found by the test suite that are not fixed yet. Each bug has an ID, and
that ID appears in the test that covers it. Run `grep -rn "<ID>" test` to
find the cases to re-enable once a bug is fixed.

How each kind of test marks a known bug:

- **Dart**: the case is marked `skip: 'bug <ID>: ...'`. Run it anyway with
  `flutter test --run-skipped <file>`.
- **Native**: a failing check prints `KNOWN BUG <ID> (...)` and does not fail
  the run. Pass `--strict` (or set `OSDEC_STRICT=1` for the OS decoder tests)
  to make these checks fail.

**When a bug is fixed:**

1. Remove the marker: delete the `skip:`, or turn the `KNOWN_BUG` check into
   a normal `CHECK`.
2. Check that the test passes, also with `--asan` where it applies.
3. Delete the bug's entry from this file.

Line numbers are approximate. IDs already used and retired: E1–E9, F1–F5,
O1–O3, S1–S9, K1–K2, P1–P9.

## Native parsers, decoders and files

| ID | Bug | Where | Test |
|---|---|---|---|

## Native concurrency, bindings and out-of-memory

| ID | Bug | Where | Test |
|---|---|---|---|

## Native DSP and engine

| ID | Bug | Where | Test |
|---|---|---|---|

## Dart API (part 1)

| ID | Bug | Where | Test |
|---|---|---|---|
| H1 | MT web build: custom audio source (and likely custom filter) looks up the JS DSP function on the AudioWorklet thread's globalThis, but the documented setup installs it on the page's; the source is silent. | `src/synth/custom_source.cpp:124` (same lookup `src/filters/custom_filter.cpp:106`) | `common/custom_audio_source_test` (5 audibility tests, --wasm only) |

## Dart API (part 2)

| ID | Bug | Where | Test |
|---|---|---|---|

## Android, iOS, macOS and web

| ID | Bug | Where | Test |
|---|---|---|---|
| L3 | Android streaming of AC-3 in MP4 wrong on device with AC-3 decoder: reads 93055/96000 frames, PCM after seek mismatches | native_stream_decoder_android.cpp read()/seek() (root cause unknown) | android/native_stream_decoder_android_test (S22) |

## Linux and FFmpeg

| ID | Bug | Where | Test |
|---|---|---|---|

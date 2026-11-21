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
| B2 | Data race on ActiveSound::handle: applyPauseState/play read without sounds_mutex while audio thread erases | player.cpp:1819,2688,2704-2706 vs 3131 | player_concurrency_test --tsan |
| B10 | Required pointer args dereferenced without null check (crash) | bindings.cpp many; player.cpp:1711; audiobuffer.cpp:583; pull_buffer_stream.cpp:809 | bindings_null_args_test |
| B11 | loadMem negative length -> SIGBUS | player.cpp:918,928 | bindings_null_args_test |
| B12 | fade*/oscillate*/schedulePause/scheduleStop return noError for handle 0 / made-up | bindings.cpp:3726+ | bindings_null_args_test |
| B13 | NaN/Inf floats accepted, reach float->int (UB) | bindings.cpp:3468,3489; soloud_core_basicops.cpp:359,477; soloud_wav.cpp:116; waveform.cpp:51; soloud.cpp:2793 | bindings_null_args_test |
| B14 | Filter param get/set on inactive filter / out-of-range attr return noError | filters.cpp:355,381; soloud_filter.cpp:165 | bindings_null_args_test |
| B15 | Enum/format ints unchecked; buffer streams accept 0/1000 channels; 0 ch div by zero | bindings.cpp:970,2586,3870; player.cpp:1223,1276,1407,4359; audiobuffer.cpp:402,417; analyzer.cpp:308 | bindings_null_args_test |
| B16 | setRealtimeStream no capacity limit (0xFFFFFFFF frames -> tens of GB) | realtime_ring_buffer.h init via realtime_stream.cpp:404 | bindings_null_args_test (flaky) |
| B17 | createBus() succeeds before init / after dispose | bindings.cpp:5051 | bindings_null_args_test |
| B18 | startMixerCapture negative bufferSizeBytes -> uncaught bad_alloc | bindings.cpp:971 | bindings_null_args_test |
| B19 | set3dSource* with made-up handle reads m3dData OOB before validating | soloud_internal.h:153-154 via bindings.cpp:4941 | bindings_null_args_test (UBSan) |
| B21 | bad_alloc escapes loadMem/loadWaveform exports -> abort via FFI | bindings.cpp:2516,2794; player.cpp:900 | out_of_memory_test bindings |
| B22 | same for play/play3d/createBus/busPlayOnEngine | bindings.cpp:2990,5051,5083 | out_of_memory_test |
| B23 | same for setBufferStream/addAudioDataStream | bindings.cpp:2586,2751 | out_of_memory_test |
| B26 | Player::loadWaveform leaves half-built entry on alloc failure | player.cpp:1739-1747 | out_of_memory_test bindings |

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
| J5 | At voice limit play() stops the oldest voice of same sound even if protected | player.cpp:2702-2706 (play3d 2832,2968) | max_voices_test |
| J12 | startMixerOutputStream(bufferSizeBytes<0) aborts with length_error | bindings.cpp:864 -> mixer_output.cpp:498 | invalid_arguments_test |
| J14 | 3D min=max distance=0 with inverse attenuation -> 0/0 NaN | soloud_core_3d.cpp:165 | invalid_arguments_test |
| J15 | negative setPullBufferStream bufferSizeBytes / setRealtimeStream sampleRate -> huge alloc (WASM abort) | pull_buffer_stream.cpp:489, realtime_stream.cpp:351 | invalid_arguments_test (web) |

## Android, iOS, macOS and web

| ID | Bug | Where | Test |
|---|---|---|---|
| L3 | Android streaming of AC-3 in MP4 wrong on device with AC-3 decoder: reads 93055/96000 frames, PCM after seek mismatches | native_stream_decoder_android.cpp read()/seek() (root cause unknown) | android/native_stream_decoder_android_test (S22) |

## Linux and FFmpeg

| ID | Bug | Where | Test |
|---|---|---|---|
| M4 | Without FFmpeg, AAC/M4A/AC-3 via loadMem/loadFile return generic fileLoadFailed (stream path audioFormatNotSupported) | soloud_wav.cpp:433 | linux/no_ffmpeg_test |

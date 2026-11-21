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
| B1 | NaN handling differs by build/position: clipRoundoff vector vs scalar tail, clipHard scalar only, peakAbs NEON vs scalar | simd_pcm.h:766,713,691; soloud_simd.h:403/418 | simd_pcm_test nanRound/nanHard/nanPeak |
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
| B20 | filter-instance alloc failure in play() leaves audio mutex locked -> deadlock | soloud_core_basicops.cpp:102 (mutex :123) | out_of_memory_test voices_filter |
| B21 | bad_alloc escapes loadMem/loadWaveform exports -> abort via FFI | bindings.cpp:2516,2794; player.cpp:900 | out_of_memory_test bindings |
| B22 | same for play/play3d/createBus/busPlayOnEngine | bindings.cpp:2990,5051,5083 | out_of_memory_test |
| B23 | same for setBufferStream/addAudioDataStream | bindings.cpp:2586,2751 | out_of_memory_test |
| B25 | WavStream::loadMem / WavStreamInstance ctor leak MemoryFile on alloc failure | soloud_wavstream.cpp:691-699,131,162,202 | out_of_memory_test wavstream |
| B26 | Player::loadWaveform leaves half-built entry on alloc failure | player.cpp:1739-1747 | out_of_memory_test bindings |

## Native DSP and engine

| ID | Bug | Where | Test |
|---|---|---|---|
| C1 | `Queue::play()` after the queue's voice ended returns success but is never heard: `findQueueHandle()` keeps the stale handle. | `soloud_queue.cpp:106,122-125` | `common/soloud_queue_test` (drain_restart) |
| C2 | A source destroyed while waiting in a Queue is still played: its instance reads the freed parent (use-after-free with Wav). | `soloud_queue.cpp:45`, `soloud_audiosource.cpp:228` | `common/soloud_queue_test` (destroyed) |
| C3 | `QueueInstance::getAudio` spins while a source returns 0 samples without ending (mixer hangs holding the audio mutex). | `soloud_queue.cpp:43-47` | `common/soloud_queue_test` (starving) |
| C4 | `FFTFilterInstance` passes 256 real samples to a complex FFT of 128 points: EQ not transparent; Bassboost boosts 22 kHz and images 172 Hz to 21.9 kHz (reachable from plugin). | `soloud_fftfilter.cpp:135`, `soloud_eqfilter.cpp:114`, `soloud_bassboostfilter.cpp:44` | `common/dc_removal_eq_filter_test` (eq_transparent, bassboost) |
| C5 | Vorbis encoder passes NaN to libvorbis (WAV/FLAC map NaN to 0): huge peaks. NaN passes NEON/scalar clip on ARM. | `vorbis_output_encoder.cpp:252-268` | `common/output_encoders_fuzz_test` (nan_in_tone) |
| C6 | `getStreamPosition()` keeps advancing while a voice is virtual; on return audio resumes where it stopped but position is ahead. | `soloud.cpp:3429` | `common/voice_virtualization_test` (resume_point) |
| C7 | `setProtectVoice()` does not set `mActiveVoiceDirty`: protecting a playing virtual voice has no effect until something else re-evaluates. | `soloud_core_setters.cpp:164-178` | `common/voice_virtualization_test` (protected) |

## Dart API (part 1)

| ID | Bug | Where | Test |
|---|---|---|---|
| H1 | MT web build: custom audio source (and likely custom filter) looks up the JS DSP function on the AudioWorklet thread's globalThis, but the documented setup installs it on the page's; the source is silent. | `src/synth/custom_source.cpp:124` (same lookup `src/filters/custom_filter.cpp:106`) | `common/custom_audio_source_test` (5 audibility tests, --wasm only) |
| H2 | `resetBufferStream` turns on ICY stripping (default metaint 16000) for streams that never asked for it: a plain MP3 stream loses a byte every 16000 and drops frames. | `src/audiobuffer/audiobuffer.cpp:434,469` | `common/buffer_stream_icy_test` ("a plain MP3 stream decodes fully after resetBufferStream") |

## Dart API (part 2)

| ID | Bug | Where | Test |
|---|---|---|---|
| J5 | At voice limit play() stops the oldest voice of same sound even if protected | player.cpp:2702-2706 (play3d 2832,2968) | max_voices_test |
| J7 | WAV capture header survives deinit/init | mixer_output.cpp:572,699 | mixer_output_wav_header_test |
| J12 | startMixerOutputStream(bufferSizeBytes<0) aborts with length_error | bindings.cpp:864 -> mixer_output.cpp:498 | invalid_arguments_test |
| J14 | 3D min=max distance=0 with inverse attenuation -> 0/0 NaN | soloud_core_3d.cpp:165 | invalid_arguments_test |
| J15 | negative setPullBufferStream bufferSizeBytes / setRealtimeStream sampleRate -> huge alloc (WASM abort) | pull_buffer_stream.cpp:489, realtime_stream.cpp:351 | invalid_arguments_test (web) |

## Android, iOS, macOS and web

| ID | Bug | Where | Test |
|---|---|---|---|
| L1 | Android full decode keeps AAC end padding for edit-list M4As (trims to extractor duration, ignores edit list) | os_decoder_android.cpp:472-481 | android/native_stream_decoder_android_test (API 34 emulator) |
| L3 | Android streaming of AC-3 in MP4 wrong on device with AC-3 decoder: reads 93055/96000 frames, PCM after seek mismatches | native_stream_decoder_android.cpp read()/seek() (root cause unknown) | android/native_stream_decoder_android_test (S22) |
| L4 | Long metadata strings cut mid UTF-8 char (strncpy 1023) | audiobuffer.cpp:994, pull_buffer_stream.cpp:1680 | common/web_realtime_stream_test |

## Linux and FFmpeg

| ID | Bug | Where | Test |
|---|---|---|---|
| M1 | Failed Linux backend switch (JACK) leaves engine with no device: state uninitialized, play() noError, no fallback | soloud_miniaudio.cpp:2304-2311 | linux/linux_backend_switch_test |
| M2 | PulseAudio server loss: state stays started, clock freezes; after restart startAudioDevice() no-op, only changeDevice recovers | soloud_miniaudio.cpp/Player (miniaudio.h off-limits) | linux/pulse_server_loss_test |
| M4 | Without FFmpeg, AAC/M4A/AC-3 via loadMem/loadFile return generic fileLoadFailed (stream path audioFormatNotSupported) | soloud_wav.cpp:433 | linux/no_ffmpeg_test |
| M5 | Linux VST3 editor WM_DELETE_WINDOW ignored: ClientMessage data.l[0] read from eventBuf[8], LP64 index 7 | vst3_editor_linux.cpp:857 | linux/vst3_editor_linux_test |
| M6 | FFmpeg 5.x (lavf 59) crash: read with lavf 60+ AVStream layout | os_decoder_linux.cpp:206 (>=59 -> >=60; lavf59 codecpar@208), :351 (lavf59 time_base@16, duration@32) | docker ffmpeg 5 |
| M7 | FFmpeg 4.4 (lavc 58) heap over-read: AVCodecParameters read at FFmpeg7 offsets 132/152 past 140-byte struct | os_decoder_linux.cpp:398-405 | docker ffmpeg 4 --asan |
| M8 | FFmpeg 4.4: truncated M4A decodes longer (no AVStream duration fallback for lavf 58) | os_decoder_linux.cpp:351 | docker ffmpeg 4 |

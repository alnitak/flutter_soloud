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
- **Failing as is**: the test fails and is excluded in CI through
  `KNOWN_FAILING_NATIVE` in `.github/workflows/tests.yaml`.

**When a bug is fixed:**

1. Remove the marker: delete the `skip:`, or turn the `KNOWN_BUG` check into
   a normal `CHECK`.
2. Check that the test passes, also with `--asan` where it applies.
3. Delete the bug's entry from this file.

Line numbers are approximate.

## Failing tests (were already failing on `main`)

| ID | Bug | Test |
|---|---|---|
| K1 | In a CBR MP3 without a Xing header, seeking to 6 s requests byte 96000, but the file map places it at 89025. | `common/pull_mp3_seek_test` (excluded in CI) |
| K2 | The edit-list trim of AC-3 in MP4 is off by 256 samples. | `common/native_stream_test` (`native_stream_test.cpp:1608`), with an ffmpeg fixture |

## Engine, 3D and Dart API

| ID | Bug | Where | Test |
|---|---|---|---|
| E1 | `play()` on a `speechText()` source segfaults: `textToSpeech` registers an `ActiveSound` without setting its `sound`. | `src/player.cpp` ~3384 | `common/speech_text_test.dart` |
| E2 | The 3D attenuation is applied twice, so the level follows the square of the model. | `soloud_core_3d.cpp:296,301` | `common/three_d_audio_test.dart` |
| E3 | The doppler shift has the wrong sign: an approaching source sounds lower. | `soloud_core_3d.cpp:144` | `common/three_d_audio_test.dart` |
| E4 | `set3dListenerUp` sets the "at" vector. | `src/player.cpp:4194` | `common/three_d_audio_test.dart` |
| E5 | 3D setters called before the first sound is loaded are lost. | `src/bindings.cpp:4862` and following | `common/three_d_audio_test.dart` |
| E6 | `createVoiceGroup()` before `init` returns 4294967295 instead of throwing. | `src/bindings.cpp:3657`, `lib/src/soloud.dart:4157` | `common/soloud_init_deinit_test.dart` |
| E7 | `deinit()` does not dispose the `Bus` objects, which stay valid on the next engine. | `lib/src/soloud.dart:1115` | `common/mixing_bus_test.dart` |
| E8 | `countAudioSource` always returns 0 for waveform sources. | `src/player.cpp:3568` | `common/voice_control_test.dart` |
| E9 | The message of `PlayerErrors.filterAlreadyAdded` is "Filter not found!". | `lib/src/enums.dart:168` | `common/exceptions_mapping_test.dart` |

## Filters

| ID | Bug | Where | Test |
|---|---|---|---|
| F1 | The Dart defaults don't match the native ones. Biquad resonance is 0.1 in Dart and 2 natively. The Flanger delay/freq defaults and ranges are wrong. The Limiter kneeWidth/releaseTime are 2/100 in Dart and 6/50 natively. The default band Q of the parametric EQ doesn't match. | `lib/src/filters/*.dart` | `common/filters_api_test.dart` |
| F2 | WaveShaper with a negative AMOUNT divides by zero and gives NaN at \|x\| = 1. | `soloud_waveshaperfilter.cpp` | `common/soloud_filters_test` (probe) |
| F3 | Biquad FREQUENCY is not clamped below Nyquist, so the output becomes Inf at 11025 Hz. | `soloud_biquadresonantfilter.cpp` | `common/soloud_filters_test` (probe) |
| F4 | PitchShift: new instances don't copy WET, and the SEMITONES range (±36) goes beyond the SHIFT range (max 3). | `src/filters/pitch_shift_filter.cpp:7-13` | `common/pitch_shift_filter_test` (probe) |
| F5 | Compressor: `getParamCount()` returns 8, but there are 7 parameters (a phantom "Wet"). | `src/filters/compressor.cpp` | `common/compressor_test` (prints the parameters) |

## Streaming decoders

| ID | Bug | Where | Test |
|---|---|---|---|
| S1 | FLAC and Ogg FLAC stay stuck in `ABORTED` after a read abort, and the drain never ends. | `flac_stream_decoder.cpp:246,273`, `ogg_flac_stream_decoder.cpp:211,239` | `common/flac_stream_decoder_test`, `common/stream_decoders_fuzz_test` |
| S2 | FLAC loses a frame that spans two `decode()` calls (`:302-308`). A first delivery of 1–3 bytes wedges the decoder (`:168`). | `flac_stream_decoder.cpp` | `common/flac_stream_decoder_test` |
| S3 | Ogg FLAC drops the last frame when the output is capped. | `ogg_flac_stream_decoder.cpp:282` | `common/ogg_flac_stream_decoder_test` |
| S4 | MP3s with an ID3 tag larger than 4 KB are detected as AC-3 when they arrive in pieces. | `stream_decoder.cpp:45` | `common/mp3_stream_decoder_test` |
| S5 | A failed lazy `drmp3_init()` loses data and can hang the drain. The WAV wrapper does the same with non-WAV data. | `mp3_stream_decoder.cpp:266` | `common/mp3_stream_decoder_test`, `common/stream_decoders_fuzz_test` |
| S6 | ID3 frames are not read when the tag has an extended header. | `mp3_stream_decoder.cpp:72` | `common/mp3_stream_decoder_test` |
| S7 | Short first deliveries are rejected instead of waiting for more data. | `stream_decoder.cpp:71,173` | `common/mp3_stream_decoder_test`, `common/ogg_flac_stream_decoder_test` |
| S8 | Vorbis: `OggMetadata.vendor` is always empty. | `vorbis_stream_decoder.cpp:80` | `common/metadata_api_test.dart` |
| S9 | `miniaudio_libvorbis`: the read callback returns `size_t` instead of `ma_result`, so `ma_libvorbis_init_memory` always fails. | `src/waveform/miniaudio_libvorbis.cpp:261-277,344` | `common/waveform_test` (probe, UBSan) |

## Platform decoders

| ID | Bug | Where | Test |
|---|---|---|---|
| P1 | Linux AAC loses most of the stream when the output is capped per call. | `aac_stream_decoder_linux.cpp:389` | `linux/os_decoder_linux_test` |
| P2 | Linux AAC: the prebuffer pass decodes the start twice and loses packets. | `aac_stream_decoder_linux.cpp:286` | `linux/os_decoder_linux_test` |
| P3 | Linux AAC: heap over-read, because the parser input lacks the FFmpeg padding. | `aac_stream_decoder_linux.cpp:294,322` | `linux/os_decoder_linux_test` (ASan) |
| P4 | Linux AAC: the codec loader is not thread-safe (`:72`). Linux `decodeMemory` uses a hard-coded `/tmp` and ignores `TMPDIR`. | `aac_stream_decoder_linux.cpp`, `os_decoder_linux.cpp` | found by reading the code |
| P5 | Apple AAC/AC-3: chunks smaller than about one frame lose audio. Linux has the same logic (`:267`). | `aac_stream_decoder_apple.mm:61` | `macos/aac_stream_decoder_apple_test` |
| P6 | Apple: the last AC-3/E-AC-3 frame never comes out after `setDataEnded()`. | `aac_stream_decoder_apple.mm:194` | `macos/aac_stream_decoder_apple_test` |
| P7 | Apple: HE-AAC ADTS streams play at 22.05 kHz, without SBR. | `aac_stream_decoder_apple.mm:234` | `macos/aac_stream_decoder_apple_test` |
| P8 | `AACDecoderWrapper::hasPendingData()` stays true forever when initialization failed. Windows has the same early return. | `aac_stream_decoder.h:24`, `aac_stream_decoder_windows.cpp:115` | `linux/os_decoder_linux_test` |
| P9 | (suspected) Windows: `CoInitializeEx` and `CoUninitialize` may run on different threads. | `aac_stream_decoder_windows.cpp:51,274` | `windows/os_decoder_windows_test` |

## Encoders and other

| ID | Bug | Where | Test |
|---|---|---|---|
| O1 | The Vorbis encoder doesn't reorder 5.1/7.1 channels: SoLoud's back-right channel lands in the LFE slot and is low-pass filtered. | `vorbis_output_encoder.cpp:242-246` | `common/vorbis_output_encoder_test` (`lfe_slot` records the current behaviour) |
| O2 | The WAV header is written in host byte order, and `dataSize` is truncated past 4 GB. The `mixer_output_encoder.h` comment says the output is "appended", but it is replaced. | `wav_output_encoder.cpp` | `common/wav_output_encoder_test` |
| O3 | TTS: signed overflow on a long run of digits (`:1315`). `isalpha` and similar receive a signed `char`. | `src/soloud/src/audiosource/speech/tts.cpp` | `common/speech_source_test` (UBSan) |

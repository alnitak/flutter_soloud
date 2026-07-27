import 'advanced_pan.dart' as advanced_pan;
import 'all_instances_finished.dart' as all_instances_finished;
import 'async_multi_load.dart' as async_multi_load;
import 'asynchronous_deinit.dart' as asynchronous_deinit;
import 'auto_dispose.dart' as auto_dispose;
import 'buffer_stream_callbacks.dart' as buffer_stream_callbacks;
import 'buffer_stream_extended.dart' as buffer_stream_extended;
import 'buffer_stream_small_mp3.dart' as buffer_stream_small_mp3;
import 'compressor_filter.dart' as compressor_filter;
import 'create_notes.dart' as create_notes;
import 'equalizer_filter.dart' as equalizer_filter;
import 'global_filters.dart' as global_filters;
import 'handles.dart' as handles;
import 'hot_restart_lifecycle.dart' as hot_restart_lifecycle;
import 'limiter_filter.dart' as limiter_filter;
import 'load_mem.dart' as load_mem;
import 'looping.dart' as looping;
import 'mixer_output_capture.dart' as mixer_output_capture;
import 'mixing_bus.dart' as mixing_bus;
import 'pan.dart' as pan;
import 'pitch_shifter_filter.dart' as pitch_shifter_filter;
import 'play_seek_pause.dart' as play_seek_pause;
import 'playback_devices.dart' as playback_devices;
import 'playback_speed.dart' as playback_speed;
import 'protect_voice.dart' as protect_voice;
import 'pull_buffer_file_stream_test.dart' as pull_buffer_file_stream;
import 'pull_buffer_range_test.dart' as pull_buffer_range;
import 'pull_buffer_seek_test.dart' as pull_buffer_seek;
import 'pull_buffer_test.dart' as pull_buffer;
import 'pull_buffer_tiny_test.dart' as pull_buffer_tiny;
import 'read_samples.dart' as read_samples;
import 'sound_filters.dart' as sound_filters;
import 'speech_text.dart' as speech_text;
import 'stop_futures.dart' as stop_futures;
import 'synchronous_deinit.dart' as synchronous_deinit;
import 'three_d_audio.dart' as three_d_audio;
import 'visualization.dart' as visualization;
import 'voice_groups.dart' as voice_groups;
import 'volume_controls.dart' as volume_controls;
import 'waveform_controls.dart' as waveform_controls;

/// A single test entry.
class TestEntry {
  const TestEntry({
    required this.name,
    required this.run,
  });

  final String name;
  final Future<StringBuffer> Function() run;
}

/// The list of all available tests.
///
/// Add new tests here to make them available in the test runner UI.
final List<TestEntry> allTests = [
  const TestEntry(
    name: '3dAudio',
    run: three_d_audio.test3dAudio,
  ),
  const TestEntry(
    name: 'AdvancedPan',
    run: advanced_pan.testAdvancedPan,
  ),
  const TestEntry(
    name: 'AllInstancesFinished',
    run: all_instances_finished.testAllInstancesFinished,
  ),
  const TestEntry(
    name: 'AsyncMultiLoad',
    run: async_multi_load.testAsyncMultiLoad,
  ),
  const TestEntry(
    name: 'AsynchronousDeinit',
    run: asynchronous_deinit.testAsynchronousDeinit,
  ),
  const TestEntry(
    name: 'AutoDispose',
    run: auto_dispose.testAutoDispose,
  ),
  const TestEntry(
    name: 'BufferStreamCallbacks',
    run: buffer_stream_callbacks.testBufferStreamCallbacks,
  ),
  const TestEntry(
    name: 'BufferStreamExtended',
    run: buffer_stream_extended.testBufferStreamExtended,
  ),
  const TestEntry(
    name: 'BufferStreamSmallMp3',
    run: buffer_stream_small_mp3.testBufferStreamSmallMp3,
  ),
  const TestEntry(
    name: 'CompressorFilterGlobal',
    run: compressor_filter.testCompressorFilterGlobal,
  ),
  const TestEntry(
    name: 'CompressorFilterSingle',
    run: compressor_filter.testCompressorFilterSingle,
  ),
  const TestEntry(
    name: 'CreateNotes',
    run: create_notes.testCreateNotes,
  ),
  const TestEntry(
    name: 'EqualizerFilterGlobal',
    run: equalizer_filter.testEqualizerFilterGlobal,
  ),
  const TestEntry(
    name: 'EqualizerFilterSingle',
    run: equalizer_filter.testEqualizerFilterSingle,
  ),
  const TestEntry(
    name: 'GlobalFilters',
    run: global_filters.testGlobalFilters,
  ),
  const TestEntry(
    name: 'Handles',
    run: handles.testHandles,
  ),
  const TestEntry(
    name: 'HotRestartLifecycle',
    run: hot_restart_lifecycle.testHotRestartLifecycle,
  ),
  const TestEntry(
    name: 'LimiterFilterGlobal',
    run: limiter_filter.testLimiterFilterGlobal,
  ),
  const TestEntry(
    name: 'LimiterFilterSingle',
    run: limiter_filter.testLimiterFilterSingle,
  ),
  const TestEntry(
    name: 'LoadMem',
    run: load_mem.testLoadMem,
  ),
  const TestEntry(
    name: 'LoopingTests',
    run: looping.loopingTests,
  ),
  const TestEntry(
    name: 'MixerOutputCapture',
    run: mixer_output_capture.testMixerOutputCapture,
  ),
  const TestEntry(
    name: 'MixingBus',
    run: mixing_bus.testMixingBus,
  ),
  const TestEntry(
    name: 'Pan',
    run: pan.testPan,
  ),
  const TestEntry(
    name: 'PitchShifterFilterGlobal',
    run: pitch_shifter_filter.testPitchShifterFilterGlobal,
  ),
  const TestEntry(
    name: 'PitchShifterFilterSingle',
    run: pitch_shifter_filter.testPitchShifterFilterSingle,
  ),
  const TestEntry(
    name: 'PlaySeekPause',
    run: play_seek_pause.testPlaySeekPause,
  ),
  const TestEntry(
    name: 'PullBuffer',
    run: pull_buffer.testPullBuffer,
  ),
  const TestEntry(
    name: 'PullBufferFileStream',
    run: pull_buffer_file_stream.testPullBufferFileStream,
  ),
  const TestEntry(
    name: 'PullBufferRange',
    run: pull_buffer_range.testPullBufferRange,
  ),
  const TestEntry(
    name: 'PullBufferSeek',
    run: pull_buffer_seek.testPullBufferSeek,
  ),
  const TestEntry(
    name: 'PullBufferTiny',
    run: pull_buffer_tiny.testPullBufferTiny,
  ),
  const TestEntry(
    name: 'PlaybackDevices',
    run: playback_devices.testPlaybackDevices,
  ),
  const TestEntry(
    name: 'PlaybackSpeed',
    run: playback_speed.testPlaybackSpeed,
  ),
  const TestEntry(
    name: 'ProtectVoice',
    run: protect_voice.testProtectVoice,
  ),
  const TestEntry(
    name: 'ReadSamples',
    run: read_samples.testReadSamples,
  ),
  const TestEntry(
    name: 'SoundFilters',
    run: sound_filters.testSoundFilters,
  ),
  const TestEntry(
    name: 'SpeechText',
    run: speech_text.testSpeechText,
  ),
  const TestEntry(
    name: 'StopFutures',
    run: stop_futures.testStopFutures,
  ),
  const TestEntry(
    name: 'SynchronousDeinit',
    run: synchronous_deinit.testSynchronousDeinit,
  ),
  const TestEntry(
    name: 'Visualization',
    run: visualization.testVisualization,
  ),
  const TestEntry(
    name: 'VoiceGroups',
    run: voice_groups.testVoiceGroups,
  ),
  const TestEntry(
    name: 'VolumeControls',
    run: volume_controls.testVolumeControls,
  ),
  const TestEntry(
    name: 'WaveformControls',
    run: waveform_controls.testWaveformControls,
  ),
];

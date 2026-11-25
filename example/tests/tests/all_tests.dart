import 'advanced_pan.dart' as advanced_pan;
import 'all_instances_finished.dart' as all_instances_finished;
import 'amplitude_modulator_filter.dart' as amplitude_modulator_filter;
import 'async_multi_load.dart' as async_multi_load;
import 'asynchronous_deinit.dart' as asynchronous_deinit;
import 'audio_device_idle_timeout.dart' as audio_device_idle_timeout;
import 'audio_device_lifecycle_races.dart' as audio_device_lifecycle_races;
import 'auto_dispose.dart' as auto_dispose;
import 'buffer_stream_small_mp3.dart' as buffer_stream_small_mp3;
import 'bus_visualization.dart' as bus_visualization;
import 'compressor_filter.dart' as compressor_filter;
import 'engine_time_across_sessions.dart' as engine_time_across_sessions;
import 'equalizer_filter.dart' as equalizer_filter;
import 'global_filters.dart' as global_filters;
import 'hardware_output_latency.dart' as hardware_output_latency;
import 'hot_restart_lifecycle.dart' as hot_restart_lifecycle;
import 'join_two_sources.dart' as join_two_sources;
import 'latency_test.dart' as latency_test;
import 'limiter_filter.dart' as limiter_filter;
import 'load_mem.dart' as load_mem;
import 'max_voices.dart' as max_voices;
import 'mixer_output_capture.dart' as mixer_output_capture;
import 'mixing_bus.dart' as mixing_bus;
import 'native_m4a_decoding.dart' as native_m4a_decoding;
import 'pitch_shifter_filter.dart' as pitch_shifter_filter;
import 'play_clocked.dart' as play_clocked;
import 'play_scheduled.dart' as play_scheduled;
import 'playback_devices.dart' as playback_devices;
import 'player_error_propagation.dart' as player_error_propagation;
import 'pull_buffer_duration_test.dart' as pull_buffer_duration;
import 'pull_buffer_file_stream_test.dart' as pull_buffer_file_stream;
import 'pull_buffer_in_buffer_seek_test.dart' as pull_buffer_in_buffer_seek;
import 'pull_buffer_range_test.dart' as pull_buffer_range;
import 'pull_buffer_seek_test.dart' as pull_buffer_seek;
import 'pull_buffer_test.dart' as pull_buffer;
import 'pull_buffer_tiny_test.dart' as pull_buffer_tiny;
import 'read_samples.dart' as read_samples;
import 'sound_filters.dart' as sound_filters;
import 'visualization.dart' as visualization;

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
    name: 'AudioDeviceIdleTimeout',
    run: audio_device_idle_timeout.testAudioDeviceIdleTimeout,
  ),
  const TestEntry(
    name: 'AudioDeviceLifecycleRaces',
    run: audio_device_lifecycle_races.testAudioDeviceLifecycleRaces,
  ),
  const TestEntry(
    name: 'AutoDispose',
    run: auto_dispose.testAutoDispose,
  ),
  const TestEntry(
    name: 'BufferStreamSmallMp3',
    run: buffer_stream_small_mp3.testBufferStreamSmallMp3,
  ),
  const TestEntry(
    name: 'BusVisualization',
    run: bus_visualization.testBusVisualization,
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
    name: 'SetGetGlobalFilters',
    run: global_filters.testSetGetGlobalFilters,
  ),
  const TestEntry(
    name: 'FadeParamsGlobalFilters',
    run: global_filters.testFadeParamsGlobalFilters,
  ),
  const TestEntry(
    name: 'HardwareOutputLatency',
    run: hardware_output_latency.testHardwareOutputLatency,
  ),
  const TestEntry(
    name: 'HotRestartLifecycle',
    run: hot_restart_lifecycle.testHotRestartLifecycle,
  ),
  const TestEntry(
    name: 'LatencyTest',
    run: latency_test.testLatency,
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
    name: 'JoinTwoSources',
    run: join_two_sources.testJoinTwoSources,
  ),
  const TestEntry(
    name: 'MaxVoices',
    run: max_voices.testMaxVoices,
  ),
  const TestEntry(
    name: 'MixingBus',
    run: mixing_bus.testMixingBus,
  ),
  const TestEntry(
    name: 'NativeM4aDecoding',
    run: native_m4a_decoding.testNativeM4aDecoding,
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
    name: 'PlayClocked',
    run: play_clocked.testPlayClocked,
  ),
  const TestEntry(
    name: 'PlayScheduled',
    run: play_scheduled.testPlayScheduled,
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
    name: 'PlayerErrorPropagation',
    run: player_error_propagation.testPlayerErrorPropagation,
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
    name: 'Visualization',
    run: visualization.testVisualization,
  ),
  TestEntry(
    name: 'PullBufferDurationOgg',
    run: () => pull_buffer_duration.testPullBufferDuration('sample-vorbis.ogg'),
  ),
  TestEntry(
    name: 'PullBufferDurationOpus',
    run: () => pull_buffer_duration.testPullBufferDuration('sample-OPUS.opus'),
  ),
  const TestEntry(
    name: 'PullBufferInBufferSeek',
    run: pull_buffer_in_buffer_seek.testInBufferSeek,
  ),
  const TestEntry(
    name: 'EngineTimeAcrossSessions',
    run: engine_time_across_sessions.testEngineTimeAcrossSessions,
  ),
  const TestEntry(
    name: 'MixerOutputCapture',
    run: mixer_output_capture.testMixerOutputCapture,
  ),
  const TestEntry(
    name: 'AmplitudeModulatorFilterGlobal',
    run: amplitude_modulator_filter.testAmplitudeModulatorFilterGlobal,
  ),
  const TestEntry(
    name: 'AmplitudeModulatorFilterSingle',
    run: amplitude_modulator_filter.testAmplitudeModulatorFilterSingle,
  ),
];

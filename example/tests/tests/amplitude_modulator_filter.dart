import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test the amplitude modulator as a global filter, including a spectral
/// check of the real engine output captured after the global filter stage.
Future<OutputBuffer> testAmplitudeModulatorFilterGlobal() async {
  final strBuf = OutputBuffer();
  // `initialize()` uses the default 44.1 kHz stereo engine.
  await initialize();
  const sampleRate = 44100.0;

  final filter = SoLoud.instance.filters.amplitudeModulatorFilter;
  assert(filter.queryWet.def == 1, 'wet default should be 1');
  assert(filter.queryFrequency.min == 0.1, 'frequency min should be 0.1 Hz');
  assert(filter.queryFrequency.max == 20000, 'frequency max should be 20 kHz');
  assert(filter.queryFrequency.def == 440, 'frequency default should be 440');

  filter.activate();
  assert(filter.isActive, 'Global amplitude modulator should be active');
  strBuf.writeln('Amplitude modulator global filter activated');

  assert(closeTo(filter.wet.value, 1, 1e-6), 'wet should start at 1');
  assert(
    closeTo(filter.frequency.value, 440, 1e-3),
    'frequency should start at 440 Hz',
  );

  // 1 kHz sine source.
  final sound = await SoLoud.instance.loadWaveform(WaveForm.sin, false, 1, 0);
  SoLoud.instance.setWaveformFreq(sound, 1000);
  SoLoud.instance.play(sound, looping: true);

  // Dry reference: wet = 0 must leave the 1 kHz tone alone.
  filter.wet.value = 0;
  filter.frequency.value = 16000;
  assert(closeTo(filter.frequency.value, 16000, 1e-3), 'frequency not set');
  await delay(300);
  final dry = await _captureChannel0(frames: 22050);
  final dryTones = _tones(dry, sampleRate);
  strBuf.writeln('wet=0 tones: $dryTones');
  assert(
    dryTones[15000]! < dryTones[1000]! * 0.01 &&
        dryTones[17000]! < dryTones[1000]! * 0.01,
    'wet=0 should not produce sidebands: $dryTones',
  );

  // Full wet: sin(1 kHz) * sin(16 kHz) = 0.5 cos(15 kHz) - 0.5 cos(17 kHz).
  filter.wet.value = 1;
  await delay(300);
  final wet = await _captureChannel0(frames: 22050);
  final wetTones = _tones(wet, sampleRate);
  strBuf.writeln('wet=1 tones: $wetTones');
  final a15 = wetTones[15000]!;
  final a17 = wetTones[17000]!;
  assert(a15 > 0 && (a15 - a17).abs() < 0.1 * a15, 'sidebands unequal');
  assert(
    wetTones[1000]! < a15 * 0.01,
    '1 kHz input should be suppressed: $wetTones',
  );
  assert(
    wetTones[16000]! < a15 * 0.01,
    'carrier should be suppressed (no DC bias): $wetTones',
  );
  strBuf.writeln('Spectral translation verified (15 kHz / 17 kHz)');

  // Out-of-range values are rejected by the Dart API.
  filter.frequency.value = 25000;
  filter.wet.value = 1.5;
  assert(closeTo(filter.frequency.value, 16000, 1e-3), 'bad freq accepted');
  assert(closeTo(filter.wet.value, 1, 1e-6), 'bad wet accepted');

  // Fade the frequency through SoLoud's fader.
  filter.frequency.fadeFilterParameter(
    to: 1000,
    time: const Duration(milliseconds: 300),
  );
  await delay(600);
  assert(
    closeTo(filter.frequency.value, 1000, 1e-2),
    'frequency fade did not reach 1000 Hz: ${filter.frequency.value}',
  );
  strBuf.writeln('Frequency fade verified');

  // Oscillate wet between 0.2 and 0.8.
  filter.wet.oscillateFilterParameter(
    from: 0.2,
    to: 0.8,
    time: const Duration(milliseconds: 400),
  );
  final samples = <double>[];
  for (var i = 0; i < 12; i++) {
    await delay(70);
    samples.add(filter.wet.value);
  }
  strBuf.writeln('wet oscillation samples: $samples');
  assert(
    samples.every((v) => v >= 0.2 - 1e-3 && v <= 0.8 + 1e-3),
    'wet oscillation out of range',
  );
  assert(
    samples.reduce(math.max) - samples.reduce(math.min) > 0.2,
    'wet does not oscillate',
  );
  filter.wet.value = 1;
  strBuf.writeln('Wet oscillation verified');

  filter.deactivate();
  assert(!filter.isActive, 'Global amplitude modulator should be inactive');
  await SoLoud.instance.disposeSource(sound);
  deinit();

  strBuf.writeln('Amplitude modulator global filter tests completed');
  return strBuf;
}

/// Test the amplitude modulator as a per-sound and as a mixing-bus filter.
Future<OutputBuffer> testAmplitudeModulatorFilterSingle() async {
  final strBuf = OutputBuffer();

  if (kIsWeb || kIsWasm) {
    return strBuf
      ..write('WARNING: Web does not support single sound filters.')
      ..writeln();
  }

  await initialize();

  final sound = await SoLoud.instance.loadWaveform(WaveForm.sin, false, 1, 0);
  SoLoud.instance.setWaveformFreq(sound, 1000);

  // Per-sound filter.
  final filter = sound.filters.amplitudeModulatorFilter..activate();
  assert(filter.isActive, 'Sound amplitude modulator should be active');
  final handle = SoLoud.instance.play(sound, looping: true);
  strBuf.writeln('Playing sound with amplitude modulator, handle: $handle');

  filter.frequency(soundHandle: handle).value = 16000;
  filter.wet(soundHandle: handle).value = 0.5;
  assert(
    closeTo(filter.frequency(soundHandle: handle).value, 16000, 1e-3),
    'sound frequency not set',
  );
  assert(
    closeTo(filter.wet(soundHandle: handle).value, 0.5, 1e-6),
    'sound wet not set',
  );

  filter
      .frequency(soundHandle: handle)
      .fadeFilterParameter(to: 200, time: const Duration(milliseconds: 300));
  await delay(600);
  assert(
    closeTo(filter.frequency(soundHandle: handle).value, 200, 1e-2),
    'sound frequency fade did not complete',
  );
  filter
      .wet(soundHandle: handle)
      .oscillateFilterParameter(
        from: 0,
        to: 1,
        time: const Duration(milliseconds: 300),
      );
  await delay(500);
  strBuf.writeln('Sound-level set/get/fade/oscillate verified');

  await SoLoud.instance.stop(handle);
  filter.deactivate();
  assert(!filter.isActive, 'Sound amplitude modulator should be inactive');

  // Mixing-bus filter.
  final bus = SoLoud.instance.createMixingBus(name: 'AM Bus');
  final busFilter = bus.filters.amplitudeModulatorFilter..activate();
  assert(busFilter.isActive, 'Bus amplitude modulator should be active');
  final busHandle = bus.playOnEngine();
  bus.play(sound, looping: true);

  busFilter.frequency(soundHandle: busHandle).value = 16000;
  assert(
    closeTo(busFilter.frequency(soundHandle: busHandle).value, 16000, 1e-3),
    'bus frequency not set',
  );
  busFilter
      .wet(soundHandle: busHandle)
      .fadeFilterParameter(to: 0.25, time: const Duration(milliseconds: 200));
  await delay(400);
  assert(
    closeTo(busFilter.wet(soundHandle: busHandle).value, 0.25, 1e-3),
    'bus wet fade did not complete',
  );
  strBuf.writeln('Bus-level set/get/fade verified');

  busFilter.deactivate();
  assert(!busFilter.isActive, 'Bus amplitude modulator should be inactive');
  bus.dispose();
  await SoLoud.instance.disposeSource(sound);
  deinit();

  strBuf.writeln('Amplitude modulator single/bus filter tests completed');
  return strBuf;
}

/// Captures the engine output as PCM F32 and returns [frames] frames of the
/// first channel, skipping the first chunk to avoid any transition.
Future<Float32List> _captureChannel0({required int frames}) async {
  const channels = 2;
  final bytes = BytesBuilder(copy: false);
  final done = Completer<void>();
  final subscription = SoLoud.instance.startMixerOutputStream().listen((c) {
    bytes.add(c);
    if (bytes.length >= (frames + 4096) * channels * 4 && !done.isCompleted) {
      done.complete();
    }
  });
  await done.future.timeout(const Duration(seconds: 5));
  SoLoud.instance.stopMixerOutputStream();
  await subscription.cancel();

  // Copy to get a 4-byte aligned buffer.
  final all = Uint8List.fromList(bytes.takeBytes()).buffer.asFloat32List();
  final out = Float32List(frames);
  for (var i = 0; i < frames; i++) {
    out[i] = all[(4096 + i) * channels];
  }
  return out;
}

/// Hann-windowed amplitudes of the tones of interest.
Map<int, double> _tones(Float32List x, double sampleRate) => {
  for (final f in const [1000, 15000, 16000, 17000])
    f: _toneAmplitude(x, f.toDouble(), sampleRate),
};

double _toneAmplitude(Float32List x, double freq, double sampleRate) {
  final n = x.length;
  var re = 0.0;
  var im = 0.0;
  var wsum = 0.0;
  for (var i = 0; i < n; i++) {
    final w = 0.5 - 0.5 * math.cos(2 * math.pi * i / n);
    final ph = 2 * math.pi * freq * i / sampleRate;
    re += w * x[i] * math.cos(ph);
    im -= w * x[i] * math.sin(ph);
    wsum += w;
  }
  return 2 * math.sqrt(re * re + im * im) / wsum;
}

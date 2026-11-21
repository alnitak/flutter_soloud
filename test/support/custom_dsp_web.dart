// Browser side of custom_dsp.dart.
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:math' as math;

const _name = 'soloudTestDsp';

/// Installs (once) a JavaScript DSP function named [_name] on `globalThis`
/// and returns its name, to pass as `webJsFunctionName`.
///
/// It writes a 110 Hz sine (amplitude 0.3) to every channel of the planar
/// buffer the engine hands it, and reports every sample as written.
String installTestDsp() {
  if (globalContext.has(_name)) return _name;
  globalContext[_name] =
      (
            JSNumber buffer,
            JSNumber samples,
            JSNumber bufferSize,
            JSNumber channels,
            JSNumber sampleRate,
            JSNumber streamTime,
            JSNumber streamPosition,
            JSAny? outHasEnded,
            JSAny? userData,
          ) {
            final heap =
                (globalContext['Module_soloud']! as JSObject)['HEAPF32']!
                    as JSFloat32Array;
            final data = heap.toDart;
            final base = buffer.toDartInt >> 2;
            final n = samples.toDartInt;
            final stride = bufferSize.toDartInt;
            final rate = sampleRate.toDartDouble;
            final start = streamPosition.toDartDouble;
            for (var i = 0; i < n; i++) {
              final t = start + i / rate;
              final v = 0.3 * math.sin(2 * math.pi * 110 * t);
              for (var c = 0; c < channels.toDartInt; c++) {
                data[base + c * stride + i] = v;
              }
            }
            return n.toJS;
          }
          .toJS;
  return _name;
}

/// Whether the source made with [installTestDsp] produces sound.
///
/// Not in the multi-threaded build (used when the page is cross-origin
/// isolated): see [testDspSilentReason].
bool get testDspIsAudible =>
    !(globalContext['crossOriginIsolated']?.dartify() == true);

/// Why [testDspIsAudible] is false, for a skip reason.
String get testDspSilentReason =>
    'JS DSP callbacks need the single-threaded WASM build: the '
    'multi-threaded build renders in an AudioWorklet, which does not see '
    "the page's globalThis (documented on createCustomAudioSource)";

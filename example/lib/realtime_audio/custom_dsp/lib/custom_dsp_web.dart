import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:math' as math;
import 'dart:typed_data';

import 'custom_dsp.dart';

@JS('globalThis')
external JSObject get _globalThis;

@JS('Module_soloud.HEAPF32')
external JSFloat32Array get _heapF32;

bool _webCallbacksRegistered = false;

void _ensureWebCallbacks() {
  if (_webCallbacksRegistered) return;
  _webCallbacksRegistered = true;

  // 1. Register procedural source callback onto JavaScript globalThis:
  _globalThis['customDspSynth'] = ((
    JSNumber bufferPtr,
    JSNumber samplesToRead,
    JSNumber bufferSize,
    JSNumber channels,
    JSNumber sampleRate,
    JSNumber streamTime,
    JSNumber streamPosition,
    JSNumber outHasEndedPtr,
    JSAny? userData,
  ) {
    final int count = samplesToRead.toDartInt;
    final int bufStride = bufferSize.toDartInt;
    final int chs = channels.toDartInt;
    final double sr =
        sampleRate.toDartDouble > 0 ? sampleRate.toDartDouble : 48000.0;
    final double t0 = streamPosition.toDartDouble;
    final int f32Offset = bufferPtr.toDartInt >> 2;
    final Float32List heap = _heapF32.toDart;

    for (int i = 0; i < count; i++) {
      final double t = t0 + (i / sr);
      final double lfoL = 1.0 + 0.0035 * math.sin(2.0 * math.pi * 0.22 * t);
      final double lfoR = 1.0 + 0.0035 * math.cos(2.0 * math.pi * 0.31 * t);

      final double sL = 0.18 * math.sin(2.0 * math.pi * 110.0 * t) +
          0.12 * math.sin(2.0 * math.pi * 164.81 * lfoL * t) +
          0.10 * math.sin(2.0 * math.pi * 196.00 * t) +
          0.08 * math.sin(2.0 * math.pi * 246.94 * lfoL * t) +
          0.05 * math.sin(2.0 * math.pi * 329.63 * t) +
          0.08 * math.sin(2.0 * math.pi * 55.0 * t);

      final double sR = 0.18 * math.sin(2.0 * math.pi * 110.2 * t) +
          0.12 * math.sin(2.0 * math.pi * 165.10 * t) +
          0.10 * math.sin(2.0 * math.pi * 196.30 * lfoR * t) +
          0.08 * math.sin(2.0 * math.pi * 247.30 * t) +
          0.05 * math.sin(2.0 * math.pi * 330.10 * lfoR * t) +
          0.08 * math.sin(2.0 * math.pi * 55.0 * t);

      heap[f32Offset + i] = sL;
      for (int ch = 1; ch < chs; ch++) {
        heap[f32Offset + ch * bufStride + i] = (ch & 1) != 0 ? sR : sL;
      }
    }
    return samplesToRead;
  }).toJS;

  // 2. Register custom saturation filter callback onto JavaScript globalThis:
  _globalThis['customDspFilter'] = ((
    JSNumber bufferPtr,
    JSNumber samples,
    JSNumber bufferSize,
    JSNumber channels,
    JSNumber sampleRate,
    JSNumber time,
    JSNumber paramsPtr,
    JSNumber maskLo,
    JSNumber maskHi,
    JSNumber numParams,
    JSAny? userData,
  ) {
    final int count = samples.toDartInt;
    final int bufStride = bufferSize.toDartInt;
    final int chs = channels.toDartInt;
    final int f32Offset = bufferPtr.toDartInt >> 2;
    final int pOffset = paramsPtr.toDartInt >> 2;
    final int nParams = numParams.toDartInt;
    final Float32List heap = _heapF32.toDart;

    final double drive =
        (nParams > 1 && heap[pOffset + 1] >= 1.0) ? heap[pOffset + 1] : 2.5;

    for (int ch = 0; ch < chs; ch++) {
      final int chStart = f32Offset + ch * bufStride;
      for (int i = 0; i < count; i++) {
        final double x = heap[chStart + i];
        // Hyperbolic tangent soft saturation:
        final double e2x = math.exp(2.0 * x * drive);
        final double y = (e2x - 1.0) / (e2x + 1.0);
        heap[chStart + i] = y;
      }
    }
  }).toJS;
}

/// Registers Web JS callbacks and returns their function names.
CustomDspCallbacks getPlatformDspCallbacks() {
  _ensureWebCallbacks();
  return const CustomDspCallbacks(
    sourceJsFunctionName: 'customDspSynth',
    filterJsFunctionName: 'customDspFilter',
  );
}

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
      final double sample = 0.5 * math.sin(2.0 * math.pi * 440.0 * t);

      for (int ch = 0; ch < chs; ch++) {
        heap[f32Offset + ch * bufStride + i] = sample;
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

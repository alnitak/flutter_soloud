#include "custom_dsp.h"
#include <math.h>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define CUSTOM_DSP_SIMD_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#define CUSTOM_DSP_SIMD_NEON 1
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define CUSTOM_DSP_SIMD_WASM 1
#endif

static inline float fastTanhApprox(float z) {
  if (z > 3.0f)
    return 1.0f;
  if (z < -3.0f)
    return -1.0f;
  const float z2 = z * z;
  return z * (27.0f + z2) / (27.0f + 9.0f * z2);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 ==============================================================================
  1. CUSTOM AUDIO SOURCE CALLBACK (SoLoudSourceDspCallback_t)
 ==============================================================================
  This function is called directly and synchronously by the low-level audio
 engine thread whenever the mixer needs a new block of audio samples for this
 voice.

  PARAMETERS:
  - aBuffer:
      Pointer to the float output buffer.
      CRITICAL: SoLoud uses DE-INTERLEAVED (planar) channel buffers:
        * Channel 0 (Left):  aBuffer + 0 * aBufferSize
        * Channel 1 (Right): aBuffer + 1 * aBufferSize
      Never write interleaved [L, R, L, R] samples!

  - aSamplesToRead:
      The number of audio frames requested for this render cycle (e.g. 128,
 256).

  - aBufferSize:
      The stride in samples between each channel in aBuffer.
      Always index channel 'ch' using: aBuffer + (ch * aBufferSize).

  - aChannels:
      The channel count configured for this source (e.g. 1 = mono, 2 = stereo).

  - aSamplerate:
      The sample rate in Hz (e.g. 44100.0 or 48000.0).

  - aStreamTime:
      Engine time in seconds since this sound instance was started.

  - aStreamPosition:
      Current playback position in seconds (advances with each sample rendered,
      and reflects seeks or pauses).

  - aOutHasEnded:
      Pointer to a flag uint32_t.
      If your audio has a finite duration, set *aOutHasEnded = 1 when finished.
      SoLoud will stop and retire the voice automatically.
      Set *aOutHasEnded = 0 for continuous/infinite generators.

  - aUserData:
      Optional pointer passed into createCustomAudioSource(userdataAddress:
 ...). Can hold stateful C/C++ struct pointers (e.g. synth state, wavetables).

  RETURN:
      The actual number of samples written to aBuffer (usually aSamplesToRead).
 ==============================================================================
*/
CUSTOM_DSP_EXPORT unsigned int myCustomSourceDspCallback(
    float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize,
    unsigned int aChannels, float aSamplerate, double aStreamTime,
    double aStreamPosition, uint32_t *aOutHasEnded, void *aUserData) {

  // Safety check: ensure buffer and channels are valid
  if (aBuffer == (float *)0 || aSamplesToRead == 0 || aChannels == 0) {
    return 0;
  }

  const double sr = aSamplerate > 0.0f ? (double)aSamplerate : 48000.0;
  const double invSr = 1.0 / sr;
  const double kTwoPi = 2.0 * M_PI;

  /*
   * LUSH STEREO AMBIENT PAD (A Minor 9th with Dual Chorus LFOs)
   *
   * Chord voicing:
   *   - A1 / A2 (55 Hz / 110 Hz) -> Deep warm root
   *   - E3      (164.81 Hz)       -> Perfect fifth
   *   - G3      (196.00 Hz)       -> Minor seventh
   *   - B3      (246.94 Hz)       -> Ninth (adds ethereal color)
   *   - E4      (329.63 Hz)       -> Octave shimmer
   *
   * Two independent slow LFOs (0.2 Hz and 0.35 Hz) slightly detune the partials
   * differently between the Left and Right channels, creating a wide, evolving
   * stereo field without any clipping.
   */
  for (unsigned int i = 0; i < aSamplesToRead; ++i) {
    const double t = aStreamPosition + (double)i * invSr;

    // Slow stereo detune modulators (LFOs)
    const double lfoL = 1.0 + 0.0035 * sin(kTwoPi * 0.22 * t);
    const double lfoR = 1.0 + 0.0035 * cos(kTwoPi * 0.31 * t);

    // Left channel mix: A minor 9th voicing with gentle soft harmonics
    const float sL =
        (float)(0.18 * sin(kTwoPi * 110.0 * t) +         // Root A2
                0.12 * sin(kTwoPi * 164.81 * lfoL * t) + // Fifth E3
                0.10 * sin(kTwoPi * 196.00 * t) +        // 7th G3
                0.08 * sin(kTwoPi * 246.94 * lfoL * t) + // 9th B3
                0.05 * sin(kTwoPi * 329.63 * t) +        // High E4
                0.08 * sin(kTwoPi * 55.0 * t)            // Sub A1 warmth
        );

    // Right channel mix: Slightly detuned frequencies for rich stereo spread
    const float sR = (float)(0.18 * sin(kTwoPi * 110.2 * t) + // Root A2 detuned
                             0.12 * sin(kTwoPi * 165.10 * t) + // Fifth E3
                             0.10 * sin(kTwoPi * 196.30 * lfoR * t) + // 7th G3
                             0.08 * sin(kTwoPi * 247.30 * t) +        // 9th B3
                             0.05 * sin(kTwoPi * 330.10 * lfoR * t) + // High E4
                             0.08 * sin(kTwoPi * 55.0 * t) // Sub A1 warmth
    );

    // Write to channel 0 (Left)
    aBuffer[i] = sL;

    // Write to remaining channels (Right on stereo, mirrored on surround)
    for (unsigned int ch = 1; ch < aChannels; ++ch) {
      aBuffer[ch * aBufferSize + i] = (ch & 1u) ? sR : sL;
    }
  }

  // Infinite ambient source: never mark as ended
  if (aOutHasEnded != (uint32_t *)0) {
    *aOutHasEnded = 0;
  }

  return aSamplesToRead;
}

/*
 ==============================================================================
  2. CUSTOM FILTER CALLBACK (SoLoudFilterDspCallback_t)
 ==============================================================================
  This function is called on the real-time audio thread to process or mutate
  the audio stream passing through a CustomFilter slot.

  PARAMETERS:
  - aBuffer:
      Pointer to the channel audio data. Modify this buffer directly IN-PLACE!
      Layout is de-interleaved:
        channel 'ch' sample 'i' = aBuffer[ch * aBufferSize + i]

  - aSamples:
      The number of audio samples to process in this block.

  - aBufferSize:
      Channel stride distance in samples.

  - aChannels:
      Number of audio channels passing through this filter.

  - aSamplerate:
      Audio sample rate in Hz.

  - aTime:
      Current engine time in seconds.

  - aParams:
      Array of float parameter values:
        * aParams[0] = "Wet" (0.0 = bypass, 1.0 = fully wet).
          Managed by SoLoud automatically.
        * aParams[1] = First user parameter (here: "Drive", 1.0 to 10.0).
        * aParams[2] = Second user parameter (e.g. "Asymmetry").
        * aParams[1..7] correspond to CustomFilterParamSpec in Flutter.

  - aParamChangedMask:
      Bitmask indicating which parameters changed value since the last call.
      (Bit 0 = Wet changed, Bit 1 = Param 1 changed, etc.). Useful for caching.

  - aParamCount:
      Number of valid parameter entries in aParams.

  - aUserData:
      Optional state pointer passed into slot.setNativeCallback(userdataAddress:
 ...).
 ==============================================================================
*/
CUSTOM_DSP_EXPORT void
myCustomFilterDspCallback(float *aBuffer, unsigned int aSamples,
                          unsigned int aBufferSize, unsigned int aChannels,
                          float aSamplerate, double aTime, const float *aParams,
                          uint64_t aParamChangedMask, unsigned int aParamCount,
                          void *aUserData) {

  if (aBuffer == (float *)0 || aSamples == 0 || aChannels == 0) {
    return;
  }

  // Read custom parameters:
  // aParams[1] is configured as 'Drive' (1.0 to 10.0, default 2.5)
  const float drive =
      (aParamCount > 1 && aParams[1] >= 1.0f) ? aParams[1] : 2.5f;

  // aParams[2] can be used as 'Asymmetry' / DC bias (default 0.0)
  const float asym = (aParamCount > 2) ? aParams[2] : 0.0f;

  // Soft-clipping normalization factor: keeps perceived volume stable as drive
  // increases
  const float dcOffset = fastTanhApprox(asym);
  const float norm = 1.0f / (drive > 0.35f ? fastTanhApprox(drive) : 0.35f);

  // In-place saturation waveshaping:
  // Tanh creates smooth tube-style odd-harmonic saturation. As Drive increases,
  // it softly rounds the peaks into a warm, pleasant analog overdrive.
  for (unsigned int ch = 0; ch < aChannels; ++ch) {
    float *dst = aBuffer + (size_t)ch * aBufferSize;
    unsigned int i = 0;
#if defined(CUSTOM_DSP_SIMD_SSE2)
    const __m128 vDrive = _mm_set1_ps(drive);
    const __m128 vAsym = _mm_set1_ps(asym);
    const __m128 vDc = _mm_set1_ps(dcOffset);
    const __m128 vNorm = _mm_set1_ps(norm);
    const __m128 vMinZ = _mm_set1_ps(-3.0f);
    const __m128 vMaxZ = _mm_set1_ps(3.0f);
    const __m128 v27 = _mm_set1_ps(27.0f);
    const __m128 v9 = _mm_set1_ps(9.0f);
    const __m128 vMinOut = _mm_set1_ps(-1.0f);
    const __m128 vMaxOut = _mm_set1_ps(1.0f);
    for (; i + 4 <= aSamples; i += 4) {
      const __m128 x = _mm_loadu_ps(dst + i);
      __m128 z = _mm_add_ps(_mm_mul_ps(x, vDrive), vAsym);
      z = _mm_max_ps(vMinZ, _mm_min_ps(vMaxZ, z));
      const __m128 z2 = _mm_mul_ps(z, z);
      const __m128 num = _mm_mul_ps(z, _mm_add_ps(v27, z2));
      const __m128 den = _mm_add_ps(v27, _mm_mul_ps(v9, z2));
      const __m128 t = _mm_div_ps(num, den);
      __m128 y = _mm_mul_ps(_mm_sub_ps(t, vDc), vNorm);
      y = _mm_max_ps(vMinOut, _mm_min_ps(vMaxOut, y));
      _mm_storeu_ps(dst + i, y);
    }
#elif defined(CUSTOM_DSP_SIMD_NEON)
    const float32x4_t vAsym = vdupq_n_f32(asym);
    const float32x4_t vDc = vdupq_n_f32(dcOffset);
    const float32x4_t vMinZ = vdupq_n_f32(-3.0f);
    const float32x4_t vMaxZ = vdupq_n_f32(3.0f);
    const float32x4_t v27 = vdupq_n_f32(27.0f);
    const float32x4_t vMinOut = vdupq_n_f32(-1.0f);
    const float32x4_t vMaxOut = vdupq_n_f32(1.0f);
    for (; i + 4 <= aSamples; i += 4) {
      const float32x4_t x = vld1q_f32(dst + i);
      float32x4_t z = vmlaq_n_f32(vAsym, x, drive);
      z = vmaxq_f32(vMinZ, vminq_f32(vMaxZ, z));
      const float32x4_t z2 = vmulq_f32(z, z);
      const float32x4_t num = vmulq_f32(z, vaddq_f32(v27, z2));
      const float32x4_t den = vmlaq_n_f32(v27, z2, 9.0f);
#if defined(__aarch64__) || defined(_M_ARM64)
      const float32x4_t t = vdivq_f32(num, den);
#else
      float32x4_t recip = vrecpeq_f32(den);
      recip = vmulq_f32(vrecpsq_f32(den, recip), recip);
      const float32x4_t t = vmulq_f32(num, recip);
#endif
      float32x4_t y = vmulq_n_f32(vsubq_f32(t, vDc), norm);
      y = vmaxq_f32(vMinOut, vminq_f32(vMaxOut, y));
      vst1q_f32(dst + i, y);
    }
#elif defined(CUSTOM_DSP_SIMD_WASM)
    const v128_t vDrive = wasm_f32x4_splat(drive);
    const v128_t vAsym = wasm_f32x4_splat(asym);
    const v128_t vDc = wasm_f32x4_splat(dcOffset);
    const v128_t vNorm = wasm_f32x4_splat(norm);
    const v128_t vMinZ = wasm_f32x4_splat(-3.0f);
    const v128_t vMaxZ = wasm_f32x4_splat(3.0f);
    const v128_t v27 = wasm_f32x4_splat(27.0f);
    const v128_t v9 = wasm_f32x4_splat(9.0f);
    const v128_t vMinOut = wasm_f32x4_splat(-1.0f);
    const v128_t vMaxOut = wasm_f32x4_splat(1.0f);
    for (; i + 4 <= aSamples; i += 4) {
      const v128_t x = wasm_v128_load(dst + i);
      v128_t z = wasm_f32x4_add(wasm_f32x4_mul(x, vDrive), vAsym);
      z = wasm_f32x4_max(vMinZ, wasm_f32x4_min(vMaxZ, z));
      const v128_t z2 = wasm_f32x4_mul(z, z);
      const v128_t num = wasm_f32x4_mul(z, wasm_f32x4_add(v27, z2));
      const v128_t den = wasm_f32x4_add(v27, wasm_f32x4_mul(v9, z2));
      const v128_t t = wasm_f32x4_div(num, den);
      v128_t y = wasm_f32x4_mul(wasm_f32x4_sub(t, vDc), vNorm);
      y = wasm_f32x4_max(vMinOut, wasm_f32x4_min(vMaxOut, y));
      wasm_v128_store(dst + i, y);
    }
#endif
    for (; i < aSamples; ++i) {
      const float x = dst[i];
      float y = (fastTanhApprox(x * drive + asym) - dcOffset) * norm;

      // Hard safety limiter (-1.0 to +1.0) to prevent DAC clipping
      if (y > 1.0f)
        y = 1.0f;
      if (y < -1.0f)
        y = -1.0f;

      dst[i] = y;
    }
  }
}

/*
 ==============================================================================
  3. CALLBACK ADDRESS GETTERS
 ==============================================================================
  These helper functions return the raw C function pointer address so that
  Dart FFI / ffigen can bind directly without manual string symbol lookup.
 ==============================================================================
*/
CUSTOM_DSP_EXPORT void *getCustomSourceCallbackAddress(void) {
  return (void *)&myCustomSourceDspCallback;
}

CUSTOM_DSP_EXPORT void *getCustomFilterCallbackAddress(void) {
  return (void *)&myCustomFilterDspCallback;
}

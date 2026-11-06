#include "custom_dsp.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 ==============================================================================
  1. CUSTOM AUDIO SOURCE CALLBACK (SoLoudSourceDspCallback_t)
 ==============================================================================
  Called directly and synchronously by the low-level audio engine thread
  whenever the mixer needs a new block of audio samples for this voice.

  BUFFER LAYOUT: PLANAR (DE-INTERLEAVED)
  ----------------------------------------------------------------------------
  SoLoud does NOT use interleaved samples [L0, R0, L1, R1, ...].
  Instead, it uses planar (de-interleaved) layout where each channel is a
  contiguous array separated by 'aBufferSize' elements:

    aBuffer + 0 * aBufferSize -> [ L0, L1, L2, L3, ... L(N-1) ]  (Channel 0)
    aBuffer + 1 * aBufferSize -> [ R0, R1, R2, R3, ... R(N-1) ]  (Channel 1)
    aBuffer + ch * aBufferSize -> Start of Channel 'ch'

  To write sample 'i' for channel 'ch':
    aBuffer[(size_t)ch * aBufferSize + i] = sampleValue;

  CALLBACK PARAMETERS:
  ----------------------------------------------------------------------------
  - aBuffer:
      Pointer to the float output buffer where generated audio must be written.
  - aSamplesToRead:
      The number of audio frames (sample points per channel) requested for this
      render cycle (e.g. 128, 256, 512). Never write past this frame count!
  - aBufferSize:
      The channel stride (in float elements) from the start of one channel to
      the next. Always index channel 'ch' using: (ch * aBufferSize).
      NOTE: aBufferSize may be greater than aSamplesToRead for alignment/headroom.
  - aChannels:
      Channel count configured for this source (e.g. 1 = Mono, 2 = Stereo).
  - aSamplerate:
      Audio sample rate in Hz (e.g. 44100.0, 48000.0). Used to calculate phase
      increments: phaseInc = (2.0 * M_PI * freq) / aSamplerate.
  - aStreamTime:
      Engine time in seconds since this sound instance was started.
  - aStreamPosition:
      Current playback position in seconds (resets to 0.0 on start or seek).
  - aOutHasEnded:
      Pointer to a uint32_t end-of-stream flag:
        * Set *aOutHasEnded = 0 for continuous / infinite synthesizers or loops.
        * Set *aOutHasEnded = 1 when finite playback is complete (voice stops).
  - aUserData:
      Optional pointer passed to createCustomAudioSource(userdataAddress: ...).
      Can hold custom C/C++ struct state (oscillators, envelopes, wavetables).

  RETURN:
      The actual number of frames written to aBuffer (usually aSamplesToRead).
 ==============================================================================
*/
static double s_sourcePhase = 0.0;

CUSTOM_DSP_EXPORT unsigned int myCustomSourceDspCallback(
    float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize,
    unsigned int aChannels, float aSamplerate, double aStreamTime,
    double aStreamPosition, uint32_t *aOutHasEnded, void *aUserData) {

  // Safety check: ensure buffer and channels are valid
  if (aBuffer == (float *)0 || aSamplesToRead == 0 || aChannels == 0) {
    return 0;
  }

  (void)aStreamTime;
  (void)aUserData;

  // Reset phase when sound starts / seeks back to 0
  if (aStreamPosition <= 0.0) {
    s_sourcePhase = 0.0;
  }

  const double sr = aSamplerate > 0.0f ? (double)aSamplerate : 48000.0;
  const double invSr = 1.0 / sr;
  const double kTwoPi = 2.0 * M_PI;
  const double freq = 440.0;    // Standard A4 reference pitch (440 Hz)
  const float amplitude = 0.5f; // Safe level to prevent clipping
  const double phaseInc = kTwoPi * freq * invSr;

  for (unsigned int i = 0; i < aSamplesToRead; ++i) {
    const float sample = (float)(amplitude * sin(s_sourcePhase));
    s_sourcePhase += phaseInc;
    if (s_sourcePhase >= kTwoPi) {
      s_sourcePhase -= kTwoPi;
    }

    // Write sample to each planar channel buffer
    for (unsigned int ch = 0; ch < aChannels; ++ch) {
      aBuffer[(size_t)ch * aBufferSize + i] = sample;
    }
  }

  // Continuous generator: never mark as ended
  if (aOutHasEnded != (uint32_t *)0) {
    *aOutHasEnded = 0;
  }

  return aSamplesToRead;
}

/*
 ==============================================================================
  2. CUSTOM FILTER CALLBACK (SoLoudFilterDspCallback_t)
 ==============================================================================
  Called directly on the real-time audio thread to process or mutate the audio
  stream passing through a CustomFilter slot IN-PLACE.

  BUFFER LAYOUT: PLANAR (DE-INTERLEAVED)
  ----------------------------------------------------------------------------
  The incoming audio in 'aBuffer' is already de-interleaved:
    Channel 0 sample 'i': aBuffer[0 * aBufferSize + i]
    Channel 1 sample 'i': aBuffer[1 * aBufferSize + i]
    Channel ch sample 'i': aBuffer[(size_t)ch * aBufferSize + i]
  You mutate the samples directly in this buffer.

  CALLBACK PARAMETERS:
  ----------------------------------------------------------------------------
  - aBuffer:
      Pointer to planar float audio data. Modify this buffer directly in-place.
  - aSamples:
      The number of audio frames to process in this block.
  - aBufferSize:
      Channel stride distance in float samples.
  - aChannels:
      Number of audio channels passing through this filter (e.g. 1, 2, 6, 8).
  - aSamplerate:
      Audio sample rate in Hz.
  - aTime:
      Current engine time in seconds.
  - aParams:
      Array of float parameter values:
        * aParams[0] = "Wet" factor (0.0 = full bypass, 1.0 = fully wet).
          Note: SoLoud automatically blends dry/wet when wet is between 0 and 1.
        * aParams[1] = First user parameter (here: Pitch Shift Ratio).
        * aParams[2..N] = Additional parameters defined via CustomFilterParamSpec.
  - aParamChangedMask:
      Bitmask indicating which parameters changed since the last invocation:
        * Bit 0: Wet changed
        * Bit 1: Param 1 changed, etc.
      Useful for avoiding recalculation of expensive coefficients every block.
  - aParamCount:
      Number of valid parameter entries in aParams.
  - aUserData:
      Optional state pointer passed into slot.setNativeCallback(userdataAddress: ...).
 ==============================================================================
*/
#define PITCH_MAX_CHANNELS 8
#define PITCH_BUFFER_SIZE 4096
#define PITCH_BUFFER_MASK (PITCH_BUFFER_SIZE - 1)
#define PITCH_WINDOW_SIZE 2048.0f

typedef struct {
  float delayBuffer[PITCH_MAX_CHANNELS][PITCH_BUFFER_SIZE];
  unsigned int writeIndex;
  float phase;
} PitchFilterState;

static PitchFilterState s_defaultPitchState;

CUSTOM_DSP_EXPORT void
myCustomFilterDspCallback(float *aBuffer, unsigned int aSamples,
                          unsigned int aBufferSize, unsigned int aChannels,
                          float aSamplerate, double aTime, const float *aParams,
                          uint64_t aParamChangedMask, unsigned int aParamCount,
                          void *aUserData) {

  if (aBuffer == (float *)0 || aSamples == 0 || aChannels == 0) {
    return;
  }

  (void)aSamplerate;
  (void)aTime;
  (void)aParamChangedMask;

  // Read pitch shift parameter:
  // aParams[0] = Wet (0.0 to 1.0, handled by SoLoud)
  // aParams[1] = Pitch shift ratio (e.g. 0.5 = 1 octave down, 1.0 = normal, 1.5 = fifth up, 2.0 = 1 octave up)
  float shift = 1.5f;
  if (aParamCount > 1 && aParams[1] > 0.0f) {
    shift = aParams[1];
  }
  // Clamp to safe range [0.25, 4.0] (-2 octaves to +2 octaves)
  if (shift < 0.25f) shift = 0.25f;
  if (shift > 4.0f) shift = 4.0f;

  PitchFilterState *state = (aUserData != (void *)0)
      ? (PitchFilterState *)aUserData
      : &s_defaultPitchState;

  const unsigned int channels =
      aChannels > PITCH_MAX_CHANNELS ? PITCH_MAX_CHANNELS : aChannels;
  const float windowSize = PITCH_WINDOW_SIZE;
  const float twoPi = 2.0f * (float)M_PI;

  for (unsigned int i = 0; i < aSamples; ++i) {
    // 1. Write incoming channel samples into circular buffer
    for (unsigned int ch = 0; ch < channels; ++ch) {
      state->delayBuffer[ch][state->writeIndex] =
          aBuffer[(size_t)ch * aBufferSize + i];
    }

    // 2. Calculate tap delays and smooth Hann window weights
    const float p1 = state->phase;
    float p2 = p1 + 0.5f;
    if (p2 >= 1.0f) {
      p2 -= 1.0f;
    }

    const float d1 = p1 * windowSize;
    const float d2 = p2 * windowSize;

    // Hann window weights: w1 + w2 == 1.0 everywhere
    const float w1 = 0.5f * (1.0f - cosf(twoPi * p1));
    const float w2 = 1.0f - w1;

    // Tap 1 read position
    float r1 = (float)state->writeIndex - d1;
    while (r1 < 0.0f) r1 += (float)PITCH_BUFFER_SIZE;
    while (r1 >= (float)PITCH_BUFFER_SIZE) r1 -= (float)PITCH_BUFFER_SIZE;

    // Tap 2 read position
    float r2 = (float)state->writeIndex - d2;
    while (r2 < 0.0f) r2 += (float)PITCH_BUFFER_SIZE;
    while (r2 >= (float)PITCH_BUFFER_SIZE) r2 -= (float)PITCH_BUFFER_SIZE;

    const int i1_0 = (int)r1;
    const float frac1 = r1 - (float)i1_0;
    const int i1_1 = (i1_0 + 1) & PITCH_BUFFER_MASK;

    const int i2_0 = (int)r2;
    const float frac2 = r2 - (float)i2_0;
    const int i2_1 = (i2_0 + 1) & PITCH_BUFFER_MASK;

    // 3. Interpolate and blend both taps for each channel
    for (unsigned int ch = 0; ch < channels; ++ch) {
      const float *buf = state->delayBuffer[ch];
      const float s1 = (1.0f - frac1) * buf[i1_0 & PITCH_BUFFER_MASK] +
                       frac1 * buf[i1_1];
      const float s2 = (1.0f - frac2) * buf[i2_0 & PITCH_BUFFER_MASK] +
                       frac2 * buf[i2_1];
      aBuffer[(size_t)ch * aBufferSize + i] = w1 * s1 + w2 * s2;
    }

    // 4. Advance phase based on pitch shift ratio
    state->phase += (1.0f - shift) / windowSize;
    while (state->phase >= 1.0f) state->phase -= 1.0f;
    while (state->phase < 0.0f) state->phase += 1.0f;

    // 5. Advance write index
    state->writeIndex = (state->writeIndex + 1) & PITCH_BUFFER_MASK;
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

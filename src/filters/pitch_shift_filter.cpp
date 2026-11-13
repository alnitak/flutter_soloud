#include "pitch_shift_filter.h"
#include "../simd/soloud_simd.h"

#include <cmath>
#include <vector>

PitchShiftInstance::PitchShiftInstance(PitchShift *aParent) {
  mParent = aParent;
  initParams(3);
  mParam[PitchShift::SHIFT] = aParent->mShift;
  mParam[PitchShift::SEMITONES] = aParent->mSemitones;
  mOutputBuffer.resize(SAMPLE_GRANULARITY * MAX_CHANNELS, 0.0f);
}

void PitchShiftInstance::filter(float *aBuffer, unsigned int aSamples,
                                unsigned int aBufferSize,
                                unsigned int aChannels, float aSamplerate,
                                SoLoud::time aTime) {
  soloud_simd::ScopedFlushDenormals flushDenormals;
  updateParams(aTime);

  const float wetRatio = mParam[PitchShift::WET];
  if (wetRatio <= 0.0f || aChannels == 0 || aSamples == 0) {
    return;
  }

  const unsigned int channels =
      aChannels > MAX_CHANNELS ? MAX_CHANNELS : aChannels;

  // Reconfigure the stretcher if sample rate or channel count changes
  if (mChannelCount != channels || mSampleRate != aSamplerate) {
    mChannelCount = channels;
    mSampleRate = aSamplerate;
    mLastShift = -1.0f;
    mStretch.presetDefault(static_cast<int>(channels), aSamplerate);
  }

  // Set the pitch shift factor only when it changes
  const float shift = mParam[PitchShift::SHIFT];
  if (shift != mLastShift) {
    mLastShift = shift;
    mStretch.setTransposeFactor(shift);
  }

  const float dryRatio = 1.0f - wetRatio;

  // The output buffer is sized once in the constructor
  // (SAMPLE_GRANULARITY frames per channel). Larger blocks (global or bus
  // filters on a large quantum, the post-ring path) are fed to the
  // stretcher in chunks rather than growing the buffer on the audio thread;
  // the stretcher is a streaming processor, so this does not change the
  // result.
  const unsigned int chunkMax = SAMPLE_GRANULARITY;
  for (unsigned int base = 0; base < aSamples; base += chunkMax) {
    const unsigned int count =
        aSamples - base < chunkMax ? aSamples - base : chunkMax;

    for (unsigned int ch = 0; ch < channels; ch++) {
      mInputChannels[ch] =
          aBuffer + static_cast<size_t>(ch) * aBufferSize + base;
      mOutputChannels[ch] =
          mOutputBuffer.data() + static_cast<size_t>(ch) * chunkMax;
    }

    // Process all channels together
    mStretch.process(mInputChannels, static_cast<int>(count),
                     mOutputChannels, static_cast<int>(count));

    // Apply wet/dry mix and copy back to buffer
    for (unsigned int ch = 0; ch < channels; ch++) {
      float *channelIn = mInputChannels[ch];
      float *channelOut = mOutputChannels[ch];

      if (wetRatio < 1.0f) {
        unsigned int j = 0;
#if defined(SOLOUD_SIMD_ENABLED)
        const soloud_simd::f32x4 vDry = soloud_simd::f32x4::splat(dryRatio);
        const soloud_simd::f32x4 vWet = soloud_simd::f32x4::splat(wetRatio);
        for (; j + 4 <= count; j += 4) {
          const soloud_simd::f32x4 inV =
              soloud_simd::f32x4::loadu(channelIn + j);
          const soloud_simd::f32x4 outV =
              soloud_simd::f32x4::loadu(channelOut + j);
          soloud_simd::fma(outV, vWet, inV * vDry).storeu(channelIn + j);
        }
#endif
        for (; j < count; j++) {
          channelIn[j] = channelIn[j] * dryRatio + channelOut[j] * wetRatio;
        }
      } else {
        std::memcpy(channelIn, channelOut, count * sizeof(float));
      }
    }
  }
}

void PitchShiftInstance::setFilterParameter(unsigned int aAttributeId,
                                            float aValue) {
  if (aAttributeId >= mNumParams)
    return;

  mParamFader[aAttributeId].mActive = 0;

  switch (aAttributeId) {
  case PitchShift::WET:
    if (aValue < 0.f || aValue > 1.f)
      break;
    mParam[PitchShift::WET] = aValue;
    break;
  case PitchShift::SHIFT:
    if (aValue < mParent->getParamMin(PitchShift::SHIFT) ||
        aValue > mParent->getParamMax(PitchShift::SHIFT))
      return;
    mParam[PitchShift::SHIFT] = aValue;
    mParam[PitchShift::SEMITONES] = 12 * log2f(aValue);
    break;
  case PitchShift::SEMITONES:
    if (aValue < mParent->getParamMin(PitchShift::SEMITONES) ||
        aValue > mParent->getParamMax(PitchShift::SEMITONES))
      return;
    mParam[PitchShift::SEMITONES] = aValue;
    mParam[PitchShift::SHIFT] = pow(2., aValue / 12.);
    break;
  }

  mParamChanged |= 1ULL << (aAttributeId < 63 ? aAttributeId : 63);
}

SoLoud::result PitchShift::setParam(unsigned int aParamIndex, float aValue) {
  switch (aParamIndex) {
  case WET:
    if (aValue < 0.f || aValue > 1.f)
      break;
    mWet = aValue;
    break;
  case SHIFT:
    if (aValue < getParamMin(SHIFT) || aValue > getParamMax(SHIFT))
      return SoLoud::INVALID_PARAMETER;
    mShift = aValue;
    mSemitones = 12 * log2f(mShift);
    break;
  case SEMITONES:
    if (aValue < getParamMin(SEMITONES) || aValue > getParamMax(SEMITONES))
      return SoLoud::INVALID_PARAMETER;
    mSemitones = aValue;
    mShift = pow(2., mSemitones / 12.);
    break;
  }
  return SoLoud::SO_NO_ERROR;
}

int PitchShift::getParamCount() { return 3; }

const char *PitchShift::getParamName(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return "Wet";
  case SHIFT:
    return "Shift";
  case SEMITONES:
    return "Semitones";
  }
  return "Wet";
}

unsigned int PitchShift::getParamType(unsigned int aParamIndex) {
  return FLOAT_PARAM;
}

float PitchShift::getParamMax(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return 1.f;
  case SHIFT:
    return 3.f;
  case SEMITONES:
    return 36.f;
  }
  return 1;
}

float PitchShift::getParamMin(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return 0.f;
  case SHIFT:
    return 0.1f;
  case SEMITONES:
    return -36.f;
  }
  return 1;
}

PitchShift::PitchShift() {
  mWet = 1.0f;
  mShift = 1.0f;
  mSemitones = 0.0f;
}

SoLoud::FilterInstance *PitchShift::createInstance() {
  return new PitchShiftInstance(this);
}

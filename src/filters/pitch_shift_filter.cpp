#include <cmath>

#include "pitch_shift_filter.h"
#include <common.h>

#include <algorithm>
#include <vector>

PitchShiftInstance::PitchShiftInstance(PitchShift *aParent) {
  mParent = aParent;
  initParams(3);
  mParam[PitchShift::SHIFT] = aParent->mShift;
  mParam[PitchShift::SEMITONES] = aParent->mSemitones;
}

void PitchShiftInstance::filter(float *aBuffer, unsigned int aSamples,
                                unsigned int aBufferSize,
                                unsigned int aChannels, float aSamplerate,
                                SoLoud::time aTime) {

  updateParams(aTime);

  // Allocate per-channel pitch shifters on first call or if channel count
  // changes
  if (mChannelCount != aChannels) {
    mChannelCount = aChannels;
    mPitchShifters.clear();
    mPitchShifters.resize(aChannels);
  }

  const float dryRatio = 1.0f - mParam[PitchShift::WET];
  const float wetRatio = mParam[PitchShift::WET];

  // Process each channel independently to preserve stereo separation
  for (unsigned int ch = 0; ch < aChannels; ch++) {
    float *channelData = aBuffer + ch * aSamples;

    if (wetRatio < 1.0f) {
      // Need to save original samples for wet/dry mixing
      std::vector<float> original(channelData, channelData + aSamples);

      // Use osamp=32 for best quality (recommended by smbPitchShift
      // documentation)
      mPitchShifters[ch].smbPitchShift(mParam[PitchShift::SHIFT], aSamples,
                                       2048, 32, aSamplerate, channelData,
                                       channelData);

      // Apply wet/dry mix
      for (unsigned int j = 0; j < aSamples; j++) {
        channelData[j] = original[j] * dryRatio + channelData[j] * wetRatio;
      }
    } else {
      // Full wet - no need for temp buffer
      mPitchShifters[ch].smbPitchShift(mParam[PitchShift::SHIFT], aSamples,
                                       2048, 32, aSamplerate, channelData,
                                       channelData);
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

  mParamChanged |= 1 << aAttributeId;
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

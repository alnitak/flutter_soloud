#include <cmath>

#include "pitch_shift_filter.h"
#include <common.h>

#include <algorithm>
#include <vector>

PitchShiftInstance::PitchShiftInstance(PitchShift *aParent) {
  mParent = aParent;
  pitchShift = CSmbPitchShift();
  initParams(3);
  mParam[PitchShift::SHIFT] = aParent->mShift;
  mParam[PitchShift::SEMITONES] = aParent->mSemitones;

  mMonoBuffer.resize(kMaxFrameLength, 0.0f);
}

void PitchShiftInstance::filter(float *aBuffer, unsigned int aSamples,
                                unsigned int aBufferSize,
                                unsigned int aChannels, float aSamplerate,
                                SoLoud::time aTime) {

  updateParams(aTime);

  if (aSamples > mMonoBuffer.size()) {
    mMonoBuffer.resize(aSamples);
  }

  std::fill_n(mMonoBuffer.begin(), aSamples, 0.0f);

  const float invChannels = 1.0f / static_cast<float>(aChannels);
  for (unsigned int j = 0; j < aSamples; j++) {
    float mixedSample = 0.0f;
    const unsigned int baseIndex = j;
    for (unsigned int n = 0; n < aChannels; n++) {
      mixedSample += aBuffer[baseIndex + aSamples * n];
    }
    mMonoBuffer[j] = mixedSample * invChannels;
  }

  pitchShift.smbPitchShift(mParam[PitchShift::SHIFT], aSamples, 2048, 32,
                           aSamplerate, mMonoBuffer.data(),
                           mMonoBuffer.data());

  const float dryRatio = 1.0f - mParam[PitchShift::WET];
  const float wetRatio = mParam[PitchShift::WET];

  for (unsigned int j = 0; j < aSamples; j++) {
    const float shifted = mMonoBuffer[j];
    aBuffer[j] = aBuffer[j] * dryRatio + shifted * wetRatio; // L chan
    for (unsigned int n = 1; n < aChannels; n++)
      aBuffer[j + aSamples] =
          aBuffer[j + aSamples] * dryRatio + shifted * wetRatio; // R chan
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

#include "pitch_shift_filter.h"

#include <cmath>
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

  // Reconfigure the stretcher if sample rate or channel count changes
  if (mChannelCount != aChannels || mSampleRate != aSamplerate) {
    mChannelCount = aChannels;
    mSampleRate = aSamplerate;
    mStretch.presetDefault(static_cast<int>(aChannels), aSamplerate);
  }

  // Set the pitch shift factor
  mStretch.setTransposeFactor(mParam[PitchShift::SHIFT]);

  const float dryRatio = 1.0f - mParam[PitchShift::WET];
  const float wetRatio = mParam[PitchShift::WET];

  // SignalSmith expects channel-separated buffers (float**)
  // SoLoud provides data where each channel's samples are contiguous.
  // I'd prefer using this way instead of using per channel stretcher
  // with SoLoud::FilterInstance::filterChannels because SignalSmith
  // is optimized for multichannels.
  std::vector<float *> inputChannels(aChannels);
  std::vector<float *> outputChannels(aChannels);
  std::vector<float> outputBuffer(aSamples * aChannels);

  for (unsigned int ch = 0; ch < aChannels; ch++) {
    inputChannels[ch] = aBuffer + ch * aSamples;
    outputChannels[ch] = outputBuffer.data() + ch * aSamples;
  }

  // Process all channels together
  mStretch.process(inputChannels.data(), static_cast<int>(aSamples),
                   outputChannels.data(), static_cast<int>(aSamples));

  // Apply wet/dry mix and copy back to buffer
  for (unsigned int ch = 0; ch < aChannels; ch++) {
    float *channelIn = aBuffer + ch * aSamples;
    float *channelOut = outputChannels[ch];

    if (wetRatio < 1.0f) {
      for (unsigned int j = 0; j < aSamples; j++) {
        channelIn[j] = channelIn[j] * dryRatio + channelOut[j] * wetRatio;
      }
    } else {
      std::memcpy(channelIn, channelOut, aSamples * sizeof(float));
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

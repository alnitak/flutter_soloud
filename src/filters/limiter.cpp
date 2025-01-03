#include "limiter.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <stdio.h>

LimiterInstance::LimiterInstance(Limiter *aParent)
{
    mParent = aParent;
    initParams(5);
    mParam[Limiter::WET] = aParent->mWet;
    mParam[Limiter::THRESHOLD] = aParent->mThreshold;
    mParam[Limiter::MAKEUP_GAIN] = aParent->mMakeupGain;
    mParam[Limiter::KNEE_WIDTH] = aParent->mKneeWidth;
    mParam[Limiter::RELEASE_TIME] = aParent->mReleaseTime;
}

void LimiterInstance::filter(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    SoLoud::time aTime)
{
    updateParams(aTime);

    const float threshold = std::pow(10.0f, mParam[Limiter::THRESHOLD] / 20.0f); // Convert threshold dB to linear
    const float makeupGain = std::pow(10.0f, mParam[Limiter::MAKEUP_GAIN] / 20.0f); // Convert make-up gain dB to linear
    const float kneeWidth = mParam[Limiter::KNEE_WIDTH]; // In dB, defines the width of the knee
    const float releaseTime = mParam[Limiter::RELEASE_TIME] / 1000.0f; // Convert release time to seconds
    
    // Variables for the release smoothing
    float releaseCoef = std::exp(-1.0f / (aSamplerate * releaseTime));
    float gain = 1.0f; // Current gain adjustment for limiting

    // Process each channel
    for (unsigned int ch = 0; ch < aChannels; ++ch) {
        for (unsigned int sample = 0; sample < aSamples; ++sample) {
            unsigned int index = sample * aChannels + ch; // Index for the sample in the interleaved buffer
            
            // Get the sample value for this channel
            float sampleValue = aBuffer[index];

            // Calculate the level of the sample in linear scale
            float level = std::fabs(sampleValue);
            
            // Apply the knee function if within knee range
            float reduction = 0.0f;
            if (level > threshold) {
                reduction = 1.0f;  // No reduction if above threshold
            } else if (level > threshold - kneeWidth) {
                // Soft knee (compress gradually as the level approaches the threshold)
                reduction = (level - (threshold - kneeWidth)) / kneeWidth;
            }

            // Apply release time for the smooth transition
            gain = releaseCoef * (gain - reduction) + reduction;

            // Apply the makeup gain after limiting
            float outputSample = sampleValue * gain * makeupGain;

            // Apply the wet/dry mix
            aBuffer[index] = (mParam[Limiter::WET] * outputSample) + ((1.0f - mParam[Limiter::WET]) * sampleValue);
        }
    }

    // printf("LimiterInstance::filter(%f, %f, %f,   gain=%f)\n",
    //        mParam[Limiter::THRESHOLD],
    //        mParam[Limiter::KNEE_WIDTH],
    //        mParam[Limiter::RELEASE_TIME], gain);
}

void LimiterInstance::setFilterParameter(unsigned int aAttributeId, float aValue)
{
    if (aAttributeId >= mNumParams)
        return;

    mParamFader[aAttributeId].mActive = 0;

    switch (aAttributeId)
    {
    case Limiter::WET:
        if (aValue < 0.f || aValue > 1.f)
            break;
        mParam[Limiter::WET] = aValue;
        break;
    case Limiter::THRESHOLD:
        if (aValue < mParent->getParamMin(Limiter::THRESHOLD) ||
            aValue > mParent->getParamMax(Limiter::THRESHOLD))
            return;
        mParam[Limiter::THRESHOLD] = aValue;
        break;
    case Limiter::MAKEUP_GAIN:
        if (aValue < mParent->getParamMin(Limiter::MAKEUP_GAIN) ||
            aValue > mParent->getParamMax(Limiter::MAKEUP_GAIN))
            return;
        mParam[Limiter::MAKEUP_GAIN] = aValue;
        break;
    case Limiter::KNEE_WIDTH:
        if (aValue < mParent->getParamMin(Limiter::KNEE_WIDTH) ||
            aValue > mParent->getParamMax(Limiter::KNEE_WIDTH))
            return;
        mParam[Limiter::KNEE_WIDTH] = aValue;
        break;
    case Limiter::RELEASE_TIME:
        if (aValue < mParent->getParamMin(Limiter::RELEASE_TIME) ||
            aValue > mParent->getParamMax(Limiter::RELEASE_TIME))
            return;
        mParam[Limiter::RELEASE_TIME] = aValue;
        break;
    }

    mParamChanged |= 1 << aAttributeId;
}

SoLoud::result Limiter::setParam(unsigned int aParamIndex, float aValue)
{
    switch (aParamIndex)
    {
    case WET:
        if (aValue < 0.f || aValue > 1.f)
            break;
        mWet = aValue;
        break;
    case THRESHOLD:
        if (aValue < getParamMin(THRESHOLD) || aValue > getParamMax(THRESHOLD))
            return SoLoud::INVALID_PARAMETER;
        mThreshold = aValue;
        break;
    case MAKEUP_GAIN:
        if (aValue < getParamMin(MAKEUP_GAIN) || aValue > getParamMax(MAKEUP_GAIN))
            return SoLoud::INVALID_PARAMETER;
        mMakeupGain = aValue;
        break;
    case KNEE_WIDTH:
        if (aValue < getParamMin(KNEE_WIDTH) || aValue > getParamMax(KNEE_WIDTH))
            return SoLoud::INVALID_PARAMETER;
        mKneeWidth = aValue;
        break;
    case RELEASE_TIME:
        if (aValue < getParamMin(RELEASE_TIME) || aValue > getParamMax(RELEASE_TIME))
            return SoLoud::INVALID_PARAMETER;
        mReleaseTime = aValue;
        break;
    }
    return SoLoud::SO_NO_ERROR;
}

int Limiter::getParamCount()
{
    return 5;
}

const char *Limiter::getParamName(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return "Wet";
    case THRESHOLD:
        return "Threshold";
    case MAKEUP_GAIN:
        return "Makeup Gain";
    case KNEE_WIDTH:
        return "Knee Width";
    case RELEASE_TIME:
        return "Release Time";
    }
    return "Wet";
}

unsigned int Limiter::getParamType(unsigned int aParamIndex)
{
    return FLOAT_PARAM;
}

float Limiter::getParamMax(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 1.f;
    case THRESHOLD:
        return 0.f;
    case MAKEUP_GAIN:
        return 30.f;
    case KNEE_WIDTH:
        return 30.0f;
    case RELEASE_TIME:
        return 1000.f;
    }
    return 1;
}

float Limiter::getParamMin(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 0.f;
    case THRESHOLD:
        return -60.0f;
    case MAKEUP_GAIN:
        return -60.f;
    case KNEE_WIDTH:
        return 0.f;
    case RELEASE_TIME:
        return 1.f;
    }
    return 1;
}

Limiter::Limiter()
{
    mWet = 1.0f;
    mThreshold = -6.f;
    mMakeupGain = 0.0f;
    mKneeWidth = 2.0f;
    mReleaseTime = 100.f;
}

SoLoud::FilterInstance *Limiter::createInstance()
{
    return new LimiterInstance(this);
}
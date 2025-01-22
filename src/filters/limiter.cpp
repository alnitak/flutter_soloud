#include "limiter.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <stdio.h>

LimiterInstance::LimiterInstance(Limiter *aParent)
{
    mParent = aParent;
    initParams(6);
    mParam[Limiter::WET] = aParent->mWet;
    mParam[Limiter::THRESHOLD] = aParent->mThreshold;
    mParam[Limiter::OUTPUT_CEILING] = aParent->mOutputCeiling;
    mParam[Limiter::KNEE_WIDTH] = aParent->mKneeWidth;
    mParam[Limiter::RELEASE_TIME] = aParent->mReleaseTime;
    mParam[Limiter::ATTACK_TIME] = aParent->mAttackTime;
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
    const float outputCeiling = std::pow(10.0f, mParam[Limiter::OUTPUT_CEILING] / 20.0f); // Convert output ceiling dB to linear
    const float kneeWidth = mParam[Limiter::KNEE_WIDTH]; // In dB, defines the width of the knee
    const float kneeStart = mParam[Limiter::THRESHOLD] - (kneeWidth / 2.0f);
    const float kneeEnd = mParam[Limiter::THRESHOLD] + (kneeWidth / 2.0f);
    
    // Time constants
    const float releaseTime = mParam[Limiter::RELEASE_TIME] / 1000.0f; // Convert release time to seconds
    const float attackTime = mParam[Limiter::ATTACK_TIME] / 1000.0f; // Convert attack time to seconds
    const float releaseCoef = std::exp(-1.0f / (aSamplerate * releaseTime));
    const float attackCoef = std::exp(-1.0f / (aSamplerate * attackTime));

    // Initialize per-channel gain tracking if needed
    if (mCurrentGain.size() != aChannels) {
        mCurrentGain.resize(aChannels, 1.0f);
    }

    // Process each channel
    for (unsigned int ch = 0; ch < aChannels; ++ch) {
        float &currentGain = mCurrentGain[ch];
        
        for (unsigned int sample = 0; sample < aSamples; ++sample) {
            unsigned int index = sample * aChannels + ch; // Index for the sample in the interleaved buffer
            
            // Get the sample value for this channel
            float input = aBuffer[index];
            float inputAbs = std::fabs(input);

            // Calculate input level in dB
            float inputDB = 20.0f * std::log10(inputAbs + 1e-6f);
            
            // Calculate gain reduction
            float targetGain = 1.0f;
            if (inputDB > kneeEnd) {
                // Full limiting above knee
                float excess = inputDB - mParam[Limiter::OUTPUT_CEILING];
                targetGain = std::pow(10.0f, -excess / 20.0f);
            }
            else if (inputDB > kneeStart) {
                // Soft knee zone
                float kneePosition = (inputDB - kneeStart) / kneeWidth;
                float excess = (inputDB - mParam[Limiter::OUTPUT_CEILING]) * kneePosition;
                targetGain = std::pow(10.0f, -excess / 20.0f);
            }

            // Smooth gain changes
            if (targetGain < currentGain) {
                // Attack phase
                currentGain = attackCoef * currentGain + (1.0f - attackCoef) * targetGain;
            } else {
                // Release phase
                currentGain = releaseCoef * currentGain + (1.0f - releaseCoef) * targetGain;
            }

            // Apply the gain and wet/dry mix
            float outputSample = input * currentGain;
            aBuffer[index] = (mParam[Limiter::WET] * outputSample) + 
                            ((1.0f - mParam[Limiter::WET]) * input);
        }
    }

    // printf("LimiterInstance::filter(%f, %f, %f, %f,   gain=%f)\n",
    //        mParam[Limiter::THRESHOLD],
    //        mParam[Limiter::OUTPUT_CEILING],
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
    case Limiter::OUTPUT_CEILING:
        if (aValue < mParent->getParamMin(Limiter::OUTPUT_CEILING) ||
            aValue > mParent->getParamMax(Limiter::OUTPUT_CEILING))
            return;
        mParam[Limiter::OUTPUT_CEILING] = aValue;
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
    case Limiter::ATTACK_TIME:
        if (aValue < mParent->getParamMin(Limiter::ATTACK_TIME) ||
            aValue > mParent->getParamMax(Limiter::ATTACK_TIME))
            return;
        mParam[Limiter::ATTACK_TIME] = aValue;
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
    case OUTPUT_CEILING:
        if (aValue < getParamMin(OUTPUT_CEILING) || aValue > getParamMax(OUTPUT_CEILING))
            return SoLoud::INVALID_PARAMETER;
        mOutputCeiling = aValue;
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
    case ATTACK_TIME:
        if (aValue < getParamMin(ATTACK_TIME) || aValue > getParamMax(ATTACK_TIME))
            return SoLoud::INVALID_PARAMETER;
        mAttackTime = aValue;
        break;
    }
    return SoLoud::SO_NO_ERROR;
}

int Limiter::getParamCount()
{
    return 6;
}

const char *Limiter::getParamName(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return "Wet";
    case THRESHOLD:
        return "Threshold";
    case OUTPUT_CEILING:
        return "Output Ceiling";
    case KNEE_WIDTH:
        return "Knee Width";
    case RELEASE_TIME:
        return "Release Time";
    case ATTACK_TIME:
        return "Attack Time";
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
        return 1.0f;
    case THRESHOLD:
        return 0.0f;
    case OUTPUT_CEILING:
        return 0.0f;
    case KNEE_WIDTH:
        return 30.0f;
    case RELEASE_TIME:
        return 1000.0f;
    case ATTACK_TIME:
        return 200.0f;
    }
    return 1;
}

float Limiter::getParamMin(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 0.0f;
    case THRESHOLD:
        return -60.0f;
    case OUTPUT_CEILING:
        return -60.0f;
    case KNEE_WIDTH:
        return 0.0f;
    case RELEASE_TIME:
        return 1.0f;
    case ATTACK_TIME:
        return 0.1f;
    }
    return 1;
}

Limiter::Limiter()
{
    mWet = 1.0f;
    mThreshold = -6.0f;
    mOutputCeiling = -1.0f;
    mKneeWidth = 6.0f;    // Wider knee for smoother transition
    mReleaseTime = 50.0f; // Faster release
    mAttackTime = 1.0f;   // Fast attack to catch peaks
}

SoLoud::FilterInstance *Limiter::createInstance()
{
    return new LimiterInstance(this);
}
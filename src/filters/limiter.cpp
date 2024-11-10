#include "limiter.h"

#include <algorithm>
#include <cmath>
#include <stdio.h>

LimiterInstance::LimiterInstance(Limiter *aParent)
{
    mParent = aParent;
    initParams(5);
    mParam[Limiter::WET] = aParent->mWet;
    mParam[Limiter::THRESHOLD] = aParent->mThreshold;
    mParam[Limiter::ATTACK_TIME] = aParent->mAttackTime;
    mParam[Limiter::RELEASE_TIME] = aParent->mReleaseTime;
    mParam[Limiter::MAKEUP_GAIN] = aParent->mMakeupGain;
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

    // Calculate coefficients based on the sample rate
    gAttackCoeff = exp(-1.0f / (mParam[Limiter::ATTACK_TIME] * aSamplerate));
    gReleaseCoeff = exp(-1.0f / (mParam[Limiter::RELEASE_TIME] * aSamplerate));

    float threshold = pow(10.0f, mParam[Limiter::THRESHOLD] / 20.0f); // Convert dB to linear scale
    float env = 0.0f;
    float gain;

    for (unsigned int i = 0; i < aSamples; i += aChannels)
    {
        // Calculate the peak level across all channels at this sample
        float peak = 0.0f;
        for (unsigned int ch = 0; ch < aChannels; ++ch)
        {
            peak = std::max(peak, std::abs(aBuffer[i + ch]));
        }

        // Smooth envelope with attack/release coefficients
        if (peak > env)
        {
            env = gAttackCoeff * (env - peak) + peak;
        }
        else
        {
            env = gReleaseCoeff * (env - peak) + peak;
        }

        // Calculate gain to apply (limit if above threshold)
        gain = (env > threshold) ? (threshold / env) : 1.0f;
        gain *= mParam[Limiter::MAKEUP_GAIN];

        // Apply gain to each channel with wet/dry mix
        for (unsigned int ch = 0; ch < aChannels; ++ch)
        {
            float drySample = aBuffer[i + ch];  // Original sample
            float wetSample = drySample * gain; // Processed (limited) sample
            aBuffer[i + ch] = (1.0f - mParam[Limiter::WET]) * drySample + mParam[Limiter::WET] * wetSample; // Mix
        }
    }
    // printf("LimiterInstance::filter(%f, %f, %f,   gain=%f)\n",
    //        mParam[Limiter::THRESHOLD],
    //        mParam[Limiter::ATTACK_TIME],
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
    case Limiter::ATTACK_TIME:
        if (aValue < mParent->getParamMin(Limiter::ATTACK_TIME) ||
            aValue > mParent->getParamMax(Limiter::ATTACK_TIME))
            return;
        mParam[Limiter::ATTACK_TIME] = aValue;
        break;
    case Limiter::RELEASE_TIME:
        if (aValue < mParent->getParamMin(Limiter::RELEASE_TIME) ||
            aValue > mParent->getParamMax(Limiter::RELEASE_TIME))
            return;
        mParam[Limiter::RELEASE_TIME] = aValue;
        break;
    case Limiter::MAKEUP_GAIN:
        if (aValue < mParent->getParamMin(Limiter::MAKEUP_GAIN) ||
            aValue > mParent->getParamMax(Limiter::MAKEUP_GAIN))
            return;
        mParam[Limiter::MAKEUP_GAIN] = aValue;
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
    case ATTACK_TIME:
        if (aValue < getParamMin(ATTACK_TIME) || aValue > getParamMax(ATTACK_TIME))
            return SoLoud::INVALID_PARAMETER;
        mAttackTime = aValue;
        break;
    case RELEASE_TIME:
        if (aValue < getParamMin(RELEASE_TIME) || aValue > getParamMax(RELEASE_TIME))
            return SoLoud::INVALID_PARAMETER;
        mAttackTime = aValue;
        break;
    case MAKEUP_GAIN:
        if (aValue < getParamMin(MAKEUP_GAIN) || aValue > getParamMax(MAKEUP_GAIN))
            return SoLoud::INVALID_PARAMETER;
        mAttackTime = aValue;
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
    case ATTACK_TIME:
        return "Attack Time";
    case RELEASE_TIME:
        return "Release Time";
    case MAKEUP_GAIN:
        return "Makeup Gain";
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
    case ATTACK_TIME:
        return 0.1f;
    case RELEASE_TIME:
        return 1.f;
    case MAKEUP_GAIN:
        return 4.f;
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
        return -24.0f;
    case ATTACK_TIME:
        return 0.001f;
    case RELEASE_TIME:
        return 0.01f;
    case MAKEUP_GAIN:
        return 0.f;
    }
    return 1;
}

Limiter::Limiter()
{
    mWet = 1.0f;
    mThreshold = -6.f;
    mAttackTime = 0.01f;
    mReleaseTime = 0.1f;
    mMakeupGain = 1.0f;
}

SoLoud::FilterInstance *Limiter::createInstance()
{
    return new LimiterInstance(this);
}
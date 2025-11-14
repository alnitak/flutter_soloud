#include "limiter.h"

#include <algorithm>
#include <vector>
#include <stdio.h>
#include <cmath>

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

    attackCoef = expf(-1.0f / (mParam[Limiter::ATTACK_TIME] * 0.001f * mParent->mSamplerate));
    releaseCoef = expf(-1.0f / (mParam[Limiter::RELEASE_TIME] * 0.001f * mParent->mSamplerate));
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

    const float thresholdDb = mParam[Limiter::THRESHOLD];
    const float ceilingDb = mParam[Limiter::OUTPUT_CEILING];
    const float kneeDb = mParam[Limiter::KNEE_WIDTH];

    // Initialize per-channel gain tracking if needed
    if (mCurrentGain.size() != aChannels) {
        mCurrentGain.resize(aChannels, 1.0f);
    }

    for (unsigned int i = 0; i < aSamples; ++i) {
        for (unsigned int ch = 0; ch < aChannels; ++ch) {
            float* sample = &aBuffer[i * aChannels + ch];
            float inputAbs = fabsf(*sample);

            // Calculate input level in dB
            float inputDb = 20.0f * log10f(fmaxf(inputAbs, 1e-8f));

            float gainReductionDb = 0.0f;
            if (inputDb > (thresholdDb - kneeDb / 2.0f)) {
                if (kneeDb > 0 && inputDb < (thresholdDb + kneeDb / 2.0f)) {
                    // Soft knee
                    float x = inputDb - thresholdDb + kneeDb / 2.0f;
                    gainReductionDb = (x * x) / (2.0f * kneeDb);
                } else {
                    // Hard knee (limiter)
                    gainReductionDb = inputDb - thresholdDb;
                }
            }

            // Apply ceiling
            gainReductionDb = fmaxf(gainReductionDb, inputDb - ceilingDb);

            float targetGain = powf(10.0f, -gainReductionDb / 20.0f);

            // Smooth gain changes
            if (targetGain < mCurrentGain[ch]) {
                // Attack phase
                mCurrentGain[ch] = attackCoef * mCurrentGain[ch] + (1.0f - attackCoef) * targetGain;
            } else {
                // Release phase
                mCurrentGain[ch] = releaseCoef * mCurrentGain[ch] + (1.0f - releaseCoef) * targetGain;
            }

            // Apply the gain and wet/dry mix
            float limitedSample = *sample * mCurrentGain[ch];
            *sample = (mParam[Limiter::WET] * limitedSample) + ((1.0f - mParam[Limiter::WET]) * *sample);
        }
    }
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
        releaseCoef = expf(-1.0f / (mParam[Limiter::RELEASE_TIME] * 0.001f * mParent->mSamplerate));
        break;
    case Limiter::ATTACK_TIME:
        if (aValue < mParent->getParamMin(Limiter::ATTACK_TIME) ||
            aValue > mParent->getParamMax(Limiter::ATTACK_TIME))
            return;
        mParam[Limiter::ATTACK_TIME] = aValue;
        attackCoef = expf(-1.0f / (mParam[Limiter::ATTACK_TIME] * 0.001f * mParent->mSamplerate));
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

Limiter::Limiter(unsigned int aSamplerate)
{
    mWet = 1.0f;
    mSamplerate = aSamplerate;
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
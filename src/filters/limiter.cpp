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
    mParam[Limiter::MAKEUP_GAIN] = aParent->mMakeupGain;
    mParam[Limiter::KNEE_WIDTH] = aParent->mKneeWidth;
    mParam[Limiter::LOOKAHEAD] = aParent->mLookahead;
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

    // Convert lookahead time to samples
    gLookaheadSamples = (mParam[Limiter::LOOKAHEAD] / 1000.0f) * aSamplerate;
    
    // Release coefficient for smooth gain reduction
    gReleaseCoeff = expf(-1.0f / ((mParam[Limiter::RELEASE_TIME] / 1000.0f) * aSamplerate));

    // Circular buffer for lookahead
    std::vector<float> lookaheadBuffer(aBufferSize * aChannels, 0.0f);
    unsigned int lookaheadIndex = 0;

    float peakLevel = 0.0f;
    float gainReduction = 1.0f;
    
    float thresholdLinear = powf(10.0f, mParam[Limiter::THRESHOLD] / 20.0f);
    float makeupGainLinear = powf(10.0f, mParam[Limiter::MAKEUP_GAIN] / 20.0f);

    // Process each sample
    for (unsigned int i = 0; i < aSamples; i++) {
        // Insert sample into lookahead buffer
        for (unsigned int c = 0; c < aChannels; c++) {
            lookaheadBuffer[lookaheadIndex + c] = aBuffer[i * aChannels + c];
        }

        // Read from lookahead buffer (anticipate peaks)
        unsigned int readIndex = (lookaheadIndex + aBufferSize - (unsigned int)gLookaheadSamples) % aBufferSize;
        peakLevel = 0.0f;

        for (unsigned int c = 0; c < aChannels; c++) {
            float sample = lookaheadBuffer[readIndex * aChannels + c];
            peakLevel = fmaxf(peakLevel, fabsf(sample));
        }

        // Calculate gain reduction based on threshold and knee
        float targetGain;
        if (peakLevel > thresholdLinear) {
            if (mParam[Limiter::KNEE_WIDTH] > 0.0f) {
                float kneeStart = thresholdLinear - (mParam[Limiter::KNEE_WIDTH] / 2.0f);
                if (peakLevel > kneeStart) {
                    targetGain = powf((peakLevel - kneeStart) / mParam[Limiter::KNEE_WIDTH], 2.0f);
                } else {
                    targetGain = thresholdLinear / peakLevel;
                }
            } else {
                targetGain = thresholdLinear / peakLevel;
            }
        } else {
            targetGain = 1.0f;
        }

        // Smooth out gain reduction (release)
        gainReduction = fmaxf(gainReduction * gReleaseCoeff, targetGain);

        // Apply gain reduction and wet/dry blend to each channel
        for (unsigned int c = 0; c < aChannels; c++) {
            float drySample = aBuffer[i * aChannels + c];
            float wetSample = drySample * gainReduction * makeupGainLinear;
            aBuffer[i * aChannels + c] = (mParam[Limiter::WET] * wetSample) + ((1.0f - mParam[Limiter::WET]) * drySample);
        }

        // Update lookahead index
        lookaheadIndex = (lookaheadIndex + 1) % aBufferSize;
    }

    // printf("LimiterInstance::filter(%f, %f, %f,   gainEnvelope=%f  peakLevel=%f)\n",
    //        mParam[Limiter::THRESHOLD],
    //        mParam[Limiter::KNEE_WIDTH],
    //        mParam[Limiter::RELEASE_TIME], gainReduction, peakLevel);
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
    case Limiter::LOOKAHEAD:
        if (aValue < mParent->getParamMin(Limiter::LOOKAHEAD) ||
            aValue > mParent->getParamMax(Limiter::LOOKAHEAD))
            return;
        mParam[Limiter::LOOKAHEAD] = aValue;
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
    case LOOKAHEAD:
        if (aValue < getParamMin(LOOKAHEAD) || aValue > getParamMax(LOOKAHEAD))
            return SoLoud::INVALID_PARAMETER;
        mLookahead = aValue;
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
    case MAKEUP_GAIN:
        return "Makeup Gain";
    case KNEE_WIDTH:
        return "Knee Width";
    case LOOKAHEAD:
        return "Lookahead";
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
    case LOOKAHEAD:
        return 20.f;
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
        return -30.f;
    case KNEE_WIDTH:
        return 0.f;
    case LOOKAHEAD:
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
    mKneeWidth = 6.0f;
    mLookahead = 1.0f;
    mReleaseTime = 100.f;
}

SoLoud::FilterInstance *Limiter::createInstance()
{
    return new LimiterInstance(this);
}
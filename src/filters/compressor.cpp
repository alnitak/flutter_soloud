#include "compressor.h"

#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <iostream>

CompressorInstance::CompressorInstance(Compressor *aParent)
{
    mParent = aParent;
    initParams(8);
    mParam[Compressor::WET] = aParent->mWet;
    mParam[Compressor::THRESHOLD] = aParent->mThreshold;
    mParam[Compressor::MAKEUP_GAIN] = aParent->mMakeupGain;
    mParam[Compressor::KNEE_WIDTH] = aParent->mKneeWidth;
    mParam[Compressor::RATIO] = aParent->mRatio;
    mParam[Compressor::ATTACK_TIME] = aParent->mAttackTime;
    mParam[Compressor::RELEASE_TIME] = aParent->mReleaseTime;

    attackCoef = expf(-1.0f / (mParam[Compressor::ATTACK_TIME] * 0.001f * mParent->mSamplerate));
    releaseCoef = expf(-1.0f / (mParam[Compressor::RELEASE_TIME] * 0.001f * mParent->mSamplerate));

    mEnvelope.assign(8, 0.0f);
}

void CompressorInstance::filter(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    SoLoud::time aTime)
{
    updateParams(aTime);

    // Convert makeup gain to a linear scale
    float makeupGainLin = powf(10.0f, mParam[Compressor::MAKEUP_GAIN] / 20.0f);

    if (mEnvelope.size() != aChannels) {
        mEnvelope.resize(aChannels, 0.0f);
    }

    for (unsigned int i = 0; i < aSamples; ++i) {
        for (unsigned int ch = 0; ch < aChannels; ++ch) {
            float *sample = &aBuffer[i * aChannels + ch];
            float inputLevel = fabsf(*sample);

            // Convert the input level to dB for comparison
            float inputLevelDb = 20.0f * log10f(fmaxf(inputLevel, 1e-8f));

            // Smooth the envelope
            if (inputLevelDb > mEnvelope[ch]) {
                mEnvelope[ch] = attackCoef * (mEnvelope[ch] - inputLevelDb) + inputLevelDb;
            } else {
                mEnvelope[ch] = releaseCoef * (mEnvelope[ch] - inputLevelDb) + inputLevelDb;
            }

            // Determine gain reduction in dB
            float gainReductionDb = 0.0f;
            float knee = mParam[Compressor::KNEE_WIDTH];
            if (mEnvelope[ch] > (mParam[Compressor::THRESHOLD] - knee / 2.0f)) {
                if (knee > 0 && mEnvelope[ch] < (mParam[Compressor::THRESHOLD] + knee / 2.0f)) {
                    // Soft knee region
                    float x = mEnvelope[ch] - mParam[Compressor::THRESHOLD] + knee / 2.0f;
                    gainReductionDb = (1.0f / mParam[Compressor::RATIO] - 1.0f) * (x * x) / (2.0f * knee);
                } else {
                    // Hard knee
                    gainReductionDb = (mEnvelope[ch] - mParam[Compressor::THRESHOLD]) * (1.0f / mParam[Compressor::RATIO] - 1.0f);
                }
            }

            // Convert gain reduction back to linear
            float gainReductionLin = powf(10.0f, gainReductionDb / 20.0f);

            // Apply gain reduction, makeup gain, and wet/dry mix
            float compressedSample = *sample * gainReductionLin * makeupGainLin;
            *sample = mParam[Compressor::WET] * compressedSample +
                      (1.0f - mParam[Compressor::WET]) * (*sample);
        }
    }

    // printf("CompressorInstance::filter(%f, %f)\n",
    //        mParam[Compressor::THRESHOLD], threshold);
}

void CompressorInstance::setFilterParameter(unsigned int aAttributeId, float aValue)
{
    if (aAttributeId >= mNumParams)
        return;

    mParamFader[aAttributeId].mActive = 0;

    switch (aAttributeId)
    {
    case Compressor::WET:
        if (aValue < 0.f || aValue > 1.f)
            break;
        mParam[Compressor::WET] = aValue;
        break;
    case Compressor::THRESHOLD:
        if (aValue < mParent->getParamMin(Compressor::THRESHOLD) ||
            aValue > mParent->getParamMax(Compressor::THRESHOLD))
            return;
        mParam[Compressor::THRESHOLD] = aValue;
        break;
    case Compressor::MAKEUP_GAIN:
        if (aValue < mParent->getParamMin(Compressor::MAKEUP_GAIN) ||
            aValue > mParent->getParamMax(Compressor::MAKEUP_GAIN))
            return;
        mParam[Compressor::MAKEUP_GAIN] = aValue;
        break;
    case Compressor::KNEE_WIDTH:
        if (aValue < mParent->getParamMin(Compressor::KNEE_WIDTH) ||
            aValue > mParent->getParamMax(Compressor::KNEE_WIDTH))
            return;
        mParam[Compressor::KNEE_WIDTH] = aValue;
        break;
    case Compressor::RATIO:
        if (aValue < mParent->getParamMin(Compressor::RATIO) ||
            aValue > mParent->getParamMax(Compressor::RATIO))
            return;
        mParam[Compressor::RATIO] = aValue;
        break;
    case Compressor::ATTACK_TIME:
        if (aValue < mParent->getParamMin(Compressor::ATTACK_TIME) ||
            aValue > mParent->getParamMax(Compressor::ATTACK_TIME))
            return;
        mParam[Compressor::ATTACK_TIME] = aValue;
        attackCoef = expf(-1.0f / (mParam[Compressor::ATTACK_TIME] * 0.001f * mParent->mSamplerate));
        break;
    case Compressor::RELEASE_TIME:
        if (aValue < mParent->getParamMin(Compressor::RELEASE_TIME) ||
            aValue > mParent->getParamMax(Compressor::RELEASE_TIME))
            return;
        mParam[Compressor::RELEASE_TIME] = aValue;
        releaseCoef = expf(-1.0f / (mParam[Compressor::RELEASE_TIME] * 0.001f * mParent->mSamplerate));
        break;
    }

    mParamChanged |= 1 << aAttributeId;
}

SoLoud::result Compressor::setParam(unsigned int aParamIndex, float aValue)
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
    case RATIO:
        if (aValue < getParamMin(RATIO) || aValue > getParamMax(RATIO))
            return SoLoud::INVALID_PARAMETER;
        mRatio = aValue;
        break;
    case ATTACK_TIME:
        if (aValue < getParamMin(ATTACK_TIME) || aValue > getParamMax(ATTACK_TIME))
            return SoLoud::INVALID_PARAMETER;
        mAttackTime = aValue;
        break;
    case RELEASE_TIME:
        if (aValue < getParamMin(RELEASE_TIME) || aValue > getParamMax(RELEASE_TIME))
            return SoLoud::INVALID_PARAMETER;
        mReleaseTime = aValue;
        break;
    }
    return SoLoud::SO_NO_ERROR;
}

int Compressor::getParamCount()
{
    return 8;
}

const char *Compressor::getParamName(unsigned int aParamIndex)
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
    case RATIO:
        return "Ratio";
    case ATTACK_TIME:
        return "Attack Time";
    case RELEASE_TIME:
        return "Release Time";
    }
    return "Wet";
}

unsigned int Compressor::getParamType(unsigned int aParamIndex)
{
    return FLOAT_PARAM;
}

float Compressor::getParamMax(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 1.f;
    case THRESHOLD:
        return 0.f;
    case MAKEUP_GAIN:
        return 40.f;
    case KNEE_WIDTH:
        return 40.0f;
    case RATIO:
        return 10.0f;
    case ATTACK_TIME:
        return 100.f;
    case RELEASE_TIME:
        return 1000.f;
    }
    return 1;
}

float Compressor::getParamMin(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 0.f;
    case THRESHOLD:
        return -80.0f;
    case MAKEUP_GAIN:
        return -40.f;
    case KNEE_WIDTH:
        return 0.f;
    case RATIO:
        return 1.f;
    case ATTACK_TIME:
        return 0.f;
    case RELEASE_TIME:
        return 0.f;
    }
    return 1;
}

Compressor::Compressor(unsigned int samplerate)
{
    mSamplerate = samplerate;
    mWet = 1.0f;
    mThreshold = -6.f;
    mMakeupGain = 0.f;
    mKneeWidth = 2.0f;
    mRatio = 3.0f;
    mAttackTime = 10.f;
    mReleaseTime = 100.f;
}

SoLoud::FilterInstance *Compressor::createInstance()
{
    return new CompressorInstance(this);
}
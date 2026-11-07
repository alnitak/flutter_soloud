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

    mEnvelope.assign(MAX_CHANNELS, 0.0f);
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

    const float wet = mParam[Compressor::WET];
    if (wet <= 0.0f) {
        return;
    }

    if ((mParamChanged & ((1ULL << Compressor::ATTACK_TIME) |
                          (1ULL << Compressor::RELEASE_TIME))) != 0) {
        attackCoef = expf(-1.0f / fmaxf(mParam[Compressor::ATTACK_TIME] * 0.001f * aSamplerate, 1e-6f));
        releaseCoef = expf(-1.0f / fmaxf(mParam[Compressor::RELEASE_TIME] * 0.001f * aSamplerate, 1e-6f));
        mParamChanged &= ~((1ULL << Compressor::ATTACK_TIME) |
                           (1ULL << Compressor::RELEASE_TIME));
    }

    // Convert makeup gain to a linear scale (10^(dB/20) = exp(dB * ln(10)/20))
    const float makeupGainLin = expf(mParam[Compressor::MAKEUP_GAIN] * 0.11512925465f);

    if (mEnvelope.size() < aChannels) {
        mEnvelope.resize(aChannels, 0.0f);
    }

    const float threshold = mParam[Compressor::THRESHOLD];
    const float knee = mParam[Compressor::KNEE_WIDTH];
    const float halfKnee = knee * 0.5f;
    const float slope = 1.0f / mParam[Compressor::RATIO] - 1.0f;
    const float dry = 1.0f - wet;

    for (unsigned int ch = 0; ch < aChannels; ++ch) {
        float *channelBuf = aBuffer + (size_t)ch * (size_t)aBufferSize;
        float env = mEnvelope[ch];
        for (unsigned int i = 0; i < aSamples; ++i) {
            float s = channelBuf[i];
            float inputLevel = fabsf(s);

            // 20 * log10(x) == (20 / ln(10)) * ln(x)
            float inputLevelDb = 8.685889638f * logf(fmaxf(inputLevel, 1e-8f));

            // Smooth the envelope
            if (inputLevelDb > env) {
                env = attackCoef * (env - inputLevelDb) + inputLevelDb;
            } else {
                env = releaseCoef * (env - inputLevelDb) + inputLevelDb;
            }

            // Determine gain reduction in dB
            float gainReductionLin = 1.0f;
            if (env > (threshold - halfKnee)) {
                float gainReductionDb;
                if (knee > 0.0f && env < (threshold + halfKnee)) {
                    // Soft knee region
                    float x = env - threshold + halfKnee;
                    gainReductionDb = slope * (x * x) / (2.0f * knee);
                } else {
                    // Hard knee
                    gainReductionDb = (env - threshold) * slope;
                }
                if (gainReductionDb != 0.0f) {
                    gainReductionLin = expf(gainReductionDb * 0.11512925465f);
                }
            }

            // Apply gain reduction, makeup gain, and wet/dry mix
            float compressedSample = s * (gainReductionLin * makeupGainLin);
            channelBuf[i] = wet * compressedSample + dry * s;
        }
        mEnvelope[ch] = env;
    }
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

    mParamChanged |= 1ULL << (aAttributeId < 63 ? aAttributeId : 63);
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

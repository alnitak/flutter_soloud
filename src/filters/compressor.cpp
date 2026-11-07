#include "compressor.h"
#include "../simd/soloud_simd.h"

#include <algorithm>
#include <cmath>

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

    mLastSamplerate = static_cast<float>(mParent->mSamplerate);
    attackCoef = expf(-1.0f / fmaxf(mParam[Compressor::ATTACK_TIME] * 0.001f * mLastSamplerate, 1e-6f));
    releaseCoef = expf(-1.0f / fmaxf(mParam[Compressor::RELEASE_TIME] * 0.001f * mLastSamplerate, 1e-6f));

    for (unsigned int i = 0; i < MAX_CHANNELS; ++i) {
        mEnvelope[i] = 0.0f;
    }
}

void CompressorInstance::filter(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    SoLoud::time aTime)
{
    soloud_simd::ScopedFlushDenormals flushDenormals;
    updateParams(aTime);

    const float wet = mParam[Compressor::WET];
    if (wet <= 0.0f || aChannels == 0 || aSamples == 0) {
        return;
    }

    if (aSamplerate != mLastSamplerate ||
        (mParamChanged & ((1ULL << Compressor::ATTACK_TIME) |
                          (1ULL << Compressor::RELEASE_TIME))) != 0) {
        mLastSamplerate = aSamplerate;
        attackCoef = expf(-1.0f / fmaxf(mParam[Compressor::ATTACK_TIME] * 0.001f * aSamplerate, 1e-6f));
        releaseCoef = expf(-1.0f / fmaxf(mParam[Compressor::RELEASE_TIME] * 0.001f * aSamplerate, 1e-6f));
        mParamChanged &= ~((1ULL << Compressor::ATTACK_TIME) |
                           (1ULL << Compressor::RELEASE_TIME));
    }

    // 10^(dB/20) == 2^(dB / (20 * log10(2))) == 2^(dB * 0.166096404744f)
    constexpr float kDbToLog2 = 0.1660964047443681f;
    // 20 * log10(x) == (20 * log10(2)) * log2(x) == 6.020599913279624f * log2(x)
    constexpr float kLog2ToDb = 6.020599913279624f;

    const float makeupGainLin =
        soloud_simd::fast_exp2(mParam[Compressor::MAKEUP_GAIN] * kDbToLog2);

    const unsigned int channels =
        aChannels > MAX_CHANNELS ? MAX_CHANNELS : aChannels;
    const float threshold = mParam[Compressor::THRESHOLD];
    const float knee = mParam[Compressor::KNEE_WIDTH];
    const float halfKnee = knee * 0.5f;
    const float kneeLo = threshold - halfKnee;
    const float kneeHi = threshold + halfKnee;
    const float slope = 1.0f / mParam[Compressor::RATIO] - 1.0f;
    const float softKneeScale = (knee > 0.0f) ? (slope / (2.0f * knee)) : 0.0f;
    const float dry = 1.0f - wet;
    const float wetMakeup = wet * makeupGainLin;
    const float atk = attackCoef;
    const float rel = releaseCoef;

    for (unsigned int ch = 0; ch < channels; ++ch) {
        float *channelBuf = aBuffer + (size_t)ch * (size_t)aBufferSize;
        float env = mEnvelope[ch];
        for (unsigned int i = 0; i < aSamples; ++i) {
            const float s = channelBuf[i];
            const float inputLevel = fabsf(s);
            const float inputLevelDb =
                kLog2ToDb * soloud_simd::fast_log2(fmaxf(inputLevel, 1e-8f));

            const float coef = (inputLevelDb > env) ? atk : rel;
            env = coef * (env - inputLevelDb) + inputLevelDb;

            float gainReductionLin = 1.0f;
            if (env > kneeLo) {
                float gainReductionDb;
                if (knee > 0.0f && env < kneeHi) {
                    const float x = env - kneeLo;
                    gainReductionDb = (x * x) * softKneeScale;
                } else {
                    gainReductionDb = (env - threshold) * slope;
                }
                if (gainReductionDb != 0.0f) {
                    gainReductionLin =
                        soloud_simd::fast_exp2(gainReductionDb * kDbToLog2);
                }
            }

            channelBuf[i] = s * (dry + wetMakeup * gainReductionLin);
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

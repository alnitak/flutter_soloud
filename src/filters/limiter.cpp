#include "limiter.h"
#include "../simd/soloud_simd.h"

#include <algorithm>
#include <vector>
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

    mRingSize = 0;
    mChannels = 0;
    mWritePos = 0;
    mSmoothedGain = 1.0f;
    mReleaseCoef = 0.0f;

    // Pre-reserve lookahead capacity outside the audio thread for up to
    // max ATTACK_TIME (200 ms) at stereo 96 kHz (or mParent->mSamplerate if
    // higher) so parameter automation in filter() does not heap-allocate.
    const unsigned int maxSr =
        std::max<unsigned int>(mParent->mSamplerate, 96000u);
    const size_t maxLookahead =
        static_cast<size_t>(mParent->getParamMax(Limiter::ATTACK_TIME) *
                                0.001f * static_cast<float>(maxSr) +
                            2.0f);
    mDelayBuffer.reserve(maxLookahead * 2u);
    mGainEnv.reserve(maxLookahead);
}

void LimiterInstance::resizeBuffers(int lookaheadSamples, int channels)
{
    mRingSize = lookaheadSamples;
    mChannels = channels;
    mDelayBuffer.assign((size_t)lookaheadSamples * (size_t)channels, 0.0f);
    mGainEnv.assign((size_t)lookaheadSamples, 1.0f);
    mWritePos = 0;
    mSmoothedGain = 1.0f;
}

void LimiterInstance::filter(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    SoLoud::time aTime)
{
    if (aChannels == 0 || aSamples == 0) {
        return;
    }

    soloud_simd::ScopedFlushDenormals flushDenormals;
    updateParams(aTime);

    const float thresholdDb = mParam[Limiter::THRESHOLD];
    const float ceilingDb = mParam[Limiter::OUTPUT_CEILING];
    const float wet = mParam[Limiter::WET];
    const float lookaheadMs = mParam[Limiter::ATTACK_TIME];
    const float releaseMs = mParam[Limiter::RELEASE_TIME];

    if (ceilingDb != mCachedCeilingDb) {
        mCachedCeilingDb = ceilingDb;
        mCeilingLin = powf(10.0f, ceilingDb / 20.0f);
    }
    if (thresholdDb != mCachedThresholdDb) {
        mCachedThresholdDb = thresholdDb;
        mDriveLin = powf(10.0f, -thresholdDb / 20.0f);
    }
    const float ceilingLin = mCeilingLin;
    const float driveLin = mDriveLin;

    // Look-ahead window in samples. Need at least 2 for a meaningful ramp.
    int desiredLookahead = (int)(lookaheadMs * 0.001f * aSamplerate + 0.5f);
    if (desiredLookahead < 2) desiredLookahead = 2;

    if (mRingSize != desiredLookahead || mChannels != (int)aChannels) {
        resizeBuffers(desiredLookahead, (int)aChannels);
    }

    if (releaseMs != mCachedReleaseMs || aSamplerate != mCachedSamplerate) {
        mCachedReleaseMs = releaseMs;
        mCachedSamplerate = aSamplerate;
        mReleaseCoef = expf(-1.0f / fmaxf(releaseMs * 0.001f * aSamplerate, 1.0f));
    }

    const int ringSize = mRingSize;
    const float perStepBase = 1.0f / (float)(ringSize - 1);
    const float releaseCoef = mReleaseCoef;
    const float oneMinusRelease = 1.0f - releaseCoef;
    const float dryMix = 1.0f - wet;

    // SoLoud passes audio in PLANAR layout: channel ch occupies the contiguous
    // range [ch*aBufferSize, ch*aBufferSize + aSamples). The internal delay
    // buffer remains frame-major (channel-interleaved) for cache locality of
    // the per-frame read/write.
    const size_t stride = (size_t)aBufferSize;
    float *delayBuf = mDelayBuffer.data();
    float *gainEnv = mGainEnv.data();
    int writePos = mWritePos;
    float smoothedGain = mSmoothedGain;

    if (aChannels == 2) {
        float *ch0 = aBuffer;
        float *ch1 = aBuffer + stride;
        for (unsigned int i = 0; i < aSamples; ++i) {
            const float in0 = ch0[i];
            const float in1 = ch1[i];
            const float peak = fmaxf(fabsf(in0), fabsf(in1)) * driveLin;
            const float reqGain = (peak > ceilingLin) ? (ceilingLin / peak) : 1.0f;

            delayBuf[(size_t)writePos * 2u] = in0;
            delayBuf[(size_t)writePos * 2u + 1u] = in1;
            gainEnv[writePos] = reqGain;

            if (reqGain < 1.0f) {
                const float perStep = (1.0f - reqGain) * perStepBase;
                float threshGain = reqGain;
                for (int back = 1; back < ringSize; ++back) {
                    threshGain += perStep;
                    if (threshGain >= 1.0f) break;
                    int idx = writePos - back;
                    if (idx < 0) idx += ringSize;
                    if (gainEnv[idx] > threshGain) {
                        gainEnv[idx] = threshGain;
                    } else {
                        break;
                    }
                }
            }

            int readPos = writePos + 1;
            if (readPos >= ringSize) readPos = 0;
            const float envGain = gainEnv[readPos];

            if (envGain >= smoothedGain) {
                smoothedGain = releaseCoef * smoothedGain + oneMinusRelease * envGain;
            } else {
                smoothedGain = envGain;
            }

            const float effDrive = driveLin * smoothedGain;
            float dry0 = delayBuf[(size_t)readPos * 2u];
            float dry1 = delayBuf[(size_t)readPos * 2u + 1u];
            float out0 = wet * (dry0 * effDrive) + dryMix * dry0;
            float out1 = wet * (dry1 * effDrive) + dryMix * dry1;
            if (out0 > ceilingLin) out0 = ceilingLin;
            else if (out0 < -ceilingLin) out0 = -ceilingLin;
            if (out1 > ceilingLin) out1 = ceilingLin;
            else if (out1 < -ceilingLin) out1 = -ceilingLin;
            ch0[i] = out0;
            ch1[i] = out1;

            writePos = readPos;
        }
    } else {
        for (unsigned int i = 0; i < aSamples; ++i) {
            float peak = 0.0f;
            for (unsigned int ch = 0; ch < aChannels; ++ch) {
                float a = fabsf(aBuffer[i + (size_t)ch * stride]) * driveLin;
                if (a > peak) peak = a;
            }

            float reqGain = (peak > ceilingLin) ? (ceilingLin / peak) : 1.0f;

            for (unsigned int ch = 0; ch < aChannels; ++ch) {
                delayBuf[(size_t)writePos * (size_t)aChannels + ch] =
                    aBuffer[i + (size_t)ch * stride];
            }
            gainEnv[writePos] = reqGain;

            if (reqGain < 1.0f) {
                float perStep = (1.0f - reqGain) * perStepBase;
                float threshGain = reqGain;
                for (int back = 1; back < ringSize; ++back) {
                    threshGain += perStep;
                    if (threshGain >= 1.0f) break;
                    int idx = writePos - back;
                    if (idx < 0) idx += ringSize;
                    if (gainEnv[idx] > threshGain) {
                        gainEnv[idx] = threshGain;
                    } else {
                        break;
                    }
                }
            }

            int readPos = writePos + 1;
            if (readPos >= ringSize) readPos = 0;
            float envGain = gainEnv[readPos];

            if (envGain >= smoothedGain) {
                smoothedGain = releaseCoef * smoothedGain +
                               oneMinusRelease * envGain;
            } else {
                smoothedGain = envGain;
            }

            for (unsigned int ch = 0; ch < aChannels; ++ch) {
                float dry = delayBuf[(size_t)readPos * (size_t)aChannels + ch];
                float limited = dry * driveLin * smoothedGain;
                float out = wet * limited + dryMix * dry;
                if (out > ceilingLin) out = ceilingLin;
                else if (out < -ceilingLin) out = -ceilingLin;
                aBuffer[i + (size_t)ch * stride] = out;
            }

            writePos = readPos;
        }
    }

    mWritePos = writePos;
    mSmoothedGain = smoothedGain;
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
        // The look-ahead ring is resized lazily in filter() the next time it
        // runs — avoids a click on every parameter touch.
        break;
    }

    mParamChanged |= 1ULL << (aAttributeId < 63 ? aAttributeId : 63);
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
        return "Threshold / Drive";
    case OUTPUT_CEILING:
        return "Output Ceiling";
    case KNEE_WIDTH:
        return "Knee Width";
    case RELEASE_TIME:
        return "Release Time";
    case ATTACK_TIME:
        return "Lookahead";
    }
    return "Wet";
}

unsigned int Limiter::getParamType(unsigned int /*aParamIndex*/)
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
    mThreshold = -3.0f;
    mOutputCeiling = -1.0f;
    mKneeWidth = 6.0f;
    mReleaseTime = 50.0f;
    mAttackTime = 1.0f; // 1 ms look-ahead = 48 samples @ 48 kHz
}

SoLoud::FilterInstance *Limiter::createInstance()
{
    return new LimiterInstance(this);
}

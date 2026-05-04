#include "limiter.h"

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
    updateParams(aTime);

    const float thresholdDb = mParam[Limiter::THRESHOLD];
    const float ceilingDb = mParam[Limiter::OUTPUT_CEILING];
    const float kneeDb = mParam[Limiter::KNEE_WIDTH];
    const float wet = mParam[Limiter::WET];
    const float lookaheadMs = mParam[Limiter::ATTACK_TIME];
    const float releaseMs = mParam[Limiter::RELEASE_TIME];

    const float ceilingLin = powf(10.0f, ceilingDb / 20.0f);
    const float halfKnee = kneeDb * 0.5f;
    const float kneeLow = thresholdDb - halfKnee;
    const float kneeHigh = thresholdDb + halfKnee;

    // Look-ahead window in samples. Need at least 2 for a meaningful ramp.
    int desiredLookahead = (int)(lookaheadMs * 0.001f * aSamplerate + 0.5f);
    if (desiredLookahead < 2) desiredLookahead = 2;

    if (mRingSize != desiredLookahead || mChannels != (int)aChannels) {
        resizeBuffers(desiredLookahead, (int)aChannels);
    }

    // One-pole release coefficient. Recomputed every call: cheap, and tracks
    // parameter changes without a click.
    mReleaseCoef = expf(-1.0f / fmaxf(releaseMs * 0.001f * aSamplerate, 1.0f));

    const int ringSize = mRingSize;
    const float perStepBase = 1.0f / (float)(ringSize - 1);

    // SoLoud passes audio in PLANAR layout: channel ch occupies the contiguous
    // range [ch*aBufferSize, ch*aBufferSize + aSamples). The internal delay
    // buffer remains frame-major (channel-interleaved) for cache locality of
    // the per-frame read/write.
    const size_t stride = (size_t)aBufferSize;

    for (unsigned int i = 0; i < aSamples; ++i) {
        // 1. Stereo-linked peak across all channels for this sample frame.
        float peak = 0.0f;
        for (unsigned int ch = 0; ch < aChannels; ++ch) {
            float a = fabsf(aBuffer[i + (size_t)ch * stride]);
            if (a > peak) peak = a;
        }

        // 2. Required gain reduction (dB), combining soft-knee threshold
        //    behaviour with hard ceiling enforcement. Always pick the larger
        //    reduction so the ceiling wins when in conflict.
        float peakDb = 20.0f * log10f(fmaxf(peak, 1e-8f));
        float reductionDb = 0.0f;

        if (kneeDb > 0.0f && peakDb > kneeLow && peakDb < kneeHigh) {
            float over = peakDb - kneeLow;
            reductionDb = (over * over) / (2.0f * kneeDb);
        } else if (peakDb >= kneeHigh) {
            reductionDb = peakDb - thresholdDb;
        }

        float ceilReductionDb = peakDb - ceilingDb;
        if (ceilReductionDb > reductionDb) reductionDb = ceilReductionDb;
        if (reductionDb < 0.0f) reductionDb = 0.0f;

        float reqGain = powf(10.0f, -reductionDb / 20.0f);
        if (reqGain > 1.0f) reqGain = 1.0f;

        // 3. Write the new sample frame and its required gain into the ring.
        for (unsigned int ch = 0; ch < aChannels; ++ch) {
            mDelayBuffer[(size_t)mWritePos * (size_t)aChannels + ch] =
                aBuffer[i + (size_t)ch * stride];
        }
        mGainEnv[mWritePos] = reqGain;

        // 4. Backward scan: walk back through the look-ahead window and lower
        //    any earlier required-gain values that would not allow a smooth
        //    linear ramp down to reqGain by the time it reaches the output.
        //    This is what makes the limiter "look ahead" — it pre-emptively
        //    starts ramping the gain down so the offending sample arrives
        //    already attenuated, instead of being clipped after the fact.
        if (reqGain < 1.0f) {
            float perStep = (1.0f - reqGain) * perStepBase;
            float threshGain = reqGain;
            for (int back = 1; back < ringSize; ++back) {
                threshGain += perStep;
                if (threshGain >= 1.0f) break;
                int idx = mWritePos - back;
                if (idx < 0) idx += ringSize;
                if (mGainEnv[idx] > threshGain) {
                    mGainEnv[idx] = threshGain;
                } else {
                    // The earlier sample already demands an equal or lower
                    // gain; its own backward scan covers everything before it.
                    break;
                }
            }
        }

        // 5. Read the oldest frame in the ring (the one being emitted now).
        int readPos = mWritePos + 1;
        if (readPos >= ringSize) readPos = 0;
        float envGain = mGainEnv[readPos];

        // 6. Release smoothing only. Attack is encoded in the envelope ramp,
        //    so going DOWN tracks the envelope sample-accurately; going UP
        //    is smoothed by the one-pole release filter.
        if (envGain >= mSmoothedGain) {
            mSmoothedGain = mReleaseCoef * mSmoothedGain +
                            (1.0f - mReleaseCoef) * envGain;
        } else {
            mSmoothedGain = envGain;
        }

        // 7. Apply gain to the delayed sample, mix wet/dry (both delayed so
        //    they stay phase-aligned), and hard-clip at ceiling as a safety
        //    net for any dry-path overshoot.
        for (unsigned int ch = 0; ch < aChannels; ++ch) {
            float dry = mDelayBuffer[(size_t)readPos * (size_t)aChannels + ch];
            float limited = dry * mSmoothedGain;
            float out = wet * limited + (1.0f - wet) * dry;
            if (out > ceilingLin) out = ceilingLin;
            else if (out < -ceilingLin) out = -ceilingLin;
            aBuffer[i + (size_t)ch * stride] = out;
        }

        // 8. Advance the write head.
        mWritePos++;
        if (mWritePos >= ringSize) mWritePos = 0;
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
    mThreshold = -6.0f;
    mOutputCeiling = -1.0f;
    mKneeWidth = 6.0f;
    mReleaseTime = 50.0f;
    mAttackTime = 1.0f; // 1 ms look-ahead = 48 samples @ 48 kHz
}

SoLoud::FilterInstance *Limiter::createInstance()
{
    return new LimiterInstance(this);
}

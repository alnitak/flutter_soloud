#include <string.h>
#include <memory>
#include <cmath>

#include "pitch_shift_filter.h"

PitchShiftInstance::PitchShiftInstance(PitchShift *aParent)
{
    mParent = aParent;
    pitchShift = CSmbPitchShift();
    initParams(3);
    mParam[PitchShift::SHIFT] = aParent->mShift;
    mParam[PitchShift::SEMITONES] = aParent->mSemitones;
}

void PitchShiftInstance::filter(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    SoLoud::time aTime)
{
    updateParams(aTime);
    float *in = (float *)calloc((aSamples), sizeof(float));
    for (int j = 0; j < aSamples; j++)
    {
        // shrink channels to one to feed smbPitchShift.
        for (int n = 0; n < aChannels; n++)
            in[j] += aBuffer[j + aSamples * n];
        in[j] /= (float)aChannels;
    }
    
#if defined(SOLOUD_SSE_INTRINSICS)
    // trying to get more quality when SIMD is enabled.
    pitchShift.smbPitchShift(mParam[PitchShift::SHIFT], aSamples, 4096, 16, aSamplerate, in, in);
#else
    pitchShift.smbPitchShift(mParam[PitchShift::SHIFT], aSamples, 2048, 8, aSamplerate, in, in);
#endif
    for (int j = 0; j < aSamples; j++)
    {
        aBuffer[j] = aBuffer[j] * (1.0f - mParam[PitchShift::WET]) + in[j] * mParam[PitchShift::WET]; // L chan
        for (int n = 1; n < aChannels; n++)
            aBuffer[j + aSamples] = aBuffer[j + aSamples] * (1.0f - mParam[PitchShift::WET]) + in[j] * mParam[PitchShift::WET]; // R chan
    }
    free(in);
}

void PitchShiftInstance::setFilterParameter(unsigned int aAttributeId, float aValue)
{
    if (aAttributeId >= mNumParams)
        return;

    mParamFader[aAttributeId].mActive = 0;

    switch (aAttributeId)
    {
    case PitchShift::WET:
        if (aValue < 0.f || aValue > 1.f)
            break;
        mParam[PitchShift::WET] = aValue;
        break;
    case PitchShift::SHIFT:
        if (aValue < mParent->getParamMin(PitchShift::SHIFT) ||
            aValue > mParent->getParamMax(PitchShift::SHIFT))
            return;
        mParam[PitchShift::SHIFT] = aValue;
        mParam[PitchShift::SEMITONES] = 12 * log2f(aValue);
        break;
    case PitchShift::SEMITONES:
        if (aValue < mParent->getParamMin(PitchShift::SEMITONES) ||
            aValue > mParent->getParamMax(PitchShift::SEMITONES))
            return;
        mParam[PitchShift::SEMITONES] = aValue;
        mParam[PitchShift::SHIFT] = pow(2., aValue / 12.);
        break;
    }

    mParamChanged |= 1 << aAttributeId;
}

SoLoud::result PitchShift::setParam(unsigned int aParamIndex, float aValue)
{
    switch (aParamIndex)
    {
    case WET:
        if (aValue < 0.f || aValue > 1.f)
            break;
        mWet = aValue;
        break;
    case SHIFT:
        if (aValue < getParamMin(SHIFT) || aValue > getParamMax(SHIFT))
            return SoLoud::INVALID_PARAMETER;
        mShift = aValue;
        mSemitones = 12 * log2f(mShift);
        break;
    case SEMITONES:
        if (aValue < getParamMin(SEMITONES) || aValue > getParamMax(SEMITONES))
            return SoLoud::INVALID_PARAMETER;
        mSemitones = aValue;
        mShift = pow(2., mSemitones / 12.);
        break;
    }
    return SoLoud::SO_NO_ERROR;
}

int PitchShift::getParamCount()
{
    return 3;
}

const char *PitchShift::getParamName(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return "Wet";
    case SHIFT:
        return "Shift";
    case SEMITONES:
        return "Semitones";
    }
    return "Wet";
}

unsigned int PitchShift::getParamType(unsigned int aParamIndex)
{
    return FLOAT_PARAM;
}

float PitchShift::getParamMax(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 1.f;
    case SHIFT:
        return 3.f;
    case SEMITONES:
        return 36.f;
    }
    return 1;
}

float PitchShift::getParamMin(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case WET:
        return 0.f;
    case SHIFT:
        return 0.1f;
    case SEMITONES:
        return -36.f;
    }
    return 1;
}

PitchShift::PitchShift()
{
    mWet = 1.0f;
    mShift = 1.0f;
    mSemitones = 0.0f;
}

SoLoud::FilterInstance *PitchShift::createInstance()
{
    return new PitchShiftInstance(this);
}

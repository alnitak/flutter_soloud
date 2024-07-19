#include <string.h>
#include <memory>
#include <cmath>

#include "soloud.h"
#include "pitch.h"

PitchInstance::PitchInstance(Pitch *aParent)
{
    mParent = aParent;
    initParams(9);
    pitchShift = CSmbPitchShift();
    mParam[BAND1] = aParent->mVolume[BAND1 - BAND1];
    mParam[BAND2] = aParent->mVolume[BAND2 - BAND1];
    mParam[BAND3] = aParent->mVolume[BAND3 - BAND1];
    mParam[BAND4] = aParent->mVolume[BAND4 - BAND1];
    mParam[BAND5] = aParent->mVolume[BAND5 - BAND1];
    mParam[BAND6] = aParent->mVolume[BAND6 - BAND1];
    mParam[BAND7] = aParent->mVolume[BAND7 - BAND1];
    mParam[BAND8] = aParent->mVolume[BAND8 - BAND1];
}

// void PitchInstance::filterChannel(
//     float *aBuffer, 
//     unsigned int aSamples, 
//     float aSamplerate, 
//     SoLoud::time aTime, 
//     unsigned int aChannel, 
//     unsigned int aChannels)
void PitchInstance::filter(
    float *aBuffer, 
    unsigned int aSamples, 
    unsigned int aBufferSize, 
    unsigned int aChannels, 
    float aSamplerate, 
    SoLoud::time aTime)
{
    float *in = (float*)calloc((aSamples), sizeof(float));
    int halfSamples = aSamples >> 1;
    for (int j = 0; j < aSamples; j++)
    {
        // decrease level to avoid clipping distortions
        // aBuffer[j] = fminf(1.f, fmaxf(aBuffer[j], -1.f));
        // shrink channels to one to feed smbPitchShift
        for (int n = 0; n < aChannels; n++)
            in[j] += aBuffer[j + aSamples * n];
        in[j] /= (float)aChannels;
    }
    // float pitchShift = pow(2., fSemitones/12.);
    pitchShift.smbPitchShift(mParam[1], aSamples, 4096, 16, aSamplerate, in, in);
    for (int j = 0; j < aSamples; j++)
    {
        aBuffer[j] = in[j]; // L
        for (int n = 1; n < aChannels; n++)
            aBuffer[j + aSamples] = in[j]; // R
    }
    free(in);
}

SoLoud::result Pitch::setParam(unsigned int aBand, float aVolume)
{
    if (aBand < BAND1 || aBand > BAND8)
        return SoLoud::INVALID_PARAMETER;
    if (aVolume < getParamMin(BAND1) || aVolume > getParamMax(BAND1))
        return SoLoud::INVALID_PARAMETER;

    mVolume[aBand - BAND1] = aVolume;
    return SoLoud::SO_NO_ERROR;
}

int Pitch::getParamCount()
{
    return 9;
}

const char *Pitch::getParamName(unsigned int aParamIndex)
{
    switch (aParamIndex)
    {
    case BAND1:
        return "Band 1";
    case BAND2:
        return "Band 2";
    case BAND3:
        return "Band 3";
    case BAND4:
        return "Band 4";
    case BAND5:
        return "Band 5";
    case BAND6:
        return "Band 6";
    case BAND7:
        return "Band 7";
    case BAND8:
        return "Band 8";
    }
    return "Wet";
}

unsigned int Pitch::getParamType(unsigned int aParamIndex)
{
    return FLOAT_PARAM;
}

float Pitch::getParamMax(unsigned int aParamIndex)
{
    if (aParamIndex == 0)
        return 1;
    return 4;
}

float Pitch::getParamMin(unsigned int aParamIndex)
{
    return 0;
}

Pitch::Pitch()
{
    for (int i = 0; i < 8; i++)
        mVolume[i] = 1;
}

SoLoud::FilterInstance *Pitch::createInstance()
{
    return new PitchInstance(this);
}

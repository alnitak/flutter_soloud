#ifndef SOLOUD_EQFILTER_H
#define SOLOUD_EQFILTER_H

#include "soloud.h"
#include "smbPitchShift.h"

class Pitch;

class PitchInstance : public SoLoud::FilterInstance
{
    enum FILTERATTRIBUTE
    {
        WET = 0,
        BAND1 = 1,
        BAND2 = 2,
        BAND3 = 3,
        BAND4 = 4,
        BAND5 = 5,
        BAND6 = 6,
        BAND7 = 7,
        BAND8 = 8
    };
    Pitch *mParent;
    CSmbPitchShift pitchShift;

public:
    virtual void filter(
        float *aBuffer,
        unsigned int aSamples,
        unsigned int aBufferSize,
        unsigned int aChannels,
        float aSamplerate,
        SoLoud::time aTime);
    PitchInstance(Pitch *aParent);
};

class Pitch : public SoLoud::Filter {

public:
    enum FILTERATTRIBUTE
    {
        WET = 0,
        BAND1 = 1,
        BAND2 = 2,
        BAND3 = 3,
        BAND4 = 4,
        BAND5 = 5,
        BAND6 = 6,
        BAND7 = 7,
        BAND8 = 8
    };
    virtual int getParamCount();
    virtual const char *getParamName(unsigned int aParamIndex);
    virtual unsigned int getParamType(unsigned int aParamIndex);
    virtual float getParamMax(unsigned int aParamIndex);
    virtual float getParamMin(unsigned int aParamIndex);
    float mVolume[8];
    SoLoud::result setParam(unsigned int aBand, float aVolume);
    virtual SoLoud::FilterInstance *createInstance();
    Pitch();
};

#endif
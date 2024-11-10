#ifndef LIMITER_H
#define LIMITER_H

#include "soloud.h"

class Limiter;

class LimiterInstance : public SoLoud::FilterInstance
{
    Limiter *mParent;
    // Helper parameters calculated from attack/release times
    float gAttackCoeff;
    float gReleaseCoeff;

public:
    virtual void filter(
        float *aBuffer,
        unsigned int aSamples,
        unsigned int aBufferSize,
        unsigned int aChannels,
        float aSamplerate,
        SoLoud::time aTime);
    LimiterInstance(Limiter *aParent);
    void setFilterParameter(unsigned int aAttributeId, float aValue);
};

class Limiter : public SoLoud::Filter
{

public:
    enum FILTERATTRIBUTE
    {
        WET = 0,
        THRESHOLD = 1,
        ATTACK_TIME = 2,
        RELEASE_TIME = 3,
        MAKEUP_GAIN = 4
    };
    float mWet;         // Wet/dry mix ratio, 1.0 means fully wet, 0.0 means fully dry
    float mThreshold;   // Threshold in dB, default -6 dB
    float mAttackTime;  // Attack time in seconds, default 10 ms
    float mReleaseTime; // Release time in seconds, default 100 ms
    float mMakeupGain;  // Makeup gain, default 1.0 (no gain adjustment)

    virtual int getParamCount();
    virtual const char *getParamName(unsigned int aParamIndex);
    virtual unsigned int getParamType(unsigned int aParamIndex);
    virtual float getParamMax(unsigned int aParamIndex);
    virtual float getParamMin(unsigned int aParamIndex);
    SoLoud::result setParam(unsigned int aParamIndex, float aValue);
    virtual SoLoud::FilterInstance *createInstance();
    Limiter();
};

#endif // LIMITER_H
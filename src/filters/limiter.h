#ifndef LIMITER_H
#define LIMITER_H

#include "soloud.h"

class Limiter;

class LimiterInstance : public SoLoud::FilterInstance
{
    Limiter *mParent;
    // Helper parameters calculated from attack/release times
    float gLookaheadSamples;
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
        MAKEUP_GAIN = 2,
        KNEE_WIDTH = 3,
        LOOKAHEAD = 4,
        RELEASE_TIME = 5
    };
    float mWet;         // Wet/dry mix ratio, 1.0 means fully wet, 0.0 means fully dry
    float mThreshold;   // Threshold in dB
    float mMakeupGain;  // Makeup gain, default 1.0 (no gain adjustment)
    float mKneeWidth;   // Knee width in dB
    float mLookahead;   // Lookahead in ms
    float mReleaseTime; // Release time in seconds

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
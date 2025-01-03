#ifndef LIMITER_H
#define LIMITER_H

#include "soloud.h"

class Limiter;

class LimiterInstance : public SoLoud::FilterInstance
{
    Limiter *mParent;

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
        RELEASE_TIME = 4
    };
    float mWet;         // Wet/dry mix ratio, 1.0 means fully wet, 0.0 means fully dry
    float mThreshold;   // The threshold in dB. Signals above this level are reduced in gain. A lower value means more aggressive limiting.
    float mMakeupGain;  // The make-up gain in dB applied after limiting to bring up the output level.
    float mKneeWidth;   // The width of the knee in dB. A larger value results in a softer transition into limiting.
    float mReleaseTime; // The release time in milliseconds. Determines how quickly the gain reduction recovers after a signal drops below the threshold.

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
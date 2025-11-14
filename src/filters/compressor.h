#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include "soloud.h"
#include <vector>

class Compressor;

class CompressorInstance : public SoLoud::FilterInstance
{
    Compressor *mParent;
    float attackCoef;
    float releaseCoef;
    std::vector<float> mEnvelope;

public:
    virtual void filter(
        float *aBuffer,
        unsigned int aSamples,
        unsigned int aBufferSize,
        unsigned int aChannels,
        float aSamplerate,
        SoLoud::time aTime);
    CompressorInstance(Compressor *aParent);
    void setFilterParameter(unsigned int aAttributeId, float aValue);
};

class Compressor : public SoLoud::Filter
{

public:
    enum FILTERATTRIBUTE
    {
        WET = 0,
        THRESHOLD = 1,
        MAKEUP_GAIN = 2,
        KNEE_WIDTH = 3,
        RATIO = 4,
        ATTACK_TIME = 5,
        RELEASE_TIME = 6
    };
    unsigned int mSamplerate; // the player samplerate
    float mWet;         // Mix between original (dry) and compressed (wet) signal. 0.0 = 100% dry, 1.0 = 100% wet.
    float mThreshold;   // The threshold in dB at which compression starts. Values lower than the threshold will be compressed.
    float mMakeupGain;  // The make-up gain in dB applied to the compressed signal to compensate for loss in volume due to compression.
    float mKneeWidth;   // The width in dB of the soft knee where compression smoothly begins to take effect. A larger value smooths compression.
    float mRatio;       // The compression ratio. The amount by which input exceeding the threshold will be reduced. For example, 4:1 reduces 4 dB of input to 1 dB.
    float mAttackTime;  // The time in ms for the compressor to react to a sudden increase in input level.
    float mReleaseTime; // The time in ms for the compressor to release the gain reduction after the input level falls below the threshold. 

    virtual int getParamCount();
    virtual const char *getParamName(unsigned int aParamIndex);
    virtual unsigned int getParamType(unsigned int aParamIndex);
    virtual float getParamMax(unsigned int aParamIndex);
    virtual float getParamMin(unsigned int aParamIndex);
    SoLoud::result setParam(unsigned int aParamIndex, float aValue);
    virtual SoLoud::FilterInstance *createInstance();
    Compressor(unsigned int samplerate);
};

#endif // COMPRESSOR_H
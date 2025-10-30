#ifndef PARAMETRIC_EQ3_FILTER_H
#define PARAMETRIC_EQ3_FILTER_H

#include "soloud.h"

class ParametricEq3;

#define EQ_BANDS 3

class ParametricEq3Instance : public SoLoud::FilterInstance
{
	ParametricEq3 *mParent;
	float* mBuffer;          // Temporary buffer for FFT processing
	float* mWindow;          // Window function coefficients
	unsigned int mBufferSize;
	unsigned int mBufferPos;
	float *mInputBuffer[MAX_CHANNELS];
	float *mMixBuffer[MAX_CHANNELS];
	float *mTemp;
	unsigned int mInputOffset[MAX_CHANNELS];
	unsigned int mMixOffset[MAX_CHANNELS];
	unsigned int mReadOffset[MAX_CHANNELS];
	
	// Helper functions for FFT processing
	void comp2MagPhase(float* aFFTBuffer, unsigned int aSamples);
	void magPhase2Comp(float* aFFTBuffer, unsigned int aSamples);
	
public:
	ParametricEq3Instance(ParametricEq3 *aParent);
	virtual ~ParametricEq3Instance();
	virtual void filterChannel(float *aBuffer, unsigned int aSamples, float aSamplerate, SoLoud::time aTime, unsigned int aChannel, unsigned int aChannels);
	virtual void fftFilterChannel(float *aFFTBuffer, unsigned int aSamples, float aSamplerate, SoLoud::time aTime, unsigned int aChannel, unsigned int aChannels);
};

class ParametricEq3 : public SoLoud::Filter
{
public:
	enum PARAMS
	{
		WET = 0,
		BASS,
		MID,
		TREBLE,
		PARAM_COUNT
	};

	// Store gain, freq, and Q for each band
	float mGain[EQ_BANDS];
	float mFreq[EQ_BANDS];
	float mQ[EQ_BANDS];

	ParametricEq3();
	virtual int getParamCount();
	virtual const char* getParamName(unsigned int aParamIndex);
	virtual unsigned int getParamType(unsigned int aParamIndex);
	virtual float getParamMax(unsigned int aParamIndex);
	virtual float getParamMin(unsigned int aParamIndex);
	SoLoud::result setParam(unsigned int aParamIndex, float aValue);
	virtual SoLoud::FilterInstance *createInstance();
};

#endif
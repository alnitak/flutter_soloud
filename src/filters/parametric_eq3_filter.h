#ifndef PARAMETRIC_EQ3_FILTER_H
#define PARAMETRIC_EQ3_FILTER_H

#include "soloud.h"
#include "../pffft/pffft.h"
#include <vector>
#include <string>

class ParametricEq3;

class ParametricEq3Instance : public SoLoud::FilterInstance
{
	ParametricEq3 *mParent;
	// float* mBuffer;          // Temporary buffer for FFT processing
	// float* mWindow;          // Window function coefficients
	// unsigned int mBufferSize;
	// unsigned int mBufferPos;
	float *mInputBuffer[MAX_CHANNELS];
	float *mMixBuffer[MAX_CHANNELS];
	float *mTemp;
    float* mFFTBuffer;  // Aligned buffer for PFFFT
    float* mFFTWork;    // Work buffer for PFFFT
    PFFFT_Setup* mFFTSetup;
	unsigned int mInputOffset[MAX_CHANNELS];
	unsigned int mMixOffset[MAX_CHANNELS];
	unsigned int mReadOffset[MAX_CHANNELS];

	// Band information precomputed for fast lookup
	int mBands;
	std::vector<float> mBandCenter;
	std::vector<float> mBandBoundary; // size = mBands+1, boundaries between bands
	
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
	// Wet param is index 0 in the filter param list; band gains start at index 1
	int mBands;                        // number of EQ bands (user configurable)
	std::vector<float> mGain;          // per-band gain
	std::vector<float> mFreq;          // per-band center frequency

	ParametricEq3(int channels, int bands = 3);
	virtual int getParamCount();
	virtual const char* getParamName(unsigned int aParamIndex);
	virtual unsigned int getParamType(unsigned int aParamIndex);
	virtual float getParamMax(unsigned int aParamIndex);
	virtual float getParamMin(unsigned int aParamIndex);
	SoLoud::result setParam(unsigned int aParamIndex, float aValue);
	virtual SoLoud::FilterInstance *createInstance();

	int mChannels;
};

#endif
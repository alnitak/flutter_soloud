#include <string.h>
#include <math.h>
#include "soloud.h"
#include "soloud_fft.h"
#include "parametric_eq3_filter.h"

#define STFT_WINDOW_SIZE 256
#define STFT_WINDOW_HALF (STFT_WINDOW_SIZE / 2)
#define STFT_WINDOW_TWICE (STFT_WINDOW_SIZE * 2)

ParametricEq3Instance::ParametricEq3Instance(ParametricEq3 *aParent)
{
    mParent = aParent;
    initParams(ParametricEq3::PARAM_COUNT);
    mParam[ParametricEq3::BASS] = aParent->mGain[0];
    mParam[ParametricEq3::MID] = aParent->mGain[1];
    mParam[ParametricEq3::TREBLE] = aParent->mGain[2];

	mInputBuffer[0] = 0;
	mMixBuffer[0] = 0;
	mTemp = 0;
	for (int i = 0; i < MAX_CHANNELS; i++)
	{
		mInputOffset[i] = STFT_WINDOW_SIZE;
		mMixOffset[i] = STFT_WINDOW_HALF;
		mReadOffset[i] = 0;
	}
}

void ParametricEq3Instance::comp2MagPhase(float* aFFTBuffer, unsigned int aSamples)
{
    for (unsigned int i = 0; i < aSamples; i++)
    {
        float re = aFFTBuffer[i * 2];
        float im = aFFTBuffer[i * 2 + 1];
        aFFTBuffer[i * 2] = sqrtf(re * re + im * im);
        aFFTBuffer[i * 2 + 1] = atan2f(im, re);
    }
}

void ParametricEq3Instance::magPhase2Comp(float* aFFTBuffer, unsigned int aSamples)
{
    for (unsigned int i = 0; i < aSamples; i++)
    {
        float mag = aFFTBuffer[i * 2];
        float phase = aFFTBuffer[i * 2 + 1];
        aFFTBuffer[i * 2] = mag * cosf(phase);
        aFFTBuffer[i * 2 + 1] = mag * sinf(phase);
    }
}

ParametricEq3Instance::~ParametricEq3Instance()
{
	for (int i = 0; i < MAX_CHANNELS; i++)
	{
		delete[] mInputBuffer[i];
		delete[] mMixBuffer[i];
	}
	delete[] mTemp;
}

void ParametricEq3Instance::filterChannel(float *aBuffer, unsigned int aSamples, float aSamplerate, SoLoud::time aTime, unsigned int aChannel, unsigned int aChannels)
{
    if (aChannel == 0)
	{
		updateParams(aTime);
	}

	if (mInputBuffer[aChannel] == 0)
	{
		mInputBuffer[aChannel] = new float[STFT_WINDOW_TWICE];
		mMixBuffer[aChannel] = new float[STFT_WINDOW_TWICE];
		mTemp = new float[STFT_WINDOW_SIZE];
		memset(mInputBuffer[aChannel], 0, sizeof(float) * STFT_WINDOW_TWICE);
		memset(mMixBuffer[aChannel], 0, sizeof(float) * STFT_WINDOW_TWICE);
	}

	unsigned int ofs = 0;
	unsigned int chofs = 0;
	unsigned int inputofs = mInputOffset[aChannel];
	unsigned int mixofs = mMixOffset[aChannel];
	unsigned int readofs = mReadOffset[aChannel];

	while (ofs < aSamples)
	{
		int samples = STFT_WINDOW_HALF - (inputofs & (STFT_WINDOW_HALF - 1));
		if (ofs + samples > aSamples) samples = aSamples - ofs;
		for (int i = 0; i < samples; i++)
		{
			mInputBuffer[aChannel][chofs + ((inputofs + STFT_WINDOW_HALF) & (STFT_WINDOW_TWICE - 1))] = aBuffer[ofs + i];
			mMixBuffer[aChannel][chofs + ((inputofs + STFT_WINDOW_HALF) & (STFT_WINDOW_TWICE - 1))] = 0;
			inputofs++;
		}
					
		if ((inputofs & (STFT_WINDOW_HALF - 1)) == 0)
		{
			for (int i = 0; i < STFT_WINDOW_SIZE; i++)
			{
				mTemp[i] = mInputBuffer[aChannel][chofs + ((inputofs + STFT_WINDOW_TWICE - STFT_WINDOW_HALF + i) & (STFT_WINDOW_TWICE - 1))];
			}
			
			SoLoud::FFT::fft(mTemp, STFT_WINDOW_SIZE);

			fftFilterChannel(mTemp, STFT_WINDOW_HALF, aSamplerate, aTime, aChannel, aChannels);

			SoLoud::FFT::ifft(mTemp, STFT_WINDOW_SIZE);
			
			for (int i = 0; i < STFT_WINDOW_SIZE; i++)
			{
				mMixBuffer[aChannel][chofs + (mixofs & (STFT_WINDOW_TWICE - 1))] += mTemp[i] * ((float)STFT_WINDOW_HALF - abs(STFT_WINDOW_HALF - i)) * (1.0f / (float)STFT_WINDOW_HALF);
				mixofs++;					
			}
			mixofs -= STFT_WINDOW_HALF;
		}
		
		for (int i = 0; i < samples; i++)
		{
			aBuffer[ofs + i] = mMixBuffer[aChannel][chofs + (readofs & (STFT_WINDOW_TWICE - 1))];
			readofs++;
		}
		
		ofs += samples;
	}
	mInputOffset[aChannel] = inputofs;
	mReadOffset[aChannel] = readofs;
	mMixOffset[aChannel] = mixofs;
}

void ParametricEq3Instance::fftFilterChannel(float *aFFTBuffer, unsigned int aSamples, float aSamplerate, SoLoud::time /*aTime*/, unsigned int /*aChannel*/, unsigned int /*aChannels*/)
{
    comp2MagPhase(aFFTBuffer, aSamples);

    float bass_gain = mParam[ParametricEq3::BASS];
    float mid_gain = mParam[ParametricEq3::MID];
    float treble_gain = mParam[ParametricEq3::TREBLE];

    float bass_freq_end = mParent->mFreq[0];
    float mid_freq_start = bass_freq_end;
    float mid_freq_end = mParent->mFreq[1];
    float treble_freq_start = mid_freq_end;

    for (unsigned int i = 0; i < aSamples; i++)
    {
        float current_freq = (float)i * aSamplerate / (float)(aSamples*2);
        
        float gain = 1.0f;
        if (current_freq <= bass_freq_end)
        {
            gain = bass_gain;
        }
        else if (current_freq > mid_freq_start && current_freq <= mid_freq_end)
        {
            gain = mid_gain;
        }
        else if (current_freq > treble_freq_start)
        {
            gain = treble_gain;
        }
        
        aFFTBuffer[i*2] *= gain;
    }

    magPhase2Comp(aFFTBuffer, aSamples);
}

SoLoud::result ParametricEq3::setParam(unsigned int aParamIndex, float aValue)
{
	if (aParamIndex < 1 || aParamIndex > 3) return SoLoud::INVALID_PARAMETER;

	mGain[aParamIndex - 1] = aValue;
	return SoLoud::SO_NO_ERROR;
}

int ParametricEq3::getParamCount()
{
	return PARAM_COUNT;
}

const char* ParametricEq3::getParamName(unsigned int aParamIndex)
{
	switch (aParamIndex)
	{
	case BASS: return "Bass";
	case MID: return "Mid";
	case TREBLE: return "Treble";
	}
	return "Wet";
}

unsigned int ParametricEq3::getParamType(unsigned int aParamIndex)
{
	return FLOAT_PARAM;
}

float ParametricEq3::getParamMax(unsigned int aParamIndex)
{
	if (aParamIndex == 0) return 1; 
	return 2.0f;
}

float ParametricEq3::getParamMin(unsigned int aParamIndex)
{
	if (aParamIndex == 0) return 0;
	return 0.0f;
}

ParametricEq3::ParametricEq3()
{
	mGain[0] = 1.0f;
	mFreq[0] = 250.0f;
	mQ[0] = 0.707f;

	mGain[1] = 1.0f;
	mFreq[1] = 2000.0f;
	mQ[1] = 1.0f;

	mGain[2] = 1.0f;
	mFreq[2] = 6000.0f;
	mQ[2] = 0.707f;
}

SoLoud::FilterInstance *ParametricEq3::createInstance()
{
	return new ParametricEq3Instance(this);
}
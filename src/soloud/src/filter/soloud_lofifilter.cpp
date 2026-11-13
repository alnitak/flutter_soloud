/*
SoLoud audio engine
Copyright (c) 2013-2020 Jari Komppa

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#include <math.h>
#include <string.h>
#include "soloud.h"
#include "soloud_lofifilter.h"

namespace SoLoud
{

	LofiFilterInstance::LofiFilterInstance(LofiFilter *aParent)
	{
		mParent = aParent;
		initParams(3);
		mParam[SAMPLERATE] = aParent->mSampleRate;
		mParam[BITDEPTH] = aParent->mBitdepth;
		for (int i = 0; i < MAX_CHANNELS; i++)
		{
			mChannelData[i].mSample = 0;
			mChannelData[i].mSamplesToSkip = 0;
		}
	}

	void LofiFilterInstance::filterChannel(float *aBuffer, unsigned int aSamples, float aSamplerate, double aTime, unsigned int aChannel, unsigned int /*aChannels*/)
	{
		if (aChannel >= MAX_CHANNELS)
			return;
		updateParams(aTime);

		const float sr = mParam[SAMPLERATE] > 1.0f ? mParam[SAMPLERATE] : 1.0f;
		const float skipStep = (aSamplerate / sr) - 1.0f;
		const float q = exp2f(mParam[BITDEPTH]);
		const float invQ = 1.0f / q;
		const float wet = mParam[WET];
		float curSample = mChannelData[aChannel].mSample;
		float toSkip = mChannelData[aChannel].mSamplesToSkip;

		for (unsigned int i = 0; i < aSamples; i++)
		{
			if (toSkip <= 0.0f)
			{
				toSkip += skipStep;
				curSample = floorf(q * aBuffer[i]) * invQ;
			}
			else
			{
				toSkip -= 1.0f;
			}
			aBuffer[i] += (curSample - aBuffer[i]) * wet;
		}

		mChannelData[aChannel].mSample = curSample;
		mChannelData[aChannel].mSamplesToSkip = toSkip;
	}

	LofiFilterInstance::~LofiFilterInstance()
	{
	}

	LofiFilter::LofiFilter()
	{
		setParams(4000, 3);
	}

	result LofiFilter::setParams(float aSampleRate, float aBitdepth)
	{
		if (aSampleRate <= 0 || aBitdepth <= 0)
			return INVALID_PARAMETER;

		mSampleRate = aSampleRate;
		mBitdepth = aBitdepth;
		return 0;
	}

	LofiFilter::~LofiFilter()
	{
	}

	int LofiFilter::getParamCount()
	{
		return 3;
	}

	const char* LofiFilter::getParamName(unsigned int aParamIndex)
	{
		if (aParamIndex > 2)
			return 0;
		const char *names[3] = {
			"Wet",
			"Samplerate",
			"Bitdepth"
		};
		return names[aParamIndex];
	}

	unsigned int LofiFilter::getParamType(unsigned int aParamIndex)
	{
		return FLOAT_PARAM;
	}

	float LofiFilter::getParamMax(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case SAMPLERATE: return 22000;
		case BITDEPTH: return 16;
		}
		return 1;
	}

	float LofiFilter::getParamMin(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case SAMPLERATE: return 100;
		case BITDEPTH: return 0.5;
		}
		return 0;
	}

	LofiFilterInstance *LofiFilter::createInstance()
	{
		return new LofiFilterInstance(this);
	}
}

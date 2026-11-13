/*
SoLoud audio engine
Copyright (c) 2020 Jari Komppa

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

#include <string.h>
#include "soloud.h"
#include "soloud_misc.h"
#include "soloud_robotizefilter.h"

namespace SoLoud
{
	RobotizeFilterInstance::RobotizeFilterInstance(RobotizeFilter *aParent)
	{
		mParent = aParent;
		initParams(3);
		mParam[FREQ] = aParent->mFreq;
		mParam[WAVE] = (float)aParent->mWave;
	}

	void RobotizeFilterInstance::filterChannel(float *aBuffer, unsigned int aSamples, float aSamplerate, time aTime, unsigned int aChannel, unsigned int /*aChannels*/)
	{
		if (aChannel == 0)
		{
			updateParams(aTime);
		}
		const float freq = mParam[FREQ] > 0.1f ? mParam[FREQ] : 0.1f;
		int period = (int)(aSamplerate / freq);
		if (period < 1)
			period = 1;
		int pos = (int)(aTime * aSamplerate) % period;
		if (pos < 0)
			pos += period;
		const float invPeriod = 1.0f / (float)period;
		const int wave = (int)mParam[WAVE];
		const float wet = mParam[WET];

		for (unsigned int i = 0; i < aSamples; i++)
		{
			const float dry = aBuffer[i];
			const float wpos = (float)pos * invPeriod;
			const float wetSample = dry * (SoLoud::Misc::generateWaveform(wave, wpos) + 0.5f);
			aBuffer[i] = dry + (wetSample - dry) * wet;
			if (++pos >= period)
				pos = 0;
		}
	}

	RobotizeFilter::RobotizeFilter()
	{
		mFreq = 30;
		mWave = 0;
	}

	void RobotizeFilter::setParams(float aFreq, int aWaveform)
	{
		mFreq = aFreq;
		mWave = aWaveform;
	}

	int RobotizeFilter::getParamCount()
	{
		return 3;
	}

	const char* RobotizeFilter::getParamName(unsigned int aParamIndex)
	{
		if (aParamIndex > 2)
			return 0;
		const char* names[3] = {
			"Wet",
			"Frequency",
			"Waveform"
		};
		return names[aParamIndex];
	}

	unsigned int RobotizeFilter::getParamType(unsigned int aParamIndex)
	{
		if (aParamIndex == WAVE)
			return INT_PARAM;
		return FLOAT_PARAM;
	}

	float RobotizeFilter::getParamMax(unsigned int aParamIndex)
	{
		if (aParamIndex == WAVE)
			return 6;
		if (aParamIndex == FREQ)
			return 100;
		return 1;
	}

	float RobotizeFilter::getParamMin(unsigned int aParamIndex)
	{
		if (aParamIndex == FREQ)
			return 0.1f;
		return 0;
	}

	FilterInstance *RobotizeFilter::createInstance()
	{
		return new RobotizeFilterInstance(this);
	}
}

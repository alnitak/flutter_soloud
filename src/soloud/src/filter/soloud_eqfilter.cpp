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
#include "soloud_eqfilter.h"
#include "../../../simd/soloud_simd.h"


namespace SoLoud
{
	
	EqFilterInstance::EqFilterInstance(EqFilter *aParent)
	{
		mParent = aParent;
		initParams(9);
		mParam[BAND1] = aParent->mVolume[BAND1 - BAND1];
		mParam[BAND2] = aParent->mVolume[BAND2 - BAND1];
		mParam[BAND3] = aParent->mVolume[BAND3 - BAND1];
		mParam[BAND4] = aParent->mVolume[BAND4 - BAND1];
		mParam[BAND5] = aParent->mVolume[BAND5 - BAND1];
		mParam[BAND6] = aParent->mVolume[BAND6 - BAND1];
		mParam[BAND7] = aParent->mVolume[BAND7 - BAND1];
		mParam[BAND8] = aParent->mVolume[BAND8 - BAND1];
	}

	/// Already defined in soloud.cpp:945
	float EqFilterInstance::catmullrom(float t, float p0, float p1, float p2, float p3)
	{
		return 0.5f * (
			(2 * p1) +
			(-p0 + p2) * t +
			(2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
			(-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t
			);
	}


	void EqFilterInstance::fftFilterChannel(float *aFFTBuffer, unsigned int aSamples, float /*aSamplerate*/, time /*aTime*/, unsigned int /*aChannel*/, unsigned int /*aChannels*/)
	{
		// Scaling interleaved (re, im) directly by 2 * gain is algebraically
		// identical to comp2MagPhase -> *= gain -> magPhase2Comp while avoiding
		// per-bin sqrt/atan2/sin/cos transcendentals.
		const unsigned int halfBins = aSamples / 2;
		const unsigned int bandWidth = aSamples / 16;
		if (halfBins == 0 || bandWidth == 0)
		{
			memset(aFFTBuffer + aSamples, 0, sizeof(float) * aSamples);
			return;
		}
		const float invHalf = 1.0f / (float)halfBins;
		const float invBandWidth = 1.0f / (float)bandWidth;
		unsigned int p = 0;
		for (; p + 2 <= halfBins; p += 2)
		{
			float g[2];
			for (unsigned int k = 0; k < 2; k++)
			{
				const unsigned int bin = p + k;
				const int i = (int)floorf(sqrtf((float)bin * invHalf) * (float)halfBins);
				int p2 = (int)(i / (int)bandWidth);
				int p1 = p2 - 1;
				int p0 = p1 - 1;
				int p3 = p2 + 1;
				if (p1 < 0) p1 = 0;
				if (p0 < 0) p0 = 0;
				if (p2 > 7) p2 = 7;
				if (p3 > 7) p3 = 7;
				const float v = (float)(i % (int)bandWidth) * invBandWidth;
				g[k] = 2.0f * catmullrom(v, mParam[p0 + 1], mParam[p1 + 1], mParam[p2 + 1], mParam[p3 + 1]);
			}
			const auto vGain = soloud_simd::f32x4::make(g[0], g[0], g[1], g[1]);
			soloud_simd::mul(soloud_simd::f32x4::loadu(aFFTBuffer + p * 2), vGain)
				.storeu(aFFTBuffer + p * 2);
		}
		for (; p < halfBins; p++)
		{
			const int i = (int)floorf(sqrtf((float)p * invHalf) * (float)halfBins);
			int p2 = (int)(i / (int)bandWidth);
			int p1 = p2 - 1;
			int p0 = p1 - 1;
			int p3 = p2 + 1;
			if (p1 < 0) p1 = 0;
			if (p0 < 0) p0 = 0;
			if (p2 > 7) p2 = 7;
			if (p3 > 7) p3 = 7;
			const float v = (float)(i % (int)bandWidth) * invBandWidth;
			const float gain = 2.0f * catmullrom(v, mParam[p0 + 1], mParam[p1 + 1], mParam[p2 + 1], mParam[p3 + 1]);
			aFFTBuffer[p * 2] *= gain;
			aFFTBuffer[p * 2 + 1] *= gain;
		}
		memset(aFFTBuffer + aSamples, 0, sizeof(float) * aSamples);
	}

	result EqFilter::setParam(unsigned int aBand, float aVolume)
	{
		if (aBand < BAND1 || aBand > BAND8) return INVALID_PARAMETER;
		if (aVolume < getParamMin(BAND1) || aVolume > getParamMax(BAND1))
			return INVALID_PARAMETER;

		mVolume[aBand - BAND1] = aVolume;
		return SO_NO_ERROR;
	}

	int EqFilter::getParamCount()
	{
		return 9;
	}

	const char* EqFilter::getParamName(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case BAND1: return "Band 1";
		case BAND2: return "Band 2";
		case BAND3: return "Band 3";
		case BAND4: return "Band 4";
		case BAND5: return "Band 5";
		case BAND6: return "Band 6";
		case BAND7: return "Band 7";
		case BAND8: return "Band 8";
		}
		return "Wet";
	}

	unsigned int EqFilter::getParamType(unsigned int aParamIndex)
	{
		return FLOAT_PARAM;
	}

	float EqFilter::getParamMax(unsigned int aParamIndex)
	{
		if (aParamIndex == 0)
			return 1;
		return 4;
	}

	float EqFilter::getParamMin(unsigned int aParamIndex)
	{
		return 0;
	}

	EqFilter::EqFilter()
	{
		for (int i = 0; i < 8; i++)
			mVolume[i] = 1;
	}

	FilterInstance *EqFilter::createInstance()
	{
		return new EqFilterInstance(this);
	}
}

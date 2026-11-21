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
#include "soloud_waveshaperfilter.h"
#include "../../../simd/soloud_simd.h"

namespace SoLoud
{

	WaveShaperFilterInstance::WaveShaperFilterInstance(WaveShaperFilter *aParent)
	{
		mParent = aParent;
		initParams(2);
		mParam[WaveShaperFilter::AMOUNT] = mParent->mAmount;
	}

	void WaveShaperFilterInstance::filterChannel(float *aBuffer, unsigned int aSamples, float /*aSamplerate*/, double aTime, unsigned int aChannel, unsigned int /*aChannels*/)
	{
		if (aChannel == 0)
			updateParams(aTime);

		float k = 0;
		if (mParam[1] == 1)
			k = 2 * mParam[WaveShaperFilter::AMOUNT] / 0.01f;
		else
			k = 2 * mParam[WaveShaperFilter::AMOUNT] / (1 - mParam[1]);

		// flutter_soloud: AMOUNT = -1 gives k = -1, where the curve is 0/0 at
		// |x| = 1. Keep 1 + k above 0 so the denominator below stays > 0.
		if (k < -0.999f)
			k = -0.999f;
		// flutter_soloud: with a negative AMOUNT (k < 0) the denominator
		// 1 + k|x| reaches 0 for |x| = -1/k > 1, so the input is shaped
		// clamped to +-1 and the excess passes through. The curve maps +-1 to
		// +-1, so this is unchanged for |x| <= 1 and continuous beyond. A
		// positive AMOUNT keeps the plain curve (it is finite everywhere).
		const float limit = k < 0 ? 1.0f : 3.402823e38f;

		const float onePlusK = 1.0f + k;
		const float wetMix = mParam[WaveShaperFilter::WET];
		const auto vOne = soloud_simd::f32x4::splat(1.0f);
		const auto vLimit = soloud_simd::f32x4::splat(limit);
		const auto vMinLimit = soloud_simd::f32x4::splat(-limit);
		const auto vK = soloud_simd::f32x4::splat(k);
		const auto vOnePlusK = soloud_simd::f32x4::splat(onePlusK);
		const auto vWet = soloud_simd::f32x4::splat(wetMix);

		unsigned int i = 0;
		for (; i + 4 <= aSamples; i += 4)
		{
			const auto dry = soloud_simd::f32x4::loadu(aBuffer + i);
			const auto x = soloud_simd::clamp(dry, vMinLimit, vLimit);
			const auto num = soloud_simd::mul(vOnePlusK, x);
			const auto den = soloud_simd::add(vOne, soloud_simd::mul(vK, soloud_simd::abs(x)));
			const auto wet = soloud_simd::add(soloud_simd::div(num, den), soloud_simd::sub(dry, x));
			soloud_simd::add(dry, soloud_simd::mul(soloud_simd::sub(wet, dry), vWet))
				.storeu(aBuffer + i);
		}
		for (; i < aSamples; i++)
		{
			const float dry = aBuffer[i];
			const float x = dry < -limit ? -limit : (dry > limit ? limit : dry);
			const float wet = onePlusK * x / (1.0f + k * fabsf(x)) + (dry - x);
			aBuffer[i] = dry + (wet - dry) * wetMix;
		}
	}

	WaveShaperFilterInstance::~WaveShaperFilterInstance()
	{
	}

	result WaveShaperFilter::setParams(float aAmount)
	{
		if (aAmount < -1 || aAmount > 1)
			return INVALID_PARAMETER;
		mAmount = aAmount;
		return 0;
	}

	WaveShaperFilter::WaveShaperFilter()
	{
		mAmount = 0.0f;
	}

	int WaveShaperFilter::getParamCount()
	{
		return 2;
	}

	const char* WaveShaperFilter::getParamName(unsigned int aParamIndex)
	{
		if (aParamIndex == 1)
			return "Amount";
		return "Wet";
	}

	unsigned int WaveShaperFilter::getParamType(unsigned int aParamIndex)
	{
		return FLOAT_PARAM;
	}

	float WaveShaperFilter::getParamMax(unsigned int aParamIndex)
	{
		return 1;
	}

	float WaveShaperFilter::getParamMin(unsigned int aParamIndex)
	{
		if (aParamIndex == AMOUNT)
			return -1;
		return 0;
	}

	WaveShaperFilter::~WaveShaperFilter()
	{
	}

	WaveShaperFilterInstance *WaveShaperFilter::createInstance()
	{
		return new WaveShaperFilterInstance(this);
	}
}

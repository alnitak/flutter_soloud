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

--

Based on "Using the Biquad Resonant Filter", 
Phil Burk, Game Programming Gems 3, p. 606
*/

#include <math.h>
#include <string.h>
#include "soloud.h"
#include "soloud_biquadresonantfilter.h"
#include "../../../simd/soloud_simd.h"

namespace SoLoud
{
	void BiquadResonantFilterInstance::calcBQRParams()
	{
		mDirty = 0;

		float omega = (float)((2.0f * M_PI * mParam[FREQUENCY]) / mSamplerate);
		float sin_omega = sinf(omega);
		float cos_omega = cosf(omega);
		float alpha = sin_omega / (2.0f * mParam[RESONANCE]);
		float scalar = 1.0f / (1.0f + alpha);

		switch ((int)(mParam[TYPE]))
		{
		default:
		case BiquadResonantFilter::LOWPASS:
			mA0 = 0.5f * (1.0f - cos_omega) * scalar;
			mA1 = (1.0f - cos_omega) * scalar;
			mA2 = mA0;
			mB1 = -2.0f * cos_omega * scalar;
			mB2 = (1.0f - alpha) * scalar;
			break;
		case BiquadResonantFilter::HIGHPASS:
			mA0 = 0.5f * (1.0f + cos_omega) * scalar;
			mA1 = -(1.0f + cos_omega) * scalar;
			mA2 = mA0;
			mB1 = -2.0f * cos_omega * scalar;
			mB2 = (1.0f - alpha) * scalar;
			break;
		case BiquadResonantFilter::BANDPASS:
			mA0 = alpha * scalar;
			mA1 = 0;
			mA2 = -mA0;
			mB1 = -2.0f * cos_omega * scalar;
			mB2 = (1.0f - alpha) * scalar;
			break;
		}
	}


	BiquadResonantFilterInstance::BiquadResonantFilterInstance(BiquadResonantFilter *aParent)
	{
		int i;
		for (i = 0; i < 8; i++)
		{
			mState[i].mX1 = 0;
			mState[i].mY1 = 0;
			mState[i].mX2 = 0;
			mState[i].mY2 = 0;
		}

		mParent = aParent;

		initParams(4);
		
		mParam[RESONANCE] = aParent->mResonance;
		mParam[FREQUENCY] = aParent->mFrequency;
		mParam[TYPE] = (float)aParent->mFilterType;
		
		mSamplerate = 44100;

		calcBQRParams();
	}

	void BiquadResonantFilterInstance::filter(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize, unsigned int aChannels, float aSamplerate, time aTime)
	{
		if (aChannels == 0 || aSamples == 0)
			return;

		// Cross-channel SIMD path for stereo (2ch) and quad (4ch): advances all
		// channels simultaneously in one f32x4 register, breaking the serial IIR
		// dependency across channels while preserving exact per-channel state.
		if (aChannels == 2 || aChannels == 4)
		{
			soloud_simd::ScopedFlushDenormals flushDenormals;
			updateParams(aTime);
			if (mParamChanged & ((1 << FREQUENCY) | (1 << RESONANCE) | (1 << TYPE)) || aSamplerate != mSamplerate)
			{
				mSamplerate = aSamplerate;
				calcBQRParams();
			}
			mParamChanged = 0;

			const auto vA0 = soloud_simd::f32x4::splat(mA0);
			const auto vA1 = soloud_simd::f32x4::splat(mA1);
			const auto vA2 = soloud_simd::f32x4::splat(mA2);
			const auto vB1 = soloud_simd::f32x4::splat(mB1);
			const auto vB2 = soloud_simd::f32x4::splat(mB2);
			const auto vWet = soloud_simd::f32x4::splat(mParam[WET]);

			if (aChannels == 2)
			{
				float *ch0 = aBuffer;
				float *ch1 = aBuffer + aBufferSize;
				auto x1 = soloud_simd::f32x4::make(mState[0].mX1, mState[1].mX1, 0.0f, 0.0f);
				auto x2 = soloud_simd::f32x4::make(mState[0].mX2, mState[1].mX2, 0.0f, 0.0f);
				auto y1 = soloud_simd::f32x4::make(mState[0].mY1, mState[1].mY1, 0.0f, 0.0f);
				auto y2 = soloud_simd::f32x4::make(mState[0].mY2, mState[1].mY2, 0.0f, 0.0f);
				float out[4];
				for (unsigned int i = 0; i < aSamples; i++)
				{
					const auto x = soloud_simd::f32x4::make(ch0[i], ch1[i], 0.0f, 0.0f);
					const auto acc = soloud_simd::sub(
						soloud_simd::sub(
							soloud_simd::add(soloud_simd::add(soloud_simd::mul(vA0, x), soloud_simd::mul(vA1, x1)), soloud_simd::mul(vA2, x2)),
							soloud_simd::mul(vB1, y1)),
						soloud_simd::mul(vB2, y2));
					soloud_simd::add(x, soloud_simd::mul(soloud_simd::sub(acc, x), vWet)).storeu(out);
					ch0[i] = out[0];
					ch1[i] = out[1];
					x2 = x1;
					x1 = x;
					y2 = y1;
					y1 = acc;
				}
				float sx1[4], sx2[4], sy1[4], sy2[4];
				x1.storeu(sx1); x2.storeu(sx2); y1.storeu(sy1); y2.storeu(sy2);
				for (unsigned int ch = 0; ch < 2; ch++)
				{
					mState[ch].mX1 = sx1[ch];
					mState[ch].mX2 = sx2[ch];
					mState[ch].mY1 = sy1[ch];
					mState[ch].mY2 = sy2[ch];
				}
				return;
			}

			float *ch0 = aBuffer;
			float *ch1 = aBuffer + aBufferSize;
			float *ch2 = aBuffer + aBufferSize * 2;
			float *ch3 = aBuffer + aBufferSize * 3;
			auto x1 = soloud_simd::f32x4::make(mState[0].mX1, mState[1].mX1, mState[2].mX1, mState[3].mX1);
			auto x2 = soloud_simd::f32x4::make(mState[0].mX2, mState[1].mX2, mState[2].mX2, mState[3].mX2);
			auto y1 = soloud_simd::f32x4::make(mState[0].mY1, mState[1].mY1, mState[2].mY1, mState[3].mY1);
			auto y2 = soloud_simd::f32x4::make(mState[0].mY2, mState[1].mY2, mState[2].mY2, mState[3].mY2);
			float out[4];
			for (unsigned int i = 0; i < aSamples; i++)
			{
				const auto x = soloud_simd::f32x4::make(ch0[i], ch1[i], ch2[i], ch3[i]);
				const auto acc = soloud_simd::sub(
					soloud_simd::sub(
						soloud_simd::add(soloud_simd::add(soloud_simd::mul(vA0, x), soloud_simd::mul(vA1, x1)), soloud_simd::mul(vA2, x2)),
						soloud_simd::mul(vB1, y1)),
					soloud_simd::mul(vB2, y2));
				soloud_simd::add(x, soloud_simd::mul(soloud_simd::sub(acc, x), vWet)).storeu(out);
				ch0[i] = out[0];
				ch1[i] = out[1];
				ch2[i] = out[2];
				ch3[i] = out[3];
				x2 = x1;
				x1 = x;
				y2 = y1;
				y1 = acc;
			}
			float sx1[4], sx2[4], sy1[4], sy2[4];
			x1.storeu(sx1); x2.storeu(sx2); y1.storeu(sy1); y2.storeu(sy2);
			for (unsigned int ch = 0; ch < 4; ch++)
			{
				mState[ch].mX1 = sx1[ch];
				mState[ch].mX2 = sx2[ch];
				mState[ch].mY1 = sy1[ch];
				mState[ch].mY2 = sy2[ch];
			}
			return;
		}

		FilterInstance::filter(aBuffer, aSamples, aBufferSize, aChannels, aSamplerate, aTime);
	}

	void BiquadResonantFilterInstance::filterChannel(float *aBuffer, unsigned int aSamples, float aSamplerate, double aTime, unsigned int aChannel, unsigned int /*aChannels*/)
	{
		if (aChannel >= 8)
			return;
		soloud_simd::ScopedFlushDenormals flushDenormals;
		unsigned int osamples = aSamples;
		if (aChannel == 0)
		{
			updateParams(aTime);

			if (mParamChanged & ((1 << FREQUENCY) | (1 << RESONANCE) | (1 << TYPE)) || aSamplerate != mSamplerate)
			{
				mSamplerate = aSamplerate;
				calcBQRParams();
			}
			mParamChanged = 0;			
		}		
		float x;
		unsigned int i;
		int c = 0;
		const float wet = mParam[WET];

		BQRStateData &s = mState[aChannel];

		// Process sample pairs first using permuted state updates
		aSamples = aSamples & ~1u; 

		for (i = 0; i < aSamples; i += 2, c++)
		{
			// Generate outputs by filtering inputs.
			x = aBuffer[c];
			s.mY2 = (mA0 * x) + (mA1 * s.mX1) + (mA2 * s.mX2) - (mB1 * s.mY1) - (mB2 * s.mY2);
			aBuffer[c] += (s.mY2 - aBuffer[c]) * wet;

			c++;

			// Permute filter operations to reduce data movement.
			// Just substitute variables instead of doing mX1=x, etc.
			s.mX2 = aBuffer[c];
			s.mY1 = (mA0 * s.mX2) + (mA1 * x) + (mA2 * s.mX1) - (mB1 * s.mY2) - (mB2 * s.mY1);
			aBuffer[c] += (s.mY1 - aBuffer[c]) * wet;

			// Only move a little data.
			s.mX1 = s.mX2;
			s.mX2 = x;
		}
		// Properly filter any trailing odd sample instead of duplicating c - 1.
		if (osamples != aSamples)
		{
			x = aBuffer[c];
			const float y = (mA0 * x) + (mA1 * s.mX1) + (mA2 * s.mX2) - (mB1 * s.mY1) - (mB2 * s.mY2);
			aBuffer[c] += (y - aBuffer[c]) * wet;
			s.mX2 = s.mX1;
			s.mX1 = x;
			s.mY2 = s.mY1;
			s.mY1 = y;
		}
	}


	struct BQRFilterSnapshot : public FilterStateSnapshot
	{
		BQRStateData mState[8];
		float mA0, mA1, mA2, mB1, mB2;
		int mDirty;
		float mSamplerate;
		uint64_t mParamChanged;
		float mParam[4];
		Fader mParamFader[4];
	};

	static const char kBiquadSnapshotKind = 0;

	FilterStateSnapshot *BiquadResonantFilterInstance::captureState()
	{
		BQRFilterSnapshot *s = new BQRFilterSnapshot();
		s->mKind = &kBiquadSnapshotKind;
		captureStateInto(s);
		return s;
	}

	bool BiquadResonantFilterInstance::captureStateInto(FilterStateSnapshot *aSnapshot)
	{
		if (aSnapshot == NULL || aSnapshot->mKind != &kBiquadSnapshotKind)
			return false;
		BQRFilterSnapshot *s = static_cast<BQRFilterSnapshot *>(aSnapshot);
		int i;
		for (i = 0; i < 8; i++)
			s->mState[i] = mState[i];
		s->mA0 = mA0;
		s->mA1 = mA1;
		s->mA2 = mA2;
		s->mB1 = mB1;
		s->mB2 = mB2;
		s->mDirty = mDirty;
		s->mSamplerate = mSamplerate;
		s->mParamChanged = mParamChanged;
		for (i = 0; i < 4; i++)
		{
			s->mParam[i] = mParam[i];
			s->mParamFader[i] = mParamFader[i];
		}
		return true;
	}

	void BiquadResonantFilterInstance::restoreState(FilterStateSnapshot *aSnapshot)
	{
		BQRFilterSnapshot *s = static_cast<BQRFilterSnapshot *>(aSnapshot);
		int i;
		for (i = 0; i < 8; i++)
			mState[i] = s->mState[i];
		mA0 = s->mA0;
		mA1 = s->mA1;
		mA2 = s->mA2;
		mB1 = s->mB1;
		mB2 = s->mB2;
		mDirty = s->mDirty;
		mSamplerate = s->mSamplerate;
		mParamChanged = s->mParamChanged;
		for (i = 0; i < 4; i++)
		{
			mParam[i] = s->mParam[i];
			mParamFader[i] = s->mParamFader[i];
		}
	}

	BiquadResonantFilterInstance::~BiquadResonantFilterInstance()
	{
	}

	BiquadResonantFilter::BiquadResonantFilter()
	{
		setParams(LOWPASS, 1000, 2);
	}

	result BiquadResonantFilter::setParams(int aType, float aFrequency, float aResonance)
	{
		if (aType < 0 || aType > 3 || aFrequency <= 0 || aResonance <= 0)
			return INVALID_PARAMETER;

		mFilterType = aType;
		mFrequency = aFrequency;
		mResonance = aResonance;

		return 0;
	}

	int BiquadResonantFilter::getParamCount()
	{
		return 4;
	}

	const char* BiquadResonantFilter::getParamName(unsigned int aParamIndex)
	{
		if (aParamIndex > 3)
			return 0;

		const char* name[4] = {
			"Wet",
			"Type",
			"Frequency",
			"Resonance"
		};
		return name[aParamIndex];
	}
	
	unsigned int BiquadResonantFilter::getParamType(unsigned int aParamIndex)
	{
		if (aParamIndex == TYPE)
			return INT_PARAM;
		return FLOAT_PARAM;
	}

	float BiquadResonantFilter::getParamMax(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case WET: return 1;
		case TYPE: return 2;
		case FREQUENCY: return 8000;
		case RESONANCE: return 20;
		}
		return 1;
	}
	
	float BiquadResonantFilter::getParamMin(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case FREQUENCY: return 10;
		case RESONANCE: return 0.1f;
		}
		return 0;
	}


	BiquadResonantFilter::~BiquadResonantFilter()
	{
	}


	BiquadResonantFilterInstance *BiquadResonantFilter::createInstance()
	{
		return new BiquadResonantFilterInstance(this);
	}
}

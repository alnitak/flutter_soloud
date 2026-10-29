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

#include <string.h>
#include "soloud.h"
#include "soloud_echofilter.h"

namespace SoLoud
{
	EchoFilterInstance::EchoFilterInstance(EchoFilter *aParent)
	{
		mBuffer = 0;
		mBufferLength = 0;
		mBufferMaxLength = 0;
		mOffset = 0;
		initParams(4);
		mParam[EchoFilter::DELAY] = aParent->mDelay;
		mParam[EchoFilter::DECAY] = aParent->mDecay;
		mParam[EchoFilter::FILTER] = aParent->mFilter;
	}

	void EchoFilterInstance::filter(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize, unsigned int aChannels, float aSamplerate, double aTime)
	{
		updateParams(aTime);

		float delay = mParam[EchoFilter::DELAY];
		if (delay < 0.001f)
			delay = 0.001f;
		if (delay > 10.0f)
			delay = 10.0f;

		int reqSamples = (int)ceil(delay * aSamplerate);
		if (reqSamples < 1)
			reqSamples = 1;

		if (mBuffer == 0 || reqSamples > mBufferMaxLength)
		{
			delete[] mBuffer;
			mBufferMaxLength = reqSamples;
			mBuffer = new float[mBufferMaxLength * aChannels];
			if (mBuffer == 0)
			{
				mBufferMaxLength = 0;
				mBufferLength = 0;
				return;
			}
			memset(mBuffer, 0, sizeof(float) * mBufferMaxLength * aChannels);
			mOffset = 0;
		}

		mBufferLength = reqSamples;
		if (mBufferLength > mBufferMaxLength)
			mBufferLength = mBufferMaxLength;
		if (mBufferLength < 1)
			mBufferLength = 1;

		mOffset %= mBufferLength;

		unsigned int i, j;
		int prevofs = (mOffset + mBufferLength - 1) % mBufferLength;
		for (i = 0; i < aSamples; i++)
		{
			for (j = 0; j < aChannels; j++)
			{
				int chofs = j * mBufferLength;
				int bchofs = j * aBufferSize;
				
				mBuffer[mOffset + chofs] = mParam[EchoFilter::FILTER] * mBuffer[prevofs + chofs] + (1 - mParam[EchoFilter::FILTER]) * mBuffer[mOffset + chofs];
				
				float n = aBuffer[i + bchofs] + mBuffer[mOffset + chofs] * mParam[EchoFilter::DECAY];
				mBuffer[mOffset + chofs] = n;

				aBuffer[i + bchofs] += (n - aBuffer[i + bchofs]) * mParam[EchoFilter::WET];
			}
			prevofs = mOffset;
			mOffset = (mOffset + 1) % mBufferLength;
		}
	}

	struct EchoFilterSnapshot : public FilterStateSnapshot
	{
		float *mBuffer;
		int mBufferLength;
		int mBufferMaxLength;
		int mOffset;
		uint64_t mParamChanged;
		float mParam[4];
		Fader mParamFader[4];
		EchoFilterSnapshot() : mBuffer(0), mBufferLength(0), mBufferMaxLength(0), mOffset(0), mParamChanged(0) {}
		virtual ~EchoFilterSnapshot() { delete[] mBuffer; }
	};

	FilterStateSnapshot *EchoFilterInstance::captureState()
	{
		EchoFilterSnapshot *s = new EchoFilterSnapshot();
		s->mBufferLength = mBufferLength;
		s->mBufferMaxLength = mBufferMaxLength;
		s->mOffset = mOffset;
		s->mParamChanged = mParamChanged;
		for (int i = 0; i < 4; i++)
		{
			s->mParam[i] = mParam[i];
			s->mParamFader[i] = mParamFader[i];
		}
		if (mBuffer && mBufferMaxLength > 0)
		{
			s->mBuffer = new float[mBufferMaxLength];
			memcpy(s->mBuffer, mBuffer, sizeof(float) * mBufferMaxLength);
		}
		return s;
	}

	void EchoFilterInstance::restoreState(FilterStateSnapshot *aSnapshot)
	{
		EchoFilterSnapshot *s = static_cast<EchoFilterSnapshot *>(aSnapshot);
		mBufferLength = s->mBufferLength;
		mOffset = s->mOffset;
		mParamChanged = s->mParamChanged;
		for (int i = 0; i < 4; i++)
		{
			mParam[i] = s->mParam[i];
			mParamFader[i] = s->mParamFader[i];
		}
		if (s->mBuffer && s->mBufferMaxLength > 0)
		{
			if (mBufferMaxLength != s->mBufferMaxLength || mBuffer == 0)
			{
				delete[] mBuffer;
				mBufferMaxLength = s->mBufferMaxLength;
				mBuffer = new float[mBufferMaxLength];
			}
			memcpy(mBuffer, s->mBuffer, sizeof(float) * mBufferMaxLength);
		}
	}

	EchoFilterInstance::~EchoFilterInstance()
	{
		delete[] mBuffer;
	}

	EchoFilter::EchoFilter()
	{
		mDelay = 0.3f;
		mDecay = 0.7f;
		mFilter = 0.0f;
	}

	result EchoFilter::setParams(float aDelay, float aDecay, float aFilter)
	{
		if (aDelay <= 0 || aDecay <= 0 || aFilter < 0 || aFilter >= 1.0f)
			return INVALID_PARAMETER;

		mDecay = aDecay;
		mDelay = aDelay;
		mFilter = aFilter;
		
		return 0;
	}

	int EchoFilter::getParamCount()
	{
		return 4;
	}

	const char* EchoFilter::getParamName(unsigned int aParamIndex)
	{
		if (aParamIndex > 3)
			return 0;
		const char *names[4] = {
			"Wet",
			"Delay",
			"Decay",
			"Filter"
		};
		return names[aParamIndex];
	}

	unsigned int EchoFilter::getParamType(unsigned int aParamIndex)
	{
		return FLOAT_PARAM;
	}

	float EchoFilter::getParamMax(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case DELAY: return 3.0f;
		}
		return 1;
	}

	float EchoFilter::getParamMin(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case DELAY: return 0.001f;
		case DECAY: return 0.001f;
		}
		return 0;
	}

	FilterInstance *EchoFilter::createInstance()
	{
		return new EchoFilterInstance(this);
	}
}

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
#include "../../../simd/soloud_simd.h"

namespace SoLoud
{
	EchoFilterInstance::EchoFilterInstance(EchoFilter *aParent)
	{
		mBuffer = 0;
		mBufferLength = 0;
		mBufferMaxLength = 0;
		mBufferChannels = 0;
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

		if (mBuffer == 0 || reqSamples > mBufferMaxLength || (int)aChannels != mBufferChannels)
		{
			delete[] mBuffer;
			mBufferMaxLength = reqSamples;
			mBufferChannels = (int)aChannels;
			mBuffer = new float[mBufferMaxLength * aChannels];
			if (mBuffer == 0)
			{
				mBufferMaxLength = 0;
				mBufferChannels = 0;
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
		if (aSamples == 0 || aChannels == 0)
			return;

		soloud_simd::ScopedFlushDenormals flushDenormals;
		const float filt = mParam[EchoFilter::FILTER];
		const float oneMinusFilt = 1.0f - filt;
		const float decay = mParam[EchoFilter::DECAY];
		const float wet = mParam[EchoFilter::WET];
		const int len = mBufferLength;
		const int startOffset = mOffset;

		// Channel-outer, sample-inner loop eliminates L1/L2 cache thrashing across
		// multi-megabyte delay buffers and removes modulo division in the inner loop.
		for (unsigned int j = 0; j < aChannels; j++)
		{
			float *dBuf = mBuffer + (size_t)j * (size_t)len;
			float *aBuf = aBuffer + (size_t)j * (size_t)aBufferSize;
			int ofs = startOffset;
			int prevofs = (ofs > 0) ? (ofs - 1) : (len - 1);
			for (unsigned int i = 0; i < aSamples; i++)
			{
				const float in = aBuf[i];
				const float lp = filt * dBuf[prevofs] + oneMinusFilt * dBuf[ofs];
				const float n = in + lp * decay;
				dBuf[ofs] = n;
				aBuf[i] = in + (n - in) * wet;
				prevofs = ofs;
				if (++ofs >= len)
					ofs = 0;
			}
		}
		mOffset = (startOffset + (int)aSamples) % len;
	}

	struct EchoFilterSnapshot : public FilterStateSnapshot
	{
		float *mBuffer;
		int mBufferLength;
		int mBufferMaxLength;
		int mBufferChannels;
		int mOffset;
		uint64_t mParamChanged;
		float mParam[4];
		Fader mParamFader[4];
		EchoFilterSnapshot() : mBuffer(0), mBufferLength(0), mBufferMaxLength(0), mBufferChannels(0), mOffset(0), mParamChanged(0) {}
		size_t floats() const { return (size_t)mBufferMaxLength * (size_t)mBufferChannels; }
		virtual ~EchoFilterSnapshot() { delete[] mBuffer; }
	};

	static const char kEchoSnapshotKind = 0;

	// Copies everything but the delay line, which the callers size.
	static void fillEchoSnapshot(EchoFilterSnapshot *s, int aBufferLength, int aBufferMaxLength,
		int aBufferChannels, int aOffset, uint64_t aParamChanged, const float *aParam,
		const Fader *aParamFader)
	{
		s->mKind = &kEchoSnapshotKind;
		s->mBufferLength = aBufferLength;
		s->mBufferMaxLength = aBufferMaxLength;
		s->mBufferChannels = aBufferChannels;
		s->mOffset = aOffset;
		s->mParamChanged = aParamChanged;
		for (int i = 0; i < 4; i++)
		{
			s->mParam[i] = aParam[i];
			s->mParamFader[i] = aParamFader[i];
		}
	}

	FilterStateSnapshot *EchoFilterInstance::captureState()
	{
		EchoFilterSnapshot *s = new EchoFilterSnapshot();
		fillEchoSnapshot(s, mBufferLength, mBufferMaxLength, mBufferChannels, mOffset, mParamChanged, mParam, mParamFader);
		// The delay line holds every channel (see mBufferChannels).
		if (mBuffer && s->floats() > 0)
		{
			s->mBuffer = new float[s->floats()];
			memcpy(s->mBuffer, mBuffer, sizeof(float) * s->floats());
		}
		return s;
	}

	bool EchoFilterInstance::captureStateInto(FilterStateSnapshot *aState)
	{
		if (aState == NULL || aState->mKind != &kEchoSnapshotKind)
			return false;
		EchoFilterSnapshot *s = static_cast<EchoFilterSnapshot *>(aState);
		const size_t floats = (size_t)mBufferMaxLength * (size_t)mBufferChannels;
		const bool hasBuffer = mBuffer && floats > 0;
		// The delay line must match in size (or both be absent).
		if (hasBuffer != (s->mBuffer != 0) || (hasBuffer && s->floats() != floats))
			return false;
		fillEchoSnapshot(s, mBufferLength, mBufferMaxLength, mBufferChannels, mOffset, mParamChanged, mParam, mParamFader);
		if (hasBuffer)
			memcpy(s->mBuffer, mBuffer, sizeof(float) * floats);
		return true;
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
		if (s->mBuffer && s->floats() > 0)
		{
			if (mBufferMaxLength != s->mBufferMaxLength ||
				mBufferChannels != s->mBufferChannels || mBuffer == 0)
			{
				delete[] mBuffer;
				mBufferMaxLength = s->mBufferMaxLength;
				mBufferChannels = s->mBufferChannels;
				mBuffer = new float[s->floats()];
			}
			memcpy(mBuffer, s->mBuffer, sizeof(float) * s->floats());
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

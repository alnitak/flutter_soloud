/*
SoLoud audio engine
Copyright (c) 2015 Jari Komppa

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
#include "soloud_dcremovalfilter.h"

namespace SoLoud
{
	DCRemovalFilterInstance::DCRemovalFilterInstance(DCRemovalFilter *aParent)
	{
		mParent = aParent;
		mBuffer = 0;
		mBufferLength = 0;
		mBufferChannels = 0;
		mTotals = 0;
		mOffset = 0;
		initParams(1);

	}

	void DCRemovalFilterInstance::filter(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize, unsigned int aChannels, float aSamplerate, double aTime)
	{
		updateParams(aTime);
		if (aSamples == 0 || aChannels == 0)
			return;

		int reqLength = (int)ceil(mParent->mLength * aSamplerate);
		if (reqLength < 1)
			reqLength = 1;

		if (mBuffer == 0 || reqLength != mBufferLength || aChannels != mBufferChannels)
		{
			delete[] mBuffer;
			delete[] mTotals;
			mBufferLength = reqLength;
			mBufferChannels = aChannels;
			mBuffer = new float[(size_t)mBufferLength * (size_t)aChannels];
			mTotals = new float[aChannels];
			memset(mTotals, 0, sizeof(float) * aChannels);
			memset(mBuffer, 0, sizeof(float) * (size_t)mBufferLength * (size_t)aChannels);
			mOffset = 0;
		}

		const int len = mBufferLength;
		const float invLen = 1.0f / (float)len;
		const float wet = mParam[0];
		const int startOffset = mOffset % len;

		for (unsigned int j = 0; j < aChannels; j++)
		{
			float *dBuf = mBuffer + (size_t)j * (size_t)len;
			float *aBuf = aBuffer + (size_t)j * (size_t)aBufferSize;
			float total = mTotals[j];
			int ofs = startOffset;
			for (unsigned int i = 0; i < aSamples; i++)
			{
				const float in = aBuf[i];
				total += in - dBuf[ofs];
				dBuf[ofs] = in;
				const float dc = total * invLen;
				aBuf[i] = in - dc * wet;
				if (++ofs >= len)
					ofs = 0;
			}
			mTotals[j] = total;
		}
		mOffset = (startOffset + (int)aSamples) % len;
	}

	DCRemovalFilterInstance::~DCRemovalFilterInstance()
	{
		delete[] mBuffer;
		delete[] mTotals;
	}

	DCRemovalFilter::DCRemovalFilter()
	{
		mLength = 0.1f;
	}

	result DCRemovalFilter::setParams(float aLength)
	{
		if (aLength <= 0)
			return INVALID_PARAMETER;

        mLength = aLength;
		
		return 0;
	}


	FilterInstance *DCRemovalFilter::createInstance()
	{
		return new DCRemovalFilterInstance(this);
	}
}

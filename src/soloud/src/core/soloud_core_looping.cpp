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

#include "soloud.h"

namespace SoLoud
{
	static uint64_t sourceFrameAt(time aPosition, float aSamplerate, bool aRoundUp)
	{
		if (aPosition <= 0 || aSamplerate <= 0)
			return 0;
		double frame = aPosition * aSamplerate;
		return (uint64_t)(aRoundUp ? ceil(frame) : floor(frame));
	}

	unsigned int Soloud::readSourceSamples_internal(AudioSourceInstance *aVoice,
		float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		unsigned int readCount = 0;
		bool restartedWithoutProgress = false;

		while (readCount < aSamplesToRead)
		{
			const bool looping =
				(aVoice->mFlags & AudioSourceInstance::LOOPING) != 0;
			const uint64_t loopStartFrame = sourceFrameAt(
				aVoice->mLoopPoint, aVoice->mBaseSamplerate, false);
			const uint64_t loopEndFrame = sourceFrameAt(
				aVoice->mLoopEndPoint, aVoice->mBaseSamplerate, true);
			const bool boundedLoop = looping && aVoice->mLoopEndPoint > 0 &&
				loopEndFrame > loopStartFrame;

			if (boundedLoop && aVoice->mSourceSamplePosition >= loopEndFrame)
			{
				if (aVoice->seek(aVoice->mLoopPoint, mScratch.mData,
					mScratchSize) != SO_NO_ERROR)
				{
					break;
				}
				aVoice->mSourceSamplePosition = loopStartFrame;
				if (aVoice->mSourceSamplePosition >= loopEndFrame)
				{
					break;
				}
				aVoice->mLoopCount++;
				restartedWithoutProgress = true;
				continue;
			}

			if (aVoice->hasEnded() && !looping)
				break;

			unsigned int requested = aSamplesToRead - readCount;
			if (boundedLoop)
			{
				const uint64_t framesToEnd =
					loopEndFrame - aVoice->mSourceSamplePosition;
				if (framesToEnd < requested)
					requested = (unsigned int)framesToEnd;
			}

			unsigned int decoded = aVoice->getAudio(
				aBuffer + readCount, requested, aBufferSize);
			if (decoded > requested)
				decoded = requested;
			aVoice->mSourceSamplePosition += decoded;
			readCount += decoded;

			if (decoded == requested)
			{
				restartedWithoutProgress = false;
				continue;
			}

			if (!looping || (decoded == 0 && restartedWithoutProgress))
				break;

			if (aVoice->seek(aVoice->mLoopPoint, mScratch.mData,
				mScratchSize) != SO_NO_ERROR)
			{
				break;
			}
			aVoice->mSourceSamplePosition = loopStartFrame;
			aVoice->mLoopCount++;
			restartedWithoutProgress = decoded == 0;
		}

		return readCount;
	}
}

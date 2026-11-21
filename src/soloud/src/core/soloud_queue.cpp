/*
SoLoud audio engine
Copyright (c) 2013-2018 Jari Komppa

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
	QueueInstance::QueueInstance(Queue *aParent)
	{
		mParent = aParent;
		mFlags |= PROTECTED;
	}
	
	unsigned int QueueInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		if (mParent->mCount == 0)
		{
			return 0;			
		}
		unsigned int copycount = aSamplesToRead;
		unsigned int copyofs = 0;
		while (copycount && mParent->mCount)
		{
			int readcount = mParent->mSource[mParent->mReadIndex]->getAudio(aBuffer + copyofs, copycount, aBufferSize);
			copyofs += readcount;
			copycount -= readcount;
			if (mParent->mSource[mParent->mReadIndex]->hasEnded())
			{
				delete mParent->mSource[mParent->mReadIndex];
				mParent->mSource[mParent->mReadIndex] = 0;
				mParent->mReadIndex = (mParent->mReadIndex + 1) % SOLOUD_QUEUE_MAX;
				mParent->mCount--;
				mLoopCount++;
			}
			else
			{
				// ###### flutter_soloud local patch (starving source) ######
				// A source that gives fewer samples than asked without ending
				// (a stream waiting for data) has nothing more now: calling it
				// again would spin here with the audio mutex held. The mixer
				// pads the rest with silence.
				break;
			}
		}
		return copyofs;
	}

	void QueueInstance::stopChildAudioSource_internal(unsigned int aAudioSourceID)
	{
		// Drop the queued instances of a source that is being stopped or
		// destroyed (they would read their freed parent), keeping the order
		// of the others.
		unsigned int kept = 0;
		AudioSourceInstance *keep[SOLOUD_QUEUE_MAX];
		for (unsigned int k = 0; k < mParent->mCount; k++)
		{
			unsigned int idx = (mParent->mReadIndex + k) % SOLOUD_QUEUE_MAX;
			AudioSourceInstance *instance = mParent->mSource[idx];
			mParent->mSource[idx] = 0;
			if (instance && instance->mAudioSourceID == aAudioSourceID)
				delete instance;
			else
				keep[kept++] = instance;
		}
		for (unsigned int k = 0; k < kept; k++)
			mParent->mSource[(mParent->mReadIndex + k) % SOLOUD_QUEUE_MAX] = keep[k];
		mParent->mCount = kept;
		mParent->mWriteIndex = (mParent->mReadIndex + kept) % SOLOUD_QUEUE_MAX;
	}

	bool QueueInstance::hasEnded()
	{
		return mLoopCount != 0 && mParent->mCount == 0;
	}

	QueueInstance::~QueueInstance()
	{
	}

	Queue::Queue()
	{
		mQueueHandle = 0;
		mInstance = 0;
		mReadIndex = 0;
		mWriteIndex = 0;
		mCount = 0;
		int i;
		for (i = 0; i < SOLOUD_QUEUE_MAX; i++)
			mSource[i] = 0;
	}

	Queue::~Queue()
	{
		stop();
		for (int i = 0; i < SOLOUD_QUEUE_MAX; i++)
		{
			delete mSource[i];
			mSource[i] = 0;
		}
	}
	
	QueueInstance * Queue::createInstance()
	{
		if (mInstance)
		{
			stop();
			mInstance = 0;
		}
		mInstance = new QueueInstance(this);
		// The handle of a previous voice is stale (see findQueueHandle).
		mQueueHandle = 0;
		return mInstance;
	}

	void Queue::findQueueHandle()
	{
		// Find the channel the queue is playing on to calculate handle..
		// ###### flutter_soloud local patch (ended queue voice) ######
		// The voice found earlier may have ended (the queue drained, or it
		// was stopped): forget its handle, so play() on a queue that is not
		// playing fails instead of queuing a source nobody will mix.
		mSoloud->lockAudioMutex_internal();
		if (mQueueHandle != 0)
		{
			int v = mSoloud->getVoiceFromHandle_internal(mQueueHandle);
			if (v < 0 || mSoloud->mVoice[v] != mInstance)
				mQueueHandle = 0;
		}
		int i;
		for (i = 0; mQueueHandle == 0 && i < (signed)mSoloud->mHighestVoice; i++)
		{
			if (mInstance && mSoloud->mVoice[i] == mInstance)
			{
				mQueueHandle = mSoloud->getHandleFromVoice_internal(i);
			}
		}
		mSoloud->unlockAudioMutex_internal();
	}

	result Queue::play(AudioSource &aSound)
	{
		if (!mSoloud)
		{
			return INVALID_PARAMETER;
		}
	
		findQueueHandle();

		if (mQueueHandle == 0)
			return INVALID_PARAMETER;

		if (mCount >= SOLOUD_QUEUE_MAX)
			return OUT_OF_MEMORY;

		if (!aSound.mAudioSourceID)
		{
			aSound.mAudioSourceID = mSoloud->mAudioSourceID;
			mSoloud->mAudioSourceID++;
		}

		// So that destroying or stopping aSound also drops its instance
		// from the queue (Soloud::stopAudioSource).
		aSound.mSoloud = mSoloud;
		SoLoud::AudioSourceInstance *instance = aSound.createInstance();

		if (instance == 0)
		{
			return OUT_OF_MEMORY;
		}
		instance->init(aSound, 0);
		instance->mAudioSourceID = aSound.mAudioSourceID;

		mSoloud->lockAudioMutex_internal();
		mSource[mWriteIndex] = instance;
		mWriteIndex = (mWriteIndex + 1) % SOLOUD_QUEUE_MAX;
		mCount++;
		mSoloud->unlockAudioMutex_internal();

		return SO_NO_ERROR;
	}


	unsigned int Queue::getQueueCount()
	{
		if (!mSoloud)
		{
			return 0;
		}
		unsigned int count;
		mSoloud->lockAudioMutex_internal();
		count = mCount;
		mSoloud->unlockAudioMutex_internal();
		return count;
	}

	bool Queue::isCurrentlyPlaying(AudioSource &aSound)
	{
		if (mSoloud == 0 || mCount == 0 || aSound.mAudioSourceID == 0)
			return false;
		mSoloud->lockAudioMutex_internal();
		bool res = mSource[mReadIndex]->mAudioSourceID == aSound.mAudioSourceID;
		mSoloud->unlockAudioMutex_internal();
		return res;
	}

	result Queue::setParamsFromAudioSource(AudioSource &aSound)
	{
		mChannels = aSound.mChannels;
		mBaseSamplerate = aSound.mBaseSamplerate;

	    return SO_NO_ERROR;
	}
	
	result Queue::setParams(float aSamplerate, unsigned int aChannels)
	{
		if (aChannels < 1 || aChannels > MAX_CHANNELS)
			return INVALID_PARAMETER;
		mChannels = aChannels;
		mBaseSamplerate = aSamplerate;
		return SO_NO_ERROR;
	}
};

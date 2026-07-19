/*
SoLoud audio engine
Copyright (c) 2013-2015 Jari Komppa

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
#include "soloud_internal.h"

// Core "basic" operations - play, stop, etc

namespace SoLoud
{
	handle Soloud::play(AudioSource &aSound, float aVolume, float aPan, bool aPaused, unsigned int aBus)
	{
		if (aSound.mFlags & AudioSource::SINGLE_INSTANCE)
		{
			// Only one instance allowed, stop others
			aSound.stop();
		}

		// Creation of an audio instance may take significant amount of time,
		// so let's not do it inside the audio thread mutex.
		aSound.mSoloud = this;
		SoLoud::AudioSourceInstance *instance = aSound.createInstance();

		lockAudioMutex_internal();
		int ch = findFreeVoice_internal();
		if (ch < 0) 
		{
			unlockAudioMutex_internal();
			delete instance;
			return UNKNOWN_ERROR;
		}
		if (!aSound.mAudioSourceID)
		{
			aSound.mAudioSourceID = mAudioSourceID;
			mAudioSourceID++;
		}
		mVoice[ch] = instance;
		mVoice[ch]->mAudioSourceID = aSound.mAudioSourceID;
		mVoice[ch]->mBusHandle = aBus;
		mVoice[ch]->init(aSound, mPlayIndex);
		m3dData[ch].init(aSound);

		mPlayIndex++;

		// 20 bits, skip the last one (top bits full = voice group)
		if (mPlayIndex == 0xfffff) 
		{
			mPlayIndex = 0;
		}

		if (aPaused)
		{
			mVoice[ch]->mFlags |= AudioSourceInstance::PAUSED;
		}

		setVoicePan_internal(ch, aPan);
		if (aVolume < 0)
		{
			setVoiceVolume_internal(ch, aSound.mVolume);
		}
		else
		{
			setVoiceVolume_internal(ch, aVolume);
		}

		// Fix initial voice volume ramp up		
		int i;
		for (i = 0; i < MAX_CHANNELS; i++)
		{
			mVoice[ch]->mCurrentChannelVolume[i] = mVoice[ch]->mChannelVolume[i] * mVoice[ch]->mOverallVolume;
		}

		setVoiceRelativePlaySpeed_internal(ch, 1);
		
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (aSound.mFilter[i])
			{
				mVoice[ch]->mFilter[i] = aSound.mFilter[i]->createInstance();
			}
		}

		mActiveVoiceDirty = true;

		unlockAudioMutex_internal();

		int handle = getHandleFromVoice_internal(ch);
		return handle;
	}

	unsigned int Soloud::getClockedDelaySamples(time aSoundTime)
	{
		lockAudioMutex_internal();
		// A voice's delay starts counting down from the first sample of the
		// next output buffer. Since the audio mutex is held by the audio
		// thread while a buffer is being mixed, that position is exactly
		// mStreamTime (advanced at the start of every mix).
		long long now = (long long)floor(mStreamTime * mSamplerate + 0.5);
		long long delay = 0;
		// Detect the caller's clock being restarted (time going backwards).
		bool restarted = aSoundTime < mClockedLastTime - 0.001;
		mClockedLastTime = aSoundTime;
		if (mClockedAnchorSample < 0 || restarted)
		{
			// First clocked play (or the caller's clock was restarted):
			// anchor the caller's clock to the audio clock. The anchor leads
			// by two output buffers: a voice can only be delayed, never
			// advanced, so the effective scheduling slack of a clocked call
			// is (lead - (elapsed % bufferSize)) which, with a lead of one
			// buffer, would shrink to ~0 at unlucky phases of the schedule.
			// A lead of two buffers guarantees at least one full buffer of
			// slack at any phase, absorbing the jitter of the caller's
			// clock, so that subsequent clocked plays can land exactly on
			// their scheduled time.
			mClockedAnchorTime = aSoundTime;
			mClockedAnchorSample = now + 2 * (long long)mBufferSize;
			delay = 2 * (long long)mBufferSize;
		}
		else
		{
			long long expected = mClockedAnchorSample +
				(long long)floor((aSoundTime - mClockedAnchorTime) * mSamplerate + 0.5);
			delay = expected - now;
			if (delay < -2 * (long long)mSamplerate ||
				delay > 2 * (long long)mSamplerate)
			{
				// The caller's clock jumped (eg the app was paused):
				// re-anchor instead of delaying by an absurd amount.
				mClockedAnchorTime = aSoundTime;
				mClockedAnchorSample = now + 2 * (long long)mBufferSize;
				delay = 2 * (long long)mBufferSize;
			}
			if (delay < 0)
			{
				// The scheduled time is already in the past: play as soon
				// as possible.
				delay = 0;
			}
		}
		unlockAudioMutex_internal();
		return (unsigned int)delay;
	}

	handle Soloud::playClocked(time aSoundTime, AudioSource &aSound, float aVolume, float aPan, unsigned int aBus)
	{
		handle h = play(aSound, aVolume, aPan, 1, aBus);
		setDelaySamples(h, getClockedDelaySamples(aSoundTime));
		setPause(h, 0);
		return h;
	}

	handle Soloud::playBackground(AudioSource &aSound, float aVolume, bool aPaused, unsigned int aBus)
	{
		handle h = play(aSound, aVolume, 0.0f, aPaused, aBus);
		setPanAbsolute(h, 1.0f, 1.0f);
		return h;
	}

	result Soloud::seek(handle aVoiceHandle, time aSeconds)
	{
		result res = SO_NO_ERROR;
		result singleres = SO_NO_ERROR;
		FOR_ALL_VOICES_PRE
			singleres = mVoice[ch]->seek(aSeconds, mScratch.mData, mScratchSize);
		if (singleres == SO_NO_ERROR)
		{
			mVoice[ch]->mSourceSamplePosition = (uint64_t)floor(
				aSeconds * mVoice[ch]->mBaseSamplerate);
		}
		if (singleres != SO_NO_ERROR)
			res = singleres;
		FOR_ALL_VOICES_POST
		return res;
	}


	void Soloud::stop(handle aVoiceHandle)
	{
		FOR_ALL_VOICES_PRE
			stopVoice_internal(ch);
		FOR_ALL_VOICES_POST
	}

	void Soloud::stopAudioSource(AudioSource &aSound)
	{
		if (aSound.mAudioSourceID)
		{
			lockAudioMutex_internal();
			
			int i;
			for (i = 0; i < (signed)mHighestVoice; i++)
			{
				if (mVoice[i] && mVoice[i]->mAudioSourceID == aSound.mAudioSourceID)
				{
					stopVoice_internal(i);
				}
			}
			unlockAudioMutex_internal();
		}
	}

	void Soloud::stopAll()
	{
		int i;
		lockAudioMutex_internal();
		for (i = 0; i < (signed)mHighestVoice; i++)
		{
			stopVoice_internal(i);
		}
		unlockAudioMutex_internal();
	}

	int Soloud::countAudioSource(AudioSource &aSound)
	{
		int count = 0;
		if (aSound.mAudioSourceID)
		{
			lockAudioMutex_internal();

			int i;
			for (i = 0; i < (signed)mHighestVoice; i++)
			{
				if (mVoice[i] && mVoice[i]->mAudioSourceID == aSound.mAudioSourceID)
				{
					count++;
				}
			}
			unlockAudioMutex_internal();
		}
		return count;
	}

}

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
	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// The under-mutex body of play(): insert a pre-created instance into a
	// free voice slot and apply the usual initialization. Returns the voice
	// slot, or -1 when no voice is free (the caller deletes the instance).
	int Soloud::insertVoice_internal(AudioSource &aSound, AudioSourceInstance *aInstance, float aVolume, float aPan, bool aPaused, unsigned int aBus)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		int ch = findFreeVoice_internal();
		if (ch < 0)
		{
			return -1;
		}
		if (!aSound.mAudioSourceID)
		{
			aSound.mAudioSourceID = mAudioSourceID;
			mAudioSourceID++;
		}
		mVoice[ch] = aInstance;
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
		return ch;
	}

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
		int ch = insertVoice_internal(aSound, instance, aVolume, aPan, aPaused, aBus);
		if (ch < 0)
		{
			unlockAudioMutex_internal();
			delete instance;
			// Return the invalid-handle sentinel, not an error enum. Handles
			// are encoded as (voice + 1) | (playIndex << 12), so the old
			// UNKNOWN_ERROR (7) is a legal encoding: voice slot 6 with play
			// index 0. mPlayIndex wraps at 0xfffff, so once it comes back
			// around while slots 0..5 are busy, a live voice really does own
			// handle 7 and a failure became indistinguishable from it --
			// isValidVoiceHandle() included. Callers then operated on an
			// unrelated voice. 0 can never encode a voice and is already what
			// getHandleFromVoice_internal() and Bus::play() return on failure.
			return 0;
		}
		handle h = getHandleFromVoice_internal(ch);

		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// With the render-ahead ring enabled, try to move the voice's start
		// into the rendered-but-unplayed window (roll back, replay the
		// journal, re-mix) so it becomes audible from the playhead on instead
		// of from the next quantum. Paused voices make no sound and keep the
		// legacy placement. Either way the birth is journaled so later
		// rollbacks can replay it.
		if (mRenderRing.isInited())
		{
			if (aPaused ||
				!retroactiveVoiceStart_internal(ch, playheadTimeLocked_internal(), aSound))
			{
				journalBirth_internal(ch,
					mStreamTime + (time)mVoice[ch]->mDelaySamples / (time)mSamplerate);
			}
		}

		unlockAudioMutex_internal();
		return h;
	}

	unsigned int Soloud::getClockedDelaySamples(time aSoundTime)
	{
		lockAudioMutex_internal();
		unsigned int r = getClockedDelaySamplesLocked_internal(aSoundTime);
		unlockAudioMutex_internal();
		return r;
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// Body of getClockedDelaySamples with the audio mutex already held by the
	// caller (the ring-aware playClocked path does the whole operation in one
	// hold, and the native mutex is not recursive).
	unsigned int Soloud::getClockedDelaySamplesLocked_internal(time aSoundTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
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
		return (unsigned int)delay;
	}

	void Soloud::resetClockedAnchor()
	{
		lockAudioMutex_internal();
		mClockedAnchorTime = 0;
		mClockedAnchorSample = -1;
		mClockedLastTime = 0;
		unlockAudioMutex_internal();
	}

	handle Soloud::playClocked(time aSoundTime, AudioSource &aSound, float aVolume, float aPan, unsigned int aBus, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint)
	{
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// With the render-ahead ring enabled, do the whole operation in one
		// audio-mutex hold (the native mutex is not recursive) so the journaled
		// birth captures the final configuration. The clocked anchor's lead
		// keeps clocked starts at or beyond the write head, so there is no
		// retroactive attempt here.
		if (isRenderAheadEnabled() && mRenderRing.isInited())
		{
			if (aSound.mFlags & AudioSource::SINGLE_INSTANCE)
			{
				aSound.stop();
			}
			aSound.mSoloud = this;
			SoLoud::AudioSourceInstance *instance = aSound.createInstance();
			lockAudioMutex_internal();
			int ch = insertVoice_internal(aSound, instance, aVolume, aPan, 1, aBus);
			if (ch < 0)
			{
				unlockAudioMutex_internal();
				delete instance;
				return 0;
			}
			handle h = getHandleFromVoice_internal(ch);
			if (aScale != 1.0f && aScale > 0.0f)
			{
				setVoiceRelativePlaySpeed_internal(ch, aScale);
			}
			if (aLooping)
			{
				mVoice[ch]->mFlags |= AudioSourceInstance::LOOPING;
				if (aLoopPoint > 0.0)
				{
					mVoice[ch]->mLoopPoint = aLoopPoint;
				}
				if (aLoopEndPoint > aLoopPoint)
				{
					mVoice[ch]->mLoopEndPoint = aLoopEndPoint;
				}
			}
			unsigned int delay = getClockedDelaySamplesLocked_internal(aSoundTime);
			mVoice[ch]->mDelaySamples = delay;
			setVoicePause_internal(ch, 0);
			journalBirth_internal(ch, mStreamTime + (time)delay / (time)mSamplerate);
			unlockAudioMutex_internal();
			return h;
		}

		handle h = play(aSound, aVolume, aPan, 1, aBus);
		// No voice was allocated: don't delay/unpause anything.
		if (h == 0)
			return 0;
		if (aScale != 1.0f && aScale > 0.0f)
		{
			setRelativePlaySpeed(h, aScale);
		}
		if (aLooping)
		{
			setLoopPoint(h, aLoopPoint);
			setLoopEndPoint(h, aLoopEndPoint);
			setLooping(h, 1);
		}
		setDelaySamples(h, getClockedDelaySamples(aSoundTime));
		setPause(h, 0);
		return h;
	}

	time Soloud::getEngineTime()
	{
		lockAudioMutex_internal();
		time t = mStreamTime;
		unlockAudioMutex_internal();
		return t;
	}

	unsigned int Soloud::getScheduledDelaySamples(time aEngineTime)
	{
		lockAudioMutex_internal();
		unsigned int r = getScheduledDelaySamplesLocked_internal(aEngineTime);
		unlockAudioMutex_internal();
		return r;
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// Body of getScheduledDelaySamples with the audio mutex already held by
	// the caller (the ring-aware playScheduled path does the whole operation
	// in one hold, and the native mutex is not recursive).
	unsigned int Soloud::getScheduledDelaySamplesLocked_internal(time aEngineTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		// A voice's delay starts counting down from the first sample of the
		// next output buffer, whose position is exactly mStreamTime (see
		// getClockedDelaySamples). Unlike the clocked variant there is no
		// anchor and no re-anchor guard: the given time is already on the
		// engine's own clock, so sounds can be scheduled arbitrarily far
		// in the future.
		long long delay = (long long)floor((aEngineTime - mStreamTime) * mSamplerate + 0.5);
		if (delay < 0)
		{
			// The scheduled time is already in the past: play as soon
			// as possible.
			delay = 0;
		}
		return (unsigned int)delay;
	}

	handle Soloud::playScheduled(time aEngineTime, AudioSource &aSound, float aVolume, float aPan, unsigned int aBus, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint)
	{
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// With the render-ahead ring enabled, do the whole operation in one
		// audio-mutex hold: insert paused, then either roll back and re-mix
		// (aEngineTime inside the rendered-but-unplayed window) or apply the
		// legacy delay placement.
		if (isRenderAheadEnabled() && mRenderRing.isInited())
		{
			if (aSound.mFlags & AudioSource::SINGLE_INSTANCE)
			{
				aSound.stop();
			}
			aSound.mSoloud = this;
			SoLoud::AudioSourceInstance *instance = aSound.createInstance();
			lockAudioMutex_internal();
			int ch = insertVoice_internal(aSound, instance, aVolume, aPan, 1, aBus);
			if (ch < 0)
			{
				unlockAudioMutex_internal();
				delete instance;
				return 0;
			}
			handle h = getHandleFromVoice_internal(ch);
			if (aScale != 1.0f && aScale > 0.0f)
			{
				setVoiceRelativePlaySpeed_internal(ch, aScale);
			}
			if (aLooping)
			{
				mVoice[ch]->mFlags |= AudioSourceInstance::LOOPING;
				if (aLoopPoint > 0.0)
				{
					mVoice[ch]->mLoopPoint = aLoopPoint;
				}
				if (aLoopEndPoint > aLoopPoint)
				{
					mVoice[ch]->mLoopEndPoint = aLoopEndPoint;
				}
			}
			if (!retroactiveVoiceStart_internal(ch, aEngineTime, aSound))
			{
				unsigned int delay = getScheduledDelaySamplesLocked_internal(aEngineTime);
				mVoice[ch]->mDelaySamples = delay;
				setVoicePause_internal(ch, 0);
				journalBirth_internal(ch, mStreamTime + (time)delay / (time)mSamplerate);
			}
			unlockAudioMutex_internal();
			return h;
		}

		handle h = play(aSound, aVolume, aPan, 1, aBus);
		// No voice was allocated: don't delay/unpause anything.
		if (h == 0)
			return 0;
		if (aScale != 1.0f && aScale > 0.0f)
		{
			setRelativePlaySpeed(h, aScale);
		}
		if (aLooping)
		{
			setLoopPoint(h, aLoopPoint);
			setLoopEndPoint(h, aLoopEndPoint);
			setLooping(h, 1);
		}
		setDelaySamples(h, getScheduledDelaySamples(aEngineTime));
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
			// ###### flutter_soloud local patch (retroactive re-mix) ######
			// With the render-ahead ring, try to stop the voice at the
			// playhead (silencing the not-yet-played tail) before falling
			// back to an immediate stop at the next quantum.
			if (!retroactiveStopVoiceAt_internal(ch, playheadTimeLocked_internal()))
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

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
#include "soloud_fft.h"
#include "soloud_internal.h"

namespace SoLoud
{
	BusInstance::BusInstance(Bus *aParent)
	{
		mParent = aParent;
		mFlags |= PROTECTED | INAUDIBLE_TICK;		
		clearVisualization();
		mScratchSize = SAMPLE_GRANULARITY;
		mScratch.init(mScratchSize * MAX_CHANNELS);
	}
	
	unsigned int BusInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		int handle = mParent->mChannelHandle;
		if (handle == 0) 
		{
			// Avoid reuse of scratch data if this bus hasn't played anything yet
			unsigned int i;
			for (i = 0; i < aBufferSize * mChannels; i++)
				aBuffer[i] = 0;
			return aSamplesToRead;
		}
		
		Soloud *s = mParent->mSoloud;
		
		s->mixBus_internal(aBuffer, aSamplesToRead, aBufferSize, mScratch.mData, handle, mSamplerate, mChannels, mParent->mResampler);

		if (mParent->mFlags & AudioSource::VISUALIZATION_DATA)
		{
			unsigned int i, j;
			for (j = 0; j < MAX_CHANNELS; j++)
				mVisualizationChannelVolume[j] = 0;

			float invChannels = 1.0f / mChannels;
			for (i = 0; i < aSamplesToRead; i++)
			{
				float sum = 0;
				float sumSquares = 0;
				for (j = 0; j < mChannels; j++)
				{
					float sample = aBuffer[i + aBufferSize * j];
					float absvol = (float)fabs(sample);
					if (absvol > mVisualizationChannelVolume[j])
						mVisualizationChannelVolume[j] = absvol;
					if (absvol > mLevelPeak)
						mLevelPeak = absvol;
					sum += sample;
					sumSquares += sample * sample;
				}
				mLevelSumSquares += sumSquares * invChannels;
				mVisualizationWaveData[mVisualizationWavePos] = sum * invChannels;
				mVisualizationWavePos = (mVisualizationWavePos + 1) & 255;
			}
			mLevelFrames += aSamplesToRead;
		}
		return aSamplesToRead;
	}

	void BusInstance::clearVisualization()
	{
		for (int i = 0; i < MAX_CHANNELS; i++)
			mVisualizationChannelVolume[i] = 0;
		for (int i = 0; i < 256; i++)
			mVisualizationWaveData[i] = 0;
		mVisualizationWavePos = 0;
		mLevelSumSquares = 0;
		mLevelPeak = 0;
		mLevelFrames = 0;
	}

	bool BusInstance::hasEnded()
	{
		// Busses never stop for fear of going under 50mph.
		return 0;
	}

	BusInstance::~BusInstance()
	{
		// The voice is deleted by the engine (stopAll, stop, an outer bus
		// stopping, ...) without going through Bus, so drop the parent's
		// pointer here. Runs under the audio mutex. On the web the delete can
		// be deferred until after the bus was replayed, so only clear it if
		// it still points at this instance.
		if (mParent->mInstance == this)
			mParent->mInstance = 0;
		// mChannelHandle is only found lazily (findBusHandle) and is reset
		// by createInstance, so it can be 0 here. 0 is the main bus's handle:
		// without this check stopping a bus that never had anything played
		// on it, or a failed play() of a bus, stopped every main bus voice.
		if (mParent->mChannelHandle == 0)
			return;
		Soloud *s = mParent->mSoloud;
		int i;
		for (i = 0; i < (signed)s->mHighestVoice; i++)
		{
			if (s->mVoice[i] && s->mVoice[i]->mBusHandle == mParent->mChannelHandle)
			{
				s->stopVoice_internal(i);
			}
		}
	}

	Bus::Bus()
	{
		mChannelHandle = 0;
		mInstance = 0;
		mChannels = 2;
		mResampler = SOLOUD_DEFAULT_RESAMPLER;
		for (int i = 0; i < 256; i++)
		{
			mFFTData[i] = 0;
			mWaveData[i] = 0;
		}
	}
	
	BusInstance * Bus::createInstance()
	{
		// Stop the previous voice even if nothing was played through it yet
		// (mChannelHandle still 0). stop() finds it by audio source id, and
		// an orphaned instance would later stop the new bus's voices.
		stop();
		BusInstance *instance = new BusInstance(this);
		// The getters, filter changes and ~BusInstance (on the audio thread
		// too) access mInstance under the audio mutex, so publish it there.
		// play() sets mSoloud before calling this.
		if (mSoloud)
			mSoloud->lockAudioMutex_internal();
		mChannelHandle = 0;
		mInstance = instance;
		if (mSoloud)
			mSoloud->unlockAudioMutex_internal();
		return instance;
	}

	void Bus::findBusHandle()
	{
		if (mChannelHandle == 0)
		{
			// Find the channel the bus is playing on to calculate handle..
			int i;
			for (i = 0; mChannelHandle == 0 && i < (signed)mSoloud->mHighestVoice; i++)
			{
				if (mSoloud->mVoice[i] == mInstance)
				{
					mChannelHandle = mSoloud->getHandleFromVoice_internal(i);
				}
			}
		}
	}

	handle Bus::play(AudioSource &aSound, float aVolume, float aPan, bool aPaused)
	{
		if (!mInstance || !mSoloud)
		{
			return 0;
		}

		findBusHandle();

		if (mChannelHandle == 0)
		{
			return 0;
		}
		return mSoloud->play(aSound, aVolume, aPan, aPaused, mChannelHandle);
	}	


	handle Bus::playClocked(time aSoundTime, AudioSource &aSound, float aVolume, float aPan, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint, long long aLoopStartOffset, long long aLoopEndOffset)
	{
		if (!mInstance || !mSoloud)
		{
			return 0;
		}

		findBusHandle();

		if (mChannelHandle == 0)
		{
			return 0;
		}

		return mSoloud->playClocked(aSoundTime, aSound, aVolume, aPan, mChannelHandle, aScale, aLooping, aLoopPoint, aLoopEndPoint, aLoopStartOffset, aLoopEndOffset);
	}	

	handle Bus::playScheduled(time aEngineTime, AudioSource &aSound, float aVolume, float aPan, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint, long long aLoopStartOffset, long long aLoopEndOffset)
	{
		if (!mInstance || !mSoloud)
		{
			return 0;
		}

		findBusHandle();

		if (mChannelHandle == 0)
		{
			return 0;
		}

		return mSoloud->playScheduled(aEngineTime, aSound, aVolume, aPan, mChannelHandle, aScale, aLooping, aLoopPoint, aLoopEndPoint, aLoopStartOffset, aLoopEndOffset);
	}	

	handle Bus::play3d(AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX, float aVelY, float aVelZ, float aVolume, bool aPaused, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint, long long aLoopStartOffset, long long aLoopEndOffset)
	{
		if (!mInstance || !mSoloud)
		{
			return 0;
		}

		findBusHandle();

		if (mChannelHandle == 0)
		{
			return 0;
		}
		return mSoloud->play3d(aSound, aPosX, aPosY, aPosZ, aVelX, aVelY, aVelZ, aVolume, aPaused, mChannelHandle, aScale, aLooping, aLoopPoint, aLoopEndPoint, aLoopStartOffset, aLoopEndOffset);
	}

	handle Bus::play3dClocked(time aSoundTime, AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX, float aVelY, float aVelZ, float aVolume, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint, long long aLoopStartOffset, long long aLoopEndOffset)
	{
		if (!mInstance || !mSoloud)
		{
			return 0;
		}

		findBusHandle();

		if (mChannelHandle == 0)
		{
			return 0;
		}
		return mSoloud->play3dClocked(aSoundTime, aSound, aPosX, aPosY, aPosZ, aVelX, aVelY, aVelZ, aVolume, mChannelHandle, aScale, aLooping, aLoopPoint, aLoopEndPoint, aLoopStartOffset, aLoopEndOffset);
	}

	handle Bus::play3dScheduled(time aEngineTime, AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX, float aVelY, float aVelZ, float aVolume, float aScale, bool aLooping, time aLoopPoint, time aLoopEndPoint, long long aLoopStartOffset, long long aLoopEndOffset)
	{
		if (!mInstance || !mSoloud)
		{
			return 0;
		}

		findBusHandle();

		if (mChannelHandle == 0)
		{
			return 0;
		}
		return mSoloud->play3dScheduled(aEngineTime, aSound, aPosX, aPosY, aPosZ, aVelX, aVelY, aVelZ, aVolume, mChannelHandle, aScale, aLooping, aLoopPoint, aLoopEndPoint, aLoopStartOffset, aLoopEndOffset);
	}

	void Bus::annexSound(handle aVoiceHandle)
	{
		findBusHandle();
		FOR_ALL_VOICES_PRE_EXT
			mSoloud->mVoice[ch]->mBusHandle = mChannelHandle;
		FOR_ALL_VOICES_POST_EXT
	}

	void Bus::setFilter(unsigned int aFilterId, Filter *aFilter)
	{
		if (aFilterId >= FILTERS_PER_STREAM)
			return;

		mFilter[aFilterId] = aFilter;

		if (mSoloud)
		{
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
			{
				delete mInstance->mFilter[aFilterId];
				mInstance->mFilter[aFilterId] = 0;
			
				if (aFilter)
				{
					mInstance->mFilter[aFilterId] = mFilter[aFilterId]->createInstance();
				}
			}
			mSoloud->unlockAudioMutex_internal();
		}
	}

	void Bus::moveFilter(unsigned int aFromSlot, unsigned int aToSlot)
	{
		if (aFromSlot >= FILTERS_PER_STREAM || aToSlot >= FILTERS_PER_STREAM)
			return;

		mFilter[aToSlot] = mFilter[aFromSlot];
		mFilter[aFromSlot] = 0;

		if (mSoloud)
		{
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
			{
				delete mInstance->mFilter[aToSlot];
				mInstance->mFilter[aToSlot] = mInstance->mFilter[aFromSlot];
				mInstance->mFilter[aFromSlot] = 0;
			}
			mSoloud->unlockAudioMutex_internal();
		}
	}

	result Bus::setChannels(unsigned int aChannels)
	{
		if (aChannels == 0 || aChannels == 3 || aChannels == 5 || aChannels == 7 || aChannels > MAX_CHANNELS)
			return INVALID_PARAMETER;
		mChannels = aChannels;
		return SO_NO_ERROR;
	}

	void Bus::setVisualizationEnable(bool aEnable)
	{
		// mInstance is cleared under the audio mutex when the voice is
		// deleted, so it is only checked while the lock is held.
		if (mSoloud)
			mSoloud->lockAudioMutex_internal();
		if (aEnable)
		{
			// Start from a clean slate so earlier captures aren't reported.
			if (mInstance)
				mInstance->clearVisualization();
			mFlags |= AudioSource::VISUALIZATION_DATA;
		}
		else
		{
			mFlags &= ~AudioSource::VISUALIZATION_DATA;
		}
		if (mSoloud)
			mSoloud->unlockAudioMutex_internal();
	}
		
	float * Bus::calcFFT()
	{
		if (mSoloud)
		{
			float temp[1024];
			int i;
			mSoloud->lockAudioMutex_internal();
			if (!mInstance)
			{
				mSoloud->unlockAudioMutex_internal();
				return mFFTData;
			}
			unsigned int pos = mInstance->mVisualizationWavePos;
			for (i = 0; i < 256; i++)
			{
				temp[i*2] = mInstance->mVisualizationWaveData[(pos + i) & 255];
				temp[i*2+1] = 0;
				temp[i+512] = 0;
				temp[i+768] = 0;
			}
			mSoloud->unlockAudioMutex_internal();

			SoLoud::FFT::fft1024(temp);

			for (i = 0; i < 256; i++)
			{
				float real = temp[i * 2];
				float imag = temp[i * 2 + 1];
				mFFTData[i] = (float)sqrt(real*real+imag*imag);
			}
		}

		return mFFTData;
	}

	float * Bus::getWave()
	{
		if (mSoloud)
		{
			int i;
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
			{
				// Unroll the ring, oldest frame first.
				unsigned int pos = mInstance->mVisualizationWavePos;
				for (i = 0; i < 256; i++)
					mWaveData[i] = mInstance->mVisualizationWaveData[(pos + i) & 255];
			}
			mSoloud->unlockAudioMutex_internal();
		}
		return mWaveData;
	}

	float Bus::getApproximateVolume(unsigned int aChannel)
	{
		if (aChannel >= mChannels)
			return 0;
		float vol = 0;
		if (mSoloud)
		{
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
				vol = mInstance->mVisualizationChannelVolume[aChannel];
			mSoloud->unlockAudioMutex_internal();
		}
		return vol;
	}

	void Bus::getLevel(float *aRms, float *aPeak, unsigned long long *aFrames)
	{
		double sumSquares = 0;
		float peak = 0;
		unsigned long long frames = 0;
		if (mSoloud)
		{
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
			{
				sumSquares = mInstance->mLevelSumSquares;
				peak = mInstance->mLevelPeak;
				frames = mInstance->mLevelFrames;
				mInstance->mLevelSumSquares = 0;
				mInstance->mLevelPeak = 0;
				mInstance->mLevelFrames = 0;
			}
			mSoloud->unlockAudioMutex_internal();
		}
		*aRms = frames ? (float)sqrt(sumSquares / (double)frames) : 0;
		*aPeak = peak;
		*aFrames = frames;
	}

	unsigned int Bus::getActiveVoiceCount()
	{
		int i;
		unsigned int count = 0;
		findBusHandle();
		mSoloud->lockAudioMutex_internal();
		for (i = 0; i < VOICE_COUNT; i++)
			if (mSoloud->mVoice[i] && mSoloud->mVoice[i]->mBusHandle == mChannelHandle)
				count++;
		mSoloud->unlockAudioMutex_internal();
		return count;
	}

	unsigned int Bus::getResampler()
	{
		return mResampler;
	}

	void Bus::setResampler(unsigned int aResampler)
	{
		if (aResampler <= Soloud::RESAMPLER_CATMULLROM)
			mResampler = aResampler;
	}

};

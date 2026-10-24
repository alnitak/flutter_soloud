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
		for (int i = 0; i < MAX_CHANNELS; i++)
			mVisualizationChannelVolume[i] = 0;
		for (int i = 0; i < 256; i++)
			mVisualizationWaveData[i] = 0;
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
			// ###### flutter_soloud local patch (per-bus visualization) ######
			tapAudio_internal(aBuffer, aSamplesToRead, aBufferSize);
			return aSamplesToRead;
		}
		
		Soloud *s = mParent->mSoloud;
		
		s->mixBus_internal(aBuffer, aSamplesToRead, aBufferSize, mScratch.mData, handle, mSamplerate, mChannels, mParent->mResampler);
		// ###### flutter_soloud local patch (per-bus visualization) ######
		tapAudio_internal(aBuffer, aSamplesToRead, aBufferSize);

		int i;
		if (mParent->mFlags & AudioSource::VISUALIZATION_DATA)
		{
			for (i = 0; i < MAX_CHANNELS; i++)
				mVisualizationChannelVolume[i] = 0;

			if (aSamplesToRead > 255)
			{
				for (i = 0; i < 256; i++)
				{
					int j;
					mVisualizationWaveData[i] = 0;
					for (j = 0; j < (signed)mChannels; j++)
					{
						float sample = aBuffer[i + aBufferSize * j];
						float absvol = (float)fabs(sample);
						if (absvol > mVisualizationChannelVolume[j])
							mVisualizationChannelVolume[j] = absvol;
						mVisualizationWaveData[i] += sample;
					}
				}
			}
			else
			{
				// Very unlikely failsafe branch
				for (i = 0; i < 256; i++)
				{
					int j;
					mVisualizationWaveData[i] = 0;
					for (j = 0; j < (signed)mChannels; j++)
					{
						float sample = aBuffer[(i % aSamplesToRead) + aBufferSize * j];
						float absvol = (float)fabs(sample);
						if (absvol > mVisualizationChannelVolume[j])
							mVisualizationChannelVolume[j] = absvol;
						mVisualizationWaveData[i] += sample;
					}
				}
			}
		}
		return aSamplesToRead;
	}

	// ###### flutter_soloud local patch (per-bus visualization) ######
	void BusInstance::tapAudio_internal(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize)
	{
		// The bus is being mixed, so this is what plays into it, silence
		// included. A re-mixed quantum replays audio the tap already had.
		if (mParent->mAudioTap && !mParent->mSoloud->mRemixing)
			mParent->mAudioTap->onBusAudio(aBuffer, aSamples, aBufferSize, mChannels);
	}

	bool BusInstance::hasEnded()
	{
		// Busses never stop for fear of going under 50mph.
		return 0;
	}

	BusInstance::~BusInstance()
	{
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// The engine deletes the bus voice without going through Bus (stopAll,
		// stop() on the bus handle, an outer bus stopping), so drop the
		// parent's pointer to this instance here. The audio mutex is held.
		// Bus::createInstance() stops the previous voice before it publishes
		// a new instance, so the bus's voices belong to this instance only if
		// it is still the published one.
		if (mParent->mInstance != this)
			return;
		mParent->mInstance = 0;
		// mChannelHandle is only found lazily (findBusHandle), so it is still 0
		// if nothing was played through the bus. 0 is the main bus's handle:
		// stopping its voices here would stop everything playing on the engine.
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
		mAudioTap = 0;
	}

	BusInstance * Bus::createInstance()
	{
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Stop the previous voice even if nothing was played through it yet
		// (mChannelHandle still 0): stop() finds it by audio source id, while
		// an orphaned instance would keep mixing the new instance's voices.
		stop();
		BusInstance *instance = new BusInstance(this);
		// mInstance is read under the audio mutex (and cleared there by
		// ~BusInstance), so publish it there. play() sets mSoloud first.
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
			// ###### flutter_soloud local patch (per-bus visualization) ######
			// Search under the audio mutex: the audio thread deletes voices,
			// and clears mInstance when it deletes this bus's voice. Once it
			// is cleared there is nothing to find.
			mSoloud->lockAudioMutex_internal();
			// Find the channel the bus is playing on to calculate handle..
			int i;
			for (i = 0; mInstance && mChannelHandle == 0 && i < (signed)mSoloud->mHighestVoice; i++)
			{
				if (mSoloud->mVoice[i] == mInstance)
				{
					mChannelHandle = mSoloud->getHandleFromVoice_internal(i);
				}
			}
			mSoloud->unlockAudioMutex_internal();
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

		// ###### flutter_soloud local patch (per-bus visualization) ######
		// mInstance is cleared under the audio mutex when the engine deletes
		// the bus voice, so only check it while holding the lock.
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

		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Checked under the audio mutex, as in setFilter.
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
		if (aEnable)
		{
			mFlags |= AudioSource::VISUALIZATION_DATA;
		}
		else
		{
			mFlags &= ~AudioSource::VISUALIZATION_DATA;
		}
	}

	// ###### flutter_soloud local patch (per-bus visualization) ######
	void Bus::setAudioTap(BusAudioTap *aTap)
	{
		if (mSoloud)
		{
			mSoloud->lockAudioMutex_internal();
			mAudioTap = aTap;
			mSoloud->unlockAudioMutex_internal();
		}
		else
		{
			// Never played, so no instance can be reading it.
			mAudioTap = aTap;
		}
	}
		
	float * Bus::calcFFT()
	{
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// mInstance is cleared under the audio mutex when the engine deletes
		// the bus voice, so only check it while holding the lock.
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
			for (i = 0; i < 256; i++)
			{
				temp[i*2] = mInstance->mVisualizationWaveData[i];
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
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Checked under the audio mutex, as in calcFFT.
		if (mSoloud)
		{
			int i;
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
			{
				for (i = 0; i < 256; i++)
					mWaveData[i] = mInstance->mVisualizationWaveData[i];
			}
			mSoloud->unlockAudioMutex_internal();
		}
		return mWaveData;
	}

	float Bus::getApproximateVolume(unsigned int aChannel)
	{
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Was `aChannel > mChannels`, which read past the channel volumes.
		if (aChannel >= mChannels)
			return 0;
		float vol = 0;
		// Checked under the audio mutex, as in calcFFT.
		if (mSoloud)
		{
			mSoloud->lockAudioMutex_internal();
			if (mInstance)
				vol = mInstance->mVisualizationChannelVolume[aChannel];
			mSoloud->unlockAudioMutex_internal();
		}
		return vol;
	}

	unsigned int Bus::getActiveVoiceCount()
	{
		int i;
		unsigned int count = 0;
		findBusHandle();
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// No handle means the bus is not playing. 0 is the main bus's handle:
		// counting its voices would count everything playing on the engine.
		if (mChannelHandle == 0)
			return 0;
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

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

#include "soloud_internal.h"

// Core operations related to filters

namespace SoLoud
{
	// ###### flutter_soloud local patch (render-ahead worker) ######
	// Both locks held.
	static void updateGlobalFilterMask(Soloud &aSoloud)
	{
		unsigned int mask = 0;
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (aSoloud.mFilterInstance[i] != NULL)
				mask |= 1u << i;
		}
		aSoloud.mGlobalFilterMask.store(mask, std::memory_order_release);
	}

	void Soloud::setGlobalFilter(unsigned int aFilterId, Filter *aFilter)
	{
		if (aFilterId >= FILTERS_PER_STREAM)
			return;

		lockAudioMutex_internal();
		// ###### flutter_soloud local patch (render-ahead worker) ######
		// The device callback runs global filters under this lock only.
		lockGlobalFilters_internal();
		delete mFilterInstance[aFilterId];
		mFilterInstance[aFilterId] = 0;
		
		mFilter[aFilterId] = aFilter;
		if (aFilter)
		{
			mFilterInstance[aFilterId] = mFilter[aFilterId]->createInstance();
		}
		updateGlobalFilterMask(*this);
		unlockGlobalFilters_internal();
		unlockAudioMutex_internal();
	}

	void Soloud::moveGlobalFilter(unsigned int aFromSlot, unsigned int aToSlot)
	{
		if (aFromSlot >= FILTERS_PER_STREAM || aToSlot >= FILTERS_PER_STREAM)
			return;

		lockAudioMutex_internal();
		lockGlobalFilters_internal();
		delete mFilterInstance[aToSlot];
		mFilter[aToSlot] = mFilter[aFromSlot];
		mFilterInstance[aToSlot] = mFilterInstance[aFromSlot];
		mFilter[aFromSlot] = 0;
		mFilterInstance[aFromSlot] = 0;
		updateGlobalFilterMask(*this);
		unlockGlobalFilters_internal();
		unlockAudioMutex_internal();
	}

	float Soloud::getFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId)
	{
		float ret = INVALID_PARAMETER;
		if (aFilterId >= FILTERS_PER_STREAM)
			return ret;

		if (aVoiceHandle == 0)
		{
			lockAudioMutex_internal();
			lockGlobalFilters_internal();
			if (mFilterInstance[aFilterId])
			{
				ret = mFilterInstance[aFilterId]->getFilterParameter(aAttributeId);
			}
			unlockGlobalFilters_internal();
			unlockAudioMutex_internal();
			return ret;
		}

		// ###### flutter_soloud local patch (filter getter voice lifetime) ######
		// Resolving the handle reads the voice table and the voice's play
		// index, which the mixer changes when a voice ends or a new play
		// reuses its slot: resolve it under the lock too.
		lockAudioMutex_internal();
		int ch = getVoiceFromHandle_internal(aVoiceHandle);
		if (ch == -1)
		{
			unlockAudioMutex_internal();
			return ret;
		}
		if (mVoice[ch] &&
			mVoice[ch]->mFilter[aFilterId])
		{
			ret = mVoice[ch]->mFilter[aFilterId]->getFilterParameter(aAttributeId);
		}
		unlockAudioMutex_internal();

		return ret;
	}

	void Soloud::setFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId, float aValue)
	{
		if (aFilterId >= FILTERS_PER_STREAM)
			return;

		if (aVoiceHandle == 0)
		{
			lockAudioMutex_internal();
			lockGlobalFilters_internal();
			if (mFilterInstance[aFilterId])
			{
				mFilterInstance[aFilterId]->setFilterParameter(aAttributeId, aValue);
			}
			unlockGlobalFilters_internal();
			unlockAudioMutex_internal();
			return;
		}

		FOR_ALL_VOICES_PRE
		if (mVoice[ch] &&
			mVoice[ch]->mFilter[aFilterId])
		{
			mVoice[ch]->mFilter[aFilterId]->setFilterParameter(aAttributeId, aValue);
			// ###### flutter_soloud local patch (retroactive re-mix) ######
			// Filter snapshots hold the parameters.
			voiceStateChanged_internal(ch);
		}
		FOR_ALL_VOICES_POST
	}

	void Soloud::removeVoiceFilter(handle aVoiceHandle, unsigned int aFilterId)
	{
		if (aFilterId >= FILTERS_PER_STREAM)
			return;

		FOR_ALL_VOICES_PRE
		if (mVoice[ch])
		{
			// delete of a NULL instance is a no-op
			delete mVoice[ch]->mFilter[aFilterId];
			unsigned int i;
			for (i = aFilterId; i < FILTERS_PER_STREAM - 1; i++)
			{
				mVoice[ch]->mFilter[i] = mVoice[ch]->mFilter[i + 1];
			}
			mVoice[ch]->mFilter[FILTERS_PER_STREAM - 1] = NULL;
			// ###### flutter_soloud local patch (retroactive re-mix) ######
			voiceStateChanged_internal(ch);
		}
		FOR_ALL_VOICES_POST
	}

	void Soloud::addVoiceFilter(handle aVoiceHandle, unsigned int aFilterId, Filter *aFilter)
	{
		if (aFilterId >= FILTERS_PER_STREAM || aFilter == NULL)
			return;

		FOR_ALL_VOICES_PRE
		if (mVoice[ch] && mVoice[ch]->mFilter[aFilterId] == NULL)
		{
			mVoice[ch]->mFilter[aFilterId] = aFilter->createInstance();
			// ###### flutter_soloud local patch (retroactive re-mix) ######
			voiceStateChanged_internal(ch);
		}
		FOR_ALL_VOICES_POST
	}

	void Soloud::fadeFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId, float aTo, double aTime)
	{
		if (aFilterId >= FILTERS_PER_STREAM)
			return;

		if (aVoiceHandle == 0)
		{
			lockAudioMutex_internal();
			lockGlobalFilters_internal();
			if (mFilterInstance[aFilterId])
			{
				time t = (isRenderAheadEnabled() && mRenderRing.isInited())
					? playheadTimeLocked_internal()
					: mStreamTime;
				mFilterInstance[aFilterId]->fadeFilterParameter(aAttributeId, aTo, aTime, t);
			}
			unlockGlobalFilters_internal();
			unlockAudioMutex_internal();
			return;
		}

		FOR_ALL_VOICES_PRE
		if (mVoice[ch] &&
			mVoice[ch]->mFilter[aFilterId])
		{
			mVoice[ch]->mFilter[aFilterId]->fadeFilterParameter(aAttributeId, aTo, aTime, mStreamTime);
			// ###### flutter_soloud local patch (retroactive re-mix) ######
			voiceStateChanged_internal(ch);
		}
		FOR_ALL_VOICES_POST
	}

	void Soloud::oscillateFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId, float aFrom, float aTo, double aTime)
	{
		if (aFilterId >= FILTERS_PER_STREAM)
			return;

		if (aVoiceHandle == 0)
		{
			lockAudioMutex_internal();
			lockGlobalFilters_internal();
			if (mFilterInstance[aFilterId])
			{
				time t = (isRenderAheadEnabled() && mRenderRing.isInited())
					? playheadTimeLocked_internal()
					: mStreamTime;
				mFilterInstance[aFilterId]->oscillateFilterParameter(aAttributeId, aFrom, aTo, aTime, t);
			}
			unlockGlobalFilters_internal();
			unlockAudioMutex_internal();
			return;
		}

		FOR_ALL_VOICES_PRE
		if (mVoice[ch] &&
			mVoice[ch]->mFilter[aFilterId])
		{
			mVoice[ch]->mFilter[aFilterId]->oscillateFilterParameter(aAttributeId, aFrom, aTo, aTime, mStreamTime);
			// ###### flutter_soloud local patch (retroactive re-mix) ######
			voiceStateChanged_internal(ch);
		}
		FOR_ALL_VOICES_POST
	}

}

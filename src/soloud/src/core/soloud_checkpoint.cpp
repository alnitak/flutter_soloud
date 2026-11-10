/*
* SoLoud audio engine
*
* Mix checkpoints (flutter_soloud "Option B" retrofit, Phase 2 of
* OPTION_B_RETROACTIVE_REMIX_PLAN.md): capture and restore of the full mixer
* state at mix-quantum boundaries, so a retroactive event can roll the engine
* back and re-mix bit-identically.
*
* Capture runs on the audio thread at the end of mix_internal, under the
* audio mutex, so it must not allocate: the pool and all its buffers are
* pre-sized by allocateCheckpoints_internal() and reused in place. Filters
* and sources that support snapshotting keep their state in heap objects
* (captureState()/captureSourceState()); a capture refills the objects of the
* checkpoint slot it overwrites, or spares left by ended voices, in place
* (captureStateInto()/captureSourceStateInto()), so it only allocates while
* the number of snapshotted voices grows.
*
* Fader hazard (invariant respected by both capture and restore): Fader::get()
* treats a time before mStartTime as a clock rollover and restarts the fade
* (soloud_fader.cpp). Re-mixing must therefore always restore the full fader
* POD before any fader is ticked against restored-past times; faders are
* copied by value here and never re-created.
*/

#include <math.h> // fabs
#include <stdio.h> // fprintf (debug log)
#include <string.h> // memcpy, memset

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_checkpoint.h"

namespace SoLoud
{
	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// Duration used for retroactive parameter changes: long enough to keep
	// Fader::get() off its 0-duration NaN path, short enough to be a step at
	// quantum-tick granularity (~4 samples at 44.1 kHz).
	static const double RETRO_FADE_TIME = 1e-4;

	// ###### flutter_soloud local patch (fade starting values) ######
	// The value a voice parameter has at voice time aTime: what its fader
	// gives then, or aSet when no fader is running. Evaluates a copy, since
	// Fader::get() updates the fader.
	static float faderValueAt(const Fader &aFader, float aSet, time aTime)
	{
		if (aFader.mActive <= 0)
			return aSet;
		Fader f = aFader;
		return f.get(aTime);
	}

	// Delete the heap snapshots owned by a checkpoint slot, preparing it for
	// reuse or teardown. CheckpointVoice/MixCheckpoint stay POD-copyable on
	// purpose (no destructors), so cleanup is explicit.
	static void releaseCheckpointStorage(MixCheckpoint &aCheckpoint)
	{
		unsigned int i;
		size_t v;
		for (v = 0; v < aCheckpoint.mVoices.size(); v++)
		{
			CheckpointVoice &voice = aCheckpoint.mVoices[v];
			for (i = 0; i < FILTERS_PER_STREAM; i++)
			{
				delete voice.mFilterState[i];
				voice.mFilterState[i] = NULL;
			}
			delete voice.mSourceState;
			voice.mSourceState = NULL;
		}
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			delete aCheckpoint.mFilterState[i];
			aCheckpoint.mFilterState[i] = NULL;
		}
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// Hand the heap snapshots owned by a journal entry's birth record to the
	// spare pools.
	static void releaseJournalEntry(Soloud &aSoloud, RetroJournalEntry &aEntry)
	{
		if (aEntry.mType != RetroJournalEntry::BIRTH)
			return;
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			aSoloud.mFilterSnapshotSpares.give(aEntry.mBirth.mFilterState[i]);
			aEntry.mBirth.mFilterState[i] = NULL;
		}
		aSoloud.mSourceSnapshotSpares.give(aEntry.mBirth.mSourceState);
		aEntry.mBirth.mSourceState = NULL;
	}

	// Time of the oldest checkpoint a rollback can still target (0 when the
	// pool is empty or unwritten). Journal entries at or before this time can
	// never be replayed again and are pruned.
	static time oldestRetainedCheckpointTime(Soloud &aSoloud)
	{
		time oldest = 0;
		bool found = false;
		unsigned int i;
		for (i = 0; i < aSoloud.mCheckpointPool.size(); i++)
		{
			const MixCheckpoint &cp = aSoloud.mCheckpointPool[i];
			if (cp.mSerial == 0 ||
				cp.mSerial + aSoloud.mCheckpointPool.size() <= aSoloud.mCheckpointCounter)
				continue;
			if (!found || cp.mTime < oldest)
			{
				oldest = cp.mTime;
				found = true;
			}
		}
		return found ? oldest : 0;
	}

	// Drop journal entries that can never be replayed again (see above).
	// Entries exactly AT the oldest retained checkpoint time are kept: a
	// rollback to that checkpoint still needs them (replay is >= based).
	// Runs on API-call threads under the audio mutex.
	static void pruneJournal(Soloud &aSoloud)
	{
		const time oldest = oldestRetainedCheckpointTime(aSoloud);
		size_t dropCount = 0;
		while (dropCount < aSoloud.mRetroJournal.size() &&
			aSoloud.mRetroJournal[dropCount].mTime < oldest)
		{
			releaseJournalEntry(aSoloud, aSoloud.mRetroJournal[dropCount]);
			dropCount++;
		}
		if (dropCount > 0)
		{
			aSoloud.mRetroJournal.erase(
				aSoloud.mRetroJournal.begin(),
				aSoloud.mRetroJournal.begin() + dropCount);
		}
	}

	// Insert an entry keeping the journal sorted by event time. When full,
	// the oldest entry is dropped (a later rollback that needed it degrades
	// to legacy behavior). Runs under the audio mutex; never allocates
	// beyond the reserved capacity.
	static void insertJournalEntry(Soloud &aSoloud, RetroJournalEntry &aEntry)
	{
		pruneJournal(aSoloud);
		size_t pos = aSoloud.mRetroJournal.size();
		while (pos > 0 &&
			(aSoloud.mRetroJournal[pos - 1].mTime > aEntry.mTime ||
			 (aSoloud.mRetroJournal[pos - 1].mTime == aEntry.mTime &&
			  aEntry.mType == RetroJournalEntry::BIRTH &&
			  aSoloud.mRetroJournal[pos - 1].mType != RetroJournalEntry::BIRTH)))
			pos--;
		if (aSoloud.mRetroJournal.size() >= RETRO_JOURNAL_CAPACITY)
		{
			releaseJournalEntry(aSoloud, aSoloud.mRetroJournal.front());
			aSoloud.mRetroJournal.erase(aSoloud.mRetroJournal.begin());
			if (pos > 0)
				pos--;
		}
		aSoloud.mRetroJournal.insert(aSoloud.mRetroJournal.begin() + pos, aEntry);
	}

	// Allocate (or free, when aPoolSize == 0) the circular checkpoint pool.
	// All buffers are pre-sized here so capture never grows a vector on the
	// audio path.
	void Soloud::allocateCheckpoints_internal(unsigned int aPoolSize)
	{
		releaseCheckpoints_internal();
		if (aPoolSize == 0)
			return;

		mCheckpointPool.resize(aPoolSize);
		mCheckpointWriteIndex = 0;
		mCheckpointCounter = 0;
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// Preallocate the in-window event journal; journal writes happen on
		// API-call threads under the audio mutex and must not grow it.
		mRetroJournal.reserve(RETRO_JOURNAL_CAPACITY);
		unsigned int i, j;
		mCheckpointRecycle.clear();
		mCheckpointRecycle.reserve(mMaxActiveVoices > 128 ? mMaxActiveVoices : 128);
		// Room for the snapshots of every voice a checkpoint slot can hold,
		// twice over, per snapshot type.
		const unsigned int spares = 2 * (mMaxActiveVoices > 128 ? mMaxActiveVoices : 128);
		mSourceSnapshotSpares.reserve(spares);
		mFilterSnapshotSpares.reserve(spares);
		for (i = 0; i < aPoolSize; i++)
		{
			MixCheckpoint &cp = mCheckpointPool[i];
			const unsigned int voiceCapacity = mMaxActiveVoices > 128 ? mMaxActiveVoices : 128;
			cp.mVoices.reserve(voiceCapacity);
			// The resample pool holds 2 blocks per active voice.
			cp.mResampleBlocks.reserve(voiceCapacity * 2);
			for (j = 0; j < voiceCapacity * 2; j++)
			{
				// Pre-size the block content buffers; capture fills only the
				// live region.
				cp.mResampleBlocks.push_back(CheckpointResampleBlock());
				cp.mResampleBlocks[j].mData.resize(SAMPLE_GRANULARITY * MAX_CHANNELS);
			}
			for (j = 0; j < FILTERS_PER_STREAM; j++)
				cp.mFilterState[j] = NULL;
		}
	}

	void Soloud::releaseCheckpoints_internal()
	{
		unsigned int i;
		for (i = 0; i < mCheckpointPool.size(); i++)
			releaseCheckpointStorage(mCheckpointPool[i]);
		mCheckpointPool.clear();
		mCheckpointWriteIndex = 0;
		mCheckpointCounter = 0;
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		for (i = 0; i < mRetroJournal.size(); i++)
			releaseJournalEntry(*this, mRetroJournal[i]);
		mRetroJournal.clear();
		mSourceSnapshotSpares.clear();
		mFilterSnapshotSpares.clear();
	}

	// Take the filter snapshot the previous capture recorded for the same
	// voice instance and filter slot out of aRecycle (ownership passes to the
	// caller), or return NULL.
	static FilterStateSnapshot *takeRecycledFilterState(std::vector<CheckpointVoice> *aRecycle,
		const int *aRecycleIdxBySlot, unsigned int aSlot, unsigned int aPlayIndex, unsigned int aFilter)
	{
		if (aRecycle == NULL)
			return NULL;
		if (aRecycleIdxBySlot != NULL && aSlot < VOICE_COUNT)
		{
			const int idx = aRecycleIdxBySlot[aSlot];
			if (idx < 0)
				return NULL;
			CheckpointVoice &old = (*aRecycle)[(size_t)idx];
			if (old.mSlot == aSlot && old.mPlayIndex == aPlayIndex)
			{
				FilterStateSnapshot *s = old.mFilterState[aFilter];
				old.mFilterState[aFilter] = NULL;
				return s;
			}
			return NULL;
		}
		size_t v;
		for (v = 0; v < aRecycle->size(); v++)
		{
			CheckpointVoice &old = (*aRecycle)[v];
			if (old.mSlot == aSlot && old.mPlayIndex == aPlayIndex)
			{
				FilterStateSnapshot *s = old.mFilterState[aFilter];
				old.mFilterState[aFilter] = NULL;
				return s;
			}
		}
		return NULL;
	}

	// Take the source snapshot the previous capture recorded for the same
	// voice instance out of aRecycle (ownership passes to the caller), or
	// return NULL.
	static SourceStateSnapshot *takeRecycledSourceState(std::vector<CheckpointVoice> *aRecycle,
		const int *aRecycleIdxBySlot, unsigned int aSlot, unsigned int aPlayIndex)
	{
		if (aRecycle == NULL)
			return NULL;
		if (aRecycleIdxBySlot != NULL && aSlot < VOICE_COUNT)
		{
			const int idx = aRecycleIdxBySlot[aSlot];
			if (idx < 0)
				return NULL;
			CheckpointVoice &old = (*aRecycle)[(size_t)idx];
			if (old.mSlot == aSlot && old.mPlayIndex == aPlayIndex)
			{
				SourceStateSnapshot *s = old.mSourceState;
				old.mSourceState = NULL;
				return s;
			}
			return NULL;
		}
		size_t v;
		for (v = 0; v < aRecycle->size(); v++)
		{
			CheckpointVoice &old = (*aRecycle)[v];
			if (old.mSlot == aSlot && old.mPlayIndex == aPlayIndex)
			{
				SourceStateSnapshot *s = old.mSourceState;
				old.mSourceState = NULL;
				return s;
			}
		}
		return NULL;
	}

	// Fill aVoiceState with the full mix-relevant POD state of the voice at
	// aSlot, plus owned heap snapshots of its filter instances and source
	// consumption state. Shared by checkpoint capture and journal births.
	// aRecycle (may be NULL) holds snapshots of an earlier capture to refill
	// in place instead of allocating new ones.
	static void fillCheckpointVoice(Soloud &aSoloud, CheckpointVoice &cv, unsigned int aSlot,
		std::vector<CheckpointVoice> *aRecycle = NULL, const int *aRecycleIdxBySlot = NULL)
	{
		AudioSourceInstance *v = aSoloud.mVoice[aSlot];
		unsigned int i;

		cv.mSlot = aSlot;
		cv.mHandle = aSoloud.getHandleFromVoice_internal(aSlot);
		cv.mPlayIndex = v->mPlayIndex;
		cv.mLoopCount = v->mLoopCount;
		cv.mFlags = v->mFlags;
		cv.mPan = v->mPan;
		for (i = 0; i < MAX_CHANNELS; i++)
		{
			cv.mChannelVolume[i] = v->mChannelVolume[i];
			cv.mCurrentChannelVolume[i] = v->mCurrentChannelVolume[i];
		}
		cv.mSetVolume = v->mSetVolume;
		cv.mOverallVolume = v->mOverallVolume;
		cv.mSamplerate = v->mSamplerate;
		cv.mChannels = v->mChannels;
		cv.mSetRelativePlaySpeed = v->mSetRelativePlaySpeed;
		cv.mOverallRelativePlaySpeed = v->mOverallRelativePlaySpeed;
		cv.mStreamTime = v->mStreamTime;
		cv.mStreamPosition = v->mStreamPosition;
		// Faders copied by value, never re-created (see header comment).
		cv.mPanFader = v->mPanFader;
		cv.mVolumeFader = v->mVolumeFader;
		cv.mRelativePlaySpeedFader = v->mRelativePlaySpeedFader;
		cv.mPauseScheduler = v->mPauseScheduler;
		cv.mStopScheduler = v->mStopScheduler;
		cv.mActiveFader = v->mActiveFader;
		cv.mAudioSourceID = v->mAudioSourceID;
		cv.mBusHandle = v->mBusHandle;
		cv.mSrcOffset = v->mSrcOffset;
		cv.mLeftoverSamples = v->mLeftoverSamples;
		// ###### flutter_soloud local patch (voice tail) ######
		cv.mSourceDrained = v->mSourceDrained;
		cv.mDelaySamples = v->mDelaySamples;
		cv.mStopSamplesLeft = v->mStopSamplesLeft;
		cv.mLoopPoint = v->mLoopPoint;
		cv.mLoopEndPoint = v->mLoopEndPoint;
		cv.mLoopStartFrame = v->mLoopStartFrame;
		cv.mLoopEndFrame = v->mLoopEndFrame;
		cv.mSourceSamplePosition = v->mSourceSamplePosition;

		// Resample ping-pong pointers become pool block indices.
		for (i = 0; i < 2; i++)
		{
			cv.mResampleBlock[i] = -1;
			if (v->mResampleData[i] != NULL && aSoloud.mResampleDataBuffer.mData != NULL)
			{
				size_t off = (size_t)(v->mResampleData[i] - aSoloud.mResampleDataBuffer.mData);
				cv.mResampleBlock[i] = (int)(off / (SAMPLE_GRANULARITY * MAX_CHANNELS));
			}
		}

		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			cv.mFilterLive[i] = v->mFilter[i] != NULL;
			cv.mFilterId[i] = v->mFilter[i] != NULL ? v->mFilter[i]->mInstanceId : 0;
			// Refill, in order of preference, the snapshot this same filter
			// produced last time, a spare of the same type, or allocate.
			FilterStateSnapshot *reuse = takeRecycledFilterState(aRecycle, aRecycleIdxBySlot, aSlot, v->mPlayIndex, i);
			FilterInstance *filter = v->mFilter[i];
			cv.mFilterState[i] = NULL;
			if (filter != NULL)
			{
				if (reuse != NULL && filter->captureStateInto(reuse))
				{
					cv.mFilterState[i] = reuse;
					reuse = NULL;
				}
				else
				{
					cv.mFilterState[i] = aSoloud.mFilterSnapshotSpares.take(
						[filter](FilterStateSnapshot *s) { return filter->captureStateInto(s); });
				}
				if (cv.mFilterState[i] == NULL)
					cv.mFilterState[i] = filter->captureState();
			}
			aSoloud.mFilterSnapshotSpares.give(reuse);
		}
		SourceStateSnapshot *reuse = takeRecycledSourceState(aRecycle, aRecycleIdxBySlot, aSlot, v->mPlayIndex);
		if (reuse != NULL && v->captureSourceStateInto(reuse))
		{
			cv.mSourceState = reuse;
			reuse = NULL;
		}
		else
		{
			cv.mSourceState = aSoloud.mSourceSnapshotSpares.take(
				[v](SourceStateSnapshot *s) { return v->captureSourceStateInto(s); });
		}
		if (cv.mSourceState == NULL)
			cv.mSourceState = v->captureSourceState();
		aSoloud.mSourceSnapshotSpares.give(reuse);
	}

	// Whether a voice described by a snapshot can be re-mixed bit-exactly:
	// its source and every live filter instance produced a complete snapshot.
	// A re-mix runs the voice's DSP over the rolled-back window a second time;
	// without a complete snapshot that DSP would continue from state already
	// advanced past the window (delay lines, envelopes, oscillator phase, a
	// user callback's or VST3 plugin's internal state) and the rewritten
	// audio would not match what was already heard. Such a rollback degrades
	// to legacy (going-forward) placement instead.
	// Paused voices are exempt: neither their source nor their filters run
	// while paused (the mix skips them), so their state does not advance.
	static bool voiceSnapshotRestorable(const CheckpointVoice &aVoiceState)
	{
		if (aVoiceState.mFlags & AudioSourceInstance::PAUSED)
			return true;
		if (aVoiceState.mSourceState == NULL || !aVoiceState.mSourceState->mComplete)
			return false;
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (aVoiceState.mFilterLive[i] &&
				(aVoiceState.mFilterState[i] == NULL ||
				 !aVoiceState.mFilterState[i]->mComplete))
				return false;
		}
		return true;
	}

	// Whether aVoice still has exactly the filter instances aVoiceState was
	// captured with. Filter snapshots are concrete per filter type and are
	// restored with a static_cast, so a snapshot must never reach a different
	// instance (a filter removed, added, moved to another slot or replaced
	// since the capture).
	static bool voiceFiltersMatch(const CheckpointVoice &aVoiceState, const AudioSourceInstance *aVoice)
	{
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			const unsigned long long live =
				aVoice->mFilter[i] != NULL ? aVoice->mFilter[i]->mInstanceId : 0;
			if (live != aVoiceState.mFilterId[i])
				return false;
		}
		return true;
	}

	// Copy the POD fields of a voice snapshot back onto a live instance.
	// Handles, resample pointers, filter instance pointers and source/filter
	// heap snapshots are NOT touched here (the callers handle them).
	static void applyCheckpointVoice(const CheckpointVoice &cv, AudioSourceInstance *v)
	{
		unsigned int i;
		v->mLoopCount = cv.mLoopCount;
		v->mFlags = cv.mFlags;
		v->mPan = cv.mPan;
		for (i = 0; i < MAX_CHANNELS; i++)
		{
			v->mChannelVolume[i] = cv.mChannelVolume[i];
			v->mCurrentChannelVolume[i] = cv.mCurrentChannelVolume[i];
		}
		v->mSetVolume = cv.mSetVolume;
		v->mOverallVolume = cv.mOverallVolume;
		v->mSamplerate = cv.mSamplerate;
		v->mChannels = cv.mChannels;
		v->mSetRelativePlaySpeed = cv.mSetRelativePlaySpeed;
		v->mOverallRelativePlaySpeed = cv.mOverallRelativePlaySpeed;
		v->mStreamTime = cv.mStreamTime;
		v->mStreamPosition = cv.mStreamPosition;
		// Faders restored by value before anything ticks them against
		// restored-past times (see the fader hazard note at the top).
		v->mPanFader = cv.mPanFader;
		v->mVolumeFader = cv.mVolumeFader;
		v->mRelativePlaySpeedFader = cv.mRelativePlaySpeedFader;
		v->mPauseScheduler = cv.mPauseScheduler;
		v->mStopScheduler = cv.mStopScheduler;
		v->mActiveFader = cv.mActiveFader;
		v->mAudioSourceID = cv.mAudioSourceID;
		v->mBusHandle = cv.mBusHandle;
		v->mSrcOffset = cv.mSrcOffset;
		v->mLeftoverSamples = cv.mLeftoverSamples;
		// ###### flutter_soloud local patch (voice tail) ######
		v->mSourceDrained = cv.mSourceDrained;
		v->mDelaySamples = cv.mDelaySamples;
		if (cv.mStopSamplesLeft >= 0 || v->mStopSamplesLeft < 0)
			v->mStopSamplesLeft = cv.mStopSamplesLeft;
		v->mLoopPoint = cv.mLoopPoint;
		v->mLoopEndPoint = cv.mLoopEndPoint;
		v->mLoopStartFrame = cv.mLoopStartFrame;
		v->mLoopEndFrame = cv.mLoopEndFrame;
		v->mSourceSamplePosition = cv.mSourceSamplePosition;
		// mBaseSamplerate is immutable; mFilter pointers are instance
		// identity.
	}

	// Snapshot one voice into aCheckpoint. Appends to mVoices; the caller
	// guarantees capacity (no allocation on the audio path).
	static void captureVoice(Soloud &aSoloud, MixCheckpoint &aCheckpoint, unsigned int aSlot,
		std::vector<CheckpointVoice> *aRecycle, const int *aRecycleIdxBySlot)
	{
		CheckpointVoice cv;
		fillCheckpointVoice(aSoloud, cv, aSlot, aRecycle, aRecycleIdxBySlot);
		aCheckpoint.mVoices.push_back(cv);
	}

	void Soloud::captureMixCheckpoint_internal()
	{
		if (mCheckpointPool.empty())
			return;

		unsigned int targetIndex = mCheckpointWriteIndex;
		bool foundExisting = false;
		if (mRemixing)
		{
			unsigned int k;
			for (k = 0; k < mCheckpointPool.size(); k++)
			{
				if (mCheckpointPool[k].mSerial != 0 &&
					fabs(mCheckpointPool[k].mTime - mStreamTime) < 1e-6)
				{
					targetIndex = k;
					foundExisting = true;
					break;
				}
			}
		}

		if (!foundExisting)
		{
			mCheckpointWriteIndex = (mCheckpointWriteIndex + 1) % (unsigned int)mCheckpointPool.size();
			mCheckpointCounter++;
		}

		MixCheckpoint &cp = mCheckpointPool[targetIndex];
		// Set the slot's previous voices aside (swap keeps both vectors'
		// reserved capacity, so nothing allocates) so their snapshots can be
		// refilled below by the same voices. The snapshots of voices that have
		// ended since become spares right away, for voices started since.
		mCheckpointRecycle.clear();
		std::swap(cp.mVoices, mCheckpointRecycle);
		int recycleIdxBySlot[VOICE_COUNT];
		if (mHighestVoice > 0)
			memset(recycleIdxBySlot, -1, sizeof(int) * mHighestVoice);
		{
			size_t r;
			unsigned int f;
			for (r = 0; r < mCheckpointRecycle.size(); r++)
			{
				CheckpointVoice &old = mCheckpointRecycle[r];
				if (old.mSlot < mHighestVoice && mVoice[old.mSlot] != NULL &&
					mVoice[old.mSlot]->mPlayIndex == old.mPlayIndex)
				{
					recycleIdxBySlot[old.mSlot] = (int)r;
					continue;
				}
				mSourceSnapshotSpares.give(old.mSourceState);
				old.mSourceState = NULL;
				for (f = 0; f < FILTERS_PER_STREAM; f++)
				{
					mFilterSnapshotSpares.give(old.mFilterState[f]);
					old.mFilterState[f] = NULL;
				}
			}
			for (f = 0; f < FILTERS_PER_STREAM; f++)
			{
				delete cp.mFilterState[f];
				cp.mFilterState[f] = NULL;
			}
		}
		cp.mSerial = mCheckpointCounter;
		cp.mTime = mStreamTime;
		if (mMixingToRenderRing)
		{
			cp.mRingWritePosition = mRenderRing.getWritePosition() + mBufferSize;
		}
		else if (mRemixing)
		{
			cp.mRingWritePosition = mRemixStartPos;
		}
		else if (mSamplerate > 0)
		{
			long long framesMixed = (long long)floor(mStreamTime * (time)mSamplerate + 0.5);
			cp.mRingWritePosition = (unsigned long long)(framesMixed >= 0 ? framesMixed : 0);
		}
		else
		{
			cp.mRingWritePosition = 0;
		}

		// Engine-level POD state.
		cp.mGlobalVolume = mGlobalVolume;
		cp.mGlobalVolumeFader = mGlobalVolumeFader;
		cp.mHighestVoice = mHighestVoice;
		cp.mPlayIndex = mPlayIndex;
		cp.mAudioSourceID = mAudioSourceID;
		if (mActiveVoiceCount > 0)
			memcpy(cp.mActiveVoice, mActiveVoice, sizeof(unsigned int) * mActiveVoiceCount);
		cp.mActiveVoiceCount = mActiveVoiceCount;
		cp.mActiveVoiceDirty = mActiveVoiceDirty;
		cp.mClockedAnchorTime = mClockedAnchorTime;
		cp.mClockedAnchorSample = mClockedAnchorSample;
		cp.mClockedLastTime = mClockedLastTime;
		unsigned int i;
		bool restorable = true;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			// Checkpoints only exist with the render-ahead ring, where the
			// global filters run post-ring at the playhead (in the device
			// callback, under their own lock): a re-mix never rolls them
			// back, so their state is not captured.
			cp.mFilterState[i] = NULL;
		}

		// Voices. Pass 1: the active (sounding) voices, so they are always
		// covered even if the total voice count exceeds the reserved
		// capacity. Pass 2: the remaining live voices (paused, inaudible) in
		// slot order, until capacity. Voices beyond capacity are treated as
		// absent on restore and left running (see restoreMixCheckpoint).
		unsigned char slotCaptured[VOICE_COUNT];
		if (mHighestVoice > 0)
			memset(slotCaptured, 0, mHighestVoice);
		const size_t capacity = cp.mVoices.capacity();
		for (i = 0; i < mActiveVoiceCount && cp.mVoices.size() < capacity; i++)
		{
			unsigned int slot = mActiveVoice[i];
			if (slot < mHighestVoice && mVoice[slot] != NULL && !slotCaptured[slot])
			{
				slotCaptured[slot] = 1;
				captureVoice(*this, cp, slot, &mCheckpointRecycle, recycleIdxBySlot);
			}
		}
		for (i = 0; i < mHighestVoice && cp.mVoices.size() < capacity; i++)
		{
			if (mVoice[i] == NULL || slotCaptured[i])
				continue;
			slotCaptured[i] = 1;
			captureVoice(*this, cp, i, &mCheckpointRecycle, recycleIdxBySlot);
		}
		// Park whatever the previous voices left unclaimed (voices beyond the
		// capture capacity, removed filters) as spares.
		{
			size_t r;
			unsigned int f;
			for (r = 0; r < mCheckpointRecycle.size(); r++)
			{
				mSourceSnapshotSpares.give(mCheckpointRecycle[r].mSourceState);
				mCheckpointRecycle[r].mSourceState = NULL;
				for (f = 0; f < FILTERS_PER_STREAM; f++)
				{
					mFilterSnapshotSpares.give(mCheckpointRecycle[r].mFilterState[f]);
					mCheckpointRecycle[r].mFilterState[f] = NULL;
				}
			}
			mCheckpointRecycle.clear();
		}

		// Resample pool: snapshot every block that has an owner. Owner entry o
		// owns the consecutive block pair 2o and 2o+1 (see
		// mapResampleBuffers_internal). Only the live region (owner's
		// channels * SAMPLE_GRANULARITY floats, stored channel-planar
		// contiguously) is copied; the reserved blocks are reused in place.
		int ownerSlotByIdx[VOICE_COUNT];
		if (mMaxActiveVoices > 0)
			memset(ownerSlotByIdx, -1, sizeof(int) * mMaxActiveVoices);
		unsigned int liveVoices = 0;
		for (i = 0; i < mHighestVoice; i++)
		{
			AudioSourceInstance *v = mVoice[i];
			if (v == NULL)
				continue;
			liveVoices++;
			if (v->mResampleData[0] != NULL && mResampleDataBuffer.mData != NULL)
			{
				size_t off = (size_t)(v->mResampleData[0] - mResampleDataBuffer.mData);
				unsigned int pairIdx = (unsigned int)(off / (2 * SAMPLE_GRANULARITY * MAX_CHANNELS));
				if (pairIdx < mMaxActiveVoices && mResampleDataOwner[pairIdx] == v)
					ownerSlotByIdx[pairIdx] = (int)i;
			}
		}
		unsigned int usedBlocks = 0;
		for (i = 0; i < mMaxActiveVoices && usedBlocks < cp.mResampleBlocks.size(); i++)
		{
			if (mResampleDataOwner[i] == NULL)
				continue;
			int ownerSlot = ownerSlotByIdx[i];
			if (ownerSlot < 0)
				continue;
			unsigned int k;
			for (k = 0; k < 2 && usedBlocks < cp.mResampleBlocks.size(); k++)
			{
				CheckpointResampleBlock &b = cp.mResampleBlocks[usedBlocks++];
				b.mBlock = (int)(i * 2 + k);
				b.mOwnerSlot = ownerSlot;
				b.mChannels = mResampleDataOwner[i]->mChannels;
				unsigned int liveFloats = b.mChannels * SAMPLE_GRANULARITY;
				if (liveFloats > (unsigned int)b.mData.size())
					liveFloats = (unsigned int)b.mData.size();
				memcpy(b.mData.data(), mResampleData[i * 2 + k], liveFloats * sizeof(float));
			}
		}
		// Mark the remaining reserved blocks unused for this capture.
		for (i = usedBlocks; i < cp.mResampleBlocks.size(); i++)
			cp.mResampleBlocks[i].mBlock = -1;

		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// Restorability gate for rollbacks targeting this checkpoint: every
		// live voice must be captured and restorable (an uncaptured voice
		// would keep its post-window state across the rollback and jump
		// ahead in the re-mix), and no live global filter may be
		// unrestorable (computed above).
		if ((size_t)liveVoices != cp.mVoices.size())
			restorable = false;
		size_t v;
		for (v = 0; v < cp.mVoices.size(); v++)
		{
			if (!voiceSnapshotRestorable(cp.mVoices[v]))
				restorable = false;
		}
		cp.mRestorable = restorable;
	}

	int Soloud::findCheckpointAtOrBefore_internal(time aTime)
	{
		int found = -1;
		unsigned int i;
		for (i = 0; i < mCheckpointPool.size(); i++)
		{
			const MixCheckpoint &cp = mCheckpointPool[i];
			// A slot is valid while its serial is within one pool size of the
			// capture counter; older slots have been overwritten.
			if (cp.mSerial == 0 || cp.mSerial + mCheckpointPool.size() <= mCheckpointCounter)
				continue;
			if (cp.mTime > aTime)
				continue;
			// A re-mix re-captures boundaries it rewrites, so two slots can
			// share a time: prefer the newer capture (higher serial), which
			// incorporates the retroactive event.
			if (found < 0 || cp.mTime > mCheckpointPool[found].mTime ||
				(cp.mTime == mCheckpointPool[found].mTime &&
				 cp.mSerial > mCheckpointPool[found].mSerial))
				found = (int)i;
		}
		return found;
	}

	bool Soloud::restoreMixCheckpoint_internal(int aPoolIndex)
	{
		if (aPoolIndex < 0 || aPoolIndex >= (int)mCheckpointPool.size())
			return false;
		MixCheckpoint &cp = mCheckpointPool[aPoolIndex];
		if (cp.mSerial == 0 || cp.mSerial + mCheckpointPool.size() <= mCheckpointCounter)
			return false;

		unsigned int i;
		size_t v;

		// Engine-level POD state. mPlayIndex and mAudioSourceID are
		// monotonic id counters: voices and sources created after the
		// checkpoint must keep their ids, so restore takes the max instead of
		// rolling the counter back (which would reissue existing handles).
		const time oldStreamTime = mStreamTime;
		const long long rollbackSamples =
			(oldStreamTime > cp.mTime && mSamplerate > 0)
				? (long long)floor((oldStreamTime - cp.mTime) * (time)mSamplerate + 0.5)
				: 0;
		for (i = 0; i < mHighestVoice; i++)
		{
			if (mVoice[i] != NULL && mVoice[i]->mStopSamplesLeft >= 0)
			{
				mVoice[i]->mStopSamplesLeft += rollbackSamples;
			}
		}
		mStreamTime = cp.mTime;
		mGlobalVolume = cp.mGlobalVolume;
		mGlobalVolumeFader = cp.mGlobalVolumeFader;
		// Voices created after the checkpoint are left running (Phase 2
		// policy), so the voice table must stay at least as tall as it is.
		mHighestVoice = cp.mHighestVoice > mHighestVoice ? cp.mHighestVoice : mHighestVoice;
		mPlayIndex = cp.mPlayIndex > mPlayIndex ? cp.mPlayIndex : mPlayIndex;
		mAudioSourceID = cp.mAudioSourceID > mAudioSourceID ? cp.mAudioSourceID : mAudioSourceID;
		mClockedAnchorTime = cp.mClockedAnchorTime;
		mClockedAnchorSample = cp.mClockedAnchorSample;
		mClockedLastTime = cp.mClockedLastTime;

		// Global filters are never restored: they run after the ring, on the
		// device's timeline (see captureMixCheckpoint_internal).

		// Voices. slotRestored tracks which current voice objects were
		// restored from the checkpoint; anything else still in the table is a
		// voice the checkpoint does not cover (created after it, or past the
		// capture capacity) and is left running.
		unsigned char slotRestored[VOICE_COUNT];
		memset(slotRestored, 0, sizeof(slotRestored));
		for (v = 0; v < cp.mVoices.size(); v++)
		{
			const CheckpointVoice &cv = cp.mVoices[v];
			if (cv.mSlot >= VOICE_COUNT)
				continue;
			AudioSourceInstance *voice = mVoice[cv.mSlot];
			// Same slot + same play index = same object. A mismatch means the
			// slot was reused by a different voice after the checkpoint; skip
			// (its current state is authoritative going forward).
			if (voice == NULL || voice->mPlayIndex != cv.mPlayIndex)
			{
#ifndef NDEBUG
				fprintf(stderr, "SoLoud: restoreMixCheckpoint skips slot %u (voice %s)\n",
					cv.mSlot, voice == NULL ? "gone" : "reused");
#endif
				continue;
			}
			slotRestored[cv.mSlot] = 1;

			voice->mPlayIndex = cv.mPlayIndex;
			const long long stopSamplesLeft = voice->mStopSamplesLeft;
			applyCheckpointVoice(cv, voice);
			if (stopSamplesLeft >= 0)
			{
				voice->mStopSamplesLeft = stopSamplesLeft;
			}
			// Resample pointers are rebuilt from block indices below, after
			// the pool contents are restored.

			if (cv.mSourceState != NULL)
				voice->restoreSourceState(cv.mSourceState);
			for (i = 0; i < FILTERS_PER_STREAM; i++)
			{
				if (cv.mFilterState[i] != NULL && voice->mFilter[i] != NULL &&
					voice->mFilter[i]->mInstanceId == cv.mFilterId[i])
					voice->mFilter[i]->restoreState(cv.mFilterState[i]);
			}
		}

		// Resample pool: rebuild the owner table from the checkpoint, restore
		// block contents, then point each restored voice's ping-pong pointers
		// at its blocks.
		for (i = 0; i < mMaxActiveVoices; i++)
			mResampleDataOwner[i] = NULL;
		for (v = 0; v < cp.mResampleBlocks.size(); v++)
		{
			const CheckpointResampleBlock &b = cp.mResampleBlocks[v];
			if (b.mBlock < 0)
				break;
			if (b.mOwnerSlot < 0 || b.mOwnerSlot >= (int)mHighestVoice ||
				!slotRestored[b.mOwnerSlot])
				continue; // owner voice did not survive the restore
			if ((unsigned int)b.mBlock >= mMaxActiveVoices * 2)
				continue; // captured against a larger, since replaced pool
			// Owner entry o owns the block pair 2o and 2o+1.
			mResampleDataOwner[b.mBlock / 2] = mVoice[b.mOwnerSlot];
			unsigned int liveFloats = b.mChannels * SAMPLE_GRANULARITY;
			if (liveFloats > (unsigned int)b.mData.size())
				liveFloats = (unsigned int)b.mData.size();
			memcpy(mResampleData[b.mBlock], b.mData.data(), liveFloats * sizeof(float));
		}
		for (v = 0; v < cp.mVoices.size(); v++)
		{
			const CheckpointVoice &cv = cp.mVoices[v];
			if (cv.mSlot >= VOICE_COUNT || !slotRestored[cv.mSlot])
				continue;
			AudioSourceInstance *voice = mVoice[cv.mSlot];
			for (i = 0; i < 2; i++)
			{
				voice->mResampleData[i] =
					(cv.mResampleBlock[i] >= 0 &&
					 (unsigned int)cv.mResampleBlock[i] < mMaxActiveVoices * 2)
					? mResampleData[cv.mResampleBlock[i]] : NULL;
			}
		}
		// Voices the checkpoint does not cover keep their pool blocks;
		// re-register their ownership so mapResampleBuffers_internal() stays
		// consistent. (Phase 2 leaves such voices running untouched.)
		for (i = 0; i < mHighestVoice; i++)
		{
			AudioSourceInstance *voice = mVoice[i];
			if (voice == NULL || slotRestored[i] || voice->mResampleData[0] == NULL)
				continue;
			size_t off = (size_t)(voice->mResampleData[0] - mResampleDataBuffer.mData);
			unsigned int block = (unsigned int)(off / (SAMPLE_GRANULARITY * MAX_CHANNELS));
			if (block < mMaxActiveVoices * 2 && mResampleDataOwner[block / 2] == NULL)
				mResampleDataOwner[block / 2] = voice;
		}

		// Active voice list: restore it exactly when the voice set is
		// unchanged (floating-point summation order depends on it); any
		// uncovered or skipped voice forces a recalculation instead.
		bool exact = true;
		for (i = 0; i < mHighestVoice; i++)
		{
			if (mVoice[i] != NULL && !slotRestored[i])
			{
				exact = false;
				break;
			}
		}
		if (exact)
		{
			if (cp.mActiveVoiceCount > 0)
				memcpy(mActiveVoice, cp.mActiveVoice, sizeof(unsigned int) * cp.mActiveVoiceCount);
			mActiveVoiceCount = cp.mActiveVoiceCount;
			mActiveVoiceDirty = cp.mActiveVoiceDirty;
		}
		else
		{
			mActiveVoiceDirty = true;
		}

		reapply3dVoices_internal();
		return true;
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######

	time Soloud::playheadTimeLocked_internal()
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
#if defined(SOLOUD_RETRO_TEST_HOOKS)
		if (mPlayheadTestHook != nullptr)
			mPlayheadTestHook(this);
#endif
		time t = mStreamTime;
		if (isRenderAheadEnabled() && mRenderRing.isInited() && mSamplerate > 0)
			t -= (time)mRenderRing.availableToRead() / (time)mSamplerate;
		return t > 0 ? t : 0;
	}

	void Soloud::journalBirth_internal(unsigned int aSlot, time aBirthTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (!mRenderRing.isInited() || aSlot >= VOICE_COUNT || mVoice[aSlot] == NULL)
			return;
		const unsigned int playIndex = mVoice[aSlot]->mPlayIndex;

		// Upsert by slot + play index: composite play paths (playScheduled,
		// playClocked) journal once after their final configuration is
		// applied, replacing the record the inner play() made.
		size_t i;
		for (i = 0; i < mRetroJournal.size(); i++)
		{
			RetroJournalEntry &e = mRetroJournal[i];
			if (e.mType == RetroJournalEntry::BIRTH && e.mSlot == aSlot &&
				e.mPlayIndex == playIndex)
			{
				releaseJournalEntry(*this, e);
				mRetroJournal.erase(mRetroJournal.begin() + i);
				break;
			}
		}

		RetroJournalEntry e;
		e.mType = RetroJournalEntry::BIRTH;
		e.mTime = aBirthTime;
		e.mSnapshotEngineTime = mStreamTime;
		e.mSlot = aSlot;
		e.mPlayIndex = playIndex;
		e.mBirth.mSourceState = NULL;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
			e.mBirth.mFilterState[i] = NULL;
		fillCheckpointVoice(*this, e.mBirth, aSlot);
		e.mRestorable = voiceSnapshotRestorable(e.mBirth);
		insertJournalEntry(*this, e);
	}

	void Soloud::journalDeath_internal(unsigned int aSlot, unsigned int aPlayIndex)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (!mRenderRing.isInited() || mRemixing)
			return;
		RetroJournalEntry e;
		e.mType = RetroJournalEntry::DEATH;
		e.mTime = mStreamTime;
		e.mSnapshotEngineTime = mStreamTime;
		e.mSlot = aSlot;
		e.mPlayIndex = aPlayIndex;
		e.mRestorable = false;
		e.mBirth.mSourceState = NULL;
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
			e.mBirth.mFilterState[i] = NULL;
		insertJournalEntry(*this, e);
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	void Soloud::journalParam_internal(unsigned int aSlot, unsigned int aPlayIndex, int aParam, float aValue, time aTime, time aDuration)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (!mRenderRing.isInited())
			return;

		// Upsert by slot + playIndex + param: a voice only has one fader per
		// parameter, so a newer fade or setter on the same parameter replaces
		// any earlier scheduled fade on that parameter.
		size_t i = 0;
		while (i < mRetroJournal.size())
		{
			RetroJournalEntry &existing = mRetroJournal[i];
			if (existing.mType == RetroJournalEntry::PARAM &&
				existing.mSlot == aSlot &&
				existing.mPlayIndex == aPlayIndex &&
				existing.mParam == aParam)
			{
				releaseJournalEntry(*this, existing);
				mRetroJournal.erase(mRetroJournal.begin() + i);
			}
			else
			{
				i++;
			}
		}

		float fromVal = aValue;
		if (aSlot < VOICE_COUNT && mVoice[aSlot] != NULL && mVoice[aSlot]->mPlayIndex == aPlayIndex)
		{
			switch (aParam)
			{
			case RetroJournalEntry::PARAM_VOLUME:
				fromVal = mVoice[aSlot]->mVolumeFader.mActive == 3
					? mVoice[aSlot]->mVolumeFader.mFrom
					: mVoice[aSlot]->mSetVolume;
				break;
			case RetroJournalEntry::PARAM_PAN:
				fromVal = mVoice[aSlot]->mPanFader.mActive == 3
					? mVoice[aSlot]->mPanFader.mFrom
					: mVoice[aSlot]->mPan;
				break;
			case RetroJournalEntry::PARAM_SPEED:
				fromVal = mVoice[aSlot]->mRelativePlaySpeedFader.mActive == 3
					? mVoice[aSlot]->mRelativePlaySpeedFader.mFrom
					: mVoice[aSlot]->mSetRelativePlaySpeed;
				break;
			default:
				fromVal = 1.0f;
				break;
			}
		}

		RetroJournalEntry e;
		e.mType = RetroJournalEntry::PARAM;
		e.mTime = aTime;
		e.mSnapshotEngineTime = mStreamTime;
		e.mSlot = aSlot;
		e.mPlayIndex = aPlayIndex;
		e.mRestorable = true;
		e.mParam = aParam;
		e.mFrom = fromVal;
		e.mValue = aValue;
		e.mDuration = aDuration;
		e.mBirth = CheckpointVoice();
		e.mBirth.mSourceState = NULL;
		insertJournalEntry(*this, e);
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	void Soloud::voiceStateChanged_internal(unsigned int aVoice)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		// Changes a rollback makes itself (restore, replay, re-mix) are not
		// user changes.
		if (mRemixing || mCheckpointPool.empty() || aVoice >= VOICE_COUNT ||
			mVoice[aVoice] == NULL)
			return;
		AudioSourceInstance *v = mVoice[aVoice];
		const unsigned int playIndex = v->mPlayIndex;
		const time halfSample = mSamplerate > 0 ? 0.5 / (time)mSamplerate : 0;

		size_t j;
		for (j = 0; j < mRetroJournal.size(); j++)
		{
			RetroJournalEntry &e = mRetroJournal[j];
			if (e.mType != RetroJournalEntry::BIRTH || e.mSlot != aVoice ||
				e.mPlayIndex != playIndex)
				continue;
			if (e.mSnapshotEngineTime >= mStreamTime - halfSample)
			{
				// No quantum has been mixed since the birth was recorded (the
				// usual case: a voice configured right after play()), so the
				// record can describe the voice as it is now.
				const unsigned int oldDelay = e.mBirth.mDelaySamples;
				unsigned int k;
				for (k = 0; k < FILTERS_PER_STREAM; k++)
				{
					mFilterSnapshotSpares.give(e.mBirth.mFilterState[k]);
					e.mBirth.mFilterState[k] = NULL;
				}
				mSourceSnapshotSpares.give(e.mBirth.mSourceState);
				e.mBirth.mSourceState = NULL;
				fillCheckpointVoice(*this, e.mBirth, aVoice);
				e.mRestorable = voiceSnapshotRestorable(e.mBirth);
				if (v->mDelaySamples != oldDelay && mSamplerate > 0)
				{
					// The first sounding sample moved with the delay; keep
					// the journal sorted by event time.
					RetroJournalEntry moved = e;
					moved.mTime += ((time)v->mDelaySamples - (time)oldDelay) / (time)mSamplerate;
					mRetroJournal.erase(mRetroJournal.begin() + j);
					insertJournalEntry(*this, moved);
				}
			}
			else
			{
				e.mRestorable = false;
			}
			break;
		}

		unsigned int i;
		for (i = 0; i < mCheckpointPool.size(); i++)
		{
			MixCheckpoint &cp = mCheckpointPool[i];
			if (!cp.mRestorable)
				continue;
			size_t k;
			for (k = 0; k < cp.mVoices.size(); k++)
			{
				if (cp.mVoices[k].mSlot == aVoice && cp.mVoices[k].mPlayIndex == playIndex)
				{
					cp.mRestorable = false;
					break;
				}
			}
		}
	}

	void Soloud::engineStateChanged_internal()
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (mRemixing)
			return;
		unsigned int i;
		for (i = 0; i < mCheckpointPool.size(); i++)
			mCheckpointPool[i].mRestorable = false;
	}

	void Soloud::reapply3dVoices_internal()
	{
		// Same locking as restoreMixCheckpoint_internal(), its caller.
		unsigned int i;
		for (i = 0; i < mHighestVoice; i++)
		{
			AudioSourceInstance *v = mVoice[i];
			if (v == NULL || !(v->mFlags & AudioSourceInstance::PROCESS_3D))
				continue;
			// The same derivation update3dAudio() applies, from the values
			// it last computed.
			updateVoiceRelativePlaySpeed_internal(i);
			updateVoiceVolume_internal(i);
			unsigned int j;
			for (j = 0; j < MAX_CHANNELS; j++)
				v->mChannelVolume[j] = m3dData[i].mChannelVolume[j];
			if (v->mOverallVolume < 0.001f)
				v->mFlags |= AudioSourceInstance::INAUDIBLE;
			else
				v->mFlags &= ~AudioSourceInstance::INAUDIBLE;
		}
	}

	void Soloud::replayJournal_internal(time aFromTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		mRemixSuppressEndedCount = 0;
		size_t i;
		unsigned int k;
		for (i = 0; i < mRetroJournal.size(); i++)
		{
			const RetroJournalEntry &e = mRetroJournal[i];
			// >=, not >: an event exactly at the checkpoint time postdates the
			// capture (the checkpoint was taken before the API call landed),
			// so it is not reflected in the restored state and must replay.
			if (e.mTime < aFromTime)
				continue;
			if (e.mSlot >= VOICE_COUNT)
				continue;
			AudioSourceInstance *v = mVoice[e.mSlot];
			// Same slot + same play index = same object. A mismatch means the
			// slot was stolen or the voice was stopped after the checkpoint:
			// drop the entry (its audio in the rewritten window is lost --
			// the instance is gone and replay must never dereference the
			// source to create a new one).
			if (v == NULL || v->mPlayIndex != e.mPlayIndex)
				continue;

			if (e.mType == RetroJournalEntry::BIRTH)
			{
				const long long stopSamplesLeft = v->mStopSamplesLeft;
				// Reset the live instance to its birth state, with the start
				// delay recomputed relative to the restore point.
				applyCheckpointVoice(e.mBirth, v);
				if (stopSamplesLeft >= 0)
				{
					v->mStopSamplesLeft = stopSamplesLeft;
				}
				// The re-mix ticks the voice from the checkpoint time on,
				// earlier than its real insertion; shift the voice's stream
				// clocks back by the difference so they land exactly where
				// the original timeline put them.
				const time shift = e.mSnapshotEngineTime - aFromTime;
				v->mStreamTime = e.mBirth.mStreamTime - shift;
				v->mStreamPosition = e.mBirth.mStreamPosition -
					shift * (time)e.mBirth.mOverallRelativePlaySpeed;
				v->mDelaySamples = (unsigned int)floor(
					(e.mTime - aFromTime) * (time)mSamplerate + 0.5);
				// Pristine resampler state: the birth snapshot predates any
				// pool block assignment; the next calcActiveVoices_internal
				// re-assigns and zeroes the blocks. Clear any owner entry that
				// restoreMixCheckpoint_internal may have registered for this
				// un-restored slot so mapResampleBuffers_internal actually
				// assigns fresh blocks instead of leaving mResampleData NULL.
				for (k = 0; k < mMaxActiveVoices; k++)
				{
					if (mResampleDataOwner[k] == v)
						mResampleDataOwner[k] = NULL;
				}
				v->mResampleData[0] = NULL;
				v->mResampleData[1] = NULL;
				v->mSrcOffset = 0;
				v->mLeftoverSamples = 0;
				// ###### flutter_soloud local patch (voice tail) ######
				v->mSourceDrained = false;
				if (e.mBirth.mSourceState != NULL)
					v->restoreSourceState(e.mBirth.mSourceState);
				for (k = 0; k < FILTERS_PER_STREAM; k++)
				{
					if (e.mBirth.mFilterState[k] != NULL && v->mFilter[k] != NULL &&
						v->mFilter[k]->mInstanceId == e.mBirth.mFilterId[k])
						v->mFilter[k]->restoreState(e.mBirth.mFilterState[k]);
				}
				mActiveVoiceDirty = true;
			}
			else if (e.mType == RetroJournalEntry::PARAM)
			{
				// Re-apply a parameter change as a scheduled fader at its
				// original time, anchored on the voice's (restored) stream
				// clock.
				const time voiceTime = v->mStreamTime + (e.mTime - aFromTime);
				const time dur = e.mDuration > 0 ? e.mDuration : RETRO_FADE_TIME;
				switch (e.mParam)
				{
				case RetroJournalEntry::PARAM_VOLUME:
					v->mVolumeFader.setScheduled(e.mFrom, e.mValue,
						dur, voiceTime);
					break;
				case RetroJournalEntry::PARAM_PAN:
					v->mPanFader.setScheduled(e.mFrom, e.mValue,
						dur, voiceTime);
					break;
				case RetroJournalEntry::PARAM_SPEED:
					v->mRelativePlaySpeedFader.setScheduled(
						e.mFrom, e.mValue, dur,
						voiceTime);
					break;
				case RetroJournalEntry::PARAM_PAUSE:
					if (!(v->mFlags & AudioSourceInstance::PAUSED))
						v->mPauseScheduler.setScheduled(1, 0, dur, voiceTime);
					break;
				case RetroJournalEntry::PARAM_STOP:
					v->mStopScheduler.setScheduled(1, 0, dur, voiceTime);
					break;
				}
			}
			else // DEATH
			{
				// Re-apply the stop as a scheduled absolute-time stop at the
				// journaled time, using the mStopSamplesLeft countdown (the
				// same machinery scheduleStopAt uses).
				long long samples = (long long)floor(
					(e.mTime - aFromTime) * (time)mSamplerate + 0.5);
				v->mStopSamplesLeft = samples;
				// The original stop already queued or dispatched its ended
				// event; the re-mix's reproduction must not fire it again.
				if (mRemixSuppressEndedCount < RETRO_JOURNAL_CAPACITY)
				{
					mRemixSuppressEnded[mRemixSuppressEndedCount++] =
						(e.mSlot + 1) | (e.mPlayIndex << 12);
				}
			}
		}
	}

	int Soloud::retroactiveBegin_internal(time aEventTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (!isRenderAheadEnabled() || !mRenderRing.isInited() || mSamplerate == 0)
			return -1;

		const time playhead = playheadTimeLocked_internal();
		if (aEventTime < playhead)
			aEventTime = playhead;
		const time halfSample = 0.5 / (time)mSamplerate;
		if (aEventTime >= mStreamTime - halfSample)
			return -1;

		const int ci = findCheckpointAtOrBefore_internal(aEventTime);
		if (ci < 0)
			return -1;
		MixCheckpoint &cp = mCheckpointPool[ci];

		// Eligibility gate (plan §7): the checkpoint and every journaled
		// in-window birth the replay would re-apply must be restorable.
		// ">=" matches replayJournal_internal(): a birth exactly at the
		// checkpoint time postdates the capture and is replayed, so a stale
		// (non-restorable) one there must block the rollback too.
		if (!cp.mRestorable)
			return -1;
		size_t j;
		for (j = 0; j < mRetroJournal.size(); j++)
		{
			const RetroJournalEntry &e = mRetroJournal[j];
			if (e.mTime >= cp.mTime &&
				e.mType == RetroJournalEntry::BIRTH && !e.mRestorable)
				return -1;
		}
		// ###### flutter_soloud local patch (bounded source history) ######
		// Sources with a bounded history (disk streams) may no longer be able
		// to reproduce what followed a snapshot; ask the live instances the
		// restore and the replay would rewind.
		size_t cvi;
		for (cvi = 0; cvi < cp.mVoices.size(); cvi++)
		{
			const CheckpointVoice &cv = cp.mVoices[cvi];
			if (cv.mSourceState == NULL || cv.mSlot >= VOICE_COUNT)
				continue;
			AudioSourceInstance *voice = mVoice[cv.mSlot];
			if (voice != NULL && voice->mPlayIndex == cv.mPlayIndex &&
				!voice->canRestoreSourceState(cv.mSourceState))
				return -1;
		}
		for (j = 0; j < mRetroJournal.size(); j++)
		{
			const RetroJournalEntry &e = mRetroJournal[j];
			if (e.mTime < cp.mTime || e.mType != RetroJournalEntry::BIRTH ||
				e.mBirth.mSourceState == NULL || e.mSlot >= VOICE_COUNT)
				continue;
			AudioSourceInstance *voice = mVoice[e.mSlot];
			if (voice != NULL && voice->mPlayIndex == e.mPlayIndex &&
				!voice->canRestoreSourceState(e.mBirth.mSourceState))
				return -1;
		}

		// The window must not contain a voice death. A voice present in the
		// checkpoint but gone from the live table ended inside
		// (C.time, writeHead], and its deleted instance cannot be resurrected
		// to reproduce its remaining audio; likewise a journaled birth whose
		// object is gone was born and died inside the window. Degrade the
		// event to legacy placement rather than silently erasing that audio.
		size_t v2;
		for (v2 = 0; v2 < cp.mVoices.size(); v2++)
		{
			const CheckpointVoice &cvoice = cp.mVoices[v2];
			if (cvoice.mSlot >= VOICE_COUNT || mVoice[cvoice.mSlot] == NULL ||
				mVoice[cvoice.mSlot]->mPlayIndex != cvoice.mPlayIndex)
				return -1;
			// A filter graph changed since the capture: the old instances'
			// state is gone and the new ones have no state at cp.mTime.
			if (!voiceFiltersMatch(cvoice, mVoice[cvoice.mSlot]))
				return -1;
		}
		for (j = 0; j < mRetroJournal.size(); j++)
		{
			const RetroJournalEntry &e = mRetroJournal[j];
			if (e.mType == RetroJournalEntry::BIRTH && e.mTime >= cp.mTime &&
				e.mTime <= mStreamTime &&
				(e.mSlot >= VOICE_COUNT || mVoice[e.mSlot] == NULL ||
				 mVoice[e.mSlot]->mPlayIndex != e.mPlayIndex))
				return -1;
			if (e.mType == RetroJournalEntry::BIRTH && e.mTime >= cp.mTime &&
				e.mSlot < VOICE_COUNT && mVoice[e.mSlot] != NULL &&
				mVoice[e.mSlot]->mPlayIndex == e.mPlayIndex &&
				!voiceFiltersMatch(e.mBirth, mVoice[e.mSlot]))
				return -1;
		}

		mRemixOldStreamTime = mStreamTime;
		mRemixWritePos = mRenderRing.getWritePosition();
		mRemixCheckpointTime = cp.mTime;
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		mRemixEventTime = aEventTime;
		// The rewrite starts at the checkpoint's ring write position (the
		// beginning of the quantum starting at cp.mTime).
		mRemixStartPos = cp.mRingWritePosition;

		// Ended-event reconciliation (plan §4.5 step 8 / §4.6): drop queued
		// ended-events whose voice the restore is about to bring back, so a
		// voice that ends again in the re-mix is reported exactly once. (The
		// queue is normally empty here -- every unlock drains it -- so this
		// is defensive.) An already-dispatched callback cannot be un-sent;
		// with the ring enabled, ended callbacks may fire up to renderAhead
		// earlier than before.
		unsigned int q = 0;
		while (q < mEndedVoiceCount)
		{
			handle h = mEndedVoiceQueue[q];
			unsigned int slot = (h & 0xfff) - 1;
			// Only a voice the restore will actually bring back -- live slot
			// with a matching play index -- may have its queued event
			// dropped; a genuinely ended voice (slot empty or reused) keeps
			// its event, so its callback still fires exactly once.
			bool dropping = slot < mHighestVoice && mVoice[slot] != NULL &&
				mVoice[slot]->mPlayIndex == (h >> 12);
			if (dropping)
			{
				size_t v;
				bool inCheckpoint = false;
				for (v = 0; v < cp.mVoices.size(); v++)
				{
					if (cp.mVoices[v].mSlot == slot &&
						cp.mVoices[v].mPlayIndex == (h >> 12))
					{
						inCheckpoint = true;
						break;
					}
				}
				dropping = inCheckpoint;
			}
			if (dropping)
			{
				mEndedVoiceCount--;
				memmove(&mEndedVoiceQueue[q], &mEndedVoiceQueue[q + 1],
					(mEndedVoiceCount - q) * sizeof(handle));
			}
			else
			{
				q++;
			}
		}

		if (!restoreMixCheckpoint_internal(ci))
			return -1;
		replayJournal_internal(cp.mTime);
		reapply3dVoices_internal();
		mRemixing = true;
		return ci;
	}

	void Soloud::retroactiveEnd_internal(int aPoolIndex)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		SOLOUD_ASSERT(mRemixing);
		(void)aPoolIndex; // the context members carry everything

		// Re-mix forward from the checkpoint to the old write head,
		// overwriting the ring in place. Frames the device has read, or is
		// reading right now, belong to it and are never touched, so the first
		// quantum may be a partial rewrite. The ring heads do not move.
		const unsigned long long writePos = mRemixWritePos;
		unsigned long long pos = mRemixStartPos;
		while (pos < writePos)
		{
			mRemixStartPos = pos + mBufferSize;
			remixQuantum_internal(mRenderRingStaging.mData, mBufferSize);
			const unsigned long long quantumEnd =
				(pos + mBufferSize <= writePos) ? (pos + mBufferSize) : writePos;
			// The device callback reads the ring without the audio mutex:
			// fence the frames from pos on for the duration of the copy only
			// (the mix above runs unfenced), and start past anything the
			// device has already claimed.
			const unsigned long long from = mRenderRing.beginRewrite(pos);
			if (from < quantumEnd)
			{
				const float *src = mRenderRingStaging.mData + (size_t)(from - pos) * mChannels;
				const unsigned int frames = (unsigned int)(quantumEnd - from);
				if (from > pos)
				{
					mRenderRing.rewriteCrossfade(from, src, frames, 32);
				}
				else
				{
					mRenderRing.rewrite(from, src, frames);
				}
			}
			mRenderRing.endRewrite();
			pos += mBufferSize;
		}
		mRemixing = false;

		// The re-mix sums the same float buffer times from the same restored
		// base in the same order, reproducing the pre-rollback mix clock
		// bit-exactly.
		SOLOUD_ASSERT(fabs(mStreamTime - mRemixOldStreamTime) < 1e-9);
	}

	bool Soloud::retroactiveVoiceStart_internal(unsigned int aSlot, time aEventTime, AudioSource &aSound)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (aSlot >= VOICE_COUNT || mVoice[aSlot] == NULL)
			return false;
		const unsigned int newPlayIndex = mVoice[aSlot]->mPlayIndex;

		const time playhead = playheadTimeLocked_internal();
		if (aEventTime < playhead)
			aEventTime = playhead;

		const int ci = retroactiveBegin_internal(aEventTime);
		if (ci < 0)
			return false;
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// The device reads the ring without the audio mutex: since the
		// playhead was read above, it may have moved past aEventTime and
		// past a checkpoint, which retroactiveBegin_internal() then
		// restored. Start the voice where that re-mix placed the event, at
		// the newer playhead: never before the checkpoint.
		aEventTime = mRemixEventTime;

		// The voice this event inserted survived the restore (the checkpoint
		// predates it, so it was left running); retarget its start to the
		// event time relative to the checkpoint.
		if (aSlot < mHighestVoice && mVoice[aSlot] != NULL &&
			mVoice[aSlot]->mPlayIndex == newPlayIndex)
		{
			AudioSourceInstance *voice = mVoice[aSlot];
			voice->mDelaySamples = (unsigned int)floor(
				(aEventTime - mRemixCheckpointTime) * (time)mSamplerate + 0.5);
			if (voice->mFlags & AudioSourceInstance::PAUSED)
				setVoicePause_internal(aSlot, 0);
			// The slot's 3d data is the one play() initialized: rollbacks
			// leave 3d state alone.
			(void)aSound;
			journalBirth_internal(aSlot, aEventTime);
		}

		retroactiveEnd_internal(ci);
		return true;
	}

	bool Soloud::retroactiveStopVoiceAt_internal(unsigned int aSlot, time aEngineTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (!mRenderRing.isInited() || aSlot >= VOICE_COUNT || mVoice[aSlot] == NULL)
			return false;
		// A paused voice makes no sound inside the window; stop it right away
		// (legacy path), there is nothing to re-mix.
		if (mVoice[aSlot]->mFlags & AudioSourceInstance::PAUSED)
			return false;
		// A scheduled stop behind the playhead clamps to the playhead (the
		// as-soon-as-possible behavior of a plain stop()).
		time t = playheadTimeLocked_internal();
		if (aEngineTime > t)
			t = aEngineTime;
		const int ci = retroactiveBegin_internal(t);
		if (ci < 0)
			return false;
		AudioSourceInstance *v = mVoice[aSlot];
		if (v == NULL)
		{
			retroactiveEnd_internal(ci);
			return false;
		}
		// Stop with sample accuracy inside the window using the
		// scheduleStopAt countdown machinery. The voice is stopped during the
		// re-mix; its ended-event queues there and dispatches at the final
		// unlock (exactly once: the original timeline never ended it). No
		// journal entry is needed: after the stop the voice is gone, so the
		// no-death-in-window gate already keeps later rollbacks from
		// crossing this point.
		long long samples = (long long)floor(
			(t - mRemixCheckpointTime) * (time)mSamplerate + 0.5);
		if (samples < 0)
			samples = 0;
		v->mStopSamplesLeft = samples;
		retroactiveEnd_internal(ci);
		return true;
	}

	bool Soloud::retroactiveParam_internal(unsigned int aSlot, int aParam, float aValue, time aDuration, time aEventTime)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		if (!mRenderRing.isInited() || aSlot >= VOICE_COUNT || mVoice[aSlot] == NULL)
			return false;
		// Paused voices make no sound inside the window; apply immediately
		// (legacy path). Unpausing is not retroactive either (v1).
		if (mVoice[aSlot]->mFlags & AudioSourceInstance::PAUSED)
			return false;
		if (aParam == RetroJournalEntry::PARAM_PAUSE && aValue == 0.0f)
			return false;
		const unsigned int playIndex = mVoice[aSlot]->mPlayIndex;
		time t = aEventTime;
		const time playhead = playheadTimeLocked_internal();
		if (t < playhead)
			t = playhead;
		const int ci = retroactiveBegin_internal(t);
		if (ci < 0)
			return false;
		AudioSourceInstance *v = mVoice[aSlot];
		if (v == NULL || v->mPlayIndex != playIndex)
		{
			retroactiveEnd_internal(ci);
			return false;
		}
		// Apply the change as a scheduled fader anchored on the voice's own
		// stream clock (which the restore and the journal replay have already
		// placed at the checkpoint time), so it takes effect at the event
		// time with quantum accuracy and stays correct if the voice pauses
		// meanwhile. A zero aDuration is a step change (RETRO_FADE_TIME keeps
		// Fader::get() off its 0-duration NaN path).
		const time dur = aDuration > 0 ? aDuration : RETRO_FADE_TIME;
		const time voiceTime = v->mStreamTime + (t - mRemixCheckpointTime);
		// ###### flutter_soloud local patch (fade starting values) ######
		// Start from the value the voice has where the re-mix becomes
		// audible: at the playhead, or at the checkpoint when that is later.
		// The restore put the voice back at the checkpoint, and a fade
		// running there, or a change the journal replayed, may have moved
		// it on by the playhead. Until the event time the new fader holds
		// that value, as a scheduled fade does on the direct mix path.
		const time liveTime = v->mStreamTime +
			(playhead > mRemixCheckpointTime ? playhead - mRemixCheckpointTime : 0);
		switch (aParam)
		{
		case RetroJournalEntry::PARAM_VOLUME:
			v->mVolumeFader.setScheduled(faderValueAt(v->mVolumeFader, v->mSetVolume, liveTime), aValue, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_PAN:
			v->mPanFader.setScheduled(faderValueAt(v->mPanFader, v->mPan, liveTime), aValue, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_SPEED:
			v->mRelativePlaySpeedFader.setScheduled(faderValueAt(v->mRelativePlaySpeedFader, v->mSetRelativePlaySpeed, liveTime), aValue, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_PAUSE:
			v->mPauseScheduler.setScheduled(1, 0, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_STOP:
			v->mStopScheduler.setScheduled(1, 0, dur, voiceTime);
			break;
		}
		journalParam_internal(aSlot, playIndex, aParam, aValue, t, aDuration);
		retroactiveEnd_internal(ci);
		return true;
	}
};

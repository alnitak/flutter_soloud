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
* pre-sized by allocateCheckpoints_internal() and reused in place. (Filters
* and sources that support snapshotting return heap objects from
* captureState()/captureSourceState(); the Phase 2 built-ins all return
* nullptr, so no allocation happens in practice.)
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
	// Delete the heap snapshots owned by a journal entry's birth record.
	static void releaseJournalEntry(RetroJournalEntry &aEntry)
	{
		if (aEntry.mType != RetroJournalEntry::BIRTH)
			return;
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			delete aEntry.mBirth.mFilterState[i];
			aEntry.mBirth.mFilterState[i] = NULL;
		}
		delete aEntry.mBirth.mSourceState;
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
		while (!aSoloud.mRetroJournal.empty() &&
			aSoloud.mRetroJournal.front().mTime < oldest)
		{
			releaseJournalEntry(aSoloud.mRetroJournal.front());
			aSoloud.mRetroJournal.erase(aSoloud.mRetroJournal.begin());
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
		while (pos > 0 && aSoloud.mRetroJournal[pos - 1].mTime > aEntry.mTime)
			pos--;
		if (aSoloud.mRetroJournal.size() >= RETRO_JOURNAL_CAPACITY)
		{
			releaseJournalEntry(aSoloud.mRetroJournal.front());
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
			releaseJournalEntry(mRetroJournal[i]);
		mRetroJournal.clear();
	}

	// Fill aVoiceState with the full mix-relevant POD state of the voice at
	// aSlot, plus owned heap snapshots of its filter instances and source
	// consumption state. Shared by checkpoint capture and journal births.
	static void fillCheckpointVoice(Soloud &aSoloud, CheckpointVoice &cv, unsigned int aSlot)
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
		cv.mDelaySamples = v->mDelaySamples;
		cv.mStopSamplesLeft = v->mStopSamplesLeft;
		cv.mLoopPoint = v->mLoopPoint;
		cv.mLoopEndPoint = v->mLoopEndPoint;
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
			cv.mFilterState[i] = v->mFilter[i] ? v->mFilter[i]->captureState() : NULL;
		}
		cv.mSourceState = v->captureSourceState();
	}

	// Whether a voice described by a snapshot can be re-mixed bit-exactly:
	// its source and all its live filter instances produced snapshots.
	// Paused voices are exempt from the source requirement: their source
	// state does not advance while paused (the mix never calls getAudio()
	// for them).
	static bool voiceSnapshotRestorable(const CheckpointVoice &aVoiceState)
	{
		if (!(aVoiceState.mFlags & AudioSourceInstance::PAUSED) &&
			aVoiceState.mSourceState == NULL)
			return false;
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (aVoiceState.mFilterLive[i] && aVoiceState.mFilterState[i] == NULL)
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
		v->mDelaySamples = cv.mDelaySamples;
		v->mStopSamplesLeft = cv.mStopSamplesLeft;
		v->mLoopPoint = cv.mLoopPoint;
		v->mLoopEndPoint = cv.mLoopEndPoint;
		v->mSourceSamplePosition = cv.mSourceSamplePosition;
		// mBaseSamplerate is immutable; mFilter pointers are instance
		// identity.
	}

	// Snapshot one voice into aCheckpoint. Appends to mVoices; the caller
	// guarantees capacity (no allocation on the audio path).
	static void captureVoice(Soloud &aSoloud, MixCheckpoint &aCheckpoint, unsigned int aSlot)
	{
		CheckpointVoice cv;
		fillCheckpointVoice(aSoloud, cv, aSlot);
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
		releaseCheckpointStorage(cp);
		cp.mVoices.clear();
		cp.mSerial = mCheckpointCounter;
		cp.mTime = mStreamTime;
		if (mSamplerate > 0)
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
		memcpy(cp.mActiveVoice, mActiveVoice, sizeof(mActiveVoice));
		cp.mActiveVoiceCount = mActiveVoiceCount;
		cp.mActiveVoiceDirty = mActiveVoiceDirty;
		cp.mClockedAnchorTime = mClockedAnchorTime;
		cp.mClockedAnchorSample = mClockedAnchorSample;
		cp.mClockedLastTime = mClockedLastTime;
		memcpy(cp.m3dData, m3dData, sizeof(m3dData));
		memcpy(cp.m3dPosition, m3dPosition, sizeof(m3dPosition));
		memcpy(cp.m3dAt, m3dAt, sizeof(m3dAt));
		memcpy(cp.m3dUp, m3dUp, sizeof(m3dUp));
		memcpy(cp.m3dVelocity, m3dVelocity, sizeof(m3dVelocity));
		cp.m3dSoundSpeed = m3dSoundSpeed;
		unsigned int i;
		bool restorable = true;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			cp.mFilterState[i] = mFilterInstance[i] ? mFilterInstance[i]->captureState() : NULL;
			// A live global filter that cannot snapshot itself makes every
			// rollback targeting this checkpoint degrade to legacy behavior.
			if (mFilterInstance[i] != NULL && cp.mFilterState[i] == NULL)
				restorable = false;
		}

		// Voices. Pass 1: the active (sounding) voices, so they are always
		// covered even if the total voice count exceeds the reserved
		// capacity. Pass 2: the remaining live voices (paused, inaudible) in
		// slot order, until capacity. Voices beyond capacity are treated as
		// absent on restore and left running (see restoreMixCheckpoint).
		const size_t capacity = cp.mVoices.capacity();
		for (i = 0; i < mActiveVoiceCount && cp.mVoices.size() < capacity; i++)
		{
			unsigned int slot = mActiveVoice[i];
			if (slot < mHighestVoice && mVoice[slot] != NULL)
				captureVoice(*this, cp, slot);
		}
		for (i = 0; i < mHighestVoice && cp.mVoices.size() < capacity; i++)
		{
			if (mVoice[i] == NULL)
				continue;
			size_t v;
			bool already = false;
			for (v = 0; v < cp.mVoices.size(); v++)
			{
				if (cp.mVoices[v].mSlot == i)
				{
					already = true;
					break;
				}
			}
			if (!already)
				captureVoice(*this, cp, i);
		}

		// Resample pool: snapshot every block that has an owner. Owner entry o
		// owns the consecutive block pair 2o and 2o+1 (see
		// mapResampleBuffers_internal). Only the live region (owner's
		// channels * SAMPLE_GRANULARITY floats, stored channel-planar
		// contiguously) is copied; the reserved blocks are reused in place.
		unsigned int usedBlocks = 0;
		for (i = 0; i < mMaxActiveVoices && usedBlocks < cp.mResampleBlocks.size(); i++)
		{
			if (mResampleDataOwner[i] == NULL)
				continue;
			unsigned int slot;
			int ownerSlot = -1;
			for (slot = 0; slot < mHighestVoice; slot++)
			{
				if (mVoice[slot] == mResampleDataOwner[i])
				{
					ownerSlot = (int)slot;
					break;
				}
			}
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
		unsigned int liveVoices = 0;
		for (i = 0; i < mHighestVoice; i++)
		{
			if (mVoice[i] != NULL)
				liveVoices++;
		}
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
		memcpy(m3dData, cp.m3dData, sizeof(m3dData));
		memcpy(m3dPosition, cp.m3dPosition, sizeof(m3dPosition));
		memcpy(m3dAt, cp.m3dAt, sizeof(m3dAt));
		memcpy(m3dUp, cp.m3dUp, sizeof(m3dUp));
		memcpy(m3dVelocity, cp.m3dVelocity, sizeof(m3dVelocity));
		m3dSoundSpeed = cp.m3dSoundSpeed;

		// Global filters. A nullptr snapshot means the filter cannot be
		// re-mixed; its state is left alone (going-forward degradation).
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (cp.mFilterState[i] != NULL && mFilterInstance[i] != NULL)
				mFilterInstance[i]->restoreState(cp.mFilterState[i]);
		}

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
			applyCheckpointVoice(cv, voice);
			// Resample pointers are rebuilt from block indices below, after
			// the pool contents are restored.

			if (cv.mSourceState != NULL)
				voice->restoreSourceState(cv.mSourceState);
			for (i = 0; i < FILTERS_PER_STREAM; i++)
			{
				if (cv.mFilterState[i] != NULL && voice->mFilter[i] != NULL)
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
				continue;
			if (b.mOwnerSlot < 0 || b.mOwnerSlot >= (int)mHighestVoice ||
				!slotRestored[b.mOwnerSlot])
				continue; // owner voice did not survive the restore
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
				voice->mResampleData[i] = cv.mResampleBlock[i] >= 0 ?
					mResampleData[cv.mResampleBlock[i]] : NULL;
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
			memcpy(mActiveVoice, cp.mActiveVoice, sizeof(mActiveVoice));
			mActiveVoiceCount = cp.mActiveVoiceCount;
			mActiveVoiceDirty = cp.mActiveVoiceDirty;
		}
		else
		{
			mActiveVoiceDirty = true;
		}

		return true;
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######

	time Soloud::playheadTimeLocked_internal()
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
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
				releaseJournalEntry(e);
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
		e.m3dData = m3dData[aSlot];
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
		RetroJournalEntry e;
		e.mType = RetroJournalEntry::PARAM;
		e.mTime = aTime;
		e.mSnapshotEngineTime = mStreamTime;
		e.mSlot = aSlot;
		e.mPlayIndex = aPlayIndex;
		e.mRestorable = true;
		e.mParam = aParam;
		e.mValue = aValue;
		e.mDuration = aDuration;
		e.mBirth = CheckpointVoice();
		e.mBirth.mSourceState = NULL;
		insertJournalEntry(*this, e);
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
				// Reset the live instance to its birth state, with the start
				// delay recomputed relative to the restore point.
				applyCheckpointVoice(e.mBirth, v);
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
				// re-assigns and zeroes the blocks.
				v->mResampleData[0] = NULL;
				v->mResampleData[1] = NULL;
				v->mSrcOffset = 0;
				v->mLeftoverSamples = 0;
				if (e.mBirth.mSourceState != NULL)
					v->restoreSourceState(e.mBirth.mSourceState);
				for (k = 0; k < FILTERS_PER_STREAM; k++)
				{
					if (e.mBirth.mFilterState[k] != NULL && v->mFilter[k] != NULL)
						v->mFilter[k]->restoreState(e.mBirth.mFilterState[k]);
				}
				m3dData[e.mSlot] = e.m3dData;
				mActiveVoiceDirty = true;
			}
			else if (e.mType == RetroJournalEntry::PARAM)
			{
				// Re-apply a retroactive parameter change as a scheduled
				// fader at its original time, anchored on the voice's
				// (restored) stream clock.
				const time voiceTime = v->mStreamTime + (e.mTime - aFromTime);
				const time dur = e.mDuration > 0 ? e.mDuration : RETRO_FADE_TIME;
				switch (e.mParam)
				{
				case RetroJournalEntry::PARAM_VOLUME:
					v->mVolumeFader.setScheduled(v->mSetVolume, e.mValue,
						dur, voiceTime);
					break;
				case RetroJournalEntry::PARAM_PAN:
					v->mPanFader.setScheduled(v->mPan, e.mValue,
						dur, voiceTime);
					break;
				case RetroJournalEntry::PARAM_SPEED:
					v->mRelativePlaySpeedFader.setScheduled(
						v->mSetRelativePlaySpeed, e.mValue, dur,
						voiceTime);
					break;
				case RetroJournalEntry::PARAM_PAUSE:
					if (!(v->mFlags & AudioSourceInstance::PAUSED))
						v->mPauseScheduler.set(1, 0, dur, voiceTime);
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
		if (aEventTime >= mStreamTime)
			return -1;

		const int ci = findCheckpointAtOrBefore_internal(aEventTime);
		if (ci < 0)
			return -1;
		MixCheckpoint &cp = mCheckpointPool[ci];

		// Eligibility gate (plan §7): the checkpoint and every journaled
		// in-window birth the replay would re-apply must be restorable.
		if (!cp.mRestorable)
			return -1;
		size_t j;
		for (j = 0; j < mRetroJournal.size(); j++)
		{
			const RetroJournalEntry &e = mRetroJournal[j];
			if (e.mTime > cp.mTime &&
				e.mType == RetroJournalEntry::BIRTH && !e.mRestorable)
				return -1;
		}

		// Gone/stopped voices are gracefully skipped during restoreMixCheckpoint_internal.
		// Allow retroactive re-mixing to proceed so subsequent key presses always have low latency.

		mRemixOldStreamTime = mStreamTime;
		mRemixWritePos = mRenderRing.getWritePosition();
		mRemixCheckpointTime = cp.mTime;
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
		mRemixing = true;
		return ci;
	}

	void Soloud::retroactiveEnd_internal(int aPoolIndex)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		SOLOUD_ASSERT(mRemixing);
		(void)aPoolIndex; // the context members carry everything

		// Re-mix forward from the checkpoint to the old write head,
		// overwriting the ring in place. Frames at/behind the read head
		// belong to the device and are never touched, so the first quantum
		// may be a partial rewrite. The ring heads do not move.
		const unsigned long long writePos = mRemixWritePos;
		const unsigned long long readPos = mRenderRing.getReadPosition();
		unsigned long long pos = mRemixStartPos;
		while (pos < writePos)
		{
			remixQuantum_internal(mRenderRingStaging.mData, mBufferSize);
			unsigned long long from = pos < readPos ? readPos : pos;
			if (from < pos + mBufferSize)
			{
				mRenderRing.rewrite(from,
					mRenderRingStaging.mData + (size_t)(from - pos) * mChannels,
					(unsigned int)(pos + mBufferSize - from));
			}
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
			// The restore reset the slot's 3d data to the checkpoint's
			// (pre-birth) contents.
			m3dData[aSlot].init(aSound);
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
		switch (aParam)
		{
		case RetroJournalEntry::PARAM_VOLUME:
			v->mVolumeFader.setScheduled(v->mSetVolume, aValue, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_PAN:
			v->mPanFader.setScheduled(v->mPan, aValue, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_SPEED:
			v->mRelativePlaySpeedFader.setScheduled(v->mSetRelativePlaySpeed, aValue, dur, voiceTime);
			break;
		case RetroJournalEntry::PARAM_PAUSE:
			v->mPauseScheduler.set(1, 0, dur, voiceTime);
			break;
		}
		journalParam_internal(aSlot, playIndex, aParam, aValue, t, aDuration);
		retroactiveEnd_internal(ci);
		return true;
	}
};

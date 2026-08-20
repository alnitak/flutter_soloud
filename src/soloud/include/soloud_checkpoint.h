/*
* SoLoud audio engine
*
* Mix checkpoints (flutter_soloud "Option B" retrofit, Phase 2 of
* OPTION_B_RETROACTIVE_REMIX_PLAN.md).
*
* At the end of every mix quantum (still under the audio mutex) the engine
* snapshots all state needed to reproduce that quantum's mix bit-exactly:
* engine POD state, per-voice POD state, resampler ping-pong block contents
* and -- where supported -- filter and source-consumption state via the
* captureState()/captureSourceState() virtuals. A retroactive event (Phase 3)
* restores the checkpoint at or before the event time and re-mixes forward.
*
* Storage is a fixed-size circular pool allocated in postinit_internal when
* the render-ahead ring is enabled; capture reuses the slots in place so the
* audio path never allocates (filters/sources that return heap snapshots are
* the documented exception -- Phase 2 built-ins all return nullptr).
*/

#ifndef SOLOUD_CHECKPOINT_H
#define SOLOUD_CHECKPOINT_H

#include "soloud.h"
#include "soloud_fader.h"
#include "soloud_filter.h"
#include "soloud_audiosource.h"

#include <vector>

namespace SoLoud
{
	// Per-voice snapshot: every mutable POD field of AudioSourceInstance,
	// copied by value. Pointers are never copied: mResampleData[0..1] are
	// stored as pool block indices and mFilter[] as owned heap snapshots.
	// mBaseSamplerate is immutable after init and intentionally not stored.
	struct CheckpointVoice
	{
		// Voice slot in Soloud::mVoice this snapshot belongs to.
		unsigned int mSlot;
		// Handle of the voice at capture time (diagnostics).
		handle mHandle;
		// Object identity: same slot + same play index = same instance.
		unsigned int mPlayIndex;
		unsigned int mLoopCount;
		unsigned int mFlags;
		float mPan;
		float mChannelVolume[MAX_CHANNELS];
		float mSetVolume;
		float mOverallVolume;
		float mSamplerate;
		unsigned int mChannels;
		float mSetRelativePlaySpeed;
		float mOverallRelativePlaySpeed;
		time mStreamTime;
		time mStreamPosition;
		// Faders are POD and must be copied by value, never re-created:
		// Fader::get() treats a time before mStartTime as a clock rollover
		// and restarts the fade (soloud_fader.cpp), so the full fader state
		// must already be in place before any fader is ticked against
		// restored-past times.
		Fader mPanFader;
		Fader mVolumeFader;
		Fader mRelativePlaySpeedFader;
		Fader mPauseScheduler;
		Fader mStopScheduler;
		int mActiveFader;
		// Click-removal ramp state (panAndExpand); critical for bit-exactness.
		float mCurrentChannelVolume[MAX_CHANNELS];
		unsigned int mAudioSourceID;
		unsigned int mBusHandle;
		unsigned int mSrcOffset;
		unsigned int mLeftoverSamples;
		unsigned int mDelaySamples;
		long long mStopSamplesLeft;
		time mLoopPoint;
		time mLoopEndPoint;
		long long mLoopStartFrame;
		long long mLoopEndFrame;
		uint64_t mSourceSamplePosition;
		// Resample pool block indices mResampleData[0..1] pointed to, -1 for
		// NULL. The ping-pong order is significant: [0] holds the current
		// block, [1] the previous one.
		int mResampleBlock[2];
		// Whether mFilter[i] held a live filter instance at capture time
		// (mFilterState[i] alone cannot distinguish "no filter" from "filter
		// cannot be re-mixed").
		bool mFilterLive[FILTERS_PER_STREAM];
		// Owned heap snapshots of the voice's filter instances; nullptr where
		// the slot is empty or the filter cannot be re-mixed.
		FilterStateSnapshot *mFilterState[FILTERS_PER_STREAM];
		// Owned heap snapshot of the source-consumption state; nullptr when
		// the source is not restorable (live streams, synths).
		SourceStateSnapshot *mSourceState;
	};

	// One resample pool block that had an owner at capture time. Only the
	// live region (SAMPLE_GRANULARITY * owner's channel count floats, stored
	// channel-planar contiguously) is copied.
	struct CheckpointResampleBlock
	{
		// Block index in Soloud::mResampleData.
		int mBlock;
		// Voice slot of the block's owner at capture time.
		int mOwnerSlot;
		// Owner's channel count; the live region is mChannels *
		// SAMPLE_GRANULARITY floats at the start of mData.
		unsigned int mChannels;
		// Pre-sized to SAMPLE_GRANULARITY * MAX_CHANNELS floats.
		std::vector<float> mData;
	};

	// Full mixer snapshot at one mix-quantum boundary.
	struct MixCheckpoint
	{
		// Engine time (mStreamTime) at the boundary: the just-mixed quantum's
		// end = the next quantum's start.
		time mTime;
		// Render-ring write position in frames at capture time. The ring
		// top-up appends the just-mixed quantum right after mix_internal
		// returns, so at capture time the write head is one quantum behind
		// mTime (0 when mixing bypassed the ring entirely).
		unsigned long long mRingWritePosition;
		// Capture serial (1-based, monotonic); 0 = slot never written. A slot
		// is stale once the serial is more than one pool size behind the
		// engine's capture counter.
		unsigned long long mSerial = 0;
		// Whether a rollback to this checkpoint can reproduce the mix
		// bit-exactly: every live voice is captured and restorable (paused
		// voices exempt -- their source state does not advance while paused)
		// and every global filter instance is snapshot-able. Computed at
		// capture time; when false, retroactive events degrade to
		// going-forward behavior (plan §7).
		bool mRestorable;

		// Engine-level POD state.
		float mGlobalVolume;
		Fader mGlobalVolumeFader;
		unsigned int mHighestVoice;
		unsigned int mPlayIndex;
		unsigned int mAudioSourceID;
		unsigned int mActiveVoice[VOICE_COUNT];
		unsigned int mActiveVoiceCount;
		bool mActiveVoiceDirty;
		time mClockedAnchorTime;
		long long mClockedAnchorSample;
		time mClockedLastTime;

		// 3d state (feeds update3dAudio; snapshotted for completeness).
		AudioSourceInstance3dData m3dData[VOICE_COUNT];
		float m3dPosition[3];
		float m3dAt[3];
		float m3dUp[3];
		float m3dVelocity[3];
		float m3dSoundSpeed;

		// Owned heap snapshots of the global filter instances; nullptr where
		// the slot is empty or the filter cannot be re-mixed.
		FilterStateSnapshot *mFilterState[FILTERS_PER_STREAM];

		// Sparse per-voice snapshots, keyed by CheckpointVoice::mSlot.
		std::vector<CheckpointVoice> mVoices;
		// Resample pool blocks that had an owner at capture time.
		std::vector<CheckpointResampleBlock> mResampleBlocks;
	};

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// Capacity of the in-window event journal. If it ever fills, the oldest
	// entry is dropped and the next rollback that needed it degrades to
	// legacy behavior.
	#define RETRO_JOURNAL_CAPACITY 128

	// One entry of the in-window event journal. Events that landed after a
	// checkpoint was taken (voices played, voices stopped, parameters changed)
	// are erased from live state by a rollback to that checkpoint; the journal
	// re-applies them on top of the restore before the re-mix (plan §4.5).
	// Births never reference the AudioSource: replay resets the POD fields of
	// the live AudioSourceInstance object (same slot + same play index = same
	// object; a mismatch means the slot was stolen or stopped -- drop the
	// entry).
	struct RetroJournalEntry
	{
		enum Type
		{
			BIRTH = 0,
			DEATH = 1,
			// Immediate parameter change (volume/pan/speed/pause) applied
			// retroactively at mTime. Replayed as a zero-duration scheduled
			// fader, which ticks at quantum boundaries (quantum accuracy).
			PARAM = 2
		};
		// PARAM kinds.
		enum Param
		{
			PARAM_VOLUME = 0,
			PARAM_PAN = 1,
			PARAM_SPEED = 2,
			PARAM_PAUSE = 3
		};
		int mType;
		// Engine time of the event: for births the first sounding sample,
		// for deaths the mix clock at the stop, for params the playhead time
		// the change takes effect at.
		time mTime;
		// Births only: engine mix clock when the snapshot was taken. Used to
		// keep the voice's own stream clocks consistent across nested
		// rollbacks (the voice starts ticking at the checkpoint time in the
		// re-mix, earlier than its real insertion).
		time mSnapshotEngineTime;
		unsigned int mSlot;
		unsigned int mPlayIndex;
		// Births only: whether the voice can be re-mixed at all (its source
		// and filters produced snapshots at journal time). Paused-at-birth
		// voices are always restorable: their source state does not advance
		// while paused.
		bool mRestorable;
		// PARAM only: which parameter (enum Param) and its new value.
		int mParam;
		float mValue;
		// PARAM only: fade duration in seconds (0 = step change; replay uses
		// the zero-duration RETRO_FADE_TIME step for it).
		time mDuration;
		// Births only: full POD snapshot of the instance right after
		// creation, including owned heap snapshots of source/filter state.
		CheckpointVoice mBirth;
		// Births only: 3d data at creation (the checkpoint's m3dData predates
		// the birth and would otherwise clobber the slot's entry).
		AudioSourceInstance3dData m3dData;
	};
};

#endif // SOLOUD_CHECKPOINT_H

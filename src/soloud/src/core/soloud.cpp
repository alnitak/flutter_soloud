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

#include <string.h>
#include <stdlib.h>
#include <math.h> // sin
#include <float.h> // _controlfp
#include <algorithm> // stable_sort
#ifdef __EMSCRIPTEN__
#include <emscripten/threading.h>
#endif
#include "soloud_internal.h"
#include "soloud_thread.h"
#include "soloud_fft.h"
#include "../../../simd/soloud_simd.h"
#include "../../../simd/simd_pcm.h"

#if !defined(__EMSCRIPTEN__)
#include <chrono>
#include <condition_variable>
#include <thread>
#endif


#ifdef SOLOUD_SSE_INTRINSICS
#include <xmmintrin.h>
#include <emmintrin.h>
#endif
#ifdef SOLOUD_NEON_INTRINSICS
#include <arm_neon.h>
#endif
#ifdef SOLOUD_WASM_SIMD_INTRINSICS
#include <wasm_simd128.h>
#endif

//#define FLOATING_POINT_DEBUG


#if !defined(WITH_SDL2) && !defined(WITH_SDL1) && !defined(WITH_PORTAUDIO) && \
   !defined(WITH_OPENAL) && !defined(WITH_XAUDIO2) && !defined(WITH_WINMM) && \
   !defined(WITH_WASAPI) && !defined(WITH_OSS) && !defined(WITH_SDL1_STATIC) && \
   !defined(WITH_SDL2_STATIC) && !defined(WITH_ALSA) && !defined(WITH_OPENSLES) && \
   !defined(WITH_NULL) && !defined(WITH_COREAUDIO) && !defined(WITH_VITA_HOMEBREW) &&\
   !defined(WITH_JACK) && !defined(WITH_NOSOUND) && !defined(WITH_MINIAUDIO)
#error It appears you haven't enabled any of the back-ends. Please #define one or more of the WITH_ defines (or use premake) '
#endif


namespace SoLoud
{
	AlignedFloatBuffer::AlignedFloatBuffer()
	{
		mBasePtr = 0;
		mData = 0;
		mFloats = 0;
	}

	result AlignedFloatBuffer::init(unsigned int aFloats)
	{
		delete[] mBasePtr;
		mBasePtr = 0;
		mData = 0;
		mFloats = aFloats;
		if (aFloats == 0)
			return SO_NO_ERROR;
		mBasePtr = new unsigned char[(size_t)aFloats * sizeof(float) + 64];
		if (mBasePtr == NULL)
			return OUT_OF_MEMORY;
		mData = (float *)(((uintptr_t)mBasePtr + 63u) & ~(uintptr_t)63u);
		return SO_NO_ERROR;
	}

	void AlignedFloatBuffer::clear()
	{
		if (mData != NULL && mFloats > 0)
			memset(mData, 0, sizeof(float) * (size_t)mFloats);
	}

	AlignedFloatBuffer::~AlignedFloatBuffer()
	{
		delete[] mBasePtr;
	}

	TinyAlignedFloatBuffer::TinyAlignedFloatBuffer()
	{
		unsigned char * basePtr = &mActualData[0];
		mData = (float *)(((uintptr_t)basePtr + 63u) & ~(uintptr_t)63u);
	}

	// ###### flutter_soloud local patch (render-ahead worker) ######
#if !defined(__EMSCRIPTEN__)
	struct RenderWorker
	{
		RenderWorker() : mRunning(false), mStop(false), mWake(false) {}
		std::thread mThread;
		std::mutex mMutex;
		std::condition_variable mCv;
		// Read by the device callback.
		std::atomic<bool> mRunning;
		std::atomic<bool> mStop;
		alignas(64) std::atomic<bool> mWake;
	};
#else
	// The web build mixes in the device callback (no render-ahead ring).
	struct RenderWorker
	{
	};
#endif

	Soloud::Soloud()
	{
#ifdef FLOATING_POINT_DEBUG
		unsigned int u;
		u = _controlfp(0, 0);
		u = u & ~(_EM_INVALID | /*_EM_DENORMAL |*/ _EM_ZERODIVIDE | _EM_OVERFLOW /*| _EM_UNDERFLOW  | _EM_INEXACT*/);
		_controlfp(u, _MCW_EM);
#endif
		mResampler = SOLOUD_DEFAULT_RESAMPLER;
		mInsideAudioThreadMutex = false;
		mScratchSize = 0;
		mSamplerate = 0;
		mBufferSize = 0;
		mFlags = 0;
		mGlobalVolume = 0;
		mPlayIndex = 0;
		mBackendData = NULL;
		mAudioThreadMutex = NULL;
		mPostClipScaler.store(0.0f, std::memory_order_relaxed);
		mBackendCleanupFunc = NULL;
		mBackendPauseFunc = NULL;
		mBackendResumeFunc = NULL;
		mChannels = 2;		
		mStreamTime = 0;
		mDevicePeriodFrames = 0;
		mRenderAheadFrames = 0;
		mCheckpointWriteIndex = 0;
		mCheckpointCounter = 0;
		mMixingToRenderRing = false;
		mRenderWorkerWanted = false;
		mRenderWorker.store(NULL, std::memory_order_relaxed);
		mRenderRingTimeOrigin.store(0.0, std::memory_order_relaxed);
		mGlobalFilterMask.store(0, std::memory_order_relaxed);
		mRemixing = false;
		mRemixSuppressEndedCount = 0;
		mClockedAnchorTime = 0;
		mClockedAnchorSample = -1;
		mClockedLastTime = 0;
		mAudioSourceID = 1;
		mBackendString = 0;
		mBackendID = 0;
		mActiveVoiceDirty = true;
		mActiveVoiceCount = 0;
		int i;
		for (i = 0; i < VOICE_COUNT; i++)
			mActiveVoice[i] = 0;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			mFilter[i] = NULL;
			mFilterInstance[i] = NULL;
		}
		for (i = 0; i < 256; i++)
		{
			mFFTData[i] = 0;
			mVisualizationWaveData[i] = 0;
			mWaveData[i] = 0;
		}
		for (i = 0; i < MAX_CHANNELS; i++)
		{
			mVisualizationChannelVolume[i] = 0;
		}
		for (i = 0; i < VOICE_COUNT; i++)
		{
			mVoice[i] = 0;
		}
		mVoiceGroup = 0;
		mVoiceGroupCount = 0;

		m3dPosition[0] = 0;
		m3dPosition[1] = 0;
		m3dPosition[2] = 0;
		m3dAt[0] = 0;
		m3dAt[1] = 0;
		m3dAt[2] = -1;
		m3dUp[0] = 0;
		m3dUp[1] = 1;
		m3dUp[2] = 0;		
		m3dVelocity[0] = 0;
		m3dVelocity[1] = 0;
		m3dVelocity[2] = 0;		
		m3dSoundSpeed = 343.3f;
		mMaxActiveVoices = 16;
		mHighestVoice = 0;
		mResampleData = NULL;
		mResampleDataOwner = NULL;
		for (i = 0; i < 3 * MAX_CHANNELS; i++)
			m3dSpeakerPosition[i] = 0;
	}

	Soloud::~Soloud()
	{
		// let's stop all sounds before deinit, so we don't mess up our mutexes
		stopAll();
		deinit();
		// ###### flutter_soloud local patch (render-ahead worker) ######
		// deinit() joined the thread and the backend has stopped the device.
		delete mRenderWorker.exchange(NULL);
		unsigned int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			delete mFilterInstance[i];
		}
		for (i = 0; i < mVoiceGroupCount; i++)
			delete[] mVoiceGroup[i];
		delete[] mVoiceGroup;
		delete[] mResampleData;
		delete[] mResampleDataOwner;
	}

	void Soloud::deinit()
	{
		// Stop the audio callback from mixing (see mEngineReady) before
		// tearing anything down.
		mEngineReady.store(0, std::memory_order_seq_cst);
		// ###### flutter_soloud local patch (render-ahead worker) ######
		stopRenderWorker_internal();
		// Make sure no audio operation is currently pending
		lockAudioMutex_internal();
		unlockAudioMutex_internal();
		// ###### flutter_soloud local patch (render-ahead worker) ######
		// No SOLOUD_ASSERT(!mInsideAudioThreadMutex) here: that flag belongs
		// to whichever thread holds the mutex, and a device callback that
		// was already running when mEngineReady was cleared may legitimately
		// hold it now (it stops at the next quantum boundary); reading it
		// unlocked would be a data race and a spurious abort.
		stopAll();
		if (mBackendCleanupFunc)
			mBackendCleanupFunc(this);
		mBackendCleanupFunc = 0;
		// ###### flutter_soloud local patch (mix checkpoints) ######
		// Free the checkpoint pool with the rest of the engine state. The
		// ring itself is released on the next postinit (AlignedFloatBuffer
		// has no release), but the pool holds heap snapshots worth freeing.
		releaseCheckpoints_internal();
		delete[] mResampleData;
		mResampleData = NULL;
		delete[] mResampleDataOwner;
		mResampleDataOwner = NULL;
		if (mAudioThreadMutex)
			Thread::destroyMutex(mAudioThreadMutex);
		mAudioThreadMutex = NULL;
	}

	// Change output device.
	// Added by Marco Bavagnoli
	result Soloud::miniaudio_changeDevice(void *pPlaybackInfos_id)
	{
#if defined(WITH_MINIAUDIO)
		// A null mutex means the engine is not (or no longer) running, so the
		// device was not replaced. Reporting success here would tell the caller
		// its output moved when nothing happened.
		if (mAudioThreadMutex == NULL)
			return UNKNOWN_ERROR;

		return miniaudio_changeDevice_impl(pPlaybackInfos_id);
#else
		(void)pPlaybackInfos_id;
		return NOT_IMPLEMENTED;
#endif
	}

	result Soloud::miniaudio_changeLinuxBackend(int aBackend)
	{
#if defined(WITH_MINIAUDIO)
		if (mAudioThreadMutex == NULL)
			return UNKNOWN_ERROR;

		return miniaudio_changeLinuxBackend_impl(aBackend);
#else
		(void)aBackend;
		return NOT_IMPLEMENTED;
#endif
	}

	result Soloud::init(unsigned int aFlags, unsigned int aBackend, unsigned int aSamplerate, unsigned int aBufferSize, unsigned int aChannels, void *pPlaybackInfos_id)
	{		
		if (aBackend >= BACKEND_MAX || aChannels == 3 || aChannels == 5 || aChannels == 7 || aChannels > MAX_CHANNELS)
			return INVALID_PARAMETER;

		deinit();

		// Invalidate the playClocked anchor on (re)init.
		mClockedAnchorTime = 0;
		mClockedAnchorSample = -1;
		mClockedLastTime = 0;

		mAudioThreadMutex = Thread::createMutex();
		struct InitFailGuard
		{
			Soloud *self;
			bool ok = false;
			~InitFailGuard()
			{
				if (!ok && self->mAudioThreadMutex)
				{
					Thread::destroyMutex(self->mAudioThreadMutex);
					self->mAudioThreadMutex = NULL;
				}
			}
		} initGuard{this};

		mBackendID = 0;
		mBackendString = 0;

		int samplerate = 44100;
		int buffersize = 2048;
		int inited = 0;

		if (aSamplerate != Soloud::AUTO) samplerate = aSamplerate;
		if (aBufferSize != Soloud::AUTO) buffersize = aBufferSize;

#if defined(WITH_SDL1_STATIC)
		if (!inited &&
			(aBackend == Soloud::SDL1 || 
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = sdl1static_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::SDL1;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_SDL2_STATIC)
		if (!inited &&
			(aBackend == Soloud::SDL2 ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = sdl2static_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::SDL2;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;
		}
#endif

#if defined(WITH_SDL2)
		if (!inited &&
			(aBackend == Soloud::SDL2 ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = sdl2_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::SDL2;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;
		}
#endif

#if defined(WITH_SDL1)
		if (!inited &&
			(aBackend == Soloud::SDL1 || 
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = sdl1_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::SDL1;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_MINIAUDIO)
		if (!inited &&
			(aBackend == Soloud::MINIAUDIO ||
				aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = miniaudio_init(this, aFlags, samplerate, buffersize, aChannels, pPlaybackInfos_id);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::MINIAUDIO;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;
		}
#endif

#if defined(WITH_PORTAUDIO)
		if (!inited &&
			(aBackend == Soloud::PORTAUDIO ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = portaudio_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::PORTAUDIO;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_XAUDIO2)
		if (!inited &&
			(aBackend == Soloud::XAUDIO2 ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 4096;

			int ret = xaudio2_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::XAUDIO2;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_WINMM)
		if (!inited &&
			(aBackend == Soloud::WINMM ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 4096;

			int ret = winmm_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::WINMM;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_WASAPI)
		if (!inited &&
			(aBackend == Soloud::WASAPI ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 4096;
			if (aSamplerate == Soloud::AUTO) samplerate = 48000;

			int ret = wasapi_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::WASAPI;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_ALSA)
		if (!inited &&
			(aBackend == Soloud::ALSA ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = alsa_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::ALSA;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_JACK)
		if (!inited &&
			(aBackend == Soloud::JACK ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = jack_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::JACK;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_OSS)
		if (!inited &&
			(aBackend == Soloud::OSS ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = oss_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::OSS;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_OPENAL)
		if (!inited &&
			(aBackend == Soloud::OPENAL ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 4096;

			int ret = openal_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::OPENAL;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_COREAUDIO)
		if (!inited &&
			(aBackend == Soloud::COREAUDIO ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = coreaudio_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::COREAUDIO;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_OPENSLES)
		if (!inited &&
			(aBackend == Soloud::OPENSLES ||
			aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 4096;

			int ret = opensles_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::OPENSLES;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_VITA_HOMEBREW)
		if (!inited &&
			(aBackend == Soloud::VITA_HOMEBREW || 
			aBackend == Soloud::AUTO))
		{
			int ret = vita_homebrew_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::VITA_HOMEBREW;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;			
		}
#endif

#if defined(WITH_NOSOUND)
		if (!inited &&
			(aBackend == Soloud::NOSOUND ||
				aBackend == Soloud::AUTO))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = nosound_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::NOSOUND;
			}

			if (ret != 0 && aBackend != Soloud::AUTO)
				return ret;
		}
#endif


#if defined(WITH_NULL)
		if (!inited &&
			(aBackend == Soloud::NULLDRIVER))
		{
			if (aBufferSize == Soloud::AUTO) buffersize = 2048;

			int ret = null_init(this, aFlags, samplerate, buffersize, aChannels);
			if (ret == 0)
			{
				inited = 1;
				mBackendID = Soloud::NULLDRIVER;
			}

			if (ret != 0)
				return ret;			
		}
#endif

		if (!inited && aBackend != Soloud::AUTO)
			return NOT_IMPLEMENTED;
		if (!inited)
			return UNKNOWN_ERROR;
		initGuard.ok = true;
		// The engine is fully initialized; let the audio callback mix (see
		// mEngineReady).
		mEngineReady.store(1, std::memory_order_seq_cst);
		// ###### flutter_soloud local patch (render-ahead worker) ######
		startRenderWorker_internal();
		return 0;
	}

	result Soloud::pause()
	{
		if (mBackendPauseFunc)
			return mBackendPauseFunc(this);

		return NOT_IMPLEMENTED;
	}

	result Soloud::resume()
	{
		if (mBackendResumeFunc)
			return mBackendResumeFunc(this);

		return NOT_IMPLEMENTED;
	}


	void Soloud::postinit_internal(unsigned int aSamplerate, unsigned int aBufferSize, unsigned int aFlags, unsigned int aChannels)
	{
		// ###### flutter_soloud local patch (render-ahead worker) ######
		// The ring is reallocated below; nothing may be mixing into it.
		stopRenderWorker_internal();
		mGlobalVolume = 1;
		mChannels = aChannels;
		mSamplerate = aSamplerate;
		mBufferSize = aBufferSize;
		mScratchSize = (aBufferSize + 15) & (~0xf); // round to the next div by 16
		if (mScratchSize < SAMPLE_GRANULARITY * 2) mScratchSize = SAMPLE_GRANULARITY * 2;
		if (mScratchSize < 4096) mScratchSize = 4096;
		mScratch.init(mScratchSize * MAX_CHANNELS);
		mOutputScratch.init(mScratchSize * MAX_CHANNELS);
		delete[] mResampleData;
		delete[] mResampleDataOwner;
		mResampleData = new float*[mMaxActiveVoices * 2];
		mResampleDataOwner = new AudioSourceInstance*[mMaxActiveVoices];
		mResampleDataBuffer.init(mMaxActiveVoices * 2 * SAMPLE_GRANULARITY * MAX_CHANNELS);
		unsigned int i;		
		for (i = 0; i < mMaxActiveVoices * 2; i++)
			mResampleData[i] = mResampleDataBuffer.mData + (SAMPLE_GRANULARITY * MAX_CHANNELS * i);
		for (i = 0; i < mMaxActiveVoices; i++)
			mResampleDataOwner[i] = NULL;
		mFlags = aFlags;
		mPostClipScaler.store(0.95f, std::memory_order_release);
		switch (mChannels)
		{
		case 1:
			m3dSpeakerPosition[0 * 3 + 0] = 0;
			m3dSpeakerPosition[0 * 3 + 1] = 0;
			m3dSpeakerPosition[0 * 3 + 2] = 1;
			break;
		case 2:
			m3dSpeakerPosition[0 * 3 + 0] = 2;
			m3dSpeakerPosition[0 * 3 + 1] = 0;
			m3dSpeakerPosition[0 * 3 + 2] = 1;
			m3dSpeakerPosition[1 * 3 + 0] = -2;
			m3dSpeakerPosition[1 * 3 + 1] = 0;
			m3dSpeakerPosition[1 * 3 + 2] = 1;
			break;
		case 4:
			m3dSpeakerPosition[0 * 3 + 0] = 2;
			m3dSpeakerPosition[0 * 3 + 1] = 0;
			m3dSpeakerPosition[0 * 3 + 2] = 1;
			m3dSpeakerPosition[1 * 3 + 0] = -2;
			m3dSpeakerPosition[1 * 3 + 1] = 0;
			m3dSpeakerPosition[1 * 3 + 2] = 1;
			// I suppose technically the second pair should be straight left & right,
			// but I prefer moving them a bit back to mirror the front speakers.
			m3dSpeakerPosition[2 * 3 + 0] = 2;
			m3dSpeakerPosition[2 * 3 + 1] = 0;
			m3dSpeakerPosition[2 * 3 + 2] = -1;
			m3dSpeakerPosition[3 * 3 + 0] = -2;
			m3dSpeakerPosition[3 * 3 + 1] = 0;
			m3dSpeakerPosition[3 * 3 + 2] = -1;
			break;
		case 6:
			m3dSpeakerPosition[0 * 3 + 0] = 2;
			m3dSpeakerPosition[0 * 3 + 1] = 0;
			m3dSpeakerPosition[0 * 3 + 2] = 1;
			m3dSpeakerPosition[1 * 3 + 0] = -2;
			m3dSpeakerPosition[1 * 3 + 1] = 0;
			m3dSpeakerPosition[1 * 3 + 2] = 1;

			// center and subwoofer. 
			m3dSpeakerPosition[2 * 3 + 0] = 0;
			m3dSpeakerPosition[2 * 3 + 1] = 0;
			m3dSpeakerPosition[2 * 3 + 2] = 1;
			// Sub should be "mix of everything". We'll handle it as a special case and make it a null vector.
			m3dSpeakerPosition[3 * 3 + 0] = 0;
			m3dSpeakerPosition[3 * 3 + 1] = 0;
			m3dSpeakerPosition[3 * 3 + 2] = 0;

			// I suppose technically the second pair should be straight left & right,
			// but I prefer moving them a bit back to mirror the front speakers.
			m3dSpeakerPosition[4 * 3 + 0] = 2;
			m3dSpeakerPosition[4 * 3 + 1] = 0;
			m3dSpeakerPosition[4 * 3 + 2] = -1;
			m3dSpeakerPosition[5 * 3 + 0] = -2;
			m3dSpeakerPosition[5 * 3 + 1] = 0;
			m3dSpeakerPosition[5 * 3 + 2] = -1;
			break;
		case 8:
			m3dSpeakerPosition[0 * 3 + 0] = 2;
			m3dSpeakerPosition[0 * 3 + 1] = 0;
			m3dSpeakerPosition[0 * 3 + 2] = 1;
			m3dSpeakerPosition[1 * 3 + 0] = -2;
			m3dSpeakerPosition[1 * 3 + 1] = 0;
			m3dSpeakerPosition[1 * 3 + 2] = 1;

			// center and subwoofer. 
			m3dSpeakerPosition[2 * 3 + 0] = 0;
			m3dSpeakerPosition[2 * 3 + 1] = 0;
			m3dSpeakerPosition[2 * 3 + 2] = 1;
			// Sub should be "mix of everything". We'll handle it as a special case and make it a null vector.
			m3dSpeakerPosition[3 * 3 + 0] = 0;
			m3dSpeakerPosition[3 * 3 + 1] = 0;
			m3dSpeakerPosition[3 * 3 + 2] = 0;

			// side
			m3dSpeakerPosition[4 * 3 + 0] = 2;
			m3dSpeakerPosition[4 * 3 + 1] = 0;
			m3dSpeakerPosition[4 * 3 + 2] = 0;
			m3dSpeakerPosition[5 * 3 + 0] = -2;
			m3dSpeakerPosition[5 * 3 + 1] = 0;
			m3dSpeakerPosition[5 * 3 + 2] = 0;

			// back
			m3dSpeakerPosition[6 * 3 + 0] = 2;
			m3dSpeakerPosition[6 * 3 + 1] = 0;
			m3dSpeakerPosition[6 * 3 + 2] = -1;
			m3dSpeakerPosition[7 * 3 + 0] = -2;
			m3dSpeakerPosition[7 * 3 + 1] = 0;
			m3dSpeakerPosition[7 * 3 + 2] = -1;
			break;
		}

		// ###### flutter_soloud local patch (render-ahead ring) ######
		// Allocate the render-ahead ring between the mixer and the device.
		// Capacity holds the render-ahead depth plus one full engine quantum
		// plus a few device periods of margin, so the top-up logic can always
		// append a whole quantum without overwriting unread frames.
		if (isRenderAheadEnabled())
		{
			unsigned int capacity =
				mRenderAheadFrames + aBufferSize + 4 * mDevicePeriodFrames;
			mRenderRing.init(capacity, aChannels);
			mRenderRingTimeOrigin.store(mStreamTime, std::memory_order_release);
			mRenderRingStaging.init(aBufferSize * aChannels);
			mPostRingScratch.init(mScratchSize * MAX_CHANNELS);
			// ###### flutter_soloud local patch (mix checkpoints) ######
			// Circular pool of quantum-boundary snapshots covering the whole
			// render-ahead window plus margin. All storage is pre-sized here;
			// capture runs on the audio thread and must not allocate.
			allocateCheckpoints_internal(
				((mRenderAheadFrames + aBufferSize - 1) / aBufferSize + 4) < 8
					? 8
					: ((mRenderAheadFrames + aBufferSize - 1) / aBufferSize + 4));
			captureMixCheckpoint_internal();
		}
		else
		{
			// Feature off: release any ring left over from a previous init.
			// (AlignedFloatBuffer has no release; leaving the staging buffer
			// allocated is harmless since isRenderAheadEnabled() gates use.)
			mRenderRing.clear();
			// ###### flutter_soloud local patch (mix checkpoints) ######
			releaseCheckpoints_internal();
		}
	}

	// ###### flutter_soloud local patch (render-ahead ring) ######
	void Soloud::setRenderAheadConfig(unsigned int aDevicePeriodFrames, unsigned int aRenderAheadFrames)
	{
#ifdef __EMSCRIPTEN__
		// The web backend keeps the direct-to-device mixing path for now; the
		// ring and retroactive re-mixing are native-only in this revision.
		(void)aDevicePeriodFrames;
		(void)aRenderAheadFrames;
		mDevicePeriodFrames = 0;
		mRenderAheadFrames = 0;
#else
		mDevicePeriodFrames = aDevicePeriodFrames != 0 ? aDevicePeriodFrames : 512;
		mRenderAheadFrames = aRenderAheadFrames;
#endif
	}

	bool Soloud::isRenderAheadEnabled() const
	{
		return mRenderAheadFrames > 0;
	}

	time Soloud::getPlayheadTime()
	{
		lockAudioMutex_internal();
		time t = mStreamTime;
		if (isRenderAheadEnabled() && mRenderRing.isInited() && mSamplerate > 0)
		{
			t -= (time)mRenderRing.availableToRead() / (time)mSamplerate;
		}
		unlockAudioMutex_internal();
		return t > 0 ? t : 0;
	}

	time Soloud::getOutputLatency()
	{
		if (!isRenderAheadEnabled() || !mRenderRing.isInited() || mSamplerate == 0)
			return 0;
		// Frames mixed ahead of the device read head right now, plus one
		// device period of cushion inside the backend.
		unsigned long long buffered = mRenderRing.availableToRead();
		return (time)(buffered + mDevicePeriodFrames) / (time)mSamplerate;
	}

	bool Soloud::renderRingMixQuantumLocked_internal()
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		unsigned int stride = (mBufferSize + 15) & ~0xf;
		time buffertime = (time)mBufferSize / (time)mSamplerate;
		float globalVolume[2];

		mMixingToRenderRing = true;
		mStreamTime += buffertime;

		globalVolume[0] = mGlobalVolume;
		if (mGlobalVolumeFader.mActive)
		{
			mGlobalVolume = mGlobalVolumeFader.get(mStreamTime);
		}
		globalVolume[1] = mGlobalVolume;

		mixVoicesLocked_internal(mBufferSize, stride);

		const bool notifyVoiceInactive = mVoiceInactiveCallbackPending;
		mVoiceInactiveCallbackPending = false;

		// Global volume only: the ring holds the unclipped mix, and
		// postProcessRenderRing_internal() clips after the global
		// filters, as the direct mix path does.
		if (globalVolume[0] == 1.0f && globalVolume[1] == 1.0f)
		{
			interlace_samples_float(mOutputScratch.mData, mRenderRingStaging.mData, mBufferSize, mChannels, stride);
		}
		else
		{
			applyGlobalVolume_internal(mOutputScratch, mScratch, stride, globalVolume[0], globalVolume[1]);
			interlace_samples_float(mScratch.mData, mRenderRingStaging.mData, mBufferSize, mChannels, stride);
		}
		mRenderRing.write(mRenderRingStaging.mData, mBufferSize);
		// Ring frames and the mix clock advance in lockstep (a re-mix puts
		// mStreamTime back where it was), so this stays constant; publishing
		// it keeps the device callback independent of how it was set up.
		mRenderRingTimeOrigin.store(
			mStreamTime - (time)mRenderRing.getWritePosition() / (time)mSamplerate,
			std::memory_order_release);
		mMixingToRenderRing = false;
		return notifyVoiceInactive;
	}

	void Soloud::renderRingTopUp_internal()
	{
		if (!mRenderRing.isInited() || mBufferSize == 0)
			return;
		while (mRenderRing.availableToRead() < mRenderAheadFrames &&
			mRenderRing.availableToWrite() >= mBufferSize)
		{
			lockAudioMutex_internal();
			// deinit() clears mEngineReady and then takes the audio mutex
			// once: no quantum may start after that.
			if (mEngineReady.load(std::memory_order_acquire) == 0)
			{
				unlockAudioMutex_internal();
				return;
			}
			const bool notifyVoiceInactive = renderRingMixQuantumLocked_internal();
			unlockAudioMutex_internal();

			auto voiceInactiveCallback =
				_voiceInactiveCallback.load(std::memory_order_acquire);
			if (notifyVoiceInactive && voiceInactiveCallback != nullptr)
				voiceInactiveCallback();
		}
	}

	void Soloud::renderRingTopUpNoWait_internal(unsigned int aFrames)
	{
		if (!mRenderRing.isInited() || mBufferSize == 0)
			return;
		while (mRenderRing.availableToRead() < aFrames &&
			mRenderRing.availableToWrite() >= mBufferSize)
		{
			if (!tryLockAudioMutex_internal())
				return;
			if (mEngineReady.load(std::memory_order_acquire) == 0)
			{
				unlockAudioMutex_internal();
				return;
			}
			const bool notifyVoiceInactive = renderRingMixQuantumLocked_internal();
			unlockAudioMutex_internal();

			auto voiceInactiveCallback =
				_voiceInactiveCallback.load(std::memory_order_acquire);
			if (notifyVoiceInactive && voiceInactiveCallback != nullptr)
				voiceInactiveCallback();
		}
	}

	// ###### flutter_soloud local patch (render-ahead worker) ######
#if !defined(__EMSCRIPTEN__)

	static void renderWorkerLoop(Soloud *aSoloud, RenderWorker *aWorker)
	{
		Thread::raiseCurrentThreadPriority();
		soloud_simd::ScopedFlushDenormals flushDenormals(
			(aSoloud->mFlags & Soloud::NO_FPU_REGISTER_CHANGE) == 0);
		while (!aWorker->mStop.load(std::memory_order_acquire))
		{
			aSoloud->renderRingTopUp_internal();
			std::unique_lock<std::mutex> lock(aWorker->mMutex);
			// Woken by the device callback after every read. The timeout is
			// only a safety net (the ring is full while it waits).
			aWorker->mCv.wait_for(lock, std::chrono::milliseconds(100), [aWorker] {
				return aWorker->mStop.load(std::memory_order_acquire) ||
					aWorker->mWake.exchange(false, std::memory_order_acq_rel);
			});
		}
	}
#endif

	void Soloud::startRenderWorker_internal()
	{
#if !defined(__EMSCRIPTEN__)
		if (!mRenderWorkerWanted || !isRenderAheadEnabled() || !mRenderRing.isInited() ||
			mBufferSize == 0 || mSamplerate == 0)
			return;
		RenderWorker *w = mRenderWorker.load(std::memory_order_acquire);
		if (w == NULL)
		{
			w = new RenderWorker();
			mRenderWorker.store(w, std::memory_order_release);
		}
		if (w->mThread.joinable())
			return;
		w->mStop.store(false, std::memory_order_release);
		w->mWake.store(false, std::memory_order_release);
		try
		{
			w->mThread = std::thread(renderWorkerLoop, this, w);
		}
		catch (...)
		{
			// No thread: the device callback keeps mixing itself.
			return;
		}
		w->mRunning.store(true, std::memory_order_release);
#endif
	}

	void Soloud::stopRenderWorker_internal()
	{
#if !defined(__EMSCRIPTEN__)
		RenderWorker *w = mRenderWorker.load(std::memory_order_acquire);
		if (w == NULL || !w->mThread.joinable())
			return;
		// The device callback mixes for itself again from here on.
		w->mRunning.store(false, std::memory_order_release);
		w->mStop.store(true, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(w->mMutex);
		}
		w->mCv.notify_all();
		w->mThread.join();
#endif
	}

	bool Soloud::isRenderWorkerRunning_internal() const
	{
#if !defined(__EMSCRIPTEN__)
		RenderWorker *w = mRenderWorker.load(std::memory_order_acquire);
		return w != NULL && w->mRunning.load(std::memory_order_acquire);
#else
		return false;
#endif
	}

	void Soloud::wakeRenderWorker_internal()
	{
#if !defined(__EMSCRIPTEN__)
		RenderWorker *w = mRenderWorker.load(std::memory_order_acquire);
		if (w == NULL || !w->mRunning.load(std::memory_order_acquire))
			return;
		w->mWake.store(true, std::memory_order_seq_cst);
		// Passing through the mutex orders this wake with the worker's check
		// of mWake: it either sees the flag or is already waiting and gets
		// the notification. The worker only holds the mutex to check the
		// flag, so a few attempts are enough; never block on it.
		int attempt;
		for (attempt = 0; attempt < 64; attempt++)
		{
			if (w->mMutex.try_lock())
			{
				w->mMutex.unlock();
				break;
			}
		}
		w->mCv.notify_one();
#endif
	}

	bool Soloud::tryLockAudioMutex_internal()
	{
		if (mAudioThreadMutex && Thread::tryLockMutex(mAudioThreadMutex) != 0)
			return false;
		SOLOUD_ASSERT(!mInsideAudioThreadMutex);
		mInsideAudioThreadMutex = true;
		return true;
	}

	void Soloud::lockGlobalFilters_internal()
	{
		mGlobalFilterMutex.lock();
	}

	void Soloud::unlockGlobalFilters_internal()
	{
		mGlobalFilterMutex.unlock();
	}

	void Soloud::audioThreadBarrier_internal()
	{
		lockAudioMutex_internal();
		unlockAudioMutex_internal();
		lockGlobalFilters_internal();
		unlockGlobalFilters_internal();
	}

	unsigned int Soloud::readRenderRingForDevice_internal(float *aBuffer, unsigned int aFrames)
	{
		const bool worker = isRenderWorkerRunning_internal();
		if (!worker)
		{
			// No worker (none wanted, or it could not be started): mix here.
			renderRingTopUp_internal();
		}
		else if (mRenderRing.availableToRead() < aFrames)
		{
			// The worker fell behind: mix what this read needs, but only if
			// the audio mutex is free right now.
			renderRingTopUpNoWait_internal(aFrames);
			// Some backends deliver callbacks in back-to-back bursts (AAudio's
			// non-low-latency path fires several callbacks within a
			// millisecond), which drains the whole render-ahead window faster
			// than the worker can be scheduled. When the try-lock above lost
			// to the worker, the worker is mixing exactly the frames this read
			// needs: wait a bounded time for them (re-trying the lock in case
			// the mutex frees up) rather than emitting silence. The bound
			// keeps a long API-thread hold from stalling the device.
			if (mRenderRing.availableToRead() < aFrames && mSamplerate > 0)
			{
				const long long budgetUs =
					(long long)aFrames * 1000000LL / (2LL * (long long)mSamplerate);
				const std::chrono::steady_clock::time_point deadline =
					std::chrono::steady_clock::now() + std::chrono::microseconds(budgetUs);
				do
				{
					wakeRenderWorker_internal();
					std::this_thread::yield();
					renderRingTopUpNoWait_internal(aFrames);
				} while (mRenderRing.availableToRead() < aFrames &&
					std::chrono::steady_clock::now() < deadline);
			}
		}
		const unsigned int got = mRenderRing.read(aBuffer, aFrames);
		if (worker)
			wakeRenderWorker_internal();
		if (got < aFrames)
		{
			const unsigned int channels = mRenderRing.getChannels();
			memset(aBuffer + (size_t)got * channels, 0,
				(size_t)(aFrames - got) * channels * sizeof(float));
		}
		postProcessRenderRing_internal(aBuffer, aFrames);
		return got;
	}

	void Soloud::postProcessRenderRing_internal(float *aBuffer, unsigned int aSamples)
	{
		if (aSamples == 0 || mChannels == 0 || mScratchSize == 0 || mPostRingScratch.mData == NULL)
			return;
		soloud_simd::ScopedFlushDenormals flushDenormals(
			(mFlags & NO_FPU_REGISTER_CHANGE) == 0);
		// Runs in the device callback. The global filters have their own lock
		// (see lockGlobalFilters_internal()): the render worker holds the
		// audio mutex while it mixes, and the device must not wait for that.
		// The clip below needs no lock (clip_internal() runs unlocked on the
		// direct mix path too), so the lock is only taken when there is a
		// filter to run.
		const bool anyFilter = mGlobalFilterMask.load(std::memory_order_acquire) != 0;
		if (!anyFilter)
		{
			// Fast path: clip interleaved ring samples directly in-place,
			// skipping deinterleave -> mPostRingScratch -> re-interleave.
			const float postClipScaler = mPostClipScaler.load(std::memory_order_acquire);
			const size_t totalFloats = (size_t)aSamples * mChannels;
			if (mFlags & CLIP_ROUNDOFF)
				clipRoundoffInPlaceF32(aBuffer, totalFloats, postClipScaler);
			else
				clipHardInPlaceF32(aBuffer, totalFloats, -1.0f, 1.0f, postClipScaler);
			return;
		}
		unsigned int i;
		time t = 0;
		lockGlobalFilters_internal();
		// The device callback has already read aBuffer out of the ring:
		// time its first frame from the ring position, not the mix clock
		// (which needs the audio mutex).
		const unsigned long long readPos = mRenderRing.getReadPosition();
		const unsigned long long firstFrame = readPos > aSamples ? readPos - aSamples : 0;
		if (mSamplerate > 0)
			t = mRenderRingTimeOrigin.load(std::memory_order_acquire) +
				(time)firstFrame / (time)mSamplerate;
		if (t < 0)
			t = 0;
		unsigned int offset = 0;
		unsigned int maxChunk = mScratchSize & ~0xfu;
		if (maxChunk == 0)
			maxChunk = mScratchSize;
		while (offset < aSamples)
		{
			unsigned int chunk = aSamples - offset;
			if (chunk > maxChunk)
				chunk = maxChunk;
			unsigned int stride = (chunk + 15) & ~0xfu;
			if (stride > mScratchSize)
				stride = mScratchSize;
			float *planar = mPostRingScratch.mData;
			const float *src = aBuffer + (size_t)offset * mChannels;
			deinterleaveFloatsToPlanar(src, planar, stride, chunk, mChannels);
			if (stride > chunk)
			{
				for (unsigned int c = 0; c < mChannels; c++)
				{
					memset(planar + (size_t)c * stride + chunk, 0,
						sizeof(float) * (stride - chunk));
				}
			}
			for (i = 0; i < FILTERS_PER_STREAM; i++)
			{
				if (mFilterInstance[i] != NULL)
				{
					mFilterInstance[i]->filter(planar, chunk, stride, mChannels, (float)mSamplerate, t);
				}
			}
			// Final clip (global volume was applied when mixing into the
			// ring). Covers the global filters' output too.
			clip_internal(mPostRingScratch, mPostRingScratch, stride, 1.0f, 1.0f);
			interlace_samples_float(planar, aBuffer + (size_t)offset * mChannels, chunk, mChannels, stride);
			if (mSamplerate > 0)
				t += (time)chunk / (time)mSamplerate;
			offset += chunk;
		}
		unlockGlobalFilters_internal();
	}

	const char * Soloud::getErrorString(result aErrorCode) const
	{
		switch (aErrorCode)
		{
		case SO_NO_ERROR: return "No error";
		case INVALID_PARAMETER: return "Some parameter is invalid";
		case FILE_NOT_FOUND: return "File not found";
		case FILE_LOAD_FAILED: return "File found, but could not be loaded";
		case DLL_NOT_FOUND: return "DLL not found, or wrong DLL";
		case OUT_OF_MEMORY: return "Out of memory";
		case NOT_IMPLEMENTED: return "Feature not implemented";
		/*case UNKNOWN_ERROR: return "Other error";*/
		}
		return "Other error";
	}


	float * Soloud::getWave()
	{
		int i;
		lockAudioMutex_internal();
		for (i = 0; i < 256; i++)
			mWaveData[i] = mVisualizationWaveData[i];
		unlockAudioMutex_internal();
		return mWaveData;
	}

	float Soloud::getApproximateVolume(unsigned int aChannel)
	{
		if (aChannel > mChannels)
			return 0;
		float vol = 0;
		lockAudioMutex_internal();
		vol = mVisualizationChannelVolume[aChannel];
		unlockAudioMutex_internal();
		return vol;
	}


	float * Soloud::calcFFT()
	{
		lockAudioMutex_internal();
		alignas(64) float temp[1024];
		int i;
		for (i = 0; i < 256; i++)
		{
			temp[i*2] = mVisualizationWaveData[i];
			temp[i*2+1] = 0;
			temp[i+512] = 0;
			temp[i+768] = 0;
		}
		unlockAudioMutex_internal();

		SoLoud::FFT::fft1024(temp);

		for (i = 0; i < 256; i++)
		{
			float real = temp[i * 2];
			float imag = temp[i * 2 + 1];
			mFFTData[i] = sqrtf(real*real+imag*imag);
		}

		return mFFTData;
	}

	// ###### flutter_soloud local patch (render-ahead ring) ######
	void Soloud::applyGlobalVolume_internal(AlignedFloatBuffer &aBuffer, AlignedFloatBuffer &aDestBuffer, unsigned int aSamples, float aVolume0, float aVolume1)
	{
		// Same per-channel ramp and the same buffer extent (whole quads of
		// aSamples per channel) as clip_internal().
		if (aSamples == 0)
			return;
		const unsigned int samplequads = (aSamples + 3) / 4;
		const unsigned int samples = samplequads * 4;
		if (aVolume0 == 1.0f && aVolume1 == 1.0f)
		{
			if (&aBuffer != &aDestBuffer)
				memcpy(aDestBuffer.mData, aBuffer.mData, sizeof(float) * samples * mChannels);
			return;
		}
		if (aVolume0 == aVolume1)
		{
			const auto vVol = soloud_simd::f32x4::splat(aVolume0);
			const unsigned int total = samples * mChannels;
			for (unsigned int c = 0; c < total; c += 4)
			{
				soloud_simd::mul(soloud_simd::f32x4::loadu(aBuffer.mData + c), vVol)
					.storeu(aDestBuffer.mData + c);
			}
			return;
		}
		const float vd = (aVolume1 - aVolume0) / (float)aSamples;
		const auto vInit = soloud_simd::f32x4::make(
			aVolume0, aVolume0 + vd, aVolume0 + vd * 2.0f, aVolume0 + vd * 3.0f);
		const auto vDelta = soloud_simd::f32x4::splat(vd * 4.0f);
		unsigned int c = 0;
		for (unsigned int j = 0; j < mChannels; j++)
		{
			auto vol = vInit;
			for (unsigned int i = 0; i < samplequads; i++, c += 4)
			{
				soloud_simd::mul(soloud_simd::f32x4::loadu(aBuffer.mData + c), vol)
					.storeu(aDestBuffer.mData + c);
				vol = soloud_simd::add(vol, vDelta);
			}
		}
	}

#if defined(SOLOUD_SSE_INTRINSICS)
	void Soloud::clip_internal(AlignedFloatBuffer &aBuffer, AlignedFloatBuffer &aDestBuffer, unsigned int aSamples, float aVolume0, float aVolume1)
	{
		float vd = (aVolume1 - aVolume0) / aSamples;
		float v = aVolume0;
		unsigned int i, j, c, d;
		unsigned int samplequads = (aSamples + 3) / 4; // rounded up

		// Clip
		if (mFlags & CLIP_ROUNDOFF)
		{
			float nb = -1.65f;		__m128 negbound = _mm_load_ps1(&nb);
			float pb = 1.65f;		__m128 posbound = _mm_load_ps1(&pb);
			float ls = 0.87f;		__m128 linearscale = _mm_load_ps1(&ls);
			float cs = -0.1f;		__m128 cubicscale = _mm_load_ps1(&cs);
			float nw = -0.9862875f;	__m128 negwall = _mm_load_ps1(&nw);
			float pw = 0.9862875f;	__m128 poswall = _mm_load_ps1(&pw);
			float postClipScaler = mPostClipScaler.load(std::memory_order_acquire);
			__m128 postscale = _mm_load_ps1(&postClipScaler);
			TinyAlignedFloatBuffer volumes;
			volumes.mData[0] = v;
			volumes.mData[1] = v + vd;
			volumes.mData[2] = v + vd + vd;
			volumes.mData[3] = v + vd + vd + vd;
			vd *= 4;
			__m128 vdelta = _mm_load_ps1(&vd);
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				__m128 vol = _mm_load_ps(volumes.mData);

				for (i = 0; i < samplequads; i++)
				{
					//float f1 = origdata[c] * v;	c++; v += vd;
					__m128 f = _mm_loadu_ps(&aBuffer.mData[c]);
					c += 4;
					f = _mm_mul_ps(f, vol);
					vol = _mm_add_ps(vol, vdelta);

					//float u1 = (f1 > -1.65f);
					__m128 u = _mm_cmpgt_ps(f, negbound);

					//float o1 = (f1 < 1.65f);
					__m128 o = _mm_cmplt_ps(f, posbound);

					//f1 = (0.87f * f1 - 0.1f * f1 * f1 * f1) * u1 * o1;
					__m128 lin = _mm_mul_ps(f, linearscale);
					__m128 cubic = _mm_mul_ps(f, f);
					cubic = _mm_mul_ps(cubic, f);
					cubic = _mm_mul_ps(cubic, cubicscale);
					f = _mm_add_ps(cubic, lin);

					//f1 = f1 * u1 + !u1 * -0.9862875f;
					__m128 lowmask = _mm_andnot_ps(u, negwall);
					__m128 ilowmask = _mm_and_ps(u, f);
					f = _mm_add_ps(lowmask, ilowmask);

					//f1 = f1 * o1 + !o1 * 0.9862875f;
					__m128 himask = _mm_andnot_ps(o, poswall);
					__m128 ihimask = _mm_and_ps(o, f);
					f = _mm_add_ps(himask, ihimask);

					// outdata[d] = f1 * postclip; d++;
					f = _mm_mul_ps(f, postscale);
					_mm_storeu_ps(&aDestBuffer.mData[d], f);
					d += 4;
				}
			}
		}
		else
		{
			float nb = -1.0f;	__m128 negbound = _mm_load_ps1(&nb);
			float pb = 1.0f;	__m128 posbound = _mm_load_ps1(&pb);
			float postClipScaler = mPostClipScaler.load(std::memory_order_acquire);
			__m128 postscale = _mm_load_ps1(&postClipScaler);
			TinyAlignedFloatBuffer volumes;
			volumes.mData[0] = v;
			volumes.mData[1] = v + vd;
			volumes.mData[2] = v + vd + vd;
			volumes.mData[3] = v + vd + vd + vd;
			vd *= 4;
			__m128 vdelta = _mm_load_ps1(&vd);
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				__m128 vol = _mm_load_ps(volumes.mData);
				for (i = 0; i < samplequads; i++) 
				{
					//float f1 = aBuffer.mData[c] * v; c++; v += vd;
					__m128 f = _mm_loadu_ps(&aBuffer.mData[c]);
					c += 4;
					f = _mm_mul_ps(f, vol);
					vol = _mm_add_ps(vol, vdelta);

					//f1 = (f1 <= -1) ? -1 : (f1 >= 1) ? 1 : f1;
					f = _mm_max_ps(f, negbound);
					f = _mm_min_ps(f, posbound);

					//aDestBuffer.mData[d] = f1 * mPostClipScaler; d++;
					f = _mm_mul_ps(f, postscale);
					_mm_storeu_ps(&aDestBuffer.mData[d], f);
					d += 4;
				}
			}
		}
	}
#elif defined(SOLOUD_NEON_INTRINSICS)
	void Soloud::clip_internal(AlignedFloatBuffer &aBuffer, AlignedFloatBuffer &aDestBuffer, unsigned int aSamples, float aVolume0, float aVolume1)
	{
		float vd = (aVolume1 - aVolume0) / aSamples;
		float v = aVolume0;
		unsigned int i, j, c, d;
		unsigned int samplequads = (aSamples + 3) / 4; // rounded up
		float postClipScaler = mPostClipScaler.load(std::memory_order_acquire);
		float32x4_t postscale = vdupq_n_f32(postClipScaler);
		TinyAlignedFloatBuffer volumes;
		volumes.mData[0] = v;
		volumes.mData[1] = v + vd;
		volumes.mData[2] = v + vd + vd;
		volumes.mData[3] = v + vd + vd + vd;
		vd *= 4;
		float32x4_t vdelta = vdupq_n_f32(vd);

		// Clip
		if (mFlags & CLIP_ROUNDOFF)
		{
			float32x4_t negbound = vdupq_n_f32(-1.65f);
			float32x4_t posbound = vdupq_n_f32(1.65f);
			float32x4_t linearscale = vdupq_n_f32(0.87f);
			float32x4_t cubicscale = vdupq_n_f32(-0.1f);
			float32x4_t negwall = vdupq_n_f32(-0.9862875f);
			float32x4_t poswall = vdupq_n_f32(0.9862875f);
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				float32x4_t vol = vld1q_f32(volumes.mData);
				for (i = 0; i < samplequads; i++)
				{
					float32x4_t f = vld1q_f32(&aBuffer.mData[c]);
					c += 4;
					f = vmulq_f32(f, vol);
					vol = vaddq_f32(vol, vdelta);

					uint32x4_t u = vcgtq_f32(f, negbound);
					uint32x4_t o = vcltq_f32(f, posbound);

					float32x4_t lin = vmulq_f32(f, linearscale);
					float32x4_t cubic = vmulq_f32(f, f);
					cubic = vmulq_f32(cubic, f);
					cubic = vmulq_f32(cubic, cubicscale);
					f = vaddq_f32(cubic, lin);

					f = vbslq_f32(u, f, negwall);
					f = vbslq_f32(o, f, poswall);

					f = vmulq_f32(f, postscale);
					vst1q_f32(&aDestBuffer.mData[d], f);
					d += 4;
				}
			}
		}
		else
		{
			float32x4_t negbound = vdupq_n_f32(-1.0f);
			float32x4_t posbound = vdupq_n_f32(1.0f);
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				float32x4_t vol = vld1q_f32(volumes.mData);
				for (i = 0; i < samplequads; i++)
				{
					float32x4_t f = vld1q_f32(&aBuffer.mData[c]);
					c += 4;
					f = vmulq_f32(f, vol);
					vol = vaddq_f32(vol, vdelta);

					f = vmaxq_f32(f, negbound);
					f = vminq_f32(f, posbound);

					f = vmulq_f32(f, postscale);
					vst1q_f32(&aDestBuffer.mData[d], f);
					d += 4;
				}
			}
		}
	}
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
	void Soloud::clip_internal(AlignedFloatBuffer &aBuffer, AlignedFloatBuffer &aDestBuffer, unsigned int aSamples, float aVolume0, float aVolume1)
	{
		float vd = (aVolume1 - aVolume0) / aSamples;
		float v = aVolume0;
		unsigned int i, j, c, d;
		unsigned int samplequads = (aSamples + 3) / 4;
		float postClipScaler = mPostClipScaler.load(std::memory_order_acquire);
		v128_t postscale = wasm_f32x4_splat(postClipScaler);
		v128_t volInit = wasm_f32x4_make(v, v + vd, v + vd + vd, v + vd + vd + vd);
		v128_t vdelta = wasm_f32x4_splat(vd * 4.0f);

		if (mFlags & CLIP_ROUNDOFF)
		{
			v128_t negbound = wasm_f32x4_splat(-1.65f);
			v128_t posbound = wasm_f32x4_splat(1.65f);
			v128_t linearscale = wasm_f32x4_splat(0.87f);
			v128_t cubicscale = wasm_f32x4_splat(-0.1f);
			v128_t negwall = wasm_f32x4_splat(-0.9862875f);
			v128_t poswall = wasm_f32x4_splat(0.9862875f);
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				v128_t vol = volInit;
				for (i = 0; i < samplequads; i++)
				{
					v128_t f = wasm_v128_load(&aBuffer.mData[c]);
					c += 4;
					f = wasm_f32x4_mul(f, vol);
					vol = wasm_f32x4_add(vol, vdelta);

					v128_t u = wasm_f32x4_gt(f, negbound);
					v128_t o = wasm_f32x4_lt(f, posbound);

					v128_t lin = wasm_f32x4_mul(f, linearscale);
					v128_t cubic = wasm_f32x4_mul(wasm_f32x4_mul(wasm_f32x4_mul(f, f), f), cubicscale);
					f = wasm_f32x4_add(cubic, lin);

					f = wasm_v128_bitselect(f, negwall, u);
					f = wasm_v128_bitselect(f, poswall, o);

					f = wasm_f32x4_mul(f, postscale);
					wasm_v128_store(&aDestBuffer.mData[d], f);
					d += 4;
				}
			}
		}
		else
		{
			v128_t negbound = wasm_f32x4_splat(-1.0f);
			v128_t posbound = wasm_f32x4_splat(1.0f);
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				v128_t vol = volInit;
				for (i = 0; i < samplequads; i++)
				{
					v128_t f = wasm_v128_load(&aBuffer.mData[c]);
					c += 4;
					f = wasm_f32x4_mul(f, vol);
					vol = wasm_f32x4_add(vol, vdelta);

					f = wasm_f32x4_pmax(negbound, f);
					f = wasm_f32x4_pmin(posbound, f);

					f = wasm_f32x4_mul(f, postscale);
					wasm_v128_store(&aDestBuffer.mData[d], f);
					d += 4;
				}
			}
		}
	}
#else // fallback code
	void Soloud::clip_internal(AlignedFloatBuffer &aBuffer, AlignedFloatBuffer &aDestBuffer, unsigned int aSamples, float aVolume0, float aVolume1)
	{
		float vd = (aVolume1 - aVolume0) / aSamples;
		float v = aVolume0;
		unsigned int i, j, c, d;
		unsigned int samplequads = (aSamples + 3) / 4; // rounded up
		// Snapshot once: this runs with the audio mutex released, so the value
		// can change under the loop. See the declaration in soloud.h.
		float postClipScaler = mPostClipScaler.load(std::memory_order_acquire);
		// Clip
		if (mFlags & CLIP_ROUNDOFF)
		{
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				v = aVolume0;
				for (i = 0; i < samplequads; i++)
				{
					float f1 = aBuffer.mData[c] * v; c++; v += vd;
					float f2 = aBuffer.mData[c] * v; c++; v += vd;
					float f3 = aBuffer.mData[c] * v; c++; v += vd;
					float f4 = aBuffer.mData[c] * v; c++; v += vd;

					f1 = (f1 <= -1.65f) ? -0.9862875f : (f1 >= 1.65f) ? 0.9862875f : (0.87f * f1 - 0.1f * f1 * f1 * f1);
					f2 = (f2 <= -1.65f) ? -0.9862875f : (f2 >= 1.65f) ? 0.9862875f : (0.87f * f2 - 0.1f * f2 * f2 * f2);
					f3 = (f3 <= -1.65f) ? -0.9862875f : (f3 >= 1.65f) ? 0.9862875f : (0.87f * f3 - 0.1f * f3 * f3 * f3);
					f4 = (f4 <= -1.65f) ? -0.9862875f : (f4 >= 1.65f) ? 0.9862875f : (0.87f * f4 - 0.1f * f4 * f4 * f4);

					aDestBuffer.mData[d] = f1 * postClipScaler; d++;
					aDestBuffer.mData[d] = f2 * postClipScaler; d++;
					aDestBuffer.mData[d] = f3 * postClipScaler; d++;
					aDestBuffer.mData[d] = f4 * postClipScaler; d++;
				}
			}
		}
		else
		{
			c = 0;
			d = 0;
			for (j = 0; j < mChannels; j++)
			{
				v = aVolume0;
				for (i = 0; i < samplequads; i++) 
				{
					float f1 = aBuffer.mData[c] * v; c++; v += vd;
					float f2 = aBuffer.mData[c] * v; c++; v += vd;
					float f3 = aBuffer.mData[c] * v; c++; v += vd;
					float f4 = aBuffer.mData[c] * v; c++; v += vd;

					f1 = (f1 <= -1) ? -1 : (f1 >= 1) ? 1 : f1;
					f2 = (f2 <= -1) ? -1 : (f2 >= 1) ? 1 : f2;
					f3 = (f3 <= -1) ? -1 : (f3 >= 1) ? 1 : f3;
					f4 = (f4 <= -1) ? -1 : (f4 >= 1) ? 1 : f4;

					aDestBuffer.mData[d] = f1 * postClipScaler; d++;
					aDestBuffer.mData[d] = f2 * postClipScaler; d++;
					aDestBuffer.mData[d] = f3 * postClipScaler; d++;
					aDestBuffer.mData[d] = f4 * postClipScaler; d++;
			}
		}
	}
}
#endif

#define FIXPOINT_FRAC_BITS 20
#define FIXPOINT_FRAC_MUL (1 << FIXPOINT_FRAC_BITS)
#define FIXPOINT_FRAC_MASK ((1 << FIXPOINT_FRAC_BITS) - 1)

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

	static float catmullrom(float t, float p0, float p1, float p2, float p3)
	{
		return 0.5f * (
			(2 * p1) +
			(-p0 + p2) * t +
			(2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
			(-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t
			);
	}

	static void resample_catmullrom(float* aSrc,
		float* aSrc1,
		float* aDst,
		int aSrcOffset,
		int aDstSampleCount,
		int aStepFixed)
	{
		int i = 0;
		int pos = aSrcOffset;

		// Peel initial lookbehind boundary samples (p < 3) where aSrc1 is needed.
		while (i < aDstSampleCount && (pos >> FIXPOINT_FRAC_BITS) < 3)
		{
			int p = pos >> FIXPOINT_FRAC_BITS;
			int f = pos & FIXPOINT_FRAC_MASK;

			float s3 = (p < 3) ? aSrc1[SAMPLE_GRANULARITY + p - 3] : aSrc[p - 3];
			float s2 = (p < 2) ? aSrc1[SAMPLE_GRANULARITY + p - 2] : aSrc[p - 2];
			float s1 = (p < 1) ? aSrc1[SAMPLE_GRANULARITY + p - 1] : aSrc[p - 1];
			float s0 = aSrc[p];

			aDst[i] = catmullrom(f / (float)FIXPOINT_FRAC_MUL, s3, s2, s1, s0);
			i++;
			pos += aStepFixed;
		}

		if (i >= aDstSampleCount)
			return;

		// Unit-rate fast paths (p >= 3 guaranteed for all remaining samples).
		if (aStepFixed == FIXPOINT_FRAC_MUL)
		{
			const int f = pos & FIXPOINT_FRAC_MASK;
			int p = pos >> FIXPOINT_FRAC_BITS;
			if (f == 0)
			{
				memcpy(aDst + i, aSrc + p - 2, sizeof(float) * (size_t)(aDstSampleCount - i));
				return;
			}
			const float tScalar = (float)f * (1.0f / (float)FIXPOINT_FRAC_MUL);
			const auto vt = soloud_simd::f32x4::splat(tScalar);
			const auto vHalf = soloud_simd::f32x4::splat(0.5f);
			const auto v2 = soloud_simd::f32x4::splat(2.0f);
			const auto v3 = soloud_simd::f32x4::splat(3.0f);
			const auto v4 = soloud_simd::f32x4::splat(4.0f);
			const auto v5 = soloud_simd::f32x4::splat(5.0f);
			for (; i + 4 <= aDstSampleCount; i += 4, p += 4)
			{
				const auto vp0 = soloud_simd::f32x4::loadu(aSrc + p - 3);
				const auto vp1 = soloud_simd::f32x4::loadu(aSrc + p - 2);
				const auto vp2 = soloud_simd::f32x4::loadu(aSrc + p - 1);
				const auto vp3 = soloud_simd::f32x4::loadu(aSrc + p);

				const auto negP0 = soloud_simd::neg(vp0);
				const auto term0 = soloud_simd::mul(v2, vp1);
				const auto term1 = soloud_simd::mul(soloud_simd::add(negP0, vp2), vt);

				const auto c2 = soloud_simd::sub(
					soloud_simd::add(
						soloud_simd::sub(soloud_simd::mul(v2, vp0), soloud_simd::mul(v5, vp1)),
						soloud_simd::mul(v4, vp2)),
					vp3);
				const auto term2 = soloud_simd::mul(soloud_simd::mul(c2, vt), vt);

				const auto c3 = soloud_simd::add(
					soloud_simd::sub(
						soloud_simd::add(negP0, soloud_simd::mul(v3, vp1)),
						soloud_simd::mul(v3, vp2)),
					vp3);
				const auto term3 = soloud_simd::mul(soloud_simd::mul(soloud_simd::mul(c3, vt), vt), vt);

				const auto sum = soloud_simd::add(soloud_simd::add(soloud_simd::add(term0, term1), term2), term3);
				soloud_simd::mul(vHalf, sum).storeu(aDst + i);
			}
			for (; i < aDstSampleCount; i++, p++)
			{
				aDst[i] = catmullrom(tScalar, aSrc[p - 3], aSrc[p - 2], aSrc[p - 1], aSrc[p]);
			}
			return;
		}

		// General 4-wide SIMD path (all p >= 3, branchless).
		const auto vInvFrac = soloud_simd::f32x4::splat(1.0f / (float)FIXPOINT_FRAC_MUL);
		const auto vHalf = soloud_simd::f32x4::splat(0.5f);
		const auto v2 = soloud_simd::f32x4::splat(2.0f);
		const auto v3 = soloud_simd::f32x4::splat(3.0f);
		const auto v4 = soloud_simd::f32x4::splat(4.0f);
		const auto v5 = soloud_simd::f32x4::splat(5.0f);
		for (; i + 4 <= aDstSampleCount; i += 4)
		{
			const int pa = pos >> FIXPOINT_FRAC_BITS; const int fa = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const int pb = pos >> FIXPOINT_FRAC_BITS; const int fb = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const int pc = pos >> FIXPOINT_FRAC_BITS; const int fc = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const int pd = pos >> FIXPOINT_FRAC_BITS; const int fd = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;

			const auto vp0 = soloud_simd::f32x4::make(aSrc[pa - 3], aSrc[pb - 3], aSrc[pc - 3], aSrc[pd - 3]);
			const auto vp1 = soloud_simd::f32x4::make(aSrc[pa - 2], aSrc[pb - 2], aSrc[pc - 2], aSrc[pd - 2]);
			const auto vp2 = soloud_simd::f32x4::make(aSrc[pa - 1], aSrc[pb - 1], aSrc[pc - 1], aSrc[pd - 1]);
			const auto vp3 = soloud_simd::f32x4::make(aSrc[pa],     aSrc[pb],     aSrc[pc],     aSrc[pd]);
			const auto vt  = soloud_simd::mul(
				soloud_simd::f32x4::make((float)fa, (float)fb, (float)fc, (float)fd),
				vInvFrac);

			const auto negP0 = soloud_simd::neg(vp0);
			const auto term0 = soloud_simd::mul(v2, vp1);
			const auto term1 = soloud_simd::mul(soloud_simd::add(negP0, vp2), vt);

			const auto c2 = soloud_simd::sub(
				soloud_simd::add(
					soloud_simd::sub(soloud_simd::mul(v2, vp0), soloud_simd::mul(v5, vp1)),
					soloud_simd::mul(v4, vp2)),
				vp3);
			const auto term2 = soloud_simd::mul(soloud_simd::mul(c2, vt), vt);

			const auto c3 = soloud_simd::add(
				soloud_simd::sub(
					soloud_simd::add(negP0, soloud_simd::mul(v3, vp1)),
					soloud_simd::mul(v3, vp2)),
				vp3);
			const auto term3 = soloud_simd::mul(soloud_simd::mul(soloud_simd::mul(c3, vt), vt), vt);

			const auto sum = soloud_simd::add(soloud_simd::add(soloud_simd::add(term0, term1), term2), term3);
			soloud_simd::mul(vHalf, sum).storeu(aDst + i);
		}

		for (; i < aDstSampleCount; i++, pos += aStepFixed)
		{
			const int p = pos >> FIXPOINT_FRAC_BITS;
			const int f = pos & FIXPOINT_FRAC_MASK;
			aDst[i] = catmullrom(f / (float)FIXPOINT_FRAC_MUL, aSrc[p - 3], aSrc[p - 2], aSrc[p - 1], aSrc[p]);
		}
	}

	static void resample_linear(float* aSrc,
		float* aSrc1,
		float* aDst,
		int aSrcOffset,
		int aDstSampleCount,
		int aStepFixed)
	{
		int i = 0;
		int pos = aSrcOffset;

		// Peel initial boundary samples (p == 0) where aSrc1[SAMPLE_GRANULARITY - 1] is needed.
		while (i < aDstSampleCount && (pos >> FIXPOINT_FRAC_BITS) < 1)
		{
			int p = pos >> FIXPOINT_FRAC_BITS;
			int f = pos & FIXPOINT_FRAC_MASK;
#ifdef _DEBUG
			if (p >= SAMPLE_GRANULARITY || p < 0)
			{
				p = SAMPLE_GRANULARITY - 1;
			}
#endif
			float s1 = (p != 0) ? aSrc[p - 1] : aSrc1[SAMPLE_GRANULARITY - 1];
			float s2 = aSrc[p];
			aDst[i] = s1 + (s2 - s1) * f * (1 / (float)FIXPOINT_FRAC_MUL);
			i++;
			pos += aStepFixed;
		}

		if (i >= aDstSampleCount)
			return;

		// Unit-rate fast paths (p >= 1 guaranteed for all remaining samples).
		if (aStepFixed == FIXPOINT_FRAC_MUL)
		{
			const int f = pos & FIXPOINT_FRAC_MASK;
			int p = pos >> FIXPOINT_FRAC_BITS;
			if (f == 0)
			{
				memcpy(aDst + i, aSrc + p - 1, sizeof(float) * (size_t)(aDstSampleCount - i));
				return;
			}
			const auto vf = soloud_simd::f32x4::splat((float)f);
			const auto vInvFrac = soloud_simd::f32x4::splat(1.0f / (float)FIXPOINT_FRAC_MUL);
			for (; i + 4 <= aDstSampleCount; i += 4, p += 4)
			{
				const auto vs1 = soloud_simd::f32x4::loadu(aSrc + p - 1);
				const auto vs2 = soloud_simd::f32x4::loadu(aSrc + p);
				const auto prod = soloud_simd::mul(soloud_simd::mul(soloud_simd::sub(vs2, vs1), vf), vInvFrac);
				soloud_simd::add(vs1, prod).storeu(aDst + i);
			}
			for (; i < aDstSampleCount; i++, p++)
			{
				const float s1 = aSrc[p - 1];
				const float s2 = aSrc[p];
				aDst[i] = s1 + (s2 - s1) * f * (1 / (float)FIXPOINT_FRAC_MUL);
			}
			return;
		}

		// General 4-wide SIMD path (all p >= 1, branchless).
		const auto vInvFrac = soloud_simd::f32x4::splat(1.0f / (float)FIXPOINT_FRAC_MUL);
		for (; i + 4 <= aDstSampleCount; i += 4)
		{
			const int pa = pos >> FIXPOINT_FRAC_BITS; const int fa = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const int pb = pos >> FIXPOINT_FRAC_BITS; const int fb = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const int pc = pos >> FIXPOINT_FRAC_BITS; const int fc = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const int pd = pos >> FIXPOINT_FRAC_BITS; const int fd = pos & FIXPOINT_FRAC_MASK; pos += aStepFixed;
			const auto vs1 = soloud_simd::f32x4::make(aSrc[pa - 1], aSrc[pb - 1], aSrc[pc - 1], aSrc[pd - 1]);
			const auto vs2 = soloud_simd::f32x4::make(aSrc[pa],     aSrc[pb],     aSrc[pc],     aSrc[pd]);
			const auto vf  = soloud_simd::f32x4::make((float)fa,    (float)fb,    (float)fc,    (float)fd);
			const auto prod = soloud_simd::mul(soloud_simd::mul(soloud_simd::sub(vs2, vs1), vf), vInvFrac);
			soloud_simd::add(vs1, prod).storeu(aDst + i);
		}

		for (; i < aDstSampleCount; i++, pos += aStepFixed)
		{
			const int p = pos >> FIXPOINT_FRAC_BITS;
			const int f = pos & FIXPOINT_FRAC_MASK;
			const float s1 = aSrc[p - 1];
			const float s2 = aSrc[p];
			aDst[i] = s1 + (s2 - s1) * f * (1 / (float)FIXPOINT_FRAC_MUL);
		}
	}

#if defined(__clang__)
#pragma clang fp contract(fast)
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

	static void resample_point(float* aSrc,
		float* /*aSrc1*/,
		float* aDst,
		int aSrcOffset,
		int aDstSampleCount,
		int aStepFixed)
	{
		if (aStepFixed == FIXPOINT_FRAC_MUL && aDstSampleCount > 0)
		{
			memcpy(aDst, aSrc + (aSrcOffset >> FIXPOINT_FRAC_BITS), sizeof(float) * (size_t)aDstSampleCount);
			return;
		}
		int i = 0;
		int pos = aSrcOffset;
		for (; i + 4 <= aDstSampleCount; i += 4)
		{
			const int p0 = pos >> FIXPOINT_FRAC_BITS; pos += aStepFixed;
			const int p1 = pos >> FIXPOINT_FRAC_BITS; pos += aStepFixed;
			const int p2 = pos >> FIXPOINT_FRAC_BITS; pos += aStepFixed;
			const int p3 = pos >> FIXPOINT_FRAC_BITS; pos += aStepFixed;
			aDst[i + 0] = aSrc[p0];
			aDst[i + 1] = aSrc[p1];
			aDst[i + 2] = aSrc[p2];
			aDst[i + 3] = aSrc[p3];
		}
		for (; i < aDstSampleCount; i++, pos += aStepFixed)
		{
			aDst[i] = aSrc[pos >> FIXPOINT_FRAC_BITS];
		}
	}



	void panAndExpand(AudioSourceInstance *aVoice, float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize, float *aScratch, unsigned int aChannels)
	{
		if (aSamplesToRead == 0)
		{
			unsigned int k;
			for (k = 0; k < aChannels; k++)
			{
				aVoice->mCurrentChannelVolume[k] = aVoice->mChannelVolume[k] * aVoice->mOverallVolume;
			}
			return;
		}
		float pan[MAX_CHANNELS]; // current speaker volume
		float pand[MAX_CHANNELS]; // destination speaker volume
		float pani[MAX_CHANNELS]; // speaker volume increment per sample
		unsigned int j, k;
		bool allSilent = true;
		for (k = 0; k < aChannels; k++)
		{
			pan[k] = aVoice->mCurrentChannelVolume[k];
			pand[k] = aVoice->mChannelVolume[k] * aVoice->mOverallVolume;
			pani[k] = (pand[k] - pan[k]) / aSamplesToRead; // TODO: this is a bit inconsistent.. but it's a hack to begin with
			if (pan[k] != 0.0f || pand[k] != 0.0f || pani[k] != 0.0f)
			{
				allSilent = false;
			}
		}
		if (allSilent)
		{
			for (k = 0; k < aChannels; k++)
				aVoice->mCurrentChannelVolume[k] = pand[k];
			return;
		}

		int ofs = 0;
		switch (aChannels)
		{
		case 1: // Target is mono. Sum everything. (1->1, 2->1, 4->1, 6->1, 8->1)
			for (j = 0, ofs = 0; j < aVoice->mChannels; j++, ofs += aBufferSize)
			{
				pan[0] = aVoice->mCurrentChannelVolume[0];
				const float step0 = pani[0];
				k = 0;
				if (step0 == 0.0f)
				{
					const auto vPan = soloud_simd::f32x4::splat(pan[0]);
					for (; k + 4 <= aSamplesToRead; k += 4)
					{
						const auto s = soloud_simd::f32x4::loadu(aScratch + ofs + k);
						const auto o = soloud_simd::f32x4::loadu(aBuffer + k);
						soloud_simd::add(o, soloud_simd::mul(s, vPan)).storeu(aBuffer + k);
					}
				}
				else
				{
					auto p0 = soloud_simd::f32x4::make(
						pan[0] + step0, pan[0] + step0 * 2.0f, pan[0] + step0 * 3.0f, pan[0] + step0 * 4.0f);
					const auto d0 = soloud_simd::f32x4::splat(step0 * 4.0f);
					for (; k + 4 <= aSamplesToRead; k += 4)
					{
						const auto s = soloud_simd::f32x4::loadu(aScratch + ofs + k);
						const auto o = soloud_simd::f32x4::loadu(aBuffer + k);
						soloud_simd::add(o, soloud_simd::mul(s, p0)).storeu(aBuffer + k);
						p0 = soloud_simd::add(p0, d0);
					}
					pan[0] += step0 * (float)k;
				}
				for (; k < aSamplesToRead; k++)
				{
					pan[0] += step0;
					aBuffer[k] += aScratch[ofs + k] * pan[0];
				}
			}
			break;
		case 2:
			switch (aVoice->mChannels)
			{
			case 8: // 8->2, just sum lefties and righties, add a bit of center and sub?
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					float s7 = aScratch[aBufferSize * 6 + j];
					float s8 = aScratch[aBufferSize * 7 + j];
					aBuffer[j + 0]           += 0.2f * (s1 + s3 + s4 + s5 + s7) * pan[0];
					aBuffer[j + aBufferSize] += 0.2f * (s2 + s3 + s4 + s6 + s8) * pan[1];
				}
				break;
			case 6: // 6->2, just sum lefties and righties, add a bit of center and sub?
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					aBuffer[j + 0] += 0.3f * (s1 + s3 + s4 + s5) * pan[0];
					aBuffer[j + aBufferSize] += 0.3f * (s2 + s3 + s4 + s6) * pan[1];
				}
				break;
			case 4: // 4->2, just sum lefties and righties
				{
					const float step0 = pani[0];
					const float step1 = pani[1];
					const auto vHalf = soloud_simd::f32x4::splat(0.5f);
					auto p0 = soloud_simd::f32x4::make(
						pan[0] + step0, pan[0] + step0 * 2.0f, pan[0] + step0 * 3.0f, pan[0] + step0 * 4.0f);
					auto p1 = soloud_simd::f32x4::make(
						pan[1] + step1, pan[1] + step1 * 2.0f, pan[1] + step1 * 3.0f, pan[1] + step1 * 4.0f);
					const auto d0 = soloud_simd::f32x4::splat(step0 * 4.0f);
					const auto d1 = soloud_simd::f32x4::splat(step1 * 4.0f);
					j = 0;
					for (; j + 4 <= aSamplesToRead; j += 4)
					{
						const auto s1 = soloud_simd::f32x4::loadu(aScratch + j);
						const auto s2 = soloud_simd::f32x4::loadu(aScratch + aBufferSize + j);
						const auto s3 = soloud_simd::f32x4::loadu(aScratch + aBufferSize * 2 + j);
						const auto s4 = soloud_simd::f32x4::loadu(aScratch + aBufferSize * 3 + j);
						const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
						const auto o1 = soloud_simd::f32x4::loadu(aBuffer + aBufferSize + j);
						soloud_simd::add(o0, soloud_simd::mul(soloud_simd::mul(vHalf, soloud_simd::add(s1, s3)), p0)).storeu(aBuffer + j);
						soloud_simd::add(o1, soloud_simd::mul(soloud_simd::mul(vHalf, soloud_simd::add(s2, s4)), p1)).storeu(aBuffer + aBufferSize + j);
						p0 = soloud_simd::add(p0, d0);
						p1 = soloud_simd::add(p1, d1);
					}
					pan[0] += step0 * (float)j;
					pan[1] += step1 * (float)j;
					for (; j < aSamplesToRead; j++)
					{
						pan[0] += step0;
						pan[1] += step1;
						float s1 = aScratch[j];
						float s2 = aScratch[aBufferSize + j];
						float s3 = aScratch[aBufferSize * 2 + j];
						float s4 = aScratch[aBufferSize * 3 + j];
						aBuffer[j + 0] += 0.5f * (s1 + s3) * pan[0];
						aBuffer[j + aBufferSize] += 0.5f * (s2 + s4) * pan[1];
					}
				}
				break;
			case 2: // 2->2
				{
					const float step0 = pani[0];
					const float step1 = pani[1];
					j = 0;
					if (step0 == 0.0f && step1 == 0.0f)
					{
						const auto p0 = soloud_simd::f32x4::splat(pan[0]);
						const auto p1 = soloud_simd::f32x4::splat(pan[1]);
						if (pan[0] != 0.0f && pan[1] != 0.0f)
						{
							for (; j + 4 <= aSamplesToRead; j += 4)
							{
								const auto f0 = soloud_simd::f32x4::loadu(aScratch + j);
								const auto f1 = soloud_simd::f32x4::loadu(aScratch + j + aBufferSize);
								const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
								const auto o1 = soloud_simd::f32x4::loadu(aBuffer + j + aBufferSize);
								soloud_simd::add(soloud_simd::mul(f0, p0), o0).storeu(aBuffer + j);
								soloud_simd::add(soloud_simd::mul(f1, p1), o1).storeu(aBuffer + j + aBufferSize);
							}
						}
						else if (pan[0] != 0.0f)
						{
							for (; j + 4 <= aSamplesToRead; j += 4)
							{
								const auto f0 = soloud_simd::f32x4::loadu(aScratch + j);
								const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
								soloud_simd::add(soloud_simd::mul(f0, p0), o0).storeu(aBuffer + j);
							}
						}
						else if (pan[1] != 0.0f)
						{
							for (; j + 4 <= aSamplesToRead; j += 4)
							{
								const auto f1 = soloud_simd::f32x4::loadu(aScratch + j + aBufferSize);
								const auto o1 = soloud_simd::f32x4::loadu(aBuffer + j + aBufferSize);
								soloud_simd::add(soloud_simd::mul(f1, p1), o1).storeu(aBuffer + j + aBufferSize);
							}
						}
						else
						{
							j = aSamplesToRead;
						}
					}
					else
					{
						auto p0 = soloud_simd::f32x4::make(
							pan[0] + step0, pan[0] + step0 * 2.0f, pan[0] + step0 * 3.0f, pan[0] + step0 * 4.0f);
						auto p1 = soloud_simd::f32x4::make(
							pan[1] + step1, pan[1] + step1 * 2.0f, pan[1] + step1 * 3.0f, pan[1] + step1 * 4.0f);
						const auto d0 = soloud_simd::f32x4::splat(step0 * 4.0f);
						const auto d1 = soloud_simd::f32x4::splat(step1 * 4.0f);
						for (; j + 4 <= aSamplesToRead; j += 4)
						{
							const auto f0 = soloud_simd::f32x4::loadu(aScratch + j);
							const auto f1 = soloud_simd::f32x4::loadu(aScratch + j + aBufferSize);
							const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
							const auto o1 = soloud_simd::f32x4::loadu(aBuffer + j + aBufferSize);
							soloud_simd::add(soloud_simd::mul(f0, p0), o0).storeu(aBuffer + j);
							soloud_simd::add(soloud_simd::mul(f1, p1), o1).storeu(aBuffer + j + aBufferSize);
							p0 = soloud_simd::add(p0, d0);
							p1 = soloud_simd::add(p1, d1);
						}
						pan[0] += step0 * (float)j;
						pan[1] += step1 * (float)j;
					}
					for (; j < aSamplesToRead; j++)
					{
						pan[0] += step0;
						pan[1] += step1;
						float s1 = aScratch[j];
						float s2 = aScratch[aBufferSize + j];
						aBuffer[j + 0] += s1 * pan[0];
						aBuffer[j + aBufferSize] += s2 * pan[1];
					}
				}
				break;
			case 1: // 1->2
				{
					const float step0 = pani[0];
					const float step1 = pani[1];
					j = 0;
					if (step0 == 0.0f && step1 == 0.0f)
					{
						const auto p0 = soloud_simd::f32x4::splat(pan[0]);
						const auto p1 = soloud_simd::f32x4::splat(pan[1]);
						if (pan[0] != 0.0f && pan[1] != 0.0f)
						{
							for (; j + 4 <= aSamplesToRead; j += 4)
							{
								const auto f = soloud_simd::f32x4::loadu(aScratch + j);
								const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
								const auto o1 = soloud_simd::f32x4::loadu(aBuffer + j + aBufferSize);
								soloud_simd::add(soloud_simd::mul(f, p0), o0).storeu(aBuffer + j);
								soloud_simd::add(soloud_simd::mul(f, p1), o1).storeu(aBuffer + j + aBufferSize);
							}
						}
						else if (pan[0] != 0.0f)
						{
							for (; j + 4 <= aSamplesToRead; j += 4)
							{
								const auto f = soloud_simd::f32x4::loadu(aScratch + j);
								const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
								soloud_simd::add(soloud_simd::mul(f, p0), o0).storeu(aBuffer + j);
							}
						}
						else if (pan[1] != 0.0f)
						{
							for (; j + 4 <= aSamplesToRead; j += 4)
							{
								const auto f = soloud_simd::f32x4::loadu(aScratch + j);
								const auto o1 = soloud_simd::f32x4::loadu(aBuffer + j + aBufferSize);
								soloud_simd::add(soloud_simd::mul(f, p1), o1).storeu(aBuffer + j + aBufferSize);
							}
						}
						else
						{
							j = aSamplesToRead;
						}
					}
					else
					{
						auto p0 = soloud_simd::f32x4::make(
							pan[0] + step0, pan[0] + step0 * 2.0f, pan[0] + step0 * 3.0f, pan[0] + step0 * 4.0f);
						auto p1 = soloud_simd::f32x4::make(
							pan[1] + step1, pan[1] + step1 * 2.0f, pan[1] + step1 * 3.0f, pan[1] + step1 * 4.0f);
						const auto d0 = soloud_simd::f32x4::splat(step0 * 4.0f);
						const auto d1 = soloud_simd::f32x4::splat(step1 * 4.0f);
						for (; j + 4 <= aSamplesToRead; j += 4)
						{
							const auto f = soloud_simd::f32x4::loadu(aScratch + j);
							const auto o0 = soloud_simd::f32x4::loadu(aBuffer + j);
							const auto o1 = soloud_simd::f32x4::loadu(aBuffer + j + aBufferSize);
							soloud_simd::add(soloud_simd::mul(f, p0), o0).storeu(aBuffer + j);
							soloud_simd::add(soloud_simd::mul(f, p1), o1).storeu(aBuffer + j + aBufferSize);
							p0 = soloud_simd::add(p0, d0);
							p1 = soloud_simd::add(p1, d1);
						}
						pan[0] += step0 * (float)j;
						pan[1] += step1 * (float)j;
					}
					for (; j < aSamplesToRead; j++)
					{
						pan[0] += step0;
						pan[1] += step1;
						float s = aScratch[j];
						aBuffer[j + 0] += s * pan[0];
						aBuffer[j + aBufferSize] += s * pan[1];
					}
				}
				break;
			}
			break;
		case 4:
			switch (aVoice->mChannels)
			{
			case 8: // 8->4, add a bit of center, sub?
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					float s7 = aScratch[aBufferSize * 6 + j];
					float s8 = aScratch[aBufferSize * 7 + j];
					float c = (s3 + s4) * 0.7f;
					aBuffer[j + 0]               += s1 * pan[0] + c;
					aBuffer[j + aBufferSize]     += s2 * pan[1] + c;
					aBuffer[j + aBufferSize * 2] += 0.5f * (s5 + s7) * pan[2];
					aBuffer[j + aBufferSize * 3] += 0.5f * (s6 + s8) * pan[3];
				}
				break;
			case 6: // 6->4, add a bit of center, sub?
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					float c = (s3 + s4) * 0.7f;
					aBuffer[j + 0] += s1 * pan[0] + c;
					aBuffer[j + aBufferSize] += s2 * pan[1] + c;
					aBuffer[j + aBufferSize * 2] += s5 * pan[2];
					aBuffer[j + aBufferSize * 3] += s6 * pan[3];
				}
				break;
			case 4: // 4->4
				{
					const float step[4] = { pani[0], pani[1], pani[2], pani[3] };
					soloud_simd::f32x4 p[4], d[4];
					for (unsigned int ch = 0; ch < 4; ch++)
					{
						p[ch] = soloud_simd::f32x4::make(
							pan[ch] + step[ch], pan[ch] + step[ch] * 2.0f, pan[ch] + step[ch] * 3.0f, pan[ch] + step[ch] * 4.0f);
						d[ch] = soloud_simd::f32x4::splat(step[ch] * 4.0f);
					}
					j = 0;
					for (; j + 4 <= aSamplesToRead; j += 4)
					{
						for (unsigned int ch = 0; ch < 4; ch++)
						{
							const auto s = soloud_simd::f32x4::loadu(aScratch + aBufferSize * ch + j);
							const auto o = soloud_simd::f32x4::loadu(aBuffer + aBufferSize * ch + j);
							soloud_simd::add(soloud_simd::mul(s, p[ch]), o).storeu(aBuffer + aBufferSize * ch + j);
							p[ch] = soloud_simd::add(p[ch], d[ch]);
						}
					}
					for (unsigned int ch = 0; ch < 4; ch++)
						pan[ch] += step[ch] * (float)j;
					for (; j < aSamplesToRead; j++)
					{
						pan[0] += step[0];
						pan[1] += step[1];
						pan[2] += step[2];
						pan[3] += step[3];
						aBuffer[j + 0]               += aScratch[j]                   * pan[0];
						aBuffer[j + aBufferSize]     += aScratch[aBufferSize + j]     * pan[1];
						aBuffer[j + aBufferSize * 2] += aScratch[aBufferSize * 2 + j] * pan[2];
						aBuffer[j + aBufferSize * 3] += aScratch[aBufferSize * 3 + j] * pan[3];
					}
				}
				break;
			case 2: // 2->4
				{
					const float step[4] = { pani[0], pani[1], pani[2], pani[3] };
					soloud_simd::f32x4 p[4], d[4];
					for (unsigned int ch = 0; ch < 4; ch++)
					{
						p[ch] = soloud_simd::f32x4::make(
							pan[ch] + step[ch], pan[ch] + step[ch] * 2.0f, pan[ch] + step[ch] * 3.0f, pan[ch] + step[ch] * 4.0f);
						d[ch] = soloud_simd::f32x4::splat(step[ch] * 4.0f);
					}
					j = 0;
					for (; j + 4 <= aSamplesToRead; j += 4)
					{
						const auto s1 = soloud_simd::f32x4::loadu(aScratch + j);
						const auto s2 = soloud_simd::f32x4::loadu(aScratch + aBufferSize + j);
						soloud_simd::add(soloud_simd::mul(s1, p[0]), soloud_simd::f32x4::loadu(aBuffer + j)).storeu(aBuffer + j);
						soloud_simd::add(soloud_simd::mul(s2, p[1]), soloud_simd::f32x4::loadu(aBuffer + aBufferSize + j)).storeu(aBuffer + aBufferSize + j);
						soloud_simd::add(soloud_simd::mul(s1, p[2]), soloud_simd::f32x4::loadu(aBuffer + aBufferSize * 2 + j)).storeu(aBuffer + aBufferSize * 2 + j);
						soloud_simd::add(soloud_simd::mul(s2, p[3]), soloud_simd::f32x4::loadu(aBuffer + aBufferSize * 3 + j)).storeu(aBuffer + aBufferSize * 3 + j);
						for (unsigned int ch = 0; ch < 4; ch++)
							p[ch] = soloud_simd::add(p[ch], d[ch]);
					}
					for (unsigned int ch = 0; ch < 4; ch++)
						pan[ch] += step[ch] * (float)j;
					for (; j < aSamplesToRead; j++)
					{
						pan[0] += step[0];
						pan[1] += step[1];
						pan[2] += step[2];
						pan[3] += step[3];
						float s1 = aScratch[j];
						float s2 = aScratch[aBufferSize + j];
						aBuffer[j + 0]               += s1 * pan[0];
						aBuffer[j + aBufferSize]     += s2 * pan[1];
						aBuffer[j + aBufferSize * 2] += s1 * pan[2];
						aBuffer[j + aBufferSize * 3] += s2 * pan[3];
					}
				}
				break;
			case 1: // 1->4
				{
					const float step[4] = { pani[0], pani[1], pani[2], pani[3] };
					soloud_simd::f32x4 p[4], d[4];
					for (unsigned int ch = 0; ch < 4; ch++)
					{
						p[ch] = soloud_simd::f32x4::make(
							pan[ch] + step[ch], pan[ch] + step[ch] * 2.0f, pan[ch] + step[ch] * 3.0f, pan[ch] + step[ch] * 4.0f);
						d[ch] = soloud_simd::f32x4::splat(step[ch] * 4.0f);
					}
					j = 0;
					for (; j + 4 <= aSamplesToRead; j += 4)
					{
						const auto s = soloud_simd::f32x4::loadu(aScratch + j);
						for (unsigned int ch = 0; ch < 4; ch++)
						{
							const auto o = soloud_simd::f32x4::loadu(aBuffer + aBufferSize * ch + j);
							soloud_simd::add(soloud_simd::mul(s, p[ch]), o).storeu(aBuffer + aBufferSize * ch + j);
							p[ch] = soloud_simd::add(p[ch], d[ch]);
						}
					}
					for (unsigned int ch = 0; ch < 4; ch++)
						pan[ch] += step[ch] * (float)j;
					for (; j < aSamplesToRead; j++)
					{
						pan[0] += step[0];
						pan[1] += step[1];
						pan[2] += step[2];
						pan[3] += step[3];
						float s = aScratch[j];
						aBuffer[j + 0]               += s * pan[0];
						aBuffer[j + aBufferSize]     += s * pan[1];
						aBuffer[j + aBufferSize * 2] += s * pan[2];
						aBuffer[j + aBufferSize * 3] += s * pan[3];
					}
				}
				break;
			}
			break;
		case 6:
			switch (aVoice->mChannels)
			{
			case 8: // 8->6
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					float s7 = aScratch[aBufferSize * 6 + j];
					float s8 = aScratch[aBufferSize * 7 + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += s3 * pan[2];
					aBuffer[j + aBufferSize * 3] += s4 * pan[3];
					aBuffer[j + aBufferSize * 4] += 0.5f * (s5 + s7) * pan[4];
					aBuffer[j + aBufferSize * 5] += 0.5f * (s6 + s8) * pan[5];
				}
				break;
			case 6: // 6->6
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += s3 * pan[2];
					aBuffer[j + aBufferSize * 3] += s4 * pan[3];
					aBuffer[j + aBufferSize * 4] += s5 * pan[4];
					aBuffer[j + aBufferSize * 5] += s6 * pan[5];
				}
				break;
			case 4: // 4->6
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += 0.5f * (s1 + s2) * pan[2];
					aBuffer[j + aBufferSize * 3] += 0.25f * (s1 + s2 + s3 + s4) * pan[3];
					aBuffer[j + aBufferSize * 4] += s3 * pan[4];
					aBuffer[j + aBufferSize * 5] += s4 * pan[5];
				}
				break;
			case 2: // 2->6
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += 0.5f * (s1 + s2) * pan[2];
					aBuffer[j + aBufferSize * 3] += 0.5f * (s1 + s2) * pan[3];
					aBuffer[j + aBufferSize * 4] += s1 * pan[4];
					aBuffer[j + aBufferSize * 5] += s2 * pan[5];
				}
				break;
			case 1: // 1->6
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					float s = aScratch[j];
					aBuffer[j + 0] += s * pan[0];
					aBuffer[j + aBufferSize] += s * pan[1];
					aBuffer[j + aBufferSize * 2] += s * pan[2];
					aBuffer[j + aBufferSize * 3] += s * pan[3];
					aBuffer[j + aBufferSize * 4] += s * pan[4];
					aBuffer[j + aBufferSize * 5] += s * pan[5];
				}
				break;
			}
			break;
		case 8:
			switch (aVoice->mChannels)
			{
			case 8: // 8->8
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					pan[6] += pani[6];
					pan[7] += pani[7];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					float s7 = aScratch[aBufferSize * 6 + j];
					float s8 = aScratch[aBufferSize * 7 + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += s3 * pan[2];
					aBuffer[j + aBufferSize * 3] += s4 * pan[3];
					aBuffer[j + aBufferSize * 4] += s5 * pan[4];
					aBuffer[j + aBufferSize * 5] += s6 * pan[5];
					aBuffer[j + aBufferSize * 6] += s7 * pan[6];
					aBuffer[j + aBufferSize * 7] += s8 * pan[7];
				}
				break;
			case 6: // 6->8
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					pan[6] += pani[6];
					pan[7] += pani[7];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					float s5 = aScratch[aBufferSize * 4 + j];
					float s6 = aScratch[aBufferSize * 5 + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += s3 * pan[2];
					aBuffer[j + aBufferSize * 3] += s4 * pan[3];
					aBuffer[j + aBufferSize * 4] += 0.5f * (s5 + s1) * pan[4];
					aBuffer[j + aBufferSize * 5] += 0.5f * (s6 + s2) * pan[5];
					aBuffer[j + aBufferSize * 6] += s5 * pan[6];
					aBuffer[j + aBufferSize * 7] += s6 * pan[7];
				}
				break;
			case 4: // 4->8
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					pan[6] += pani[6];
					pan[7] += pani[7];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					float s3 = aScratch[aBufferSize * 2 + j];
					float s4 = aScratch[aBufferSize * 3 + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += 0.5f * (s1 + s2) * pan[2];
					aBuffer[j + aBufferSize * 3] += 0.25f * (s1 + s2 + s3 + s4) * pan[3];
					aBuffer[j + aBufferSize * 4] += 0.5f * (s1 + s3) * pan[4];
					aBuffer[j + aBufferSize * 5] += 0.5f * (s2 + s4) * pan[5];
					aBuffer[j + aBufferSize * 6] += s3 * pan[4];
					aBuffer[j + aBufferSize * 7] += s4 * pan[5];
				}
				break;
			case 2: // 2->8
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					pan[6] += pani[6];
					pan[7] += pani[7];
					float s1 = aScratch[j];
					float s2 = aScratch[aBufferSize + j];
					aBuffer[j + 0] += s1 * pan[0];
					aBuffer[j + aBufferSize] += s2 * pan[1];
					aBuffer[j + aBufferSize * 2] += 0.5f * (s1 + s2) * pan[2];
					aBuffer[j + aBufferSize * 3] += 0.5f * (s1 + s2) * pan[3];
					aBuffer[j + aBufferSize * 4] += s1 * pan[4];
					aBuffer[j + aBufferSize * 5] += s2 * pan[5];
					aBuffer[j + aBufferSize * 6] += s1 * pan[6];
					aBuffer[j + aBufferSize * 7] += s2 * pan[7];
				}
				break;
			case 1: // 1->8
				for (j = 0; j < aSamplesToRead; j++)
				{
					pan[0] += pani[0];
					pan[1] += pani[1];
					pan[2] += pani[2];
					pan[3] += pani[3];
					pan[4] += pani[4];
					pan[5] += pani[5];
					pan[6] += pani[6];
					pan[7] += pani[7];
					float s = aScratch[j];
					aBuffer[j + 0] += s * pan[0];
					aBuffer[j + aBufferSize] += s * pan[1];
					aBuffer[j + aBufferSize * 2] += s * pan[2];
					aBuffer[j + aBufferSize * 3] += s * pan[3];
					aBuffer[j + aBufferSize * 4] += s * pan[4];
					aBuffer[j + aBufferSize * 5] += s * pan[5];
					aBuffer[j + aBufferSize * 6] += s * pan[6];
					aBuffer[j + aBufferSize * 7] += s * pan[7];
				}
				break;
			}
			break;
		}

		for (k = 0; k < aChannels; k++)
			aVoice->mCurrentChannelVolume[k] = pand[k];
	}

	void Soloud::mixBus_internal(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize, float *aScratch, unsigned int aBus, float aSamplerate, unsigned int aChannels, unsigned int aResampler)
	{
		unsigned int i, j;
		// Clear accumulation buffer
		for (j = 0; j < aChannels; j++)
		{
			memset(aBuffer + j * aBufferSize, 0, sizeof(float) * aSamplesToRead);
		}

		// Accumulate sound sources		
		for (i = 0; i < mActiveVoiceCount; i++)
		{
			AudioSourceInstance *voice = mVoice[mActiveVoice[i]];
			if (voice &&
				voice->mBusHandle == aBus &&
				!(voice->mFlags & AudioSourceInstance::PAUSED) &&
				!(voice->mFlags & AudioSourceInstance::INAUDIBLE))
			{
				float step = voice->mSamplerate / aSamplerate;
				// avoid step overflow
				if (step > (1 << (32 - FIXPOINT_FRAC_BITS)))
					step = 0;
				unsigned int step_fixed = (int)floor(step * FIXPOINT_FRAC_MUL);
				unsigned int outofs = 0;
			
				if (voice->mDelaySamples)
				{
					if (voice->mDelaySamples > aSamplesToRead)
					{
						outofs = aSamplesToRead;
						voice->mDelaySamples -= aSamplesToRead;
					}
					else
					{
						outofs = voice->mDelaySamples;
						voice->mDelaySamples = 0;
					}
					
					// Clear scratch where we're skipping
					unsigned int k;
					for (k = 0; k < voice->mChannels; k++)
					{
						memset(aScratch + k * aBufferSize, 0, sizeof(float) * outofs); 
					}
				}												

				// Scheduled absolute-time stop (scheduleStopAt): limit how
				// many samples this voice may still produce in this pass
				// and stop it once the final partial window is panned.
				unsigned int samplesToProduce = aSamplesToRead;
				bool stopAfterProduce = false;
				if (voice->mStopSamplesLeft >= 0)
				{
					if (voice->mStopSamplesLeft <= (long long)aSamplesToRead)
					{
						samplesToProduce = (unsigned int)voice->mStopSamplesLeft;
						stopAfterProduce = true;
						if (voice->mVolumeFader.mActive != 0 &&
							voice->mVolumeFader.mTo <= 1e-5f)
						{
							voice->mOverallVolume = 0.0f;
							bool hasResidual = false;
							unsigned int ch;
							for (ch = 0; ch < aChannels; ch++)
							{
								if (voice->mCurrentChannelVolume[ch] > 1e-5f)
								{
									hasResidual = true;
									break;
								}
							}
							if (hasResidual && samplesToProduce < 64)
							{
								samplesToProduce = aSamplesToRead < 64 ? aSamplesToRead : 64;
							}
						}
					}
					else
					{
						voice->mStopSamplesLeft -= aSamplesToRead;
					}
				}

				if ((voice->mFlags & AudioSourceInstance::REALTIME_DIRECT) &&
					!(voice->mFlags & AudioSourceInstance::LOOPING) &&
					step_fixed == FIXPOINT_FRAC_MUL &&
					voice->mLeftoverSamples == 0 &&
					voice->mSrcOffset == 0 &&
					(outofs & 3u) == 0 &&
					outofs < samplesToProduce)
				{
					unsigned int directSamples = samplesToProduce - outofs;
					unsigned int readcount = 0;
					if (!voice->hasEnded() || (voice->mFlags & AudioSourceInstance::LOOPING))
					{
						readcount = readSourceSamples_internal(
							voice, aScratch + outofs, directSamples, aBufferSize);
					}
					//REALTIME_DIRECT writes directly into aScratch without
					// mResampleData lookahead, so once the source ends everything it
					// produced is already output in this pass.
					voice->mSourceDrained = (readcount < directSamples) || voice->hasEnded();
					if (readcount < directSamples)
					{
						unsigned int k;
						for (k = 0; k < voice->mChannels; k++)
						{
							memset(aScratch + outofs + readcount + aBufferSize * k,
								0, sizeof(float) * (directSamples - readcount));
						}
					}
					for (j = 0; j < FILTERS_PER_STREAM; j++)
					{
						if (voice->mFilter[j])
						{
							voice->mFilter[j]->filter(
								aScratch + outofs,
								directSamples,
								aBufferSize,
								voice->mChannels,
								voice->mSamplerate,
								mStreamTime);
						}
					}
					outofs = samplesToProduce;
				}

				while (step_fixed != 0 && outofs < samplesToProduce)
				{
					if (voice->mLeftoverSamples == 0)
					{
						// Swap resample buffers (ping-pong)
						float * t = voice->mResampleData[0];
						voice->mResampleData[0] = voice->mResampleData[1];
						voice->mResampleData[1] = t;

						if (voice->mResampleData[0] == NULL)
						{
							break;
						}

						// Get a block of source data

						unsigned int readcount = 0;
						if (!voice->hasEnded() || voice->mFlags & AudioSourceInstance::LOOPING)
						{
							readcount = readSourceSamples_internal(voice,
								voice->mResampleData[0], SAMPLE_GRANULARITY,
								SAMPLE_GRANULARITY);
						}
						// ###### flutter_soloud local patch (voice tail) ######
						// Nothing came back: everything read before this block has
						// been resampled, so the voice may now be auto-stopped.
						voice->mSourceDrained = readcount == 0;

                        // Clear remaining of the resample data if the full scratch wasn't used
						if (readcount < SAMPLE_GRANULARITY)
						{
							unsigned int k;
							for (k = 0; k < voice->mChannels; k++)
								memset(voice->mResampleData[0] + readcount + SAMPLE_GRANULARITY * k, 0, sizeof(float) * (SAMPLE_GRANULARITY - readcount));
						}

						// ###### flutter_soloud local patch (resampler carry) ######
						// We have new block of data, move pointer backwards. The previous
						// block was resampled to its end (see writesamples below), so this
						// carries what is left of the last step into the new block. Only a
						// new or reset voice is still at offset 0 here; upstream cropped
						// every block to 0, which skipped up to one step per block.
						if (voice->mSrcOffset < SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL)
						{
							voice->mSrcOffset = 0;
						}
						else
						{
							voice->mSrcOffset -= SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL;
						}

					
						// Run the per-stream filters to get our source data

						for (j = 0; j < FILTERS_PER_STREAM; j++)
						{
							if (voice->mFilter[j])
							{
								voice->mFilter[j]->filter(
									voice->mResampleData[0],
									SAMPLE_GRANULARITY,
									SAMPLE_GRANULARITY,
									voice->mChannels,
									voice->mSamplerate,
									mStreamTime);
							}
						}
					}
					else
					{
						voice->mLeftoverSamples = 0;
					}

					// Figure out how many samples we can generate from this source data.
					// The value may be zero.

					unsigned int writesamples = 0;

					if (voice->mSrcOffset < SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL)
					{
						// ###### flutter_soloud local patch (resampler carry) ######
						// Every position left in this block, rounded up: the last one is
						// still inside the block, the one after it is at or past its end.
						writesamples = ((SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL) - voice->mSrcOffset + step_fixed - 1) / step_fixed;
					}


					// If this is too much for our output buffer, don't write that many:
					if (writesamples + outofs > samplesToProduce)
					{
						voice->mLeftoverSamples = (writesamples + outofs) - samplesToProduce;
						writesamples = samplesToProduce - outofs;
					}

					// Call resampler to generate the samples, once per channel
					if (writesamples)
					{
						for (j = 0; j < voice->mChannels; j++)
						{
							switch (aResampler)
							{
							case RESAMPLER_POINT:
								resample_point(voice->mResampleData[0] + SAMPLE_GRANULARITY * j,
									voice->mResampleData[1] + SAMPLE_GRANULARITY * j,
									aScratch + aBufferSize * j + outofs,
									voice->mSrcOffset,
									writesamples,
									/*voice->mSamplerate,
									aSamplerate,*/
									step_fixed);
								break;
							case RESAMPLER_CATMULLROM:
								resample_catmullrom(voice->mResampleData[0] + SAMPLE_GRANULARITY * j,
									voice->mResampleData[1] + SAMPLE_GRANULARITY * j,
									aScratch + aBufferSize * j + outofs,
									voice->mSrcOffset,
									writesamples,
									/*voice->mSamplerate,
									aSamplerate,*/
									step_fixed);
								break;
							default:
							//case RESAMPLER_LINEAR:
								resample_linear(voice->mResampleData[0] + SAMPLE_GRANULARITY * j,
									voice->mResampleData[1] + SAMPLE_GRANULARITY * j,
									aScratch + aBufferSize * j + outofs,
									voice->mSrcOffset,
									writesamples,
									/*voice->mSamplerate,
									aSamplerate,*/
									step_fixed);
								break;
							}
						}
					}

					// Keep track of how many samples we've written so far
					outofs += writesamples;

					// Move source pointer onwards (writesamples may be zero)
					voice->mSrcOffset += writesamples * step_fixed;
				}
				
				// If the resampler loop exited early (e.g. no resample buffer),
				// zero the unwritten region of aScratch so panAndExpand never
				// mixes stale scratch memory.
				if (outofs < samplesToProduce)
				{
					unsigned int k;
					for (k = 0; k < voice->mChannels; k++)
					{
						memset(aScratch + k * aBufferSize + outofs, 0,
							sizeof(float) * (samplesToProduce - outofs));
					}
				}

				// Handle panning and channel expansion (and/or shrinking)
				panAndExpand(voice, aBuffer, samplesToProduce, aBufferSize, aScratch, aChannels);

				// stop the voice if a scheduled absolute-time stop cut it
				// in this pass (even if it is looping), otherwise clear it
				// if the sound is over
				if (stopAfterProduce)
				{
					voice->mStopSamplesLeft = -1;
					stopVoice_internal(mActiveVoice[i]);
				}
				// ###### flutter_soloud local patch (voice tail) ######
				// hasEnded() alone turns true when the last block is read, with
				// up to SAMPLE_GRANULARITY source samples of it not yet output.
				// Wait for a read after the end to come back empty, so all of
				// that block is output: the voice outlives its last sample by
				// the silent rest of the block, at most one more pass at the
				// usual buffer sizes.
				else if (!(voice->mFlags & (AudioSourceInstance::LOOPING | AudioSourceInstance::DISABLE_AUTOSTOP)) && voice->hasEnded() && voice->mSourceDrained)
				{
					stopVoice_internal(mActiveVoice[i]);
				}
			}
			else
				if (voice &&
					voice->mBusHandle == aBus &&
					!(voice->mFlags & AudioSourceInstance::PAUSED) &&
					(voice->mFlags & AudioSourceInstance::INAUDIBLE) &&
					(voice->mFlags & AudioSourceInstance::INAUDIBLE_TICK))
			{
				// Inaudible but needs ticking. Do minimal work (keep counters up to date and ask audiosource for data)
				float step = voice->mSamplerate / aSamplerate;
				int step_fixed = (int)floor(step * FIXPOINT_FRAC_MUL);
				unsigned int outofs = 0;

				if (voice->mDelaySamples)
				{
					if (voice->mDelaySamples > aSamplesToRead)
					{
						outofs = aSamplesToRead;
						voice->mDelaySamples -= aSamplesToRead;
					}
					else
					{
						outofs = voice->mDelaySamples;
						voice->mDelaySamples = 0;
					}
				}

				// Scheduled absolute-time stop (scheduleStopAt): ticking
				// voices produce no audible output, so counting down here
				// keeps them in lockstep with the audible mixing path.
				if (voice->mStopSamplesLeft >= 0)
				{
					if (voice->mStopSamplesLeft <= (long long)aSamplesToRead)
					{
						voice->mStopSamplesLeft = -1;
						stopVoice_internal(mActiveVoice[i]);
						continue;
					}
					voice->mStopSamplesLeft -= aSamplesToRead;
				}

				while (step_fixed != 0 && outofs < aSamplesToRead)
				{
					if (voice->mLeftoverSamples == 0)
					{
						// Swap resample buffers (ping-pong)
						float * t = voice->mResampleData[0];
						voice->mResampleData[0] = voice->mResampleData[1];
						voice->mResampleData[1] = t;

						if (voice->mResampleData[0] == NULL)
						{
							break;
						}

						// Get a block of source data

						// ###### flutter_soloud local patch (voice tail) ######
						// Keep the read count, as the audible path does, so a
						// ticking voice is auto-stopped in the same pass.
						unsigned int readcount = 0;
						if (!voice->hasEnded() || voice->mFlags & AudioSourceInstance::LOOPING)
						{
							readcount = readSourceSamples_internal(voice,
								voice->mResampleData[0], SAMPLE_GRANULARITY,
								SAMPLE_GRANULARITY);
						}
						voice->mSourceDrained = readcount == 0;

						// ###### flutter_soloud local patch (resampler carry) ######
						// We have new block of data, move pointer backwards. The previous
						// block was resampled to its end (see writesamples below), so this
						// carries what is left of the last step into the new block. Only a
						// new or reset voice is still at offset 0 here; upstream cropped
						// every block to 0, which skipped up to one step per block.
						if (voice->mSrcOffset < SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL)
						{
							voice->mSrcOffset = 0;
						}
						else
						{
							voice->mSrcOffset -= SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL;
						}

						// Skip filters
					}
					else
					{
						voice->mLeftoverSamples = 0;
					}

					// Figure out how many samples we can generate from this source data.
					// The value may be zero.

					unsigned int writesamples = 0;

					if (voice->mSrcOffset < SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL)
					{
						// ###### flutter_soloud local patch (resampler carry) ######
						// Every position left in this block, rounded up: the last one is
						// still inside the block, the one after it is at or past its end.
						writesamples = ((SAMPLE_GRANULARITY * FIXPOINT_FRAC_MUL) - voice->mSrcOffset + step_fixed - 1) / step_fixed;
					}


					// If this is too much for our output buffer, don't write that many:
					if (writesamples + outofs > aSamplesToRead)
					{
						voice->mLeftoverSamples = (writesamples + outofs) - aSamplesToRead;
						writesamples = aSamplesToRead - outofs;
					}

					// Skip resampler

					// Keep track of how many samples we've written so far
					outofs += writesamples;

					// Move source pointer onwards (writesamples may be zero)
					voice->mSrcOffset += writesamples * step_fixed;
				}

				// clear voice if the sound is over
				// ###### flutter_soloud local patch (voice tail) ######
				// Once a read after the end came back empty, as above.
				if (!(voice->mFlags & (AudioSourceInstance::LOOPING | AudioSourceInstance::DISABLE_AUTOSTOP)) && voice->hasEnded() && voice->mSourceDrained)
				{
					stopVoice_internal(mActiveVoice[i]);
				}
			}
		}
	}

	void Soloud::mapResampleBuffers_internal()
	{
		SOLOUD_ASSERT(mMaxActiveVoices < VOICE_COUNT);
		char live[VOICE_COUNT];
		memset(live, 0, mMaxActiveVoices);
		unsigned int i, j;
		for (i = 0; i < mMaxActiveVoices; i++)
		{
			if (!mResampleDataOwner[i])
				continue;
			for (j = 0; j < mActiveVoiceCount; j++)
			{
				if (mResampleDataOwner[i] == mVoice[mActiveVoice[j]] &&
					mResampleDataOwner[i]->mResampleData[0] != NULL &&
					mResampleDataOwner[i]->mResampleData[1] != NULL)
				{
					live[i] |= 1; // Live channel
					live[j] |= 2; // Live voice
				}
			}
		}

		for (i = 0; i < mMaxActiveVoices; i++)
		{
			if (!(live[i] & 1) && mResampleDataOwner[i]) // For all dead channels with owners..
			{
				mResampleDataOwner[i]->mResampleData[0] = 0;
				mResampleDataOwner[i]->mResampleData[1] = 0;
				mResampleDataOwner[i] = 0;
			}
		}

		int latestfree = 0;
		for (i = 0; i < mActiveVoiceCount; i++)
		{
			if (!(live[i] & 2) && mVoice[mActiveVoice[i]]) // For all live voices with no channel..
			{
				int found = -1;
				for (j = latestfree; found == -1 && j < mMaxActiveVoices; j++)
				{
					if (mResampleDataOwner[j] == 0)
					{
						found = j;
					}
				}
				SOLOUD_ASSERT(found != -1);
				mResampleDataOwner[found] = mVoice[mActiveVoice[i]];
				mResampleDataOwner[found]->mResampleData[0] = mResampleData[found * 2 + 0];
				mResampleDataOwner[found]->mResampleData[1] = mResampleData[found * 2 + 1];
				const unsigned int ownerCh = mResampleDataOwner[found]->mChannels;
				memset(mResampleDataOwner[found]->mResampleData[0], 0, sizeof(float) * SAMPLE_GRANULARITY * ownerCh);
				memset(mResampleDataOwner[found]->mResampleData[1], 0, sizeof(float) * SAMPLE_GRANULARITY * ownerCh);
				latestfree = found + 1;
			}
		}
	}

	void Soloud::calcActiveVoices_internal()
	{
		// TODO: consider whether we need to re-evaluate the active voices all the time.
		// It is a must when new voices are started, but otherwise we could get away
		// with postponing it sometimes..

		mActiveVoiceDirty = false;

		// Populate
		unsigned int i, candidates, mustlive;
		candidates = 0;
		mustlive = 0;
		for (i = 0; i < mHighestVoice; i++)
		{
			if (mVoice[i] && (!(mVoice[i]->mFlags & (AudioSourceInstance::INAUDIBLE | AudioSourceInstance::PAUSED)) 
				|| (mVoice[i]->mFlags & AudioSourceInstance::INAUDIBLE_TICK)))
			{
				mActiveVoice[candidates] = i;
				candidates++;
				if (mVoice[i]->mFlags & AudioSourceInstance::INAUDIBLE_TICK)
				{
					mActiveVoice[candidates - 1] = mActiveVoice[mustlive];
					mActiveVoice[mustlive] = i;
					mustlive++;
				}
			}
		}

		// Check for early out
		if (candidates <= mMaxActiveVoices)
		{
			// everything is audible, early out
			mActiveVoiceCount = candidates;
			mapResampleBuffers_internal();
			return;
		}

		mActiveVoiceCount = mMaxActiveVoices;

		if (mustlive >= mMaxActiveVoices)
		{
			// Oopsie. Well, nothing to sort, since the "must live" voices already
			// ate all our active voice slots.
			// This is a potentially an error situation, but we have no way to report
			// error from here. And asserting could be bad, too.
			return;
		}

		// If we get this far, there's nothing to it: we'll have to sort the voices to find the most audible.

		// Iterative partial quicksort:
		// int left = 0, stack[24], pos = 0, right;
		// int len = candidates - mustlive;
		// unsigned int *data = mActiveVoice + mustlive;
		// int k = mActiveVoiceCount;
		// for (;;) 
		// {                                 
		// 	for (; left + 1 < len; len++) 
		// 	{                
		// 		if (pos == 24) len = stack[pos = 0]; 
		// 		int pivot = data[left];
		// 		float pivotvol = mVoice[pivot]->mOverallVolume;
		// 		stack[pos++] = len;      
		// 		for (right = left - 1;;) 
		// 		{
		// 			do 
		// 			{
		// 				right++;
		// 			} 
		// 			while (mVoice[data[right]]->mOverallVolume > pivotvol);
		// 			do
		// 			{
		// 				len--;
		// 			}
		// 			while (pivotvol > mVoice[data[len]]->mOverallVolume);
		// 			if (right >= len) break;       
		// 			int temp = data[right];
		// 			data[right] = data[len];
		// 			data[len] = temp;
		// 		}                        
		// 	}
		// 	if (pos == 0) break;         
		// 	if (left >= k) break;
		// 	left = len;                  
		// 	len = stack[--pos];          
		// }
		// // https://github.com/jarikomppa/soloud/issues/298
		// Pack original candidate rank into upper 16 bits so std::partial_sort
		// preserves exact stable tie-breaking in O(N log K) with zero heap allocations.
		for (unsigned int k = mustlive; k < candidates; k++)
		{
			mActiveVoice[k] |= ((k - mustlive) << 16);
		}
		auto comp = [this](const unsigned int aPacked, const unsigned int bPacked)
		{
			const unsigned int a = aPacked & 0xffffu;
			const unsigned int b = bPacked & 0xffffu;
			const unsigned int aProt = mVoice[a]->mFlags & AudioSourceInstance::PROTECTED;
			const unsigned int bProt = mVoice[b]->mFlags & AudioSourceInstance::PROTECTED;
			if (aProt != bProt)
				return aProt != 0;
			const float va = mVoice[a]->mOverallVolume;
			const float vb = mVoice[b]->mOverallVolume;
			if (va > vb)
				return true;
			if (vb > va)
				return false;
			return (aPacked >> 16) < (bPacked >> 16);
		};
		std::partial_sort(&mActiveVoice[mustlive], &mActiveVoice[mMaxActiveVoices], &mActiveVoice[candidates], comp);
		for (unsigned int k = mustlive; k < candidates; k++)
		{
			mActiveVoice[k] &= 0xffffu;
		}
		// TODO: should the rest of the voices be flagged INAUDIBLE?
		mapResampleBuffers_internal();
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// The locked section of mix_internal(), split out so the retroactive
	// re-mix loop can drive quanta while holding the audio mutex for the
	// whole rollback (the native audio mutex is not recursive, so it cannot
	// call mix()/mix_internal()). Behavior is identical to the original
	// inlined body.
	void Soloud::mixVoicesLocked_internal(unsigned int aSamples, unsigned int aStride)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		time buffertime = (time)aSamples / (time)mSamplerate;

		// Process faders. May change scratch size.
		int i;
		for (i = 0; i < (signed)mHighestVoice; i++)
		{
			if (mVoice[i] && !(mVoice[i]->mFlags & AudioSourceInstance::PAUSED))
			{
				float volume[2];

				mVoice[i]->mActiveFader = 0;

				if (mGlobalVolumeFader.mActive > 0)
				{
					mVoice[i]->mActiveFader = 1;
				}

				mVoice[i]->mStreamTime += buffertime;
				mVoice[i]->mStreamPosition += (double)buffertime * (double)mVoice[i]->mOverallRelativePlaySpeed;

				// TODO: this is actually unstable, because mStreamTime depends on the relative
				// play speed.
				if (mVoice[i]->mRelativePlaySpeedFader.mActive > 0)
				{
					float speed = mVoice[i]->mRelativePlaySpeedFader.get(mVoice[i]->mStreamTime);
					setVoiceRelativePlaySpeed_internal(i, speed);
				}

				volume[0] = mVoice[i]->mOverallVolume;
				if (mVoice[i]->mVolumeFader.mActive > 0)
				{
					mVoice[i]->mSetVolume = mVoice[i]->mVolumeFader.get(mVoice[i]->mStreamTime);
					mVoice[i]->mActiveFader = 1;
					updateVoiceVolume_internal(i);
					mActiveVoiceDirty = true;
				}
				volume[1] = mVoice[i]->mOverallVolume;

				if (mVoice[i]->mPanFader.mActive > 0)
				{
					float pan = mVoice[i]->mPanFader.get(mVoice[i]->mStreamTime);
					setVoicePan_internal(i, pan);
					mVoice[i]->mActiveFader = 1;
				}

				if (mVoice[i]->mPauseScheduler.mActive)
				{
					mVoice[i]->mPauseScheduler.get(mVoice[i]->mStreamTime);
					if (mVoice[i]->mPauseScheduler.mActive == -1)
					{
						mVoice[i]->mPauseScheduler.mActive = 0;
						setVoicePause_internal(i, 1);
					}
				}

				if (mVoice[i]->mStopScheduler.mActive)
				{
					mVoice[i]->mStopScheduler.get(mVoice[i]->mStreamTime);
					if (mVoice[i]->mStopScheduler.mActive == -1)
					{
						mVoice[i]->mStopScheduler.mActive = 0;
						// If the voice was fading to 0 on the same quantum tick,
						// mVolumeFader just set mSetVolume = 0 above, but
						// mCurrentChannelVolume still holds the previous quantum's
						// end volume until mixBus_internal runs panAndExpand.
						// Stopping before mixBus_internal would drop the final
						// quantum ramp and leave a step discontinuity. Instead,
						// let audible voices with non-zero channel volume ramp to
						// 0 across this quantum and stop at the end of it.
						bool hasResidualVolume = false;
						if (!(mVoice[i]->mFlags & AudioSourceInstance::INAUDIBLE))
						{
							unsigned int ch;
							for (ch = 0; ch < mChannels; ch++)
							{
								if (mVoice[i]->mCurrentChannelVolume[ch] > 1e-5f)
								{
									hasResidualVolume = true;
									break;
								}
							}
						}
						if (hasResidualVolume)
						{
							mVoice[i]->mVolumeFader.mActive = 0;
							setVoiceVolume_internal(i, 0.0f);
							if (mVoice[i]->mStopSamplesLeft < 0 ||
								mVoice[i]->mStopSamplesLeft > (long long)aSamples)
							{
								mVoice[i]->mStopSamplesLeft = aSamples;
							}
						}
						else
						{
							stopVoice_internal(i);
						}
					}
				}

				// Scheduled absolute-time stop (scheduleStopAt) for voices
				// that never reach the mixing pass (inaudible without
				// ticking): count down here and stop when it expires.
				// Audible and ticking voices count down in mixBus_internal,
				// where the stop is applied with sample accuracy.
				if (mVoice[i] && mVoice[i]->mStopSamplesLeft >= 0 &&
					(mVoice[i]->mFlags & AudioSourceInstance::INAUDIBLE) &&
					!(mVoice[i]->mFlags & AudioSourceInstance::INAUDIBLE_TICK))
				{
					mVoice[i]->mStopSamplesLeft -= aSamples;
					if (mVoice[i]->mStopSamplesLeft <= 0)
					{
						mVoice[i]->mStopSamplesLeft = -1;
						stopVoice_internal(i);
					}
				}
			}
		}

		if (mActiveVoiceDirty)
			calcActiveVoices_internal();

		// ###### flutter_soloud local patch (voice tail) ######
		// mixBus_internal stops a voice that read its last block only once a
		// later read comes back empty, and only voices that are mixed read.
		// One that is not mixed now (inaudible without ticking, or beyond the
		// active voice count) would keep the unplayed rest of that block and
		// stay alive, then play it late if it is ever mixed again. Nothing of
		// it can be heard now, so stop the voice, as the mixer did before.
		{
			unsigned char mixed[VOICE_COUNT];
			memset(mixed, 0, mHighestVoice);
			for (i = 0; i < (signed)mActiveVoiceCount; i++)
				mixed[mActiveVoice[i]] = 1;
			for (i = 0; i < (signed)mHighestVoice; i++)
			{
				AudioSourceInstance *voice = mVoice[i];
				if (voice && !mixed[i] &&
					!(voice->mFlags & (AudioSourceInstance::PAUSED | AudioSourceInstance::LOOPING | AudioSourceInstance::DISABLE_AUTOSTOP)) &&
					voice->hasEnded())
				{
					stopVoice_internal(i);
				}
			}
		}

		mixBus_internal(mOutputScratch.mData, aSamples, aStride, mScratch.mData, 0, (float)mSamplerate, mChannels, mResampler);

		// When mixing into mRenderRing (or re-mixing a rolled-back window),
		// global filters are applied post-ring at the device playhead in
		// postProcessRenderRing_internal() so stateful DSP (PitchShift,
		// Freeverb, Echo, VST3, etc.) never processes rolled-back audio twice.
		if (!mMixingToRenderRing && !mRemixing)
		{
			for (i = 0; i < FILTERS_PER_STREAM; i++)
			{
				if (mFilterInstance[i])
				{
					mFilterInstance[i]->filter(mOutputScratch.mData, aSamples, aStride, mChannels, (float)mSamplerate, mStreamTime);
				}
			}
		}

		// ###### flutter_soloud local patch (mix checkpoints) ######
		// Snapshot the mixer state at this quantum boundary so a later
		// retroactive event can roll the engine back here and re-mix
		// bit-identically. Runs under the audio mutex; the single `if` is the
		// only cost when the render-ahead ring is disabled. Skipped during a
		// re-mix: the re-mix reproduces history, and the checkpoints of the
		// rewritten window stay valid (the journal re-applies the events they
		// predate).
		if (isRenderAheadEnabled() && mRenderRing.isInited() && !mRemixing)
			captureMixCheckpoint_internal();
	}

	void Soloud::mix_internal(unsigned int aSamples, unsigned int aStride)
	{
#ifdef FLOATING_POINT_DEBUG
		// This needs to be done in the audio thread as well..
		static int done = 0;
		if (!done)
		{
			unsigned int u;
			u = _controlfp(0, 0);
			u = u & ~(_EM_INVALID | /*_EM_DENORMAL |*/ _EM_ZERODIVIDE | _EM_OVERFLOW /*| _EM_UNDERFLOW  | _EM_INEXACT*/);
			_controlfp(u, _MCW_EM);
			done = 1;
		}
#endif

		soloud_simd::ScopedFlushDenormals scopedFtz((mFlags & NO_FPU_REGISTER_CHANGE) == 0);

		time buffertime = (time)aSamples / (time)mSamplerate;
		float globalVolume[2];

		// Advance the mix clock under the same lock as the mix itself.
		// playScheduled() computes a voice's delay from mStreamTime under this
		// lock; if the clock advanced before the lock was taken, a voice
		// scheduled in between got its delay from the next buffer while this
		// buffer still counted it down, and started one buffer early.
		lockAudioMutex_internal();
		mStreamTime += buffertime;

		globalVolume[0] = mGlobalVolume;
		if (mGlobalVolumeFader.mActive)
		{
			mGlobalVolume = mGlobalVolumeFader.get(mStreamTime);
		}
		globalVolume[1] = mGlobalVolume;

		mixVoicesLocked_internal(aSamples, aStride);

		const bool notifyVoiceInactive = mVoiceInactiveCallbackPending;
		mVoiceInactiveCallbackPending = false;
		unlockAudioMutex_internal();

		auto voiceInactiveCallback =
			_voiceInactiveCallback.load(std::memory_order_acquire);
		if (notifyVoiceInactive && voiceInactiveCallback != nullptr)
			voiceInactiveCallback();
		
		// Note: clipping channels*aStride, not channels*aSamples, so we're possibly clipping some unused data.
		// The buffers should be large enough for it, we just may do a few bytes of unneccessary work.
		clip_internal(mOutputScratch, mScratch, aStride, globalVolume[0], globalVolume[1]);

		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// Skip visualization during re-mix quanta: the rewritten window would
		// momentarily show up twice (plan §4.6).
		if ((mFlags & ENABLE_VISUALIZATION) && !mRemixing)
		{
			for (int i = 0; i < MAX_CHANNELS; i++)
			{
				mVisualizationChannelVolume[i] = 0;
			}
			if (aSamples > 255)
			{
				if (mChannels == 0)
				{
					memset(mVisualizationWaveData, 0, sizeof(float) * 256);
				}
				else
				{
					const float *ch0 = mScratch.mData;
					mVisualizationChannelVolume[0] = peakAbsF32(ch0, 256);
					memcpy(mVisualizationWaveData, ch0, sizeof(float) * 256);
					for (unsigned int j = 1; j < mChannels; j++)
					{
						const float *chPtr = mScratch.mData + j * aStride;
						mVisualizationChannelVolume[j] = peakAbsF32(chPtr, 256);
						for (unsigned int i = 0; i < 256; i += 4)
						{
							soloud_simd::add(
								soloud_simd::f32x4::loadu(mVisualizationWaveData + i),
								soloud_simd::f32x4::loadu(chPtr + i))
								.storeu(mVisualizationWaveData + i);
						}
					}
				}
			}
			else if (aSamples > 0)
			{
				// Very unlikely failsafe branch
				for (int i = 0; i < 256; i++)
				{
					mVisualizationWaveData[i] = 0;
					for (int j = 0; j < (signed)mChannels; j++)
					{
						float sample = mScratch.mData[(i % aSamples) + j * aStride];
						float absvol = fabsf(sample);
						if (mVisualizationChannelVolume[j] < absvol)
							mVisualizationChannelVolume[j] = absvol;
						mVisualizationWaveData[i] += sample;
					}
				}
			}
			else
			{
				memset(mVisualizationWaveData, 0, sizeof(float) * 256);
			}
		}
	}

	void Soloud::mix(float *aBuffer, unsigned int aSamples)
	{
		unsigned int stride = (aSamples + 15) & ~0xf;
		mix_internal(aSamples, stride);
		interlace_samples_float(mScratch.mData, aBuffer, aSamples, mChannels, stride);
	}

	void Soloud::mixSigned16(short *aBuffer, unsigned int aSamples)
	{
		unsigned int stride = (aSamples + 15) & ~0xf;
		mix_internal(aSamples, stride);
		interlace_samples_s16(mScratch.mData, aBuffer, aSamples, mChannels, stride);
	}

	// ###### flutter_soloud local patch (retroactive re-mix) ######
	// One full mix quantum for the retroactive re-mix loop. Identical to
	// mix() except the caller already holds the audio mutex for the whole
	// rollback (the native audio mutex is not recursive, so mix() cannot be
	// used here), and the mRemixing flag suppresses visualization: the re-mix
	// reproduces history, it does not create new events. Checkpoints ARE
	// re-captured (captureMixCheckpoint_internal derives the ring position
	// from the mix clock under mRemixing): the re-captured slots incorporate
	// the retroactive event and the replayed journal, keeping the checkpoint
	// chain consistent with the rewritten timeline for the next rollback.
	// Ended voices stopVoice_internal() queues during the re-mix stay
	// queued until the caller's final unlock, so the dispatch never releases
	// the mutex mid-rollback.
	void Soloud::remixQuantum_internal(float *aBuffer, unsigned int aSamples)
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		SOLOUD_ASSERT(mRemixing);
		unsigned int stride = (aSamples + 15) & ~0xf;
		time buffertime = (time)aSamples / (time)mSamplerate;
		float globalVolume[2];
		mStreamTime += buffertime;

		globalVolume[0] = mGlobalVolume;
		if (mGlobalVolumeFader.mActive)
		{
			mGlobalVolume = mGlobalVolumeFader.get(mStreamTime);
		}
		globalVolume[1] = mGlobalVolume;

		mixVoicesLocked_internal(aSamples, stride);

		// Global volume only, same as renderRingTopUp_internal(): the ring
		// holds the unclipped mix and postProcessRenderRing_internal() clips.
		if (globalVolume[0] == 1.0f && globalVolume[1] == 1.0f)
		{
			interlace_samples_float(mOutputScratch.mData, aBuffer, aSamples, mChannels, stride);
		}
		else
		{
			applyGlobalVolume_internal(mOutputScratch, mScratch, stride, globalVolume[0], globalVolume[1]);
			interlace_samples_float(mScratch.mData, aBuffer, aSamples, mChannels, stride);
		}

		// Re-capture the checkpoint for this boundary (see the comment above).
		captureMixCheckpoint_internal();
	}

	void interlace_samples_float(const float *aSourceBuffer, float *aDestBuffer, unsigned int aSamples, unsigned int aChannels, unsigned int aStride)
	{
		// 111222 -> 121212
		unsigned int i, j, c;
		if (aChannels == 1)
		{
			memcpy(aDestBuffer, aSourceBuffer, sizeof(float) * aSamples);
			return;
		}
		if (aChannels == 2)
		{
			const float *srcL = aSourceBuffer;
			const float *srcR = aSourceBuffer + aStride;
			i = 0;
#if defined(SOLOUD_SSE_INTRINSICS)
			for (; i + 4 <= aSamples; i += 4)
			{
				__m128 l = _mm_loadu_ps(srcL + i);
				__m128 r = _mm_loadu_ps(srcR + i);
				_mm_storeu_ps(aDestBuffer + i * 2, _mm_unpacklo_ps(l, r));
				_mm_storeu_ps(aDestBuffer + i * 2 + 4, _mm_unpackhi_ps(l, r));
			}
#elif defined(SOLOUD_NEON_INTRINSICS)
			for (; i + 4 <= aSamples; i += 4)
			{
				float32x4x2_t lr;
				lr.val[0] = vld1q_f32(srcL + i);
				lr.val[1] = vld1q_f32(srcR + i);
				vst2q_f32(aDestBuffer + i * 2, lr);
			}
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
			for (; i + 4 <= aSamples; i += 4)
			{
				v128_t l = wasm_v128_load(srcL + i);
				v128_t r = wasm_v128_load(srcR + i);
				wasm_v128_store(aDestBuffer + i * 2, wasm_i32x4_shuffle(l, r, 0, 4, 1, 5));
				wasm_v128_store(aDestBuffer + i * 2 + 4, wasm_i32x4_shuffle(l, r, 2, 6, 3, 7));
			}
#endif
			for (; i < aSamples; i++)
			{
				aDestBuffer[i * 2] = srcL[i];
				aDestBuffer[i * 2 + 1] = srcR[i];
			}
			return;
		}
		if (aChannels == 4)
		{
			const float *c0 = aSourceBuffer;
			const float *c1 = aSourceBuffer + aStride;
			const float *c2 = aSourceBuffer + aStride * 2;
			const float *c3 = aSourceBuffer + aStride * 3;
			i = 0;
#if defined(SOLOUD_SSE_INTRINSICS)
			for (; i + 4 <= aSamples; i += 4)
			{
				__m128 r0 = _mm_loadu_ps(c0 + i);
				__m128 r1 = _mm_loadu_ps(c1 + i);
				__m128 r2 = _mm_loadu_ps(c2 + i);
				__m128 r3 = _mm_loadu_ps(c3 + i);
				_MM_TRANSPOSE4_PS(r0, r1, r2, r3);
				_mm_storeu_ps(aDestBuffer + i * 4 + 0, r0);
				_mm_storeu_ps(aDestBuffer + i * 4 + 4, r1);
				_mm_storeu_ps(aDestBuffer + i * 4 + 8, r2);
				_mm_storeu_ps(aDestBuffer + i * 4 + 12, r3);
			}
#elif defined(SOLOUD_NEON_INTRINSICS)
			for (; i + 4 <= aSamples; i += 4)
			{
				float32x4x4_t q;
				q.val[0] = vld1q_f32(c0 + i);
				q.val[1] = vld1q_f32(c1 + i);
				q.val[2] = vld1q_f32(c2 + i);
				q.val[3] = vld1q_f32(c3 + i);
				vst4q_f32(aDestBuffer + i * 4, q);
			}
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
			for (; i + 4 <= aSamples; i += 4)
			{
				v128_t r0 = wasm_v128_load(c0 + i);
				v128_t r1 = wasm_v128_load(c1 + i);
				v128_t r2 = wasm_v128_load(c2 + i);
				v128_t r3 = wasm_v128_load(c3 + i);
				v128_t t0 = wasm_i32x4_shuffle(r0, r1, 0, 4, 1, 5);
				v128_t t1 = wasm_i32x4_shuffle(r2, r3, 0, 4, 1, 5);
				v128_t t2 = wasm_i32x4_shuffle(r0, r1, 2, 6, 3, 7);
				v128_t t3 = wasm_i32x4_shuffle(r2, r3, 2, 6, 3, 7);
				wasm_v128_store(aDestBuffer + i * 4 + 0, wasm_i32x4_shuffle(t0, t1, 0, 1, 4, 5));
				wasm_v128_store(aDestBuffer + i * 4 + 4, wasm_i32x4_shuffle(t0, t1, 2, 3, 6, 7));
				wasm_v128_store(aDestBuffer + i * 4 + 8, wasm_i32x4_shuffle(t2, t3, 0, 1, 4, 5));
				wasm_v128_store(aDestBuffer + i * 4 + 12, wasm_i32x4_shuffle(t2, t3, 2, 3, 6, 7));
			}
#endif
			for (; i < aSamples; i++)
			{
				aDestBuffer[i * 4 + 0] = c0[i];
				aDestBuffer[i * 4 + 1] = c1[i];
				aDestBuffer[i * 4 + 2] = c2[i];
				aDestBuffer[i * 4 + 3] = c3[i];
			}
			return;
		}
		c = 0;
		for (j = 0; j < aChannels; j++)
		{
			c = j * aStride;
			for (i = j; i < aSamples * aChannels; i += aChannels)
			{
				aDestBuffer[i] = aSourceBuffer[c];
				c++;
			}
		}
	}

	void interlace_samples_s16(const float *aSourceBuffer, short *aDestBuffer, unsigned int aSamples, unsigned int aChannels, unsigned int aStride)
	{
		// 111222 -> 121212
		unsigned int i = 0, j, c;
		if (aChannels == 1)
		{
#if defined(SOLOUD_SSE_INTRINSICS)
			const __m128 scale = _mm_set1_ps(32767.0f);
			for (; i + 8 <= aSamples; i += 8)
			{
				__m128i lo = _mm_cvttps_epi32(_mm_mul_ps(_mm_loadu_ps(aSourceBuffer + i), scale));
				__m128i hi = _mm_cvttps_epi32(_mm_mul_ps(_mm_loadu_ps(aSourceBuffer + i + 4), scale));
				_mm_storeu_si128(reinterpret_cast<__m128i *>(aDestBuffer + i), _mm_packs_epi32(lo, hi));
			}
#elif defined(SOLOUD_NEON_INTRINSICS)
			const float32x4_t scale = vdupq_n_f32(32767.0f);
			for (; i + 8 <= aSamples; i += 8)
			{
				int16x4_t lo = vqmovn_s32(vcvtq_s32_f32(vmulq_f32(vld1q_f32(aSourceBuffer + i), scale)));
				int16x4_t hi = vqmovn_s32(vcvtq_s32_f32(vmulq_f32(vld1q_f32(aSourceBuffer + i + 4), scale)));
				vst1q_s16(reinterpret_cast<int16_t *>(aDestBuffer + i), vcombine_s16(lo, hi));
			}
#endif
			for (; i < aSamples; i++)
			{
				aDestBuffer[i] = (short)(aSourceBuffer[i] * 0x7fff);
			}
			return;
		}
		if (aChannels == 2)
		{
			const float *srcL = aSourceBuffer;
			const float *srcR = aSourceBuffer + aStride;
#if defined(SOLOUD_SSE_INTRINSICS)
			const __m128 scale = _mm_set1_ps(32767.0f);
			for (; i + 4 <= aSamples; i += 4)
			{
				__m128 l = _mm_loadu_ps(srcL + i);
				__m128 r = _mm_loadu_ps(srcR + i);
				__m128i lo = _mm_cvttps_epi32(_mm_mul_ps(_mm_unpacklo_ps(l, r), scale));
				__m128i hi = _mm_cvttps_epi32(_mm_mul_ps(_mm_unpackhi_ps(l, r), scale));
				_mm_storeu_si128(reinterpret_cast<__m128i *>(aDestBuffer + i * 2), _mm_packs_epi32(lo, hi));
			}
#elif defined(SOLOUD_NEON_INTRINSICS)
			const float32x4_t scale = vdupq_n_f32(32767.0f);
			for (; i + 4 <= aSamples; i += 4)
			{
				int16x4x2_t lr16;
				lr16.val[0] = vqmovn_s32(vcvtq_s32_f32(vmulq_f32(vld1q_f32(srcL + i), scale)));
				lr16.val[1] = vqmovn_s32(vcvtq_s32_f32(vmulq_f32(vld1q_f32(srcR + i), scale)));
				vst2_s16(reinterpret_cast<int16_t *>(aDestBuffer + i * 2), lr16);
			}
#endif
			for (; i < aSamples; i++)
			{
				aDestBuffer[i * 2] = (short)(srcL[i] * 0x7fff);
				aDestBuffer[i * 2 + 1] = (short)(srcR[i] * 0x7fff);
			}
			return;
		}
		c = 0;
		for (j = 0; j < aChannels; j++)
		{
			c = j * aStride;
			for (i = j; i < aSamples * aChannels; i += aChannels)
			{
				aDestBuffer[i] = (short)(aSourceBuffer[c] * 0x7fff);
				c++;
			}
		}
	}

	void Soloud::lockAudioMutex_internal()
	{
		if (mAudioThreadMutex)
		{
			Thread::lockMutex(mAudioThreadMutex);
		}
		SOLOUD_ASSERT(!mInsideAudioThreadMutex);
		mInsideAudioThreadMutex = true;
	}

	void Soloud::unlockAudioMutex_internal()
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);

		if (mEndedVoiceCount != 0 && !mDispatchingEndedVoices)
		{
			unlockAudioMutexAndDispatchEndedVoices_internal();
			return;
		}

#ifdef __EMSCRIPTEN__
		// Free voice instances whose deletion was deferred because
		// stopVoice_internal() ran on the AudioWorklet rendering thread (see
		// mPendingVoiceFree). Only the main browser thread may take the heap
		// lock safely: on the worklet a contended lock lowers to a futex
		// wait, which Emscripten aborts on.
		if (mPendingVoiceFreeCount != 0 && emscripten_is_main_browser_thread())
		{
			unsigned int i;
			for (i = 0; i < mPendingVoiceFreeCount; i++)
				delete mPendingVoiceFree[i];
			mPendingVoiceFreeCount = 0;
		}
#endif

		mInsideAudioThreadMutex = false;
		if (mAudioThreadMutex)
		{
			Thread::unlockMutex(mAudioThreadMutex);
		}
	}

	// Release the audio mutex, then notify the embedder about voices that ended
	// while it was held. stopVoice_internal() cannot call out directly because it
	// runs under the mutex, and an embedder callback that takes its own locks
	// there inverts the lock order (and one that stalls or crashes strands the
	// mutex), wedging every later SoLoud call, teardown included.
	//
	// The pending handles are copied out and the queue cleared *before* the
	// unlock, so another thread that acquires the mutex and queues more work
	// cannot corrupt this dispatch or have its own work consumed here.
	//
	// Voices that end while the batch is being dispatched -- a callback stopping
	// another voice, or another thread stopping one meanwhile -- are picked up by
	// the loop below instead of by a nested dispatch, which keeps delivery in
	// queue order and keeps this frame's snapshot buffer the only one on the
	// stack no matter how many times the embedder re-enters.
	void Soloud::unlockAudioMutexAndDispatchEndedVoices_internal()
	{
		SOLOUD_ASSERT(mInsideAudioThreadMutex);
		SOLOUD_ASSERT(!mDispatchingEndedVoices);

		unsigned int endedVoices[VOICE_COUNT];
		unsigned int endedVoiceCount;
		unsigned int i;

		// Claim the drain. Read and written under the mutex only, so a thread
		// that queues a handle while this one dispatches either sees the guard
		// still set (and leaves its handle for the loop below, which has not yet
		// made its final empty-queue check) or sees it cleared (and dispatches
		// the handle itself). Either way the handle is never stranded.
		mDispatchingEndedVoices = true;

		for (;;)
		{
			endedVoiceCount = mEndedVoiceCount;
			if (endedVoiceCount > VOICE_COUNT)
				endedVoiceCount = VOICE_COUNT;
			for (i = 0; i < endedVoiceCount; i++)
				endedVoices[i] = mEndedVoiceQueue[i];
			mEndedVoiceCount = 0;

			mInsideAudioThreadMutex = false;
			if (mAudioThreadMutex)
			{
				Thread::unlockMutex(mAudioThreadMutex);
			}

			for (i = 0; i < endedVoiceCount; i++)
			{
				// Re-read for every handle: teardown on another thread, or the
				// callback unregistering itself, must suppress the rest of the
				// batch rather than keep calling a pointer the embedder has
				// already retired.
				auto voiceEndedCallback =
					_voiceEndedCallback.load(std::memory_order_acquire);
				if (voiceEndedCallback == nullptr)
					break;

				unsigned int handle = endedVoices[i];
				voiceEndedCallback(&handle);
			}

			// Re-acquire to look for handles queued during the dispatch. No
			// embedder lock is held here -- the callbacks have all returned --
			// so this cannot reintroduce the lock-order inversion.
			if (mAudioThreadMutex)
			{
				Thread::lockMutex(mAudioThreadMutex);
			}
			SOLOUD_ASSERT(!mInsideAudioThreadMutex);
			mInsideAudioThreadMutex = true;

			if (mEndedVoiceCount == 0)
				break;
		}

		mDispatchingEndedVoices = false;
		mInsideAudioThreadMutex = false;
		if (mAudioThreadMutex)
		{
			Thread::unlockMutex(mAudioThreadMutex);
		}
	}


};

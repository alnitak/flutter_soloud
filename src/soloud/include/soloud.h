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

#ifndef SOLOUD_H
#define SOLOUD_H

#include <atomic>
#include <stdlib.h> // rand
#include <math.h> // sin
#include <atomic> // std::atomic

#ifdef SOLOUD_NO_ASSERTS
#define SOLOUD_ASSERT(x)
#else
#ifdef _MSC_VER
#include <stdio.h> // for sprintf in asserts
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // only needed for OutputDebugStringA, should be solved somehow.
#define SOLOUD_ASSERT(x) if (!(x)) { char temp[200]; sprintf(temp, "%s(%d): assert(%s) failed.\n", __FILE__, __LINE__, #x); OutputDebugStringA(temp); __debugbreak(); }
#else
#include <assert.h> // assert
#define SOLOUD_ASSERT(x) assert(x)
#endif
#endif

#ifdef WITH_SDL
#undef WITH_SDL2
#undef WITH_SDL1
#define WITH_SDL1
#define WITH_SDL2
#endif

#ifdef WITH_SDL_STATIC
#undef WITH_SDL1_STATIC
#define WITH_SDL1_STATIC
#endif

#ifndef M_PI
#define M_PI 3.14159265359
#endif

#if defined(_WIN32)||defined(_WIN64)
#define WINDOWS_VERSION
#endif

#if !defined(DISABLE_SIMD)
#if defined(__x86_64__) || defined( _M_X64 ) || defined( __i386 ) || defined( _M_IX86 )
#define SOLOUD_SSE_INTRINSICS
#endif
#endif

#define SOLOUD_VERSION 202002

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
// Configuration defines

// Maximum number of filters per stream
#define FILTERS_PER_STREAM 8

// Number of samples to process on one go
#define SAMPLE_GRANULARITY 512

// Maximum number of concurrent voices (hard limit is 4095)
#define VOICE_COUNT 1024

// 1)mono, 2)stereo 4)quad 6)5.1 8)7.1
#define MAX_CHANNELS 8

// Default resampler for both main and bus mixers
#define SOLOUD_DEFAULT_RESAMPLER SoLoud::Soloud::RESAMPLER_LINEAR

//
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

// Typedefs have to be made before the includes, as the
// includes depend on them.
namespace SoLoud
{
	class Soloud;
	typedef void (*mutexCallFunction)(void *aMutexPtr);
	typedef void (*soloudCallFunction)(Soloud *aSoloud);
	typedef unsigned int result;
	typedef result (*soloudResultFunction)(Soloud *aSoloud);
	typedef unsigned int handle;
	typedef double time;
};

namespace SoLoud
{
	// Class that handles aligned allocations to support vectorized operations
	class AlignedFloatBuffer
	{
	public:
		float *mData; // aligned pointer
		unsigned char *mBasePtr; // raw allocated pointer (for delete)
		int mFloats; // size of buffer (w/out padding)

		// ctor
		AlignedFloatBuffer();
		// Allocate and align buffer
		result init(unsigned int aFloats);
		// Clear data to zero.
		void clear();
		// dtor
		~AlignedFloatBuffer();
	};

	// Lightweight class that handles small aligned buffer to support vectorized operations
	class TinyAlignedFloatBuffer
	{
	public:
		float *mData; // aligned pointer
		unsigned char mActualData[sizeof(float) * 16 + 16];

		// ctor
		TinyAlignedFloatBuffer();
	};
};

#include "soloud_filter.h"
#include "soloud_fader.h"
#include "soloud_audiosource.h"
#include "soloud_bus.h"
#include "soloud_queue.h"
#include "soloud_error.h"
#include "soloud_render_ring.h"
#include "soloud_checkpoint.h"
namespace SoLoud
{

	// Soloud core class.
	class Soloud
	{
	public:
		// Back-end data; content is up to the back-end implementation.
		void * mBackendData;
		// Pointer for the audio thread mutex.
		void * mAudioThreadMutex;
		// Set only when the engine is fully initialized (end of init()) and
		// cleared at the start of deinit(). The audio callback checks it
		// before mixing: on the web, a stale AudioWorklet from a previous
		// engine session can still fire while the global miniaudio device is
		// being re-initialized, and mixing would touch half-initialized or
		// torn-down engine state (see soloud_miniaudio_audiomixer).
		volatile unsigned int mEngineReady = 0;
		// Flag for when we're inside the mutex, used for debugging.
		bool mInsideAudioThreadMutex;
		// Called by SoLoud to shut down the back-end. If NULL, not called. Should be set by back-end.
		soloudCallFunction mBackendCleanupFunc;

		// Some backends like CoreAudio on iOS must be paused/resumed in some cases. On incoming call as instance.
		soloudResultFunction mBackendPauseFunc;
		soloudResultFunction mBackendResumeFunc;

		// Set the callback to call when a voice is ended/stopped.
		//
		// stopVoice_internal() runs with the audio mutex held, so it must not
		// call out to the embedder directly. The callback reaches back into the
		// embedder's own bookkeeping (and its locks), which inverts the lock
		// order against callers that hold those locks across a SoLoud call and
		// deadlocks the engine; and a callback that crashes, stalls or blocks
		// (for example a Dart NativeCallable whose isolate has gone away) would
		// strand the audio mutex and wedge every later SoLoud call, including
		// deinit(). Ended voices are queued instead and dispatched by
		// unlockAudioMutex_internal() once the mutex is released.
		std::atomic<void (*)(unsigned int*)> _voiceEndedCallback{nullptr};
		void setVoiceEndedCallback(void (*voiceEndedCallback)(unsigned int*)) {
			_voiceEndedCallback.store(voiceEndedCallback,
				std::memory_order_release);
		}

		// Called after a mix cycle in which a voice stopped or became paused.
		// The callback runs after the audio mutex has been released.
		std::atomic<void (*)()> _voiceInactiveCallback{nullptr};
		bool mVoiceInactiveCallbackPending = false;
		void setVoiceInactiveCallback(void (*voiceInactiveCallback)()) {
			_voiceInactiveCallback.store(voiceInactiveCallback,
				std::memory_order_release);
		}

		// Handles of voices that ended while the audio mutex was held, pending
		// dispatch. All three members are only touched with the audio mutex held.
		unsigned int mEndedVoiceQueue[VOICE_COUNT];
		unsigned int mEndedVoiceCount = 0;
		// True while a thread is draining mEndedVoiceQueue. A callback that
		// stops another voice re-enters SoLoud and would otherwise start a
		// nested dispatch from inside the current one, delivering the newer
		// handle ahead of the rest of the batch and stacking another
		// VOICE_COUNT-sized snapshot per level. With the guard set, the nested
		// unlock just leaves its handle queued for the running drain loop.
		bool mDispatchingEndedVoices = false;

#ifdef __EMSCRIPTEN__
		// Voice instances whose deletion was deferred because
		// stopVoice_internal() ran on the AudioWorklet rendering thread (of
		// the multi-threaded build). Freeing there would take the heap lock,
		// and a contended lock on that thread lowers to a futex wait, which
		// Emscripten aborts on (futex waits are illegal on AudioWorklet
		// threads). Drained by unlockAudioMutex_internal() on the main
		// browser thread. Only touched with the audio mutex held.
		AudioSourceInstance* mPendingVoiceFree[VOICE_COUNT];
		unsigned int mPendingVoiceFreeCount = 0;
#endif

		// Set the callback to call when the device receive a state changed.
		//
		// Atomic like the other cross-thread callbacks: miniaudio dispatches
		// notifications from backend/platform threads while teardown clears
		// this from the calling thread, and the embedder's device scheduler
		// publishes its own events through it.
		std::atomic<void (*)(unsigned int)> _stateChangedCallback{nullptr};
		void setStateChangedCallback(void (*stateChangedCallback)(unsigned int)) {
			_stateChangedCallback.store(stateChangedCallback,
				std::memory_order_release);
		}

		// Snapshot once and dispatch. Centralized so no call site can
		// reintroduce a check-then-load: with two separate reads, a teardown
		// landing between them turns a non-null check into a null call.
		void notifyStateChanged(unsigned int aState) {
			auto stateChangedCallback =
				_stateChangedCallback.load(std::memory_order_acquire);
			if (stateChangedCallback != nullptr)
				stateChangedCallback(aState);
		}

		// Device-interruption callback used by the embedding lifecycle owner.
		// The context is published before the callback and cleared afterward so
		// notification threads never call through a non-null callback with a
		// partially registered context.
		std::atomic<void (*)(void *, bool)> _audioInterruptionCallback{nullptr};
		std::atomic<void *> _audioInterruptionContext{nullptr};
		void setAudioInterruptionCallback(
			void (*audioInterruptionCallback)(void *, bool), void *context) {
			if (audioInterruptionCallback == nullptr) {
				_audioInterruptionCallback.store(nullptr, std::memory_order_release);
				_audioInterruptionContext.store(nullptr, std::memory_order_release);
				return;
			}
			_audioInterruptionContext.store(context, std::memory_order_release);
			_audioInterruptionCallback.store(
				audioInterruptionCallback, std::memory_order_release);
		}

		// CTor
		Soloud();
		// DTor
		~Soloud();

		enum BACKENDS
		{
			AUTO = 0,
			SDL1,
			SDL2,
			PORTAUDIO,
			WINMM,
			XAUDIO2,
			WASAPI,
			ALSA,
			JACK,
			OSS,
			OPENAL,
			COREAUDIO,
			OPENSLES,
			VITA_HOMEBREW,
			MINIAUDIO,
			NOSOUND,
			NULLDRIVER,
			BACKEND_MAX,
		};

		enum FLAGS
		{
			// Use round-off clipper
			CLIP_ROUNDOFF = 1,
			ENABLE_VISUALIZATION = 2,
			LEFT_HANDED_3D = 4,
			NO_FPU_REGISTER_CHANGE = 8
		};

		enum WAVEFORM
		{
			WAVE_SQUARE = 0,
			WAVE_SAW,
			WAVE_SIN,
			WAVE_TRIANGLE,
			WAVE_BOUNCE,
			WAVE_JAWS,
			WAVE_HUMPS,
			WAVE_FSQUARE,
			WAVE_FSAW
		};

		enum RESAMPLER
		{
			RESAMPLER_POINT,
			RESAMPLER_LINEAR,
			RESAMPLER_CATMULLROM
		};

		// Initialize SoLoud. Must be called before SoLoud can be used.
		result init(unsigned int aFlags = Soloud::CLIP_ROUNDOFF, unsigned int aBackend = Soloud::AUTO, unsigned int aSamplerate = Soloud::AUTO, unsigned int aBufferSize = Soloud::AUTO, unsigned int aChannels = 2, void *pPlaybackInfos_id = nullptr);

		// Change output device.
		// Added by Marco Bavagnoli
		result miniaudio_changeDevice(void *pPlaybackInfos_id);

		result pause();
		result resume();

		// Deinitialize SoLoud. Must be called before shutting down.
		void deinit();

		// Query SoLoud version number (should equal to SOLOUD_VERSION macro)
		unsigned int getVersion() const;

		// Translate error number to an asciiz string
		const char * getErrorString(result aErrorCode) const;

		// Returns current backend ID (BACKENDS enum)
		unsigned int getBackendId();
		// Returns current backend string. May be NULL.
		const char * getBackendString();
		// Returns current backend channel count (1 mono, 2 stereo, etc)
		unsigned int getBackendChannels();
		// Returns current backend sample rate
		unsigned int getBackendSamplerate();
		// Returns current backend buffer size
		unsigned int getBackendBufferSize();

		// Set speaker position in 3d space
		result setSpeakerPosition(unsigned int aChannel, float aX, float aY, float aZ);
		// Get speaker position in 3d space
		result getSpeakerPosition(unsigned int aChannel, float &aX, float &aY, float &aZ);

		// Start playing a sound. Returns voice handle, which can be ignored or used to alter the playing sound's parameters. Negative volume means to use default.
		handle play(AudioSource &aSound, float aVolume = -1.0f, float aPan = 0.0f, bool aPaused = 0, unsigned int aBus = 0);
		// Start playing a sound delayed in relation to other sounds called via this function. Negative volume means to use default.
		handle playClocked(time aSoundTime, AudioSource &aSound, float aVolume = -1.0f, float aPan = 0.0f, unsigned int aBus = 0);
		// Start playing a 3d audio source
		handle play3d(AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX = 0.0f, float aVelY = 0.0f, float aVelZ = 0.0f, float aVolume = 1.0f, bool aPaused = 0, unsigned int aBus = 0);
		// Start playing a 3d audio source, delayed in relation to other sounds called via this function.
		handle play3dClocked(time aSoundTime, AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX = 0.0f, float aVelY = 0.0f, float aVelZ = 0.0f, float aVolume = 1.0f, unsigned int aBus = 0);
		// Calculate the delay in samples for a clocked play call. Maps the caller's "physics time" to the output sample timeline using a persistent anchor, so sounds can be scheduled with sample accuracy across output buffers. Used internally by playClocked and play3dClocked.
		unsigned int getClockedDelaySamples(time aSoundTime);
		// Reset the clocked play anchor to the state as if no playClocked/play3dClocked call was ever made. The next clocked play will anchor the caller's clock to the audio clock again.
		void resetClockedAnchor();
		// Get the engine's global stream time, in seconds. This is the clock the mixer advances at the start of every output buffer and the time base used by playScheduled, scheduleStopAt and scheduleFadeAt. It only advances while the audio device is mixing.
		time getEngineTime();
		// ###### flutter_soloud local patch (render-ahead ring) ######
		// Configure the render-ahead ring: an engine-owned buffer interposed
		// between the mixer and the output device. When enabled, the device
		// callback runs with a small period (aDevicePeriodFrames) and consumes
		// from the ring while the engine keeps mixing in aBufferSize quanta
		// aRenderAheadFrames ahead of the device. This is the prerequisite for
		// retroactive re-mixing; on its own it decouples the device period from
		// the engine mix quantum. Must be called before init().
		// aRenderAheadFrames == 0 (the default) disables the feature and keeps
		// the historical direct-to-device mixing path. aDevicePeriodFrames == 0
		// selects the default small period (512 frames). Ignored on the web
		// backend. Init-time only; cannot be changed while running.
		void setRenderAheadConfig(unsigned int aDevicePeriodFrames, unsigned int aRenderAheadFrames);
		// Whether the render-ahead ring is active.
		bool isRenderAheadEnabled() const;
		// Engine time of the sample currently reaching the device: the mix
		// clock (getEngineTime) minus the ring depth. Equals getEngineTime()
		// when the ring is disabled.
		time getPlayheadTime();
		// Estimated output latency in seconds: frames buffered ahead of the
		// device plus one device period. 0 when the ring is disabled.
		time getOutputLatency();
		// Start playing a sound at an absolute engine time (see getEngineTime), with sample accuracy. Unlike playClocked there is no anchor and no re-anchor guard, so sounds can be scheduled arbitrarily far in the future. A time in the past plays as soon as possible. Negative volume means to use default.
		handle playScheduled(time aEngineTime, AudioSource &aSound, float aVolume = -1.0f, float aPan = 0.0f, unsigned int aBus = 0);
		// Calculate the delay in samples for a scheduled play call. Maps an absolute engine time to the output sample timeline. Used internally by playScheduled.
		unsigned int getScheduledDelaySamples(time aEngineTime);
		// Start playing a sound without any panning. It will be played at full volume.
		handle playBackground(AudioSource &aSound, float aVolume = -1.0f, bool aPaused = 0, unsigned int aBus = 0);

		// Seek the audio stream to certain point in time. Some streams can't seek backwards. Relative play speed affects time.
		result seek(handle aVoiceHandle, time aSeconds);
		// Stop the sound.
		void stop(handle aVoiceHandle);
		// Stop all voices.
		void stopAll();
		// Stop all voices that play this sound source
		void stopAudioSource(AudioSource &aSound);
		// Count voices that play this audio source
		int countAudioSource(AudioSource &aSound);

		// Set a live filter parameter. Use 0 for the global filters.
		void setFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId, float aValue);
		// Get a live filter parameter. Use 0 for the global filters.
		float getFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId);
		// Fade a live filter parameter. Use 0 for the global filters.
		void fadeFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId, float aTo, time aTime);
		// Oscillate a live filter parameter. Use 0 for the global filters.
		void oscillateFilterParameter(handle aVoiceHandle, unsigned int aFilterId, unsigned int aAttributeId, float aFrom, float aTo, time aTime);
		// Delete the live filter instance at aFilterId from a voice and shift the
		// remaining instances down by one slot, keeping the voice filter slots in
		// sync with the sound source filter list. Must be called for every active
		// voice of a sound before deleting a Filter object, since voice filter
		// instances keep a reference to their parent filter.
		void removeVoiceFilter(handle aVoiceHandle, unsigned int aFilterId);
		// Create a live instance of aFilter at aFilterId for a voice. Voices
		// snapshot the source filters only at play time, so this is needed to
		// make a filter added to a sound affect its already playing voices.
		void addVoiceFilter(handle aVoiceHandle, unsigned int aFilterId, Filter *aFilter);

		// Get current play time, in seconds.
		time getStreamTime(handle aVoiceHandle);
		// Get current sample position, in seconds.
		time getStreamPosition(handle aVoiceHandle);
		// Get current pause state.
		bool getPause(handle aVoiceHandle);
		// Get current volume.
		float getVolume(handle aVoiceHandle);
		// Get current overall volume (set volume * 3d volume)
		float getOverallVolume(handle aVoiceHandle);
		// Get current pan.
		float getPan(handle aVoiceHandle);
		// Get current sample rate.
		float getSamplerate(handle aVoiceHandle);
		// Get current voice protection state.
		bool getProtectVoice(handle aVoiceHandle);
		// Get the current number of busy voices.
		unsigned int getActiveVoiceCount();
		// Get the current number of voices in SoLoud
		unsigned int getVoiceCount();
		// Check if the handle is still valid, or if the sound has stopped.
		bool isValidVoiceHandle(handle aVoiceHandle);
		// Get current relative play speed.
		float getRelativePlaySpeed(handle aVoiceHandle);
		// Get current post-clip scaler value.
		float getPostClipScaler() const;
		// Get the current main resampler
		unsigned int getMainResampler() const;
		// Get current global volume
		float getGlobalVolume() const;
		// Get current maximum active voice setting
		unsigned int getMaxActiveVoiceCount() const;
		// Query whether a voice is set to loop.
		bool getLooping(handle aVoiceHandle);
		// Query whether a voice is set to auto-stop when it ends.
		bool getAutoStop(handle aVoiceHandle);
		// Get voice loop point value
		time getLoopPoint(handle aVoiceHandle);
		// Get voice loop end point value. Zero uses the natural source end.
		time getLoopEndPoint(handle aVoiceHandle);

		// Set voice loop point value. Live playback applies the change at the
		// next source refill; the getter reflects it immediately.
		void setLoopPoint(handle aVoiceHandle, time aLoopPoint);
		// Set voice loop end point value. Zero uses the natural source end.
		// Live playback applies the change at the next source refill; the getter
		// reflects it immediately.
		void setLoopEndPoint(handle aVoiceHandle, time aLoopEndPoint);
		// Set voice's loop state
		void setLooping(handle aVoiceHandle, bool aLooping);
		// Set whether sound should auto-stop when it ends
		void setAutoStop(handle aVoiceHandle, bool aAutoStop);
		// Set current maximum active voice setting
		result setMaxActiveVoiceCount(unsigned int aVoiceCount);
		// Set behavior for inaudible sounds
		void setInaudibleBehavior(handle aVoiceHandle, bool aMustTick, bool aKill);
		// Set the global volume
		void setGlobalVolume(float aVolume);
		// Set the post clip scaler value
		void setPostClipScaler(float aScaler);
		// Set the main resampler
		void setMainResampler(unsigned int aResampler);
		// Set the pause state
		void setPause(handle aVoiceHandle, bool aPause);
		// Pause all voices
		void setPauseAll(bool aPause);
		// Set the relative play speed
		result setRelativePlaySpeed(handle aVoiceHandle, float aSpeed);
		// Set the voice protection state
		void setProtectVoice(handle aVoiceHandle, bool aProtect);
		// Set the sample rate
		void setSamplerate(handle aVoiceHandle, float aSamplerate);
		// Set panning value; -1 is left, 0 is center, 1 is right
		void setPan(handle aVoiceHandle, float aPan);
		// Set absolute left/right volumes
		void setPanAbsolute(handle aVoiceHandle, float aLVolume, float aRVolume);
		// Set channel volume (volume for a specific speaker)
		void setChannelVolume(handle aVoiceHandle, unsigned int aChannel, float aVolume);
		// Set overall volume
		void setVolume(handle aVoiceHandle, float aVolume);
		// Set delay, in samples, before starting to play samples. Calling this on a live sound will cause glitches.
		void setDelaySamples(handle aVoiceHandle, unsigned int aSamples);

		// Set up volume fader
		void fadeVolume(handle aVoiceHandle, float aTo, time aTime);
		// Set up panning fader
		void fadePan(handle aVoiceHandle, float aTo, time aTime);
		// Set up relative play speed fader
		void fadeRelativePlaySpeed(handle aVoiceHandle, float aTo, time aTime);
		// Set up global volume fader
		void fadeGlobalVolume(float aTo, time aTime);
		// Schedule a stream to pause
		void schedulePause(handle aVoiceHandle, time aTime);
		// Schedule a stream to stop
		void scheduleStop(handle aVoiceHandle, time aTime);
		// Schedule a stream to stop at an absolute engine time (see getEngineTime). A time in the past stops immediately.
		void scheduleStopAt(handle aVoiceHandle, time aEngineTime);
		// Schedule a volume fade to start at an absolute engine time (see getEngineTime), fading from the current volume to aTo over aFadeTime seconds. If aThenStop is true, the voice is stopped when the fade ends.
		void scheduleFadeAt(handle aVoiceHandle, time aEngineTime, float aTo, time aFadeTime, bool aThenStop);

		// Set up volume oscillator
		void oscillateVolume(handle aVoiceHandle, float aFrom, float aTo, time aTime);
		// Set up panning oscillator
		void oscillatePan(handle aVoiceHandle, float aFrom, float aTo, time aTime);
		// Set up relative play speed oscillator
		void oscillateRelativePlaySpeed(handle aVoiceHandle, float aFrom, float aTo, time aTime);
		// Set up global volume oscillator
		void oscillateGlobalVolume(float aFrom, float aTo, time aTime);

		// Set global filters. Set to NULL to clear the filter.
		void setGlobalFilter(unsigned int aFilterId, Filter *aFilter);
		// Move the global filter and its live instance from aFromSlot to aToSlot,
		// leaving aFromSlot empty. Unlike setGlobalFilter, the live instance is
		// moved as-is, preserving its current parameter values.
		void moveGlobalFilter(unsigned int aFromSlot, unsigned int aToSlot);

		// Enable or disable visualization data gathering
		void setVisualizationEnable(bool aEnable);

		// Calculate and get 256 floats of FFT data for visualization. Visualization has to be enabled before use.
		float *calcFFT();

		// Get 256 floats of wave data for visualization. Visualization has to be enabled before use.
		float *getWave();

		// Get approximate output volume for a channel for visualization. Visualization has to be enabled before use.
		float getApproximateVolume(unsigned int aChannel);

		// Get current loop count. Returns 0 if handle is not valid. (All audio sources may not update loop count)
		unsigned int getLoopCount(handle aVoiceHandle);

		// Get audiosource-specific information from a voice.
		float getInfo(handle aVoiceHandle, unsigned int aInfoKey);

		// Create a voice group. Returns 0 if unable (out of voice groups / out of memory)
		handle createVoiceGroup();
		// Destroy a voice group.
		result destroyVoiceGroup(handle aVoiceGroupHandle);
		// Add a voice handle to a voice group
		result addVoiceToGroup(handle aVoiceGroupHandle, handle aVoiceHandle);
		// Is this handle a valid voice group?
		bool isVoiceGroup(handle aVoiceGroupHandle);
		// Is this voice group empty?
		bool isVoiceGroupEmpty(handle aVoiceGroupHandle);

		// Perform 3d audio parameter update
		void update3dAudio();

		// Set the speed of sound constant for doppler
		result set3dSoundSpeed(float aSpeed);
		// Get the current speed of sound constant for doppler
		float get3dSoundSpeed();
		// Set 3d listener parameters
		void set3dListenerParameters(float aPosX, float aPosY, float aPosZ, float aAtX, float aAtY, float aAtZ, float aUpX, float aUpY, float aUpZ, float aVelocityX = 0.0f, float aVelocityY = 0.0f, float aVelocityZ = 0.0f);
		// Set 3d listener position
		void set3dListenerPosition(float aPosX, float aPosY, float aPosZ);
		// Set 3d listener "at" vector
		void set3dListenerAt(float aAtX, float aAtY, float aAtZ);
		// set 3d listener "up" vector
		void set3dListenerUp(float aUpX, float aUpY, float aUpZ);
		// Set 3d listener velocity
		void set3dListenerVelocity(float aVelocityX, float aVelocityY, float aVelocityZ);

		// Set 3d audio source parameters
		void set3dSourceParameters(handle aVoiceHandle, float aPosX, float aPosY, float aPosZ, float aVelocityX = 0.0f, float aVelocityY = 0.0f, float aVelocityZ = 0.0f);
		// Set 3d audio source position
		void set3dSourcePosition(handle aVoiceHandle, float aPosX, float aPosY, float aPosZ);
		// Set 3d audio source velocity
		void set3dSourceVelocity(handle aVoiceHandle, float aVelocityX, float aVelocityY, float aVelocityZ);
		// Set 3d audio source min/max distance (distance < min means max volume)
		void set3dSourceMinMaxDistance(handle aVoiceHandle, float aMinDistance, float aMaxDistance);
		// Set 3d audio source attenuation parameters
		void set3dSourceAttenuation(handle aVoiceHandle, unsigned int aAttenuationModel, float aAttenuationRolloffFactor);
		// Set 3d audio source doppler factor to reduce or enhance doppler effect. Default = 1.0
		void set3dSourceDopplerFactor(handle aVoiceHandle, float aDopplerFactor);

		// Rest of the stuff is used internally.

		// Returns mixed float samples in buffer. Called by the back-end, or user with null driver.
		void mix(float *aBuffer, unsigned int aSamples);
		// Returns mixed 16-bit signed integer samples in buffer. Called by the back-end, or user with null driver.
		void mixSigned16(short *aBuffer, unsigned int aSamples);
	public:
		// Mix N samples * M channels. Called by other mix_ functions.
		void mix_internal(unsigned int aSamples, unsigned int aStride);

		// Handle rest of initialization (called from backend)
		void postinit_internal(unsigned int aSamplerate, unsigned int aBufferSize, unsigned int aFlags, unsigned int aChannels);

		// ###### flutter_soloud local patch (render-ahead ring) ######
		// Mix engine quanta into the render-ahead ring until the configured
		// render-ahead depth is reached (or the ring is full). Called from the
		// backend device callback when the ring is enabled.
		void renderRingTopUp_internal();

		// ###### flutter_soloud local patch (mix checkpoints) ######
		// Snapshot all mix-relevant state at the current quantum boundary
		// into the next circular pool slot. Called at the end of
		// mix_internal, under the audio mutex, when the ring is enabled.
		void captureMixCheckpoint_internal();
		// Pool index of the newest checkpoint at or before aTime, or -1 when
		// no checkpoint covers it (pool empty, disabled, or aTime older than
		// the oldest retained checkpoint).
		int findCheckpointAtOrBefore_internal(time aTime);
		// Restore engine, voice, resampler and filter state from a pool slot.
		// Returns false when the slot is out of range, never written, or
		// stale (already overwritten by newer captures). Voices present now
		// but absent in the checkpoint are left running (Phase 3 reconciles
		// them via event replay); checkpoint voices whose slot now holds a
		// different object (play index mismatch) are skipped.
		bool restoreMixCheckpoint_internal(int aPoolIndex);
		// Release all heap snapshots held by the checkpoint pool and empty
		// it. Idempotent; called from deinit and when the ring is disabled.
		void releaseCheckpoints_internal();
		// Allocate the circular checkpoint pool with all storage pre-sized
		// (aPoolSize == 0 just releases). Called from postinit_internal when
		// the ring is enabled.
		void allocateCheckpoints_internal(unsigned int aPoolSize);

		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// Insert a pre-created voice instance into a free voice slot,
		// performing the usual initialization (this is the under-mutex body
		// of play()). Caller must hold the audio mutex; the caller deletes
		// the instance when this returns -1 (no free voice).
		int insertVoice_internal(AudioSource &aSound, AudioSourceInstance *aInstance, float aVolume, float aPan, bool aPaused, unsigned int aBus);
		// Engine time of the sample currently reaching the device. Caller
		// must hold the audio mutex.
		time playheadTimeLocked_internal();
		// Bodies of getScheduledDelaySamples/getClockedDelaySamples with the
		// audio mutex already held by the caller.
		unsigned int getScheduledDelaySamplesLocked_internal(time aEngineTime);
		unsigned int getClockedDelaySamplesLocked_internal(time aSoundTime);
		// Try to move the already-inserted voice at aSlot into the
		// rendered-but-unplayed window: roll back to the checkpoint at or
		// before aEventTime, replay the in-window journal, retarget the
		// voice's start and re-mix forward, overwriting the ring. Returns
		// false when the gate declines (event outside the window, no covering
		// checkpoint, unrestorable state) and the caller keeps the legacy
		// placement. Caller must hold the audio mutex.
		bool retroactiveVoiceStart_internal(unsigned int aSlot, time aEventTime, AudioSource &aSound);
		// Try to stop the voice at aSlot retroactively at the given engine
		// time (retroactive stop: the voice goes silent from that time on,
		// sample-accurate within the window; aEngineTime behind the playhead
		// clamps to the playhead). Returns false when the gate declines and
		// the caller must fall back to the legacy path. Caller must hold the
		// audio mutex.
		bool retroactiveStopVoiceAt_internal(unsigned int aSlot, time aEngineTime);
		// Try to apply a parameter change (RetroJournalEntry::PARAM_*) to the
		// voice at aSlot retroactively at aEventTime, as a scheduled fader of
		// aDuration seconds (0 = step change) taking effect at that time
		// (quantum accuracy inside the window). Returns false when the gate
		// declines and the caller must fall back to the legacy immediate
		// setter. For PARAM_PAUSE only pausing (aValue 1) is supported;
		// unpausing always takes the legacy path. Caller must hold the audio
		// mutex.
		bool retroactiveParam_internal(unsigned int aSlot, int aParam, float aValue, time aDuration, time aEventTime);
		// Rollback primitive shared by all retroactive events: gate-checks
		// aEventTime, drops queued ended-events the restore would resurrect,
		// restores the checkpoint at or before aEventTime and replays the
		// journal. Returns the checkpoint pool index, or -1 when the event
		// must take the legacy path. On success the engine state sits at the
		// checkpoint time and mRemixing is true; the caller applies its event
		// and then calls retroactiveEnd_internal exactly once.
		int retroactiveBegin_internal(time aEventTime);
		// Re-mix from the restored checkpoint to the saved write head,
		// overwriting the ring, and clear mRemixing.
		void retroactiveEnd_internal(int aPoolIndex);
		// Journal a voice birth (upserted by slot+play index; aBirthTime is
		// the engine time of the first sounding sample), a voice death, and a
		// retroactive parameter change.
		void journalBirth_internal(unsigned int aSlot, time aBirthTime);
		void journalDeath_internal(unsigned int aSlot, unsigned int aPlayIndex);
		void journalParam_internal(unsigned int aSlot, unsigned int aPlayIndex, int aParam, float aValue, time aTime, time aDuration);
		// Re-apply journal entries with time > aFromTime onto freshly
		// restored checkpoint state.
		void replayJournal_internal(time aFromTime);
		// One full mix quantum for the re-mix loop: like mix(), but the
		// caller holds the audio mutex for the whole re-mix (the native audio
		// mutex is not recursive, so mix()/mix_internal() cannot be called
		// here), and visualization and death journaling are suppressed via
		// mRemixing. Checkpoints ARE re-captured, with the ring position
		// derived from the mix clock.
		void remixQuantum_internal(float *aBuffer, unsigned int aSamples);
		// The locked section of mix_internal: voice faders, active voice
		// recalculation, bus mixing, global filters, checkpoint capture.
		// Caller must hold the audio mutex.
		void mixVoicesLocked_internal(unsigned int aSamples, unsigned int aStride);

		// Update list of active voices
		void calcActiveVoices_internal();
		// Map resample buffers to active voices
		void mapResampleBuffers_internal();
		// Perform mixing for a specific bus
		void mixBus_internal(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize, float *aScratch, unsigned int aBus, float aSamplerate, unsigned int aChannels, unsigned int aResampler);
		// Fill a source block while enforcing its optional loop end point.
		unsigned int readSourceSamples_internal(AudioSourceInstance *aVoice, float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize);
		// Find a free voice, stopping the oldest if no free voice is found.
		int findFreeVoice_internal();
		// Converts handle to voice, if the handle is valid. Returns -1 if not.
		int getVoiceFromHandle_internal(handle aVoiceHandle) const;
		// Converts voice + playindex into handle
		handle getHandleFromVoice_internal(unsigned int aVoice) const;
		// Stop voice (not handle).
		void stopVoice_internal(unsigned int aVoice);
		// Set voice (not handle) pan.
		void setVoicePan_internal(unsigned int aVoice, float aPan);
		// Set voice (not handle) relative play speed.
		result setVoiceRelativePlaySpeed_internal(unsigned int aVoice, float aSpeed);
		// Set voice (not handle) volume.
		void setVoiceVolume_internal(unsigned int aVoice, float aVolume);
		// Set voice (not handle) pause state.
		void setVoicePause_internal(unsigned int aVoice, int aPause);
		// Update overall volume from set and 3d volumes
		void updateVoiceVolume_internal(unsigned int aVoice);
		// Update overall relative play speed from set and 3d speeds
		void updateVoiceRelativePlaySpeed_internal(unsigned int aVoice);
		// Perform 3d audio calculation for array of voices
		void update3dVoices_internal(unsigned int *aVoiceList, unsigned int aVoiceCount);
		// Clip the samples in the buffer
		void clip_internal(AlignedFloatBuffer &aBuffer, AlignedFloatBuffer &aDestBuffer, unsigned int aSamples, float aVolume0, float aVolume1);
		// Remove all non-active voices from group
		void trimVoiceGroup_internal(handle aVoiceGroupHandle);
		// Get pointer to the zero-terminated array of voice handles in a voice group
		handle * voiceGroupHandleToArray_internal(handle aVoiceGroupHandle) const;

		// Lock audio thread mutex.
		void lockAudioMutex_internal();
		// Unlock audio thread mutex.
		void unlockAudioMutex_internal();
		// Slow path of unlockAudioMutex_internal(): drains mEndedVoiceQueue.
		// Kept out of line so the common (empty queue) path does not carry the
		// snapshot buffer in its stack frame.
		void unlockAudioMutexAndDispatchEndedVoices_internal();

		// Max. number of active voices. Busses and tickable inaudibles also count against this.
		unsigned int mMaxActiveVoices;
		// Highest voice in use so far
		unsigned int mHighestVoice;
		// Scratch buffer, used for resampling.
		AlignedFloatBuffer mScratch;
		// Current size of the scratch, in samples.
		unsigned int mScratchSize;
		// Output scratch buffer, used in mix_().
		AlignedFloatBuffer mOutputScratch;
		// Pointers to resampler buffers, two per active voice.
		float **mResampleData;
		// Actual allocated memory for resampler buffers
		AlignedFloatBuffer mResampleDataBuffer;
		// Owners of the resample data
		AudioSourceInstance **mResampleDataOwner;
		// Audio voices.
		AudioSourceInstance *mVoice[VOICE_COUNT];
		// Resampler for the main bus
		unsigned int mResampler;
		// Output sample rate (not float)
		unsigned int mSamplerate;
		// Output channel count
		unsigned int mChannels;
		// Current backend ID
		unsigned int mBackendID;
		// Current backend string
		const char * mBackendString;
		// Maximum size of output buffer; used to calculate needed scratch.
		unsigned int mBufferSize;
		// ###### flutter_soloud local patch (render-ahead ring) ######
		// Requested device callback period in frames when the render-ahead
		// ring is enabled (0 = default small period). Set before init() via
		// setRenderAheadConfig().
		unsigned int mDevicePeriodFrames;
		// Render-ahead depth in frames; 0 disables the ring (default).
		unsigned int mRenderAheadFrames;
		// Engine-owned ring between the mixer and the output device. Only
		// allocated when mRenderAheadFrames > 0 (never on the web backend).
		RenderRing mRenderRing;
		// Staging buffer for one engine quantum of interleaved float samples,
		// mixed into and then appended to mRenderRing by
		// renderRingTopUp_internal(). Allocated in postinit_internal when the
		// ring is enabled.
		AlignedFloatBuffer mRenderRingStaging;
		// ###### flutter_soloud local patch (mix checkpoints) ######
		// Circular pool of mix-quantum-boundary snapshots for retroactive
		// re-mixing. Allocated in postinit_internal when the render-ahead
		// ring is enabled, empty otherwise. All storage is pre-sized at
		// allocation time and reused in place by capture, so the audio path
		// performs no allocation (filter/source heap snapshots are the
		// documented exception; the Phase 2 built-ins return nullptr).
		std::vector<MixCheckpoint> mCheckpointPool;
		// Next pool slot to write (circular).
		unsigned int mCheckpointWriteIndex;
		// Monotonic capture counter; together with MixCheckpoint::mSerial it
		// decides which pool slots still hold valid data.
		unsigned long long mCheckpointCounter;
		// ###### flutter_soloud local patch (retroactive re-mix) ######
		// In-window event journal (see soloud_checkpoint.h): births and deaths
		// that landed after the oldest retained checkpoint, kept sorted by
		// event time. Preallocated with the checkpoint pool; written on
		// API-call threads under the audio mutex, replayed during rollback.
		std::vector<RetroJournalEntry> mRetroJournal;
		// True while a retroactive re-mix is running: visualization updates and
		// death journaling are suppressed (the re-mix reproduces history, it
		// does not create new events). Checkpoint capture still runs, with the
		// ring position derived from the mix clock, so the checkpoint chain
		// incorporates the rewritten timeline.
		bool mRemixing;
		// Handles whose ended-event must not be re-queued when the re-mix
		// reproduces a journaled death (the original dispatch already
		// happened or is in flight and cannot be un-sent).
		handle mRemixSuppressEnded[RETRO_JOURNAL_CAPACITY];
		unsigned int mRemixSuppressEndedCount;
		// Re-mix context saved by retroactiveBegin_internal for
		// retroactiveEnd_internal: ring write head and mix clock before the
		// rollback, the frame the rewrite starts at, and the restored
		// checkpoint's time.
		unsigned long long mRemixWritePos;
		unsigned long long mRemixStartPos;
		time mRemixOldStreamTime;
		time mRemixCheckpointTime;
		// Flags; see Soloud::FLAGS
		unsigned int mFlags;
		// Global volume. Applied before clipping.
		float mGlobalVolume;
		// Post-clip scaler. Applied after clipping.
		// ###### flutter_soloud local patch ######
		// Atomic: clip_internal() runs on the audio thread *after*
		// unlockAudioMutex_internal(), so the audio mutex does not order it
		// against setPostClipScaler() -- which flutter_soloud calls from
		// init(), by which point the device is already mixing.
		// ThreadSanitizer reports the bare float as a data race in
		// clip_internal(). Every reader snapshots it once into a local, which
		// is also what the SSE paths need since they take its address.
		std::atomic<float> mPostClipScaler;
		// Current play index. Used to create audio handles.
		unsigned int mPlayIndex;
		// Current sound source index. Used to create sound source IDs.
		unsigned int mAudioSourceID;
		// Fader for the global volume.
		Fader mGlobalVolumeFader;
		// Global stream time, for the global volume fader.
		time mStreamTime;
		// Anchor for the playClocked calls: maps the "physics time" given by
		// the caller to the output sample timeline. A negative
		// mClockedAnchorSample means the anchor has not been set yet.
		time mClockedAnchorTime;
		long long mClockedAnchorSample;
		// Last "physics time" seen by a clocked play call. Used to detect
		// when the caller's clock is restarted (time going backwards).
		time mClockedLastTime;
		// Global filter
		Filter *mFilter[FILTERS_PER_STREAM];
		// Global filter instance
		FilterInstance *mFilterInstance[FILTERS_PER_STREAM];

		// Approximate volume for channels.
		float mVisualizationChannelVolume[MAX_CHANNELS];
		// Mono-mixed wave data for visualization and for visualization FFT input
		float mVisualizationWaveData[256];
		// FFT output data
		float mFFTData[256];
		// Snapshot of wave data for visualization
		float mWaveData[256];

		// 3d listener position
		float m3dPosition[3];
		// 3d listener look-at
		float m3dAt[3];
		// 3d listener up
		float m3dUp[3];
		// 3d listener velocity
		float m3dVelocity[3];
		// 3d speed of sound (for doppler)
		float m3dSoundSpeed;

		// 3d position of speakers
		float m3dSpeakerPosition[3 * MAX_CHANNELS];

		// Data related to 3d processing, separate from AudioSource so we can do 3d calculations without audio mutex.
		AudioSourceInstance3dData m3dData[VOICE_COUNT];

		// For each voice group, first int is number of ints alocated.
		unsigned int **mVoiceGroup;
		unsigned int mVoiceGroupCount;

		// List of currently active voices
		unsigned int mActiveVoice[VOICE_COUNT];
		// Number of currently active voices
		unsigned int mActiveVoiceCount;
		// Active voices list needs to be recalculated
		bool mActiveVoiceDirty;
	};
};

#endif

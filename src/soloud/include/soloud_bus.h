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

#ifndef SOLOUD_BUS_H
#define SOLOUD_BUS_H

#include "soloud.h"

namespace SoLoud
{
	class Bus;

	// ###### flutter_soloud local patch (per-bus visualization) ######
	// Receives the audio a bus has just mixed from its voices: before the bus
	// voice's own filters, volume and pan, which are applied when the parent
	// mixes the bus voice. Called on the audio thread with the audio mutex
	// held, in SoLoud's planar layout: channel c starts at aBuffer + c * aStride.
	// Not called while nothing is mixed through the bus (the bus voice or an
	// outer bus is paused or stopped, or the device is stopped), nor for the
	// re-mixed quanta of a retroactive re-mix.
	class BusAudioTap
	{
	public:
		virtual ~BusAudioTap() {}
		virtual void onBusAudio(const float *aBuffer, unsigned int aSamples, unsigned int aStride, unsigned int aChannels) = 0;
	};

	class BusInstance : public AudioSourceInstance
	{
	public:
		Bus *mParent;
		unsigned int mScratchSize;
		AlignedFloatBuffer mScratch;

		// Approximate volume for channels.
		float mVisualizationChannelVolume[MAX_CHANNELS];
		// Mono-mixed wave data for visualization and for visualization FFT input
		float mVisualizationWaveData[256];

		BusInstance(Bus *aParent);
		virtual unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize);
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Hand the mixed block to the parent's audio tap, if any.
		void tapAudio_internal(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize);
		virtual bool hasEnded();
		virtual ~BusInstance();
	};

	class Bus : public AudioSource
	{
	public:
		Bus();
		virtual BusInstance *createInstance();
		// Set filter. Set to NULL to clear the filter.
		virtual void setFilter(unsigned int aFilterId, Filter *aFilter);
		// Move the filter and its live instance from aFromSlot to aToSlot,
		// leaving aFromSlot empty. Unlike setFilter, the live instance is
		// moved as-is, preserving its current parameter values.
		virtual void moveFilter(unsigned int aFromSlot, unsigned int aToSlot);
		// Play sound through the bus
		handle play(AudioSource &aSound, float aVolume = 1.0f, float aPan = 0.0f, bool aPaused = 0);
		// Play sound through the bus, delayed in relation to other sounds called via this function.
		// aLoopStartOffset / aLoopEndOffset: optional exact frame offsets (-1 = inactive / use aLoopPoint/aLoopEndPoint).
		handle playClocked(time aSoundTime, AudioSource &aSound, float aVolume = 1.0f, float aPan = 0.0f, float aScale = 1.0f, bool aLooping = false, time aLoopPoint = 0.0, time aLoopEndPoint = 0.0, long long aLoopStartOffset = -1, long long aLoopEndOffset = -1);
		// Play sound through the bus at an absolute engine time (see Soloud::getEngineTime), with sample accuracy.
		handle playScheduled(time aEngineTime, AudioSource &aSound, float aVolume = 1.0f, float aPan = 0.0f, float aScale = 1.0f, bool aLooping = false, time aLoopPoint = 0.0, time aLoopEndPoint = 0.0, long long aLoopStartOffset = -1, long long aLoopEndOffset = -1);
		// Start playing a 3d audio source through the bus
		handle play3d(AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX = 0.0f, float aVelY = 0.0f, float aVelZ = 0.0f, float aVolume = 1.0f, bool aPaused = 0, float aScale = 1.0f, bool aLooping = false, time aLoopPoint = 0.0, time aLoopEndPoint = 0.0, long long aLoopStartOffset = -1, long long aLoopEndOffset = -1);
		// Start playing a 3d audio source through the bus, delayed in relation to other sounds called via this function.
		handle play3dClocked(time aSoundTime, AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX = 0.0f, float aVelY = 0.0f, float aVelZ = 0.0f, float aVolume = 1.0f, float aScale = 1.0f, bool aLooping = false, time aLoopPoint = 0.0, time aLoopEndPoint = 0.0, long long aLoopStartOffset = -1, long long aLoopEndOffset = -1);
		// Start playing a 3d audio source through the bus at an absolute engine time (see Soloud::getEngineTime), with sample accuracy.
		handle play3dScheduled(time aEngineTime, AudioSource &aSound, float aPosX, float aPosY, float aPosZ, float aVelX = 0.0f, float aVelY = 0.0f, float aVelZ = 0.0f, float aVolume = 1.0f, float aScale = 1.0f, bool aLooping = false, time aLoopPoint = 0.0, time aLoopEndPoint = 0.0, long long aLoopStartOffset = -1, long long aLoopEndOffset = -1);
		// Set number of channels for the bus (default 2)
		result setChannels(unsigned int aChannels);
		// Enable or disable visualization data gathering
		void setVisualizationEnable(bool aEnable);
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Set the tap that receives what this bus mixes, or NULL to remove it.
		// Swapped under the audio mutex, so once this returns the audio thread
		// no longer calls the previous tap and it may be destroyed.
		// flutter_soloud visualizes buses through this rather than through
		// setVisualizationEnable and the getters below.
		void setAudioTap(BusAudioTap *aTap);
		// Move a live sound to this bus. Does nothing while the bus is not
		// playing (flutter_soloud local patch).
		void annexSound(handle aVoiceHandle);
		
		// Calculate and get 256 floats of FFT data for visualization. Visualization has to be enabled before use.
		float *calcFFT();

		// Get 256 floats of wave data for visualization. Visualization has to be enabled before use.
		float *getWave();

		// Get approximate volume for output channel for visualization. Visualization has to be enabled before use.
		float getApproximateVolume(unsigned int aChannel);

		// Get number of immediate child voices to this bus
		unsigned int getActiveVoiceCount();

		// Get current the resampler for this bus
		unsigned int getResampler();
		// Set the resampler for this bus
		void setResampler(unsigned int aResampler);
	public:
		// The live instance, or NULL while the bus is not playing. The engine
		// can delete the bus voice without going through Bus (stopAll, stop()
		// on the bus handle, an outer bus stopping, a scheduled stop on the
		// audio thread), so ~BusInstance clears it. Only read or written with
		// the audio mutex held.
		BusInstance *mInstance;
		unsigned int mChannelHandle;
		unsigned int mResampler;
		// FFT output data
		float mFFTData[256];
		// Snapshot of wave data for visualization
		float mWaveData[256];
		// Read by the audio thread under the audio mutex; see setAudioTap.
		BusAudioTap *mAudioTap;
		// Internal: find the bus' channel
		void findBusHandle();
		// ###### flutter_soloud local patch (per-bus visualization) ######
		// Internal: findBusHandle() with the audio mutex already held.
		void findBusHandle_internal();
		// Internal: the handle to play a sound through, 0 if not playing.
		handle getPlayHandle_internal();
	};
};

#endif
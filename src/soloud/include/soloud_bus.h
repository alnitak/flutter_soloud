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

	class BusInstance : public AudioSourceInstance
	{
	public:
		Bus *mParent;
		unsigned int mScratchSize;
		AlignedFloatBuffer mScratch;

		// Approximate volume for channels: per-channel peak of the last mixed block.
		float mVisualizationChannelVolume[MAX_CHANNELS];
		// Ring of the latest 256 frames, channels averaged. Visualization and FFT input.
		float mVisualizationWaveData[256];
		// Next write position in mVisualizationWaveData (oldest frame).
		unsigned int mVisualizationWavePos;
		// Level accumulated since the last Bus::getLevel: sum of per-frame
		// mean squares, peak of every sample, and frames mixed.
		double mLevelSumSquares;
		float mLevelPeak;
		unsigned long long mLevelFrames;

		// Clear all visualization data. Call with the audio mutex held.
		void clearVisualization();

		BusInstance(Bus *aParent);
		virtual unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize);
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
		// Move a live sound to this bus
		void annexSound(handle aVoiceHandle);
		
		// Calculate and get 256 floats of FFT data for visualization. Visualization has to be enabled before use.
		float *calcFFT();

		// Get 256 floats of wave data for visualization. Visualization has to be enabled before use.
		float *getWave();

		// Get approximate volume for output channel for visualization. Visualization has to be enabled before use.
		float getApproximateVolume(unsigned int aChannel);

		// Get the RMS and peak of every frame mixed since the previous call, then reset.
		// aFrames is 0 if nothing was mixed. Visualization has to be enabled before use.
		void getLevel(float *aRms, float *aPeak, unsigned long long *aFrames);

		// Get number of immediate child voices to this bus
		unsigned int getActiveVoiceCount();

		// Get current the resampler for this bus
		unsigned int getResampler();
		// Set the resampler for this bus
		void setResampler(unsigned int aResampler);
	public:
		BusInstance *mInstance;
		unsigned int mChannelHandle;
		unsigned int mResampler;
		// FFT output data
		float mFFTData[256];
		// Snapshot of wave data for visualization
		float mWaveData[256];
		// Internal: find the bus' channel
		void findBusHandle();
	};
};

#endif
/*
SoLoud audio engine
Copyright (c) 2013-2021 Jari Komppa

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

#include "basic_wave.h"
#include "soloud_misc.h"

#include <iostream>

BasicwaveInstance::BasicwaveInstance(Basicwave *aParent)
{
    mParent = aParent;
    mT = 0;
    mPhase = 0;
    mCurrentFrequency = mParent->mFreq;
}

unsigned int BasicwaveInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
{
    unsigned int i;
    double d = 1.0 / mSamplerate;
    double targetFrequency = mParent->mFreq;

    // Number of steps for smoothing frequency
    const unsigned int smoothingSteps = aSamplesToRead / 3;

    // Calculate the smoothing factor dynamically
    const double smoothingFactor = smoothingSteps > 0 ? (1.0 / smoothingSteps) : 1.0;

    if (!mParent->mSuperwave)
    {
        for (i = 0; i < aSamplesToRead; i++)
        {
            // Smoothly adjust frequency
            mCurrentFrequency += (targetFrequency - mCurrentFrequency) * smoothingFactor;

            // Calculate the phase increment
            double phaseIncrement = mCurrentFrequency * d;

            // Update the phase
            mPhase += phaseIncrement;

            // Wrap the phase to keep it in [0.0, 1.0) range
            if (mPhase >= 1.0)
                mPhase -= 1.0;

            aBuffer[i] = SoLoud::Misc::generateWaveform(
                             mParent->mWaveform,
                             mPhase) *
                         mParent->mADSR.val(mT, 10000000000000.0);

            mT += d;
        }
    }
    else
    {
        for (i = 0; i < aSamplesToRead; i++)
        {
            // Smoothly adjust frequency
            mCurrentFrequency += (targetFrequency - mCurrentFrequency) * smoothingFactor;

            // Calculate the primary phase increment
            double phaseIncrement = mCurrentFrequency * d;

            // Update the primary phase
            mPhase += phaseIncrement;

            // Wrap the primary phase to [0.0, 1.0)
            if (mPhase >= 1.0)
                mPhase -= 1.0;

            // Generate the primary waveform
            aBuffer[i] = SoLoud::Misc::generateWaveform(
                            mParent->mWaveform, mPhase) *
                        mParent->mADSR.val(mT, 10000000000000.0);

            // Generate additional harmonics
            for (int j = 0; j < 3; j++)
            {
                // Calculate the harmonic frequency
                double harmonicFrequency = mCurrentFrequency * (2.0 + j * mParent->mSuperwaveDetune);

                // Calculate the harmonic phase increment
                double harmonicPhaseIncrement = harmonicFrequency * d;

                // Update and wrap the harmonic phase
                mHarmonicPhases[j] += harmonicPhaseIncrement;
                if (mHarmonicPhases[j] >= 1.0)
                    mHarmonicPhases[j] -= 1.0;

                // Generate the harmonic waveform
                aBuffer[i] += SoLoud::Misc::generateWaveform(
                                mParent->mWaveform, mHarmonicPhases[j]) *
                            mParent->mADSR.val(mT, 10000000000000.0) * mParent->mSuperwaveScale;
            }

            // Increment the time for ADSR
            mT += d;
        }
    }
    return aSamplesToRead;
}

bool BasicwaveInstance::hasEnded()
{
    // This audio source never ends.
    return 0;
}

Basicwave::Basicwave(
    SoLoud::Soloud::WAVEFORM waveform,
    bool superWave,
    double scale,
    double detune)
{
    setSamplerate(44100);
    setWaveform(waveform);
    mSuperwave = superWave;
    mSuperwaveScale = scale;
    mSuperwaveDetune = detune;
}

Basicwave::~Basicwave()
{
    stop();
}

void Basicwave::setScale(double aScale)
{
    mSuperwaveScale = aScale;
}

void Basicwave::setDetune(double aDetune)
{
    mSuperwaveDetune = aDetune;
}

void Basicwave::setSamplerate(double aSamplerate)
{
    mBaseSamplerate = aSamplerate;
    mFreq = (double)(440 / mBaseSamplerate);
}

void Basicwave::setFreq(double aFreq)
{
    mFreq = aFreq;
}

void Basicwave::setSuperWave(bool aSuperwave)
{
    mSuperwave = aSuperwave;
}

void Basicwave::setWaveform(int aWaveform)
{
    mWaveform = aWaveform;
}

SoLoud::AudioSourceInstance *Basicwave::createInstance()
{
    return new BasicwaveInstance(this);
}

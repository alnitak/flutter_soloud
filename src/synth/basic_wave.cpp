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
    mCurrentFrequency = mParent->mFreq.load(std::memory_order_relaxed);
    mHarmonicPhases[0] = 0.0;
    mHarmonicPhases[1] = 0.0;
    mHarmonicPhases[2] = 0.0;
}

unsigned int BasicwaveInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
{
    (void)aBufferSize;
    unsigned int i;
    const double d = 1.0 / mSamplerate;
    // One snapshot of the parameters per block: the setters may change them
    // meanwhile.
    const double targetFrequency = mParent->mFreq.load(std::memory_order_relaxed);
    const int waveform = mParent->mWaveform.load(std::memory_order_relaxed);
    const bool superwave = mParent->mSuperwave.load(std::memory_order_relaxed);

    // Number of steps for smoothing frequency
    const unsigned int smoothingSteps = aSamplesToRead / 3;

    // Calculate the smoothing factor dynamically
    const double smoothingFactor = smoothingSteps > 0 ? (1.0 / smoothingSteps) : 1.0;

    if (!superwave)
    {
        for (i = 0; i < aSamplesToRead; i++)
        {
            mCurrentFrequency += (targetFrequency - mCurrentFrequency) * smoothingFactor;
            mPhase += mCurrentFrequency * d;
            if (mPhase >= 1.0)
                mPhase -= 1.0;

            aBuffer[i] = SoLoud::Misc::generateWaveform(
                             waveform,
                             static_cast<float>(mPhase)) *
                         mParent->mADSR.val(mT, 10000000000000.0);

            mT += d;
        }
    }
    else
    {
        const double detune = mParent->mSuperwaveDetune.load(std::memory_order_relaxed);
        const double harmMult0 = 2.0;
        const double harmMult1 = 2.0 + detune;
        const double harmMult2 = 2.0 + 2.0 * detune;
        const float scale = static_cast<float>(
            mParent->mSuperwaveScale.load(std::memory_order_relaxed));

        for (i = 0; i < aSamplesToRead; i++)
        {
            mCurrentFrequency += (targetFrequency - mCurrentFrequency) * smoothingFactor;
            const double phaseIncrement = mCurrentFrequency * d;

            mPhase += phaseIncrement;
            if (mPhase >= 1.0)
                mPhase -= 1.0;

            mHarmonicPhases[0] += phaseIncrement * harmMult0;
            if (mHarmonicPhases[0] >= 1.0)
                mHarmonicPhases[0] -= 1.0;

            mHarmonicPhases[1] += phaseIncrement * harmMult1;
            if (mHarmonicPhases[1] >= 1.0)
                mHarmonicPhases[1] -= 1.0;

            mHarmonicPhases[2] += phaseIncrement * harmMult2;
            if (mHarmonicPhases[2] >= 1.0)
                mHarmonicPhases[2] -= 1.0;

            const float env = mParent->mADSR.val(mT, 10000000000000.0);
            const float primary = SoLoud::Misc::generateWaveform(
                waveform, static_cast<float>(mPhase));
            const float harmonics =
                SoLoud::Misc::generateWaveform(
                    waveform, static_cast<float>(mHarmonicPhases[0])) +
                SoLoud::Misc::generateWaveform(
                    waveform, static_cast<float>(mHarmonicPhases[1])) +
                SoLoud::Misc::generateWaveform(
                    waveform, static_cast<float>(mHarmonicPhases[2]));

            aBuffer[i] = (primary + harmonics * scale) * env;
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
    mSuperwave.store(superWave, std::memory_order_relaxed);
    mSuperwaveScale.store(scale, std::memory_order_relaxed);
    mSuperwaveDetune.store(detune, std::memory_order_relaxed);
}

Basicwave::~Basicwave()
{
    stop();
}

void Basicwave::setScale(double aScale)
{
    mSuperwaveScale.store(aScale, std::memory_order_relaxed);
}

void Basicwave::setDetune(double aDetune)
{
    mSuperwaveDetune.store(aDetune, std::memory_order_relaxed);
}

void Basicwave::setSamplerate(double aSamplerate)
{
    mBaseSamplerate = aSamplerate;
    mFreq.store((double)(440 / mBaseSamplerate), std::memory_order_relaxed);
}

void Basicwave::setFreq(double aFreq)
{
    mFreq.store(aFreq, std::memory_order_relaxed);
}

void Basicwave::setSuperWave(bool aSuperwave)
{
    mSuperwave.store(aSuperwave, std::memory_order_relaxed);
}

void Basicwave::setWaveform(int aWaveform)
{
    mWaveform.store(aWaveform, std::memory_order_relaxed);
}

SoLoud::AudioSourceInstance *Basicwave::createInstance()
{
    return new BasicwaveInstance(this);
}

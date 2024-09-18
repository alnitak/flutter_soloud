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

BasicwaveInstance::BasicwaveInstance(Basicwave *aParent)
{
    mParent = aParent;
    mOffset = 0;
    mT = 0;
}

unsigned int BasicwaveInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
{
    unsigned int i;
    double d = 1.0 / mSamplerate;
    if (!mParent->mSuperwave)
    {
        for (i = 0; i < aSamplesToRead; i++)
        {
            aBuffer[i] = SoLoud::Misc::generateWaveform(
                             mParent->mWaveform,
                             (double)fmod(mParent->mFreq * (double)mOffset, 1.0)) *
                         mParent->mADSR.val(mT, 10000000000000.0);
            mOffset++;
            mT += d;
        }
    }
    else
    {
        for (i = 0; i < aSamplesToRead; i++)
        {
            aBuffer[i] = SoLoud::Misc::generateWaveform(
                             mParent->mWaveform, (double)fmod(mParent->mFreq * (double)mOffset, 1.0)) *
                         mParent->mADSR.val(mT, 10000000000000.0);
            double f = mParent->mFreq * (double)mOffset;
            for (int j = 0; j < 3; j++)
            {
                f *= 2;
                aBuffer[i] += SoLoud::Misc::generateWaveform(
                                  mParent->mWaveform, (double)fmod(mParent->mSuperwaveDetune * f, 1.0)) *
                              mParent->mADSR.val(mT, 10000000000000.0) * mParent->mSuperwaveScale;
            }
            mOffset++;
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
    mFreq = aFreq / mBaseSamplerate;
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

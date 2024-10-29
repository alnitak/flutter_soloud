#pragma once

#ifndef _AUDIOBUFFER_H
#define _AUDIOBUFFER_H

#include <stdio.h>
#include <atomic>
#include <chrono>
#include "soloud.h"
#include "../enums.h"
#include "../active_sound.h"
#include "../player.h"
#include "buffer.h"

class Player;

namespace SoLoud
{
  class BufferStream;
  typedef void (*dartOnBufferingCallback_t)(unsigned int *, double time);

  class BufferStreamInstance : public AudioSourceInstance
  {
    BufferStream *mParent;
    unsigned int mOffset;

  public:
    BufferStreamInstance(BufferStream *aParent);
    virtual unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize);
    virtual result seek(double aSeconds, float *mScratch, unsigned int mScratchSize);
    virtual result rewind();
    virtual bool hasEnded();
    virtual ~BufferStreamInstance();
  };

  class BufferStream : public AudioSource
  {
  public:
    // The flutter_soloud main [player] instance
    Player *mThePlayer;
    // Used to access the AudioSource this stream belongs to
    ActiveSound* mParent;
    void (*mOnBufferingCallback)();
    enum Endianness mEndianness; // TODO?
    unsigned int mMaxBufferSize;
    unsigned int mSampleCount;
    SoLoud::time mBufferingTimeNeeds;
    PCMformat mPCMformat;
    Buffer mBuffer;
    bool dataIsEnded;

    BufferStream();
    virtual ~BufferStream();
    void setBufferStream(
        Player *aPlayer,
        ActiveSound *aParent,
        unsigned int maxBufferSize = 1024 * 1024 * 100, // 100 Mbytes
        SoLoud::time bufferingTimeNeeds = 2.0f, // 2 seconds of data to wait
        PCMformat pcmFormat = {44100, 2, 2, PCM_S16LE},
        void (*onBufferingCallback)() = nullptr);
    void setDataIsEnded();
    PlayerErrors addData(const void *aData, unsigned int numSamples);
    virtual AudioSourceInstance *createInstance();
    time getLength();
  };
};

#endif // _AUDIOBUFFER_H
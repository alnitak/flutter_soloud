#pragma once

#ifndef _AUDIOBUFFER_H
#define _AUDIOBUFFER_H

#include <stdio.h>
#include <atomic>
#include <chrono>
#include "soloud.h"
#include "../player.h"
#include "../enums.h"
#include "../active_sound.h"
#include "buffer.h"
#if !defined(NO_OPUS_OGG_LIBS)
#include "opus_stream_decoder.h"
#endif

class Player;

namespace SoLoud
{
  class BufferStream;

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
    dartOnBufferingCallback_t mOnBufferingCallback;
    unsigned int mMaxBufferSize;
    int64_t mSampleCount;
    uint64_t mBytesConsumed;
    SoLoud::time mBufferingTimeNeeds;
    PCMformat mPCMformat;
    Buffer mBuffer;
    uint64_t mBytesReceived;
    uint64_t mUncompressedBytesReceived;
    bool dataIsEnded;
    bool mIsBuffering;
#if !defined(NO_OPUS_OGG_LIBS)
    std::unique_ptr<OpusDecoderWrapper> decoder;
#endif

    BufferStream();
    virtual ~BufferStream();
    PlayerErrors setBufferStream(
        Player *aPlayer,
        ActiveSound *aParent,
        unsigned int maxBufferSize = 1024 * 1024 * 100, // 100 Mbytes
        BufferingType bufferingType = BufferingType::PRESERVED,
        time bufferingTimeNeeds = 2.0f, // 2 seconds of data to wait
        PCMformat pcmFormat = {44100, 2, 2, PCM_S16LE},
        dartOnBufferingCallback_t onBufferingCallback = nullptr);
    void resetBuffer();
    void setDataIsEnded();
    PlayerErrors addData(const void *aData, unsigned int numSamples, bool forceAdd = false);
    void checkBuffering(unsigned int afterAddingBytesCount);
    void callOnBufferingCallback(bool isBuffering, unsigned int handle, double time);
    BufferingType getBufferingType();
    virtual AudioSourceInstance *createInstance();
    SoLoud::time getLength();
    SoLoud::time getStreamTimeConsumed();

    std::vector<unsigned char> buffer;
  };
};

#endif // _AUDIOBUFFER_H
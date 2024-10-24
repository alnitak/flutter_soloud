#ifndef _AUDIOBUFFER_H
#define _AUDIOBUFFER_H

#include <stdio.h>
#include <chrono>
#include "soloud.h"
#include "../enums.h"
#include "buffer.h"

namespace SoLoud
{
  class BufferStream;
  typedef void (*dartVoiceEndedCallback_t)(unsigned int *);

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

  struct PCMformat
  {
    unsigned int sampleRate;
    unsigned int channels;
    unsigned int bytesPerSample;
    BufferPcmType dataType;
  };

  class BufferStream : public AudioSource
  {
  public:
    void (*mOnBufferingCallback)();
    enum Endianness mEndianness; // TODO?
    unsigned int mMaxBufferSize;
    unsigned int mSampleCount;
    PCMformat mPCMformat;
    Buffer mBuffer;
    bool dataIsEnded;

    BufferStream();
    virtual ~BufferStream();
    void setBufferStream(
        unsigned int maxBufferSize = 1024 * 1024 * 100, // 100 Mbytes
        PCMformat pcmFormat = {44100, 2, 2, PCM_S16LE},
        void (*onBufferingCallback)() = nullptr);
    // TODO: add Base64 decoding: https://github.com/aklomp/base64
    void setDataIsEnded();
    PlayerErrors addData(const void *aData, unsigned int numSamples);
    virtual AudioSourceInstance *createInstance();
    time getLength();

  };
};

#endif // _AUDIOBUFFER_H
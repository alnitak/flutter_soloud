/*
SoLoud audio engine
Copyright (c) 2013-2018 Jari Komppa

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

#include "soloud_wav.h"
#include "soloud.h"
#include "soloud_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(NO_XIPH_LIBS)
#include "mb_ogg.h"
#endif
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"
#include "native_audio_decoder.h"
#include "../../../../simd/simd_pcm.h"
#include <vector>

namespace SoLoud {
WavInstance::WavInstance(Wav *aParent) {
  mParent = aParent;
  mOffset = 0;
}

unsigned int WavInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead,
                                   unsigned int aBufferSize) {
  if (mParent->mData == NULL)
    return 0;

  unsigned int dataleft = mParent->mActualSampleCount - mOffset;
  unsigned int copylen = dataleft;
  if (copylen > aSamplesToRead)
    copylen = aSamplesToRead;

  unsigned int i;
  for (i = 0; i < mChannels; i++) {
    memcpy(aBuffer + i * aBufferSize,
           mParent->mData + mOffset + i * mParent->mSampleCount,
           sizeof(float) * copylen);
  }

  mOffset += copylen;
  return copylen;
}

result WavInstance::rewind() {
  mOffset = 0;
  mStreamPosition = 0.0f;
  return 0;
}

// ###### flutter_soloud local patch (retroactive re-mix) ######
class WavSourceStateSnapshot : public SourceStateSnapshot {
public:
  unsigned int mOffset;
};

static const char kWavSnapshotKind = 0;

SourceStateSnapshot *WavInstance::captureSourceState() {
  // The wav data is fully decoded in memory (mParent->mData), so the
  // consumption state is exactly the read offset.
  WavSourceStateSnapshot *s = new WavSourceStateSnapshot();
  s->mKind = &kWavSnapshotKind;
  s->mOffset = mOffset;
  return s;
}

bool WavInstance::captureSourceStateInto(SourceStateSnapshot *aState) {
  if (aState == NULL || aState->mKind != &kWavSnapshotKind)
    return false;
  static_cast<WavSourceStateSnapshot *>(aState)->mOffset = mOffset;
  return true;
}

void WavInstance::restoreSourceState(SourceStateSnapshot *aState) {
  WavSourceStateSnapshot *s = static_cast<WavSourceStateSnapshot *>(aState);
  if (s == NULL)
    return;
  mOffset = s->mOffset;
}

result WavInstance::seek(double aSeconds, float *mScratch,
                         unsigned int mScratchSize) {
  (void)mScratch;
  (void)mScratchSize;
  if (aSeconds <= 0.0) {
    return rewind();
  }
  if (mParent == NULL || mBaseSamplerate <= 0.0f) {
    return INVALID_PARAMETER;
  }
  unsigned int pos = (unsigned int)floor(mBaseSamplerate * aSeconds + 0.5);
  if (pos > mParent->mActualSampleCount) {
    pos = mParent->mActualSampleCount;
  }
  mOffset = pos;
  mStreamPosition = float(pos / mBaseSamplerate);
  return SO_NO_ERROR;
}

bool WavInstance::hasEnded() {
  if (!(mFlags & AudioSourceInstance::LOOPING) &&
      mOffset >= mParent->mActualSampleCount) {
    return 1;
  }
  return 0;
}

Wav::Wav() {
  mData = NULL;
  mSampleCount = 0;
  mActualSampleCount = 0;
}

Wav::~Wav() {
  stop();
  delete[] mData;
}

#define MAKEDWORD(a, b, c, d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))

result Wav::loadwav(MemoryFile *aReader) {
  drwav decoder;

  if (!drwav_init_memory(&decoder, aReader->getMemPtr(), aReader->length(),
                         NULL)) {
    return FILE_LOAD_FAILED;
  }

  drwav_uint64 samples = decoder.totalPCMFrameCount;

  if (!samples || decoder.channels == 0) {
    drwav_uninit(&decoder);
    return FILE_LOAD_FAILED;
  }

  mChannels = decoder.channels > MAX_CHANNELS ? MAX_CHANNELS : decoder.channels;
  mData = new float[(size_t)samples * mChannels];
  mBaseSamplerate = (float)decoder.sampleRate;
  mSampleCount = (unsigned int)samples;
  mActualSampleCount = (unsigned int)samples;

  constexpr unsigned int kChunkFrames = 1024;
  if (decoder.channels <= MAX_CHANNELS) {
    float tmp[kChunkFrames * MAX_CHANNELS];
    for (unsigned int i = 0; i < mSampleCount; i += kChunkFrames) {
      unsigned int blockSize =
          (mSampleCount - i) > kChunkFrames ? kChunkFrames : mSampleCount - i;
      unsigned int got =
          (unsigned int)drwav_read_pcm_frames_f32(&decoder, blockSize, tmp);
      if (got < blockSize) {
        memset(tmp + (size_t)got * decoder.channels, 0,
               sizeof(float) * (blockSize - got) * decoder.channels);
      }
      deinterleaveFloatsToPlanar(tmp, mData + i, mSampleCount, blockSize,
                                 mChannels);
    }
  } else {
    std::vector<float> tmp((size_t)kChunkFrames * decoder.channels);
    for (unsigned int i = 0; i < mSampleCount; i += kChunkFrames) {
      unsigned int blockSize =
          (mSampleCount - i) > kChunkFrames ? kChunkFrames : mSampleCount - i;
      unsigned int got = (unsigned int)drwav_read_pcm_frames_f32(
          &decoder, blockSize, tmp.data());
      if (got < blockSize) {
        memset(tmp.data() + (size_t)got * decoder.channels, 0,
               sizeof(float) * (blockSize - got) * decoder.channels);
      }
      for (unsigned int j = 0; j < blockSize; j++) {
        for (unsigned int k = 0; k < mChannels; k++) {
          mData[(size_t)k * mSampleCount + i + j] =
              tmp[(size_t)j * decoder.channels + k];
        }
      }
    }
  }
  drwav_uninit(&decoder);

  return SO_NO_ERROR;
}

result Wav::loadogg(MemoryFile *aReader) {
#if defined(NO_XIPH_LIBS)
  (void)aReader;
  printf("[Wav::loadogg] NO_XIPH_LIBS defined, returning FILE_LOAD_FAILED\n");
  return FILE_LOAD_FAILED;
#else
  MBOggDecoder decoder;
  if (!decoder.open(aReader->getMemPtr(), aReader->length())) {
    printf("[Wav::loadogg] decoder.open failed\n");
    return FILE_LOAD_FAILED;
  }

  mBaseSamplerate = (float)decoder.getSampleRate();
  int totalSamples = (int)decoder.getLengthInSamples();

  if (decoder.getChannels() <= 0) {
    return FILE_LOAD_FAILED;
  }
  if (decoder.getChannels() > MAX_CHANNELS) {
    mChannels = MAX_CHANNELS;
  } else {
    mChannels = decoder.getChannels();
  }

  if (totalSamples == 0) {
    // Unknown length (e.g. unseekable OGG/FLAC) - accumulate planar channels
    // directly without interleave/deinterleave round-trips.
    std::vector<float> chBuffers[MAX_CHANNELS];
    constexpr unsigned int kChunk = 1024;
    float tmp[kChunk * MAX_CHANNELS];
    while (true) {
      unsigned int got = decoder.read(tmp, kChunk, kChunk);
      if (got == 0) {
        break;
      }
      for (unsigned int ch = 0; ch < mChannels; ch++) {
        const float *chSrc = tmp + (size_t)ch * kChunk;
        chBuffers[ch].insert(chBuffers[ch].end(), chSrc, chSrc + got);
      }
    }
    totalSamples = (int)chBuffers[0].size();
    mData = new float[(size_t)totalSamples * mChannels];
    mSampleCount = (unsigned int)totalSamples;
    for (unsigned int ch = 0; ch < mChannels; ch++) {
      if (totalSamples > 0) {
        memcpy(mData + (size_t)ch * mSampleCount, chBuffers[ch].data(),
               (size_t)totalSamples * sizeof(float));
      }
    }
    mActualSampleCount = (unsigned int)totalSamples;
    return 0;
  }

  mData = new float[(size_t)totalSamples * mChannels];
  mSampleCount = (unsigned int)totalSamples;
  unsigned int decodedSamples =
      decoder.read(mData, (unsigned int)totalSamples, mSampleCount);
  if (decodedSamples < mSampleCount) {
    for (unsigned int ch = 0; ch < mChannels; ch++) {
      memset(mData + (size_t)ch * mSampleCount + decodedSamples, 0,
             sizeof(float) * (mSampleCount - decodedSamples));
    }
  }

  // Set mActualSampleCount to the actual decoded samples.
  // This is necessary because getLengthInSamples() returns
  // the granule position from the last page header, which may not match the
  // actual decoded sample count if the file has an empty final page.
  mActualSampleCount = decodedSamples;

  return 0;
#endif
}

result Wav::loadmp3(MemoryFile *aReader) {
  drmp3 decoder;

  if (!drmp3_init_memory(&decoder, aReader->getMemPtr(), aReader->length(),
                         NULL)) {
    return FILE_LOAD_FAILED;
  }

  drmp3_uint64 samples = drmp3_get_pcm_frame_count(&decoder);

  if (!samples || decoder.channels == 0) {
    drmp3_uninit(&decoder);
    return FILE_LOAD_FAILED;
  }

  mChannels = decoder.channels > MAX_CHANNELS ? MAX_CHANNELS : decoder.channels;
  mData = new float[(size_t)samples * mChannels];
  mBaseSamplerate = (float)decoder.sampleRate;
  mSampleCount = (unsigned int)samples;
  mActualSampleCount = (unsigned int)samples;
  drmp3_seek_to_pcm_frame(&decoder, 0);

  constexpr unsigned int kChunkFrames = 1024;
  if (decoder.channels <= MAX_CHANNELS) {
    float tmp[kChunkFrames * MAX_CHANNELS];
    for (unsigned int i = 0; i < mSampleCount; i += kChunkFrames) {
      unsigned int blockSize =
          (mSampleCount - i) > kChunkFrames ? kChunkFrames : mSampleCount - i;
      unsigned int got =
          (unsigned int)drmp3_read_pcm_frames_f32(&decoder, blockSize, tmp);
      if (got < blockSize) {
        memset(tmp + (size_t)got * decoder.channels, 0,
               sizeof(float) * (blockSize - got) * decoder.channels);
      }
      deinterleaveFloatsToPlanar(tmp, mData + i, mSampleCount, blockSize,
                                 mChannels);
    }
  } else {
    std::vector<float> tmp((size_t)kChunkFrames * decoder.channels);
    for (unsigned int i = 0; i < mSampleCount; i += kChunkFrames) {
      unsigned int blockSize =
          (mSampleCount - i) > kChunkFrames ? kChunkFrames : mSampleCount - i;
      unsigned int got = (unsigned int)drmp3_read_pcm_frames_f32(
          &decoder, blockSize, tmp.data());
      if (got < blockSize) {
        memset(tmp.data() + (size_t)got * decoder.channels, 0,
               sizeof(float) * (blockSize - got) * decoder.channels);
      }
      for (unsigned int j = 0; j < blockSize; j++) {
        for (unsigned int k = 0; k < mChannels; k++) {
          mData[(size_t)k * mSampleCount + i + j] =
              tmp[(size_t)j * decoder.channels + k];
        }
      }
    }
  }
  drmp3_uninit(&decoder);

  return SO_NO_ERROR;
}

result Wav::loadflac(MemoryFile *aReader) {
  drflac *decoder =
      drflac_open_memory(aReader->mDataPtr, aReader->mDataLength, NULL);

  if (!decoder) {
    return FILE_LOAD_FAILED;
  }

  drflac_uint64 samples = decoder->totalPCMFrameCount;

  if (!samples || decoder->channels == 0) {
    drflac_close(decoder);
    return FILE_LOAD_FAILED;
  }

  mChannels =
      decoder->channels > MAX_CHANNELS ? MAX_CHANNELS : decoder->channels;
  mData = new float[(size_t)samples * mChannels];
  mBaseSamplerate = (float)decoder->sampleRate;
  mSampleCount = (unsigned int)samples;
  mActualSampleCount = (unsigned int)samples;
  drflac_seek_to_pcm_frame(decoder, 0);

  constexpr unsigned int kChunkFrames = 1024;
  if (decoder->channels <= MAX_CHANNELS) {
    float tmp[kChunkFrames * MAX_CHANNELS];
    for (unsigned int i = 0; i < mSampleCount; i += kChunkFrames) {
      unsigned int blockSize =
          (mSampleCount - i) > kChunkFrames ? kChunkFrames : mSampleCount - i;
      unsigned int got =
          (unsigned int)drflac_read_pcm_frames_f32(decoder, blockSize, tmp);
      if (got < blockSize) {
        memset(tmp + (size_t)got * decoder->channels, 0,
               sizeof(float) * (blockSize - got) * decoder->channels);
      }
      deinterleaveFloatsToPlanar(tmp, mData + i, mSampleCount, blockSize,
                                 mChannels);
    }
  } else {
    std::vector<float> tmp((size_t)kChunkFrames * decoder->channels);
    for (unsigned int i = 0; i < mSampleCount; i += kChunkFrames) {
      unsigned int blockSize =
          (mSampleCount - i) > kChunkFrames ? kChunkFrames : mSampleCount - i;
      unsigned int got = (unsigned int)drflac_read_pcm_frames_f32(
          decoder, blockSize, tmp.data());
      if (got < blockSize) {
        memset(tmp.data() + (size_t)got * decoder->channels, 0,
               sizeof(float) * (blockSize - got) * decoder->channels);
      }
      for (unsigned int j = 0; j < blockSize; j++) {
        for (unsigned int k = 0; k < mChannels; k++) {
          mData[(size_t)k * mSampleCount + i + j] =
              tmp[(size_t)j * decoder->channels + k];
        }
      }
    }
  }
  drflac_close(decoder);

  return SO_NO_ERROR;
}

// Takes ownership of a native decoder result. Rejects results SoLoud cannot
// represent (sample counts are unsigned int, at most MAX_CHANNELS channels).
static result adoptNativeDecoded(Wav *aWav, DecodedAudioData aDecoded) {
  if (!aDecoded.success || aDecoded.samples == nullptr || aDecoded.sampleCount == 0 ||
      aDecoded.sampleCount > 0xFFFFFFFFu || aDecoded.channels == 0 ||
      aDecoded.channels > MAX_CHANNELS || !(aDecoded.sampleRate > 0.0f)) {
    if (!aDecoded.errorMessage.empty())
      printf("[Wav::loadnative] %s\n", aDecoded.errorMessage.c_str());
    aDecoded.freeSamples();
    return FILE_LOAD_FAILED;
  }

  delete[] aWav->mData;
  aWav->mData = aDecoded.samples;
  aWav->mSampleCount = (unsigned int)aDecoded.sampleCount;
  aWav->mActualSampleCount = (unsigned int)aDecoded.sampleCount;
  aWav->mChannels = aDecoded.channels;
  aWav->mBaseSamplerate = aDecoded.sampleRate;
  return SO_NO_ERROR;
}

result Wav::loadnative(MemoryFile *aReader) {
  if (aReader == nullptr || aReader->length() == 0) {
    return INVALID_PARAMETER;
  }

  return adoptNativeDecoded(this, NativeAudioDecoder::decodeMemory(aReader->getMemPtr(), aReader->length()));
}

result Wav::loadnativefile(const char *aFilename) {
  delete[] mData;
  mData = 0;
  mSampleCount = 0;
  mActualSampleCount = 0;
  mChannels = 1;
  return adoptNativeDecoded(this, NativeAudioDecoder::decodeFile(aFilename));
}

result Wav::testAndLoadFile(MemoryFile *aReader) {
  delete[] mData;
  mData = 0;
  mSampleCount = 0;
  mChannels = 1;
  int tag = aReader->read32();
  if (tag == MAKEDWORD('O', 'g', 'g', 'S')) {
    return loadogg(aReader);

  } else if (tag == MAKEDWORD('R', 'I', 'F', 'F')) {
    return loadwav(aReader);
  } else if (tag == MAKEDWORD('f', 'L', 'a', 'C')) {
    return loadflac(aReader);
  } else if (loadmp3(aReader) == SO_NO_ERROR) {
    return SO_NO_ERROR;
  }

  // Fallback to native OS decoder for M4A, MP4, AAC, AC3/EAC3, ALAC, etc.
  aReader->seek(0);
  if (loadnative(aReader) == SO_NO_ERROR) {
    return SO_NO_ERROR;
  }

  printf("[Wav::testAndLoadFile] -> FILE_LOAD_FAILED\n");
  return FILE_LOAD_FAILED;
}

result Wav::load(const char *aFilename) {
  if (aFilename == 0)
    return INVALID_PARAMETER;
  stop();
  DiskFile dr;
  int res = dr.open(aFilename);
  if (res != SO_NO_ERROR)
    return res;
#if !defined(__EMSCRIPTEN__)
  // MP4/M4A containers go straight to the platform decoder by path, avoiding
  // an unnecessary file -> memory round trip (and on Linux/Android, a temp file).
  unsigned char header[8];
  if (dr.read(header, sizeof(header)) == sizeof(header) &&
      NativeAudioDecoder::isM4aOrMp4(header, sizeof(header))) {
    return loadnativefile(aFilename);
  }
  dr.seek(0);
#endif
  return loadFile(&dr);
}

result Wav::loadMem(const unsigned char *aMem, unsigned int aLength, bool aCopy,
                    bool aTakeOwnership) {
  if (aMem == NULL || aLength == 0) {
    if (aTakeOwnership && !aCopy && aMem != NULL) {
      delete[] aMem;
    }
    return INVALID_PARAMETER;
  }
  stop();

  MemoryFile dr;
  dr.openMem(aMem, aLength, aCopy, aTakeOwnership);
  return testAndLoadFile(&dr);
}

result Wav::loadFile(File *aFile) {
  if (!aFile)
    return INVALID_PARAMETER;
  stop();

  MemoryFile mr;
  result res = mr.openFileToMem(aFile);

  if (res != SO_NO_ERROR) {
    return res;
  }
  return testAndLoadFile(&mr);
}

AudioSourceInstance *Wav::createInstance() { return new WavInstance(this); }

double Wav::getLength() {
  if (mBaseSamplerate == 0)
    return 0;
  return mSampleCount / mBaseSamplerate;
}

result Wav::loadRawWave8(unsigned char *aMem, unsigned int aLength,
                         float aSamplerate, unsigned int aChannels) {
  if (aMem == 0 || aLength == 0 || aSamplerate <= 0 || aChannels < 1)
    return INVALID_PARAMETER;
  stop();
  delete[] mData;
  mData = new float[aLength];
  mSampleCount = aLength / aChannels;
  mActualSampleCount = mSampleCount;
  mChannels = aChannels;
  mBaseSamplerate = aSamplerate;
  constexpr float kInv128 = 1.0f / 128.0f;
  for (unsigned int i = 0; i < aLength; i++)
    mData[i] = ((int)aMem[i] - 128) * kInv128;
  return SO_NO_ERROR;
}

result Wav::loadRawWave16(short *aMem, unsigned int aLength, float aSamplerate,
                          unsigned int aChannels) {
  if (aMem == 0 || aLength == 0 || aSamplerate <= 0 || aChannels < 1)
    return INVALID_PARAMETER;
  stop();
  delete[] mData;
  mData = new float[aLength];
  mSampleCount = aLength / aChannels;
  mActualSampleCount = mSampleCount;
  mChannels = aChannels;
  mBaseSamplerate = aSamplerate;
  convertInt16ToFloats(reinterpret_cast<const int16_t *>(aMem), mData, aLength);
  return SO_NO_ERROR;
}

result Wav::loadRawWave(float *aMem, unsigned int aLength, float aSamplerate,
                        unsigned int aChannels, bool aCopy,
                        bool aTakeOwndership) {
  if (aMem == 0 || aLength == 0 || aSamplerate <= 0 || aChannels < 1) {
    if (aTakeOwndership && !aCopy && aMem != 0) {
      delete[] aMem;
    }
    return INVALID_PARAMETER;
  }
  stop();
  delete[] mData;
  if (aCopy == true || aTakeOwndership == false) {
    mData = new float[aLength];
    memcpy(mData, aMem, sizeof(float) * aLength);
  } else {
    mData = aMem;
  }
  mSampleCount = aLength / aChannels;
  mActualSampleCount = mSampleCount;
  mChannels = aChannels;
  mBaseSamplerate = aSamplerate;
  return SO_NO_ERROR;
}
}; // namespace SoLoud

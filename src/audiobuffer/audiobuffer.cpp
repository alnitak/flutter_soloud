
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>

#include "soloud.h"
#include "../soloud/src/audiosource/wav/dr_flac.h"
#include "../soloud/src/audiosource/wav/dr_mp3.h"
#include "../soloud/src/audiosource/wav/dr_wav.h"
#include "soloud_wavstream.h"
#include "soloud_file.h"
#include "../soloud/src/audiosource/wav/stb_vorbis.h"

#include "audiobuffer.h"

namespace SoLoud
{

	BufferStreamInstance::BufferStreamInstance(BufferStream *aParent)
	{
		mOggFrameSize = 0;
		mParent = aParent;
		mOffset = 0;
		mCodec.mOgg = 0;
		mCodec.mFlac = 0;
		mFile = 0;
		if (aParent->mMemFile)
		{
			MemoryFile *mf = new MemoryFile();
			mFile = mf;
			mf->openMem(aParent->mMemFile->getMemPtr(), aParent->mMemFile->length(), false, false);
		}
		else
		{
			return;
		}
	}

	BufferStreamInstance::~BufferStreamInstance()
	{
		if (mParent->mBuffer.getCurrentBufferSizeInBytes() > 0)
		{
			mParent->mBuffer.clear();
		}
		delete mFile;
	}

	unsigned int BufferStreamInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		unsigned int offset = 0;
		float tmp[512 * MAX_CHANNELS];
		if (mFile == NULL)
			return 0;
		unsigned int bufferSize = mParent->mBuffer.getCurrentBufferSize() / 4;
		float* buffer = reinterpret_cast<float*>(mParent->mBuffer.buffer.data());
		int samplesToRead = mOffset + aSamplesToRead > bufferSize ? bufferSize - mOffset : aSamplesToRead;
		if (samplesToRead <= 0)
			return 0;

		if (mChannels == 1)
		{
			// Optimization: if we have a mono audio source, we can just copy all the data in one go. 
			memcpy(aBuffer, buffer + mOffset, sizeof(float) * samplesToRead);
		}
		else
		{
			// From SoLoud documentation:
			// So, if 1024 samples are requested from a stereo audio source, the first 1024 floats
			// should be for the first channel, and the next 1024 samples should be for the second channel.
			unsigned int i, j;
			for (i = 0; i < samplesToRead; i++)
			{
				for (j = 0; j < mChannels; j++)
				{
					aBuffer[j * samplesToRead + i] = buffer[mOffset + i * mChannels + j];
				}
			}
		}

		mOffset += samplesToRead * mChannels;
		return samplesToRead;
		return aSamplesToRead;
	}

	result BufferStreamInstance::seek(double aSeconds, float *mScratch, unsigned int mScratchSize)
	{
		return AudioSourceInstance::seek(aSeconds, mScratch, mScratchSize);
	}

	result BufferStreamInstance::rewind()
	{
		mOffset = 0;
		mStreamPosition = 0.0f;
		return 0;
	}

	bool BufferStreamInstance::hasEnded()
	{
		if (mOffset >= mParent->mSampleCount)
		{
			return 1;
		}
		return 0;
	}

	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////

	BufferStream::BufferStream()
	{
		mSampleCount = 0;
		mMemFile = 0;
	}

	BufferStream::~BufferStream()
	{
		stop();
		delete mMemFile;
	}

	void BufferStream::loadpcm(File *fp, const PCMformat pcmFormat)
	{
		fp->seek(0);

		mChannels = pcmFormat.channels;
		if (mChannels > MAX_CHANNELS)
		{
			mChannels = MAX_CHANNELS;
		}

		mBaseSamplerate = (float)pcmFormat.sampleRate;
	}

	void BufferStream::setBufferStream(
		unsigned int maxBufferSize,
		bool aCopy,
		bool aTakeOwnership,
        bool isPCM,
		PCMformat pcmFormat)
	{
		delete mMemFile;
		mMemFile = 0;
		mSampleCount = 0;
		mEndianness = Endianness::BUFFER_LITTLE_ENDIAN;
		mCopy = aCopy;
		mTakeOwnership = aTakeOwnership;
		mPCMformat.sampleRate = pcmFormat.sampleRate;
		mPCMformat.channels = pcmFormat.channels;
		mPCMformat.bytesPerSample = pcmFormat.bytesPerSample;
		mPCMformat.dataType = pcmFormat.dataType;
		mBuffer.clear();
		mBuffer.setSizeInBytes(maxBufferSize);
	}

	result BufferStream::loadFirstChunk()
	{
		MemoryFile *mf = new MemoryFile();
		int res = mf->openMem((const unsigned char *)mBuffer.buffer.data(), mBuffer.getCurrentBufferSizeInBytes(), mCopy, mTakeOwnership);
		if (res != SO_NO_ERROR)
		{
			delete mf;
			return res;
		}

		loadpcm(mf, mPCMformat);

		mMemFile = mf;

		return 0;
	}

	result BufferStream::addData(const void *aData, unsigned int aDataLen)
	{
		addToBuffer((const unsigned char *)aData, aDataLen);
		if (mMemFile == 0 && mBuffer.getCurrentBufferSizeInBytes() > 100000) 
		{
			return loadFirstChunk();
		}
		return SO_NO_ERROR;
	}

	void BufferStream::addToBuffer(
        const unsigned char *aData,
		unsigned int aDataLen)
	{
		// add PCM data to the buffer
		switch (mPCMformat.dataType)
		{
		case 0:
			mBuffer.addData((float *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		case 1:
			mBuffer.addData((int8_t *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		case 2:
			mBuffer.addData((int16_t *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		case 3:
			mBuffer.addData((int32_t *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		}
		mSampleCount += aDataLen / mPCMformat.bytesPerSample / mChannels;
	}

	AudioSourceInstance *BufferStream::createInstance()
	{
		return new BufferStreamInstance(this);
	}

	double BufferStream::getLength()
	{
		if (mBaseSamplerate == 0)
			return 0;
		return mSampleCount / mBaseSamplerate;
	}
};

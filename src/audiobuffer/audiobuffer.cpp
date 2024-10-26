
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>

#include "soloud.h"
#include "audiobuffer.h"

// TODO: readSamplesFromBuffer

namespace SoLoud
{

	BufferStreamInstance::BufferStreamInstance(BufferStream *aParent)
	{
		mParent = aParent;
		mOffset = 0;
	}

	BufferStreamInstance::~BufferStreamInstance()
	{
	}

	unsigned int BufferStreamInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		if (mParent->mBuffer.getFloatsBufferSize() == 0)
			return 0;
		unsigned int bufferSize = mParent->mBuffer.getFloatsBufferSize();
		float *buffer = reinterpret_cast<float *>(mParent->mBuffer.buffer.data());
		int samplesToRead = mOffset + aSamplesToRead > bufferSize ? bufferSize - mOffset : aSamplesToRead;
		if (samplesToRead <= 0)
			return 0;

		if (samplesToRead != aSamplesToRead)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);

			if (mParent->mOnBufferingCallback != nullptr && !mParent->dataIsEnded)
			{
				mParent->mOnBufferingCallback();
			}
		}

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
		if (mOffset >= mParent->mSampleCount * mParent->mPCMformat.bytesPerSample &&
			mParent->dataIsEnded)
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

	BufferStream::BufferStream() {}

	BufferStream::~BufferStream()
	{
		stop();
	}

	void BufferStream::setBufferStream(
		unsigned int maxBufferSize,
		PCMformat pcmFormat,
		void (*onBufferingCallback)())
	{
		mSampleCount = 0;
		dataIsEnded = false;
		mEndianness = Endianness::BUFFER_LITTLE_ENDIAN; // TODO?
		mPCMformat.sampleRate = pcmFormat.sampleRate;
		mPCMformat.channels = pcmFormat.channels;
		mPCMformat.bytesPerSample = pcmFormat.bytesPerSample;
		mPCMformat.dataType = pcmFormat.dataType;
		mBuffer.clear();
		mBuffer.setSizeInBytes(maxBufferSize);
		mChannels = pcmFormat.channels;
		mBaseSamplerate = (float)pcmFormat.sampleRate;
		mOnBufferingCallback = onBufferingCallback;
	}

	void BufferStream::setDataIsEnded()
	{
		dataIsEnded = true;
	}

	PlayerErrors BufferStream::addData(const void *aData, unsigned int aDataLen)
	{
		if (dataIsEnded)
		{
			return pcmBufferFullOrStreamEnded;
		}

		unsigned int bytesWritten = 0;
		// add PCM data to the buffer
		switch (mPCMformat.dataType)
		{
		case 0:
			bytesWritten = mBuffer.addData((float *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		case 1:
			bytesWritten = mBuffer.addData((int8_t *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		case 2:
			bytesWritten = mBuffer.addData((int16_t *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		case 3:
			bytesWritten = mBuffer.addData((int32_t *)aData, aDataLen / mPCMformat.bytesPerSample);
			break;
		}

		mSampleCount += bytesWritten / mPCMformat.bytesPerSample;

		// data has been added to the buffer, but not all because reached its full capacity.
		// So mark this stream as ended and no more data can be added.
		if (bytesWritten < aDataLen / mPCMformat.bytesPerSample)
		{
			dataIsEnded = true;
			return pcmBufferFullOrStreamEnded;
		}

		return noError;
	}

	AudioSourceInstance *BufferStream::createInstance()
	{
		return new BufferStreamInstance(this);
	}

	double BufferStream::getLength()
	{
		if (mBaseSamplerate == 0)
			return 0;
		return mSampleCount / mBaseSamplerate * mPCMformat.bytesPerSample / mPCMformat.channels;
	}
};
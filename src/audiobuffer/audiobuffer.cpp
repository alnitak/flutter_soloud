#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include "opus_stream_decoder.h"

#include "audiobuffer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// TODO: readSamplesFromBuffer as for waveform

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
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);
			return 0;
		}

		unsigned int bufferSize = mParent->mBuffer.getFloatsBufferSize();
		float *buffer = reinterpret_cast<float *>(mParent->mBuffer.buffer.data());
		int samplesToRead = mOffset + aSamplesToRead > bufferSize ? bufferSize - mOffset : aSamplesToRead;
		if (samplesToRead <= 0)
			return 0;

		if (samplesToRead != aSamplesToRead)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);
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
			for (j = 0; j < mChannels; j++)
			{
				for (i = 0; i < samplesToRead; i++)
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
		Player *aPlayer,
		ActiveSound *aParent,
		unsigned int maxBufferSize,
		SoLoud::time bufferingTimeNeeds,
		PCMformat pcmFormat,
		dartOnBufferingCallback_t onBufferingCallback)
	{
		mSampleCount = 0;
		dataIsEnded = false;
		mThePlayer = aPlayer;
		mParent = aParent;
		mPCMformat.sampleRate = pcmFormat.sampleRate;
		mPCMformat.channels = pcmFormat.channels;
		mPCMformat.bytesPerSample = pcmFormat.bytesPerSample;
		mPCMformat.dataType = pcmFormat.dataType;
		mBuffer.clear();
		mBuffer.setSizeInBytes(maxBufferSize);
		mBufferingTimeNeeds = bufferingTimeNeeds;
		mChannels = pcmFormat.channels;
		mBaseSamplerate = (float)pcmFormat.sampleRate;
		mOnBufferingCallback = onBufferingCallback;

		decoder = nullptr;
		if (pcmFormat.dataType == OPUS)
		{
			decoder = std::make_unique<OpusDecoderWrapper>(
				pcmFormat.sampleRate, pcmFormat.sampleRate,
				pcmFormat.channels, pcmFormat.channels);
				// pcmFormat.sampleRate, mThePlayer->mSampleRate,
				// pcmFormat.channels, mThePlayer->mChannels);
		}
	}

	void BufferStream::setDataIsEnded()
	{
		dataIsEnded = true;
	}

	PlayerErrors BufferStream::addData(const void *aData, unsigned int aDataLen)
	{
		if (dataIsEnded)
		{
			return streamEndedAlready;
		}
		printf("BufferStream::addData: %d\n", aDataLen);
		unsigned int bytesWritten = 0;

		if (mPCMformat.dataType == BufferType::OPUS)
		{
			// Decode the Opus data
			auto newData = decoder.get()->decode(
				reinterpret_cast<const unsigned char*>(aData),
				aDataLen);
			if (newData.size() > 0)
				bytesWritten = mBuffer.addData(BufferType::OPUS, newData.data(), newData.size());
			else 
				return noError;
		}
		else
		{
			bytesWritten = mBuffer.addData(mPCMformat.dataType, aData, aDataLen / mPCMformat.bytesPerSample);
		}

		mSampleCount += bytesWritten / mPCMformat.bytesPerSample;

		// If a handle reaches the end and data is not ended, we have to wait for it has enough data
		// to reach [TIME_FOR_BUFFERING] and restart playing it.
		time currBufferTime = getLength();
		for (int i = 0; i < mParent->handle.size(); i++)
		{
			double pos = mThePlayer->getPosition(mParent->handle[i].handle);
			// This handle needs to wait for [TIME_FOR_BUFFERING]
			if (pos >= currBufferTime && !mThePlayer->getPause(mParent->handle[i].handle))
			{
				mParent->handle[i].bufferingTime = currBufferTime;
				mThePlayer->setPause(mParent->handle[i].handle, true);
				if (mOnBufferingCallback != nullptr)
				{
#ifdef __EMSCRIPTEN__
					// Call the Dart callback stored on globalThis, if it exists.
					// The `dartOnBufferingCallback_$hash` function is created in
					// `setBufferStream()` in `bindings_player_web.dart` and it's
					// meant to call the Dart callback passed to `setBufferStream()`.
					EM_ASM({
							// Compose the function name for this soundHash
							var functionName = "dartOnBufferingCallback_" + $3;
							if (typeof window[functionName] === "function") {
								var buffering = $0 == 1 ? true : false;
								window[functionName](buffering, $1, $2); // Call it
							} else {
								console.log("EM_ASM 'dartOnBufferingCallback_$hash' not found.");
							} }, true, mParent->handle[i].handle, currBufferTime, mParent->soundHash);
#else
					mOnBufferingCallback(true, mParent->handle[i].handle, currBufferTime);
#endif
				}
			}
			if (currBufferTime - mParent->handle[i].bufferingTime >= mBufferingTimeNeeds &&
				mThePlayer->getPause(mParent->handle[i].handle))
			{
				mThePlayer->setPause(mParent->handle[i].handle, false);
				mParent->handle[i].bufferingTime = MAX_DOUBLE;
				if (mOnBufferingCallback != nullptr)
				{
#ifdef __EMSCRIPTEN__
					// Call the Dart callback stored on globalThis, if it exists.
					EM_ASM({
							// Compose the function name for this soundHash
							var functionName = "dartOnBufferingCallback_" + $3;
							if (typeof window[functionName] === "function") {
								var buffering = $0 == 1 ? true : false;
								window[functionName](buffering, $1, $2); // Call it
							} else {
								console.log("EM_ASM 'dartOnBufferingCallback_$hash' not found.");
							} }, false, mParent->handle[i].handle, currBufferTime, mParent->soundHash);
#else
					mOnBufferingCallback(false, mParent->handle[i].handle, currBufferTime);
#endif
				}
			}
		}

		// data has been added to the buffer, but not all because reached its full capacity.
		// So mark this stream as ended and no more data can be added.
		if (bytesWritten < aDataLen / mPCMformat.bytesPerSample)
		{
			dataIsEnded = true;
			return pcmBufferFull;
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

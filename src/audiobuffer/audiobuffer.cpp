#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>

#include "audiobuffer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// TODO: readSamplesFromBuffer as for waveform

namespace SoLoud
{
    std::mutex buffer_lock_mutex;
	
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
		std::lock_guard<std::mutex> lock(buffer_lock_mutex);

		if (mParent->mBuffer.getFloatsBufferSize() == 0)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);
			// Calculate mStreamPosition based on mOffset
			mStreamPosition = mOffset / (float)(mSamplerate * mChannels);
			return 0;
		}

		unsigned int bufferSize = mParent->mBuffer.getFloatsBufferSize();
		float *buffer = reinterpret_cast<float *>(mParent->mBuffer.buffer.data());
		int samplesToRead = mOffset + aSamplesToRead > bufferSize ? bufferSize - mOffset : aSamplesToRead;
		if (samplesToRead <= 0)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);
			// Calculate mStreamPosition based on mOffset
			mStreamPosition = mOffset / (float)(mSamplerate * mChannels);
			return 0;
		}

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

		unsigned int totalBytesRead = samplesToRead * mChannels * sizeof(float);
		size_t samplesRemoved = mParent->mBuffer.removeData(totalBytesRead);
		
		 // Update stream position regardless of buffering type
        mStreamTime += samplesToRead / (float)mSamplerate;
        
		// If buffering type is RELEASED, adjust mSampleCount and don't increment mOffset
		if (mParent->mBuffer.bufferingType == BufferingType::RELEASED) {
			mParent->mSampleCount -= samplesRemoved / mParent->mPCMformat.bytesPerSample;
			// For RELEASED type, streamPosition is always at the start of the remaining buffer
            mStreamPosition = 0;
		}
		else
		{
			mOffset += samplesToRead * mChannels;
			// For PRESERVED type, streamPosition advances with the offset
            mStreamPosition = mOffset / (float)(mSamplerate * mChannels);
		}

		return samplesToRead;
	}

	result BufferStreamInstance::seek(double aSeconds, float *mScratch, unsigned int mScratchSize)
	{
		if (mParent->mBuffer.bufferingType == BufferingType::RELEASED) {
            // Seeking not supported in RELEASED mode since data is discarded
			// TODO: Support seeking forward in RELEASED mode
            return INVALID_PARAMETER;
        }
		double offset = aSeconds - mStreamPosition;
		if (offset <= 0)
		{
			if (rewind() != SO_NO_ERROR)
			{
				// can't do generic seek backwards unless we can rewind.
				return NOT_IMPLEMENTED;
			}
			offset = aSeconds;
		}
		long samples_to_discard = (long)floor(mSamplerate * offset);

		while (samples_to_discard)
		{
			long samples = mScratchSize / mChannels;
			if (samples > samples_to_discard)
				samples = samples_to_discard;
			getAudio(mScratch, samples, samples);
			samples_to_discard -= samples;
		}
		int pos = (int)floor(mBaseSamplerate * aSeconds);
		mOffset = pos;
		mStreamPosition = float(pos / mBaseSamplerate);
		return SO_NO_ERROR;
	}

	result BufferStreamInstance::rewind()
	{
		if (mParent->mBuffer.bufferingType == BufferingType::RELEASED) {
            // Rewinding not supported in RELEASED mode since data is discarded
            return INVALID_PARAMETER;
        }
		mOffset = 0;
		mStreamPosition = 0.0f;
		return 0;
	}

	bool BufferStreamInstance::hasEnded()
	{
		if (mParent->dataIsEnded &&
			mOffset >= mParent->mSampleCount * mParent->mPCMformat.bytesPerSample)
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

	int counter;
	PlayerErrors BufferStream::setBufferStream(
		Player *aPlayer,
		ActiveSound *aParent,
		unsigned int maxBufferSize,
		BufferingType bufferingType,
		SoLoud::time bufferingTimeNeeds,
		PCMformat pcmFormat,
		dartOnBufferingCallback_t onBufferingCallback)
	{
		/// maxBufferSize must be a number divisible by channels * sizeof(float)
		if (maxBufferSize % (pcmFormat.channels * sizeof(float)) != 0)
			maxBufferSize -= maxBufferSize % (pcmFormat.channels * sizeof(float));

		mBytesReceived = 0;
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
		buffer = std::vector<unsigned char>();
		mBuffer.setBufferType(bufferingType);

#if !defined(NO_OPUS_OGG_LIBS)
		decoder = nullptr;
		if (pcmFormat.dataType == BufferType::OPUS)
		{
			try
			{
				decoder = std::make_unique<OpusDecoderWrapper>(
					pcmFormat.sampleRate, pcmFormat.channels);
			}
			catch (const std::exception &e)
			{
				return PlayerErrors::failedToCreateOpusDecoder;
			}
		}
#else
		if (pcmFormat.dataType == OPUS)
		{
			return PlayerErrors::failedToCreateOpusDecoder;
		}
#endif
		return PlayerErrors::noError;
	}

	void BufferStream::resetBuffer()
	{
		std::lock_guard<std::mutex> lock(buffer_lock_mutex);
		buffer.clear();
		mBuffer.clear();
		mSampleCount = 0;
		mBytesReceived = 0;
	}

	void BufferStream::setDataIsEnded()
	{
		// Eventually add any remaining data
		if (buffer.size() > 0)
		{
			addData(buffer.data(), buffer.size(), true);
		}
		// Check if some handles was paused for buffering and unpause them
		time currBufferTime = getLength();
		for (int i = 0; i < mParent->handle.size(); i++)
		{
			SoLoud::handle handle = mParent->handle[i].handle;
			double pos = mThePlayer->getPosition(handle);
			if (pos < currBufferTime)
			{
				mThePlayer->setPause(handle, false);
			}
		}

		buffer.clear();
		dataIsEnded = true;
	}

	PlayerErrors BufferStream::addData(const void *aData, unsigned int aDataLen, bool forceAdd)
	{
		if (dataIsEnded)
		{
			return PlayerErrors::streamEndedAlready;
		}

		unsigned int bytesWritten = 0;

		buffer.insert(buffer.end(),
			static_cast<const unsigned char *>(aData),
			static_cast<const unsigned char *>(aData) + aDataLen);
		mBytesReceived += aDataLen;
		int bufferDataToAdd = 0;
		// Performing some buffering. We need some data to be added expecially when using opus.
		if (buffer.size() > 1024 * 2 && !forceAdd) // 2 KB of data
		{
			// For PCM data we should align the data to the bytes per sample.
			if (mPCMformat.dataType != BufferType::OPUS)
			{
				int alignment = mPCMformat.bytesPerSample * mPCMformat.channels;
				bufferDataToAdd = (int)(buffer.size() / alignment) * alignment;
			}
			else
			{
				// When using opus we don't need to align.
				bufferDataToAdd = buffer.size();
			}
		} else
			// Return if there is not enough data to add.
			return PlayerErrors::noError;


		if (mPCMformat.dataType == BufferType::OPUS)
		{

#if !defined(NO_OPUS_OGG_LIBS)
			// Decode the Opus data
			try {
				auto newData = decoder.get()->decode(
					buffer.data(),
					bufferDataToAdd);
				if (newData.size() > 0)
					bytesWritten = mBuffer.addData(BufferType::OPUS, newData.data(), newData.size());
				else
					return PlayerErrors::noError;
			}
			catch (const std::exception &e)
			{
				return PlayerErrors::failedToDecodeOpusPacket;
			}
#else
			return PlayerErrors::failedToDecodeOpusPacket;
#endif
		}
		else
		{
			bytesWritten = mBuffer.addData(mPCMformat.dataType, buffer.data(), bufferDataToAdd / mPCMformat.bytesPerSample);
		}

		// Remove the processed data from the buffer
		if (bytesWritten > 0) {
			buffer.erase(buffer.begin(), buffer.begin() + bufferDataToAdd);
		}


		// If a handle reaches the end and data is not ended, we have to wait for it has enough data
		// to reach [TIME_FOR_BUFFERING] and restart playing it.
		time currBufferTime = getLength();
		for (int i = 0; i < mParent->handle.size(); i++)
		{
			SoLoud::handle handle = mParent->handle[i].handle;
			double pos = mThePlayer->getPosition(handle);
			// This handle needs to wait for [TIME_FOR_BUFFERING]. Pause it.
			if (pos >= currBufferTime && !mThePlayer->getPause(handle))
			{
				mParent->handle[i].bufferingTime = currBufferTime;
				mThePlayer->setPause(handle, true);
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
							} }, true, handle, currBufferTime, mParent->soundHash);
#else
					mOnBufferingCallback(true, handle, currBufferTime);
#endif
				}
			}
			// This handle has reached [TIME_FOR_BUFFERING]. Unpause it.
			if (currBufferTime - mParent->handle[i].bufferingTime >= mBufferingTimeNeeds &&
				mThePlayer->getPause(handle))
			{
				mThePlayer->setPause(handle, false);
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
							} }, false, handle, currBufferTime, mParent->soundHash);
#else
					mOnBufferingCallback(false, handle, currBufferTime);
#endif
				}
			}
		}

		mSampleCount += bytesWritten / mPCMformat.bytesPerSample;

		// data has been added to the buffer, but not all because reached its full capacity.
		// So mark this stream as ended and no more data can be added.
		if (bytesWritten < aDataLen / mPCMformat.bytesPerSample)
		{
			dataIsEnded = true;
			return PlayerErrors::pcmBufferFull;
		}

		return PlayerErrors::noError;
	}

	BufferingType BufferStream::getBufferingType()
	{
		return mBuffer.bufferingType;
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

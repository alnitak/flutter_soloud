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
    std::mutex check_buffer_mutex;
	
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

		// This happens when using RELEASED buffer type
		if (mParent->mBuffer.getFloatsBufferSize() == 0)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);
			// Calculate mStreamPosition based on mOffset
			mStreamPosition = mOffset / (float)(mSamplerate * mChannels);


			// This is not nice to do in the audio callback, but I didn't
			// find a better way to get lenght and pause the sound and the
			// `checkBuffering` function is fast enough.
			if (!mParent->mIsBuffering) {
				mParent->mThePlayer->soloud.unlockAudioMutex_internal();
				mParent->checkBuffering(0);
				mParent->mThePlayer->soloud.lockAudioMutex_internal();
			}
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

			// This is not nice to do in the audio callback, but I didn't
			// find a better way to get lenght and pause the sound and the
			// `chakeBuffering` function is fast enough.
			if (!mParent->mIsBuffering) {
				mParent->mThePlayer->soloud.unlockAudioMutex_internal();
				mParent->checkBuffering(0);
				mParent->mThePlayer->soloud.lockAudioMutex_internal();
			}
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
			mParent->mBytesConsumed += totalBytesRead;
		}
		else
		{
			mOffset += samplesToRead * mChannels;
			// For PRESERVED type, streamPosition advances with the offset
            // mStreamPosition = mOffset / (float)(sizeof(float) * mSamplerate * mChannels);
			mStreamPosition = mOffset / (float)(mSamplerate * mChannels);
		}

		return samplesToRead;
	}

	result BufferStreamInstance::seek(double aSeconds, float *mScratch, unsigned int mScratchSize)
	{
		if (aSeconds <= 0.0) {
			rewind();
			return SO_NO_ERROR;
		}
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
		mOffset = 0;
		mStreamPosition = 0.0f;
		return 0;
	}

	bool BufferStreamInstance::hasEnded()
	{
		auto b = mParent->mBuffer.bufferingType == BufferingType::PRESERVED;
		// PRESERVED
		if (b && mParent->dataIsEnded && 
			mOffset >= mParent->mSampleCount)
		{
			return 1;
		} else 
		// RELEASED
		if (!b && mParent->dataIsEnded && mParent->mBuffer.getFloatsBufferSize() == 0)
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
		mUncompressedBytesReceived = 0;
		mSampleCount = 0;
		mBytesConsumed = 0;
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
		mIsBuffering = true;

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

		mp3Decoder = nullptr;
		minRequiredMp3Bytes = 0;
		if (pcmFormat.dataType == BufferType::MP3)
		{
			try
			{
				mp3Decoder = std::make_unique<MP3DecoderWrapper>(
					pcmFormat.sampleRate, pcmFormat.channels);
			}
			catch (const std::exception &e)
			{
				return PlayerErrors::failedToCreateMp3Decoder;
			}
		}
		return PlayerErrors::noError;
	}

	void BufferStream::resetBuffer()
	{
		std::lock_guard<std::mutex> lock(buffer_lock_mutex);
		buffer.clear();
		mBuffer.clear();
		mSampleCount = 0;
		mBytesReceived = 0;
		mUncompressedBytesReceived = 0;

		for (int i = 0; i < mParent->handle.size(); i++)
		{
			mThePlayer->soloud.seek(mParent->handle[i].handle, 0.0f);
			mParent->handle[i].bufferingTime = 0.0f;
		}
	}

	int counter = 0;
	void BufferStream::setDataIsEnded()
	{
		// Eventually add any remaining data
		if (buffer.size() > 0)
		{
			addData(buffer.data(), buffer.size(), true);
		}

		buffer.clear();
		dataIsEnded = true;
		counter = 0;
		checkBuffering(0);
	}
	PlayerErrors BufferStream::addData(const void *aData, unsigned int aDataLen, bool forceAdd)
	{
		printf("***** dataIsEnded %d    counter %d   mBuffer size %d    buffer size %d   aDataLen %d\n", dataIsEnded, counter++, mBuffer.buffer.size(), buffer.size(), aDataLen);
		if (dataIsEnded)
		{
			return PlayerErrors::streamEndedAlready;
		}

		size_t bytesWritten = 0;
		bool allDataAdded = -1;

		buffer.insert(buffer.end(),
			static_cast<const unsigned char *>(aData),
			static_cast<const unsigned char *>(aData) + aDataLen);
		mBytesReceived += aDataLen;
		int bufferDataToAdd = 0;
		// Performing some buffering. We need some data to be added expecially when using opus.
		if (buffer.size() > 1024 * 64 && !forceAdd) // 16 KB of data. forceAdd used only in setDataIsEnded()
		{
			// For PCM data we should align the data to the bytes per sample.
			if (!(mPCMformat.dataType == BufferType::OPUS || mPCMformat.dataType == BufferType::MP3))
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

		// printf("***** OK... adding %d bytes\n", bufferDataToAdd);

		if (mPCMformat.dataType == BufferType::OPUS)
		{
#if !defined(NO_OPUS_OGG_LIBS)
			// Decode the Opus data
			try
			{
				std::vector<float> decoded = decoder->decode(
					buffer.data(),
					bufferDataToAdd);

				if (!decoded.empty())
				{
					bytesWritten = mBuffer.addData(
						BufferType::PCM_F32LE,
						decoded.data(),
						decoded.size(),
						&allDataAdded) * sizeof(float);
					// printf("***** all has been added %d\n", allDataAdded);
				}
				// Remove the processed data from the buffer
				if (bytesWritten > 0) {
					buffer.erase(buffer.begin(), buffer.begin() + bufferDataToAdd);
				}
			}
			catch (const std::exception &e)
			{
				return PlayerErrors::failedToDecodeOpusPacket;
			}
#else
			return PlayerErrors::failedToDecodeOpusPacket;
#endif
		}
		else if (mPCMformat.dataType == BufferType::MP3)
		{
			// Initialize the decoder if needed
			if (!mp3Decoder) {
				try {
					mp3Decoder = std::make_unique<MP3DecoderWrapper>(
						mPCMformat.sampleRate,
						mPCMformat.channels
					);
					minRequiredMp3Bytes = 0;  // Will be set after first successful decode
				} catch (const std::exception &e) {
					return PlayerErrors::failedToCreateMp3Decoder;
				}
			}

            std::vector<float> decoded = mp3Decoder->decode(buffer);
        	
			// printf("streamStartOffset: %d  decodedBytes: %d\n", mp3Decoder->decoder.streamStartOffset, decodedBytes);
			// printf("bufferDataToAdd %d buffer size %d\n", bufferDataToAdd, buffer.size());

            if (!decoded.empty())
            {
				bytesWritten = mBuffer.addData(
								   BufferType::PCM_F32LE,
								   decoded.data(),
								   decoded.size(),
								   &allDataAdded) *
							   sizeof(float);
			}
		}
		else
		{   // PCM data
			bytesWritten = mBuffer.addData(
				mPCMformat.dataType,
				buffer.data(),
				bufferDataToAdd / mPCMformat.bytesPerSample,
				&allDataAdded) *
					mPCMformat.bytesPerSample;
			// Remove the processed data from the buffer
			if (bytesWritten > 0) {
				buffer.erase(buffer.begin(), buffer.begin() + bufferDataToAdd);
			}
		}
		
		
		if (mIsBuffering)
			checkBuffering(bytesWritten);
		mUncompressedBytesReceived += bytesWritten;

		mSampleCount += bytesWritten / mPCMformat.bytesPerSample;

		// data has been added to the buffer, but not all because reached its full capacity.
		// So mark this stream as ended and no more data can be added.
		if (!allDataAdded)
		{
			dataIsEnded = true;
			return PlayerErrors::pcmBufferFull;
		}

		return PlayerErrors::noError;
	}

	/// Check if some handles was paused for buffering and unpause them or restart them
	/// if needed after adding [afterAddingBytesCount] bytes.
	void BufferStream::checkBuffering(unsigned int afterAddingBytesCount)
	{
		std::lock_guard<std::mutex> lock(check_buffer_mutex);

		// If a handle reaches the end and data is not ended, we have to wait for it has enough data
		// to reach [TIME_FOR_BUFFERING] and restart playing it.
		SoLoud::time currBufferTime = getLength();
		SoLoud::time addedDataTime = (afterAddingBytesCount / mPCMformat.bytesPerSample) / (mBaseSamplerate * mChannels);
		SoLoud::time bufferLength = (double)mBuffer.getFloatsBufferSize() / (mBaseSamplerate * mChannels);

		for (int i = 0; i < mParent->handle.size(); i++)
		{
			SoLoud::handle handle = mParent->handle[i].handle;
			SoLoud::time pos = mBuffer.bufferingType == BufferingType::RELEASED ? getStreamTimeConsumed() : mThePlayer->getPosition(handle);
			// SoLoud::time pos = mThePlayer->getPosition(handle);
			bool isPaused = mThePlayer->getPause(handle);

			// printf("checkBuffering -- bufferLength: %lf, currBufferTime: %lf\n", 
			// 	bufferLength, currBufferTime);

			// This handle needs to wait for [TIME_FOR_BUFFERING]. Pause it.
			if (pos >= currBufferTime + addedDataTime && !isPaused)
			{
				mParent->handle[i].bufferingTime = currBufferTime;
				mThePlayer->setPause(handle, true);
				isPaused = true;
				callOnBufferingCallback(true, handle, currBufferTime);
			} else
			// This handle has reached [TIME_FOR_BUFFERING]. Unpause it.
			if (currBufferTime + addedDataTime - mParent->handle[i].bufferingTime >= mBufferingTimeNeeds && isPaused)
			{
				mThePlayer->setPause(handle, false);
				isPaused = false;
				mParent->handle[i].bufferingTime = currBufferTime + addedDataTime;
				callOnBufferingCallback(false, handle, currBufferTime);
			}
			// If data is ended and the handle is paused, unpause it to listen to the rest of the data.
			if (dataIsEnded && isPaused)
			{
				mThePlayer->setPause(handle, false);
				isPaused = false;
				mParent->handle[i].bufferingTime = MAX_DOUBLE;
				callOnBufferingCallback(false, handle, currBufferTime);
			}
		}
	}

	void BufferStream::callOnBufferingCallback(bool isBuffering, unsigned int handle, double time)
	{
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
					} }, isBuffering, handle, time, mParent->soundHash);
#else
			mOnBufferingCallback(isBuffering, handle, time);
#endif
		}
		mIsBuffering = isBuffering;
	}

	BufferingType BufferStream::getBufferingType()
	{
		return mBuffer.bufferingType;
	}

	AudioSourceInstance *BufferStream::createInstance()
	{
		return new BufferStreamInstance(this);
	}

	SoLoud::time BufferStream::getLength()
	{
		if (mBaseSamplerate == 0 || mUncompressedBytesReceived == 0 || mPCMformat.bytesPerSample == 0)
			return 0;
		return (mUncompressedBytesReceived / mPCMformat.bytesPerSample) / (mBaseSamplerate * mPCMformat.channels);
	}

	/// Get the time consumed by this stream of type RELEASED
	SoLoud::time BufferStream::getStreamTimeConsumed()
	{
		return (double)(mBytesConsumed / mPCMformat.bytesPerSample) / (mBaseSamplerate * mPCMformat.channels);
	}
};

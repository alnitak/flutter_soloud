#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include <algorithm> // std::min

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

	void BufferStream::setDataIsEnded()
	{
		// Eventually add any remaining data
		if (buffer.size() > 0)
		{
			addData(buffer.data(), buffer.size(), true);
		}

		buffer.clear();
		dataIsEnded = true;
		checkBuffering(0);
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
						decoded.size()) * sizeof(float);
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
			static const size_t MIN_MP3_DATA = 2048; // Minimum data needed for reliable MP3 frame detection
			
			// Initialize the decoder if needed
			if (!mp3Decoder) {
				try {
					// Try to find MP3 header in the buffer
					const unsigned char* data = buffer.data();
					size_t size = buffer.size();
					bool foundHeader = false;
					size_t headerOffset = 0;

					// Search for MP3 sync word
					for (size_t i = 0; i < size - 4; i++) {
						if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0) {
							// Verify header
							unsigned char version = (data[i + 1] >> 3) & 0x03;
							unsigned char layer = (data[i + 1] >> 1) & 0x03;
							unsigned char bitrate = (data[i + 2] >> 4) & 0x0F;
							unsigned char sampleRate = (data[i + 2] >> 2) & 0x03;
							
							if (layer != 0 && bitrate != 0x0F && sampleRate != 0x03) {
								foundHeader = true;
								headerOffset = i;
								break;
							}
						}
					}

					if (!foundHeader) {
						printf("No valid MP3 header found in %zu bytes\n", size);
						return PlayerErrors::noError;  // Wait for more data
					}

					// Remove any data before the header
					if (headerOffset > 0) {
						printf("Removing %zu bytes before MP3 header\n", headerOffset);
						buffer.erase(buffer.begin(), buffer.begin() + headerOffset);
					}

					mp3Decoder = std::make_unique<MP3DecoderWrapper>(
						mPCMformat.sampleRate,
						mPCMformat.channels
					);
					minRequiredMp3Bytes = MIN_MP3_DATA;
					
					printf("MP3 Decoder created with sr=%d, ch=%d\n", 
						   mPCMformat.sampleRate, mPCMformat.channels);
						   
				} catch (const std::exception &e) {
					printf("Failed to create MP3 decoder: %s\n", e.what());
					return PlayerErrors::failedToCreateMp3Decoder;
				}
			}

			// Ensure we have enough data for reliable frame detection
			if (!forceAdd && buffer.size() < std::max(MIN_MP3_DATA, minRequiredMp3Bytes)) {
				printf("Need more data: have %zu, need %zu\n", 
					   buffer.size(), std::max(MIN_MP3_DATA, minRequiredMp3Bytes));
				return PlayerErrors::noError;
			}
			
			// Limit how much data we try to decode at once to prevent memory issues
			size_t maxDecodeAttempt = std::min(bufferDataToAdd, 
				16 * 1024); // Process max 16KB at a time
			
			// Decode the MP3 data
			try {
				// Print first few bytes of the buffer for debugging
				printf("Buffer starts with: ");
				for (int i = 0; i < std::min(16UL, buffer.size()); i++) {
					printf("%02X ", buffer[i]);
				}
				printf("\n");

				std::vector<float> decoded = mp3Decoder->decode(
					buffer.data(),
					maxDecodeAttempt
				);

				printf("Attempted to decode %zu bytes, got %zu decoded samples\n", 
					   maxDecodeAttempt, decoded.size());
				
				if (decoded.empty()) {
					if (!forceAdd) {
						// Try to find next valid frame
						const unsigned char* data = buffer.data();
						size_t size = buffer.size();
						bool foundSync = false;
						size_t offset = 1;  // Start after current position

						while (offset < size - 4) {
							if (data[offset] == 0xFF && (data[offset + 1] & 0xE0) == 0xE0) {
								// Found potential new sync point
								printf("Found new sync point at offset %zu\n", offset);
								// Remove data up to new sync point
								buffer.erase(buffer.begin(), buffer.begin() + offset);
								return PlayerErrors::noError;  // Try again with realigned data
							}
							offset++;
						}
						
						// No sync found, wait for more data
						return PlayerErrors::noError;
					}
					
					// If forcing, try to decode any remaining data
					decoded = mp3Decoder->decode(
						buffer.data(),
						buffer.size()
					);
					
					printf("Force decode of %zu bytes, got %zu samples\n",
						   buffer.size(), decoded.size());
						   
					if (decoded.empty()) {
						return PlayerErrors::noError;
					}
				}

				// Get new minimum bytes needed before next decode
				size_t newMinBytes = mp3Decoder->getMinimumBytesNeeded();
				minRequiredMp3Bytes = std::max(newMinBytes, MIN_MP3_DATA);
				
				// Add decoded PCM data to the buffer
				bytesWritten = mBuffer.addData(
					BufferType::PCM_F32LE,
					decoded.data(),
					decoded.size()
				) * sizeof(float);

				// For VBR, adjust bufferDataToAdd based on what was actually processed
				if (mp3Decoder->isVariableBitrate()) {
					bufferDataToAdd = std::min(maxDecodeAttempt, minRequiredMp3Bytes);
				} else {
					bufferDataToAdd = maxDecodeAttempt;
				}
				
			} catch (const std::exception &e) {
				// Only reset decoder on critical errors
				if (std::string(e.what()).find("Failed to initialize") != std::string::npos) {
					mp3Decoder.reset();
					minRequiredMp3Bytes = MIN_MP3_DATA;
				}
				return PlayerErrors::failedToDecodeMp3Frame;
			}
		}
		else
		{
			bytesWritten = mBuffer.addData(mPCMformat.dataType, buffer.data(), bufferDataToAdd / mPCMformat.bytesPerSample) * mPCMformat.bytesPerSample;
		}
		// Remove the processed data from the buffer
		if (bytesWritten > 0) {
			buffer.erase(buffer.begin(), buffer.begin() + bufferDataToAdd);
		}
		
		if (mIsBuffering)
			checkBuffering(bytesWritten);
		mUncompressedBytesReceived += bytesWritten;

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
